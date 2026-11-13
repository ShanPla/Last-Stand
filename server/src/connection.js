// One Connection wraps one WebSocket. It runs each incoming frame through the same checks in
// order (rate, size, hello, JSON) before the lobby sees it, keeps the ping state, and is the only
// place that sends, so every send gets the same safety checks.
import { RateLimiter } from './limits.js';
import { isPingNumber } from './shapes.js';

export const PROTOCOL_VERSION = 1;
const OPEN = 1; // WebSocket.OPEN

export class Connection {
  /**
   * @param {import('ws').WebSocket} ws
   * @param {import('./lobby.js').Lobby} lobby
   * @param {ReturnType<import('./config.js').loadConfig>} config
   * @param {() => number} now steady clock in ms
   * @param {string | null} [ip] the client address for the per-address limits, null when off
   */
  constructor(ws, lobby, config, now, ip = null) {
    this.ws = ws;
    this.lobby = lobby;
    this.config = config;
    this.now = now;
    this.ip = ip;
    this.limiter = new RateLimiter({
      perSec: config.ratePerSec,
      burst: config.rateBurst,
      closeAfterMs: config.rateCloseAfterMs,
    });
    this.helloDone = false;
    /** @type {import('./rooms.js').Room | null} */
    this.room = null;
    this.slot = -1;
    this.openedAt = now();
    // When the client last proved it is reading: the open, or its last pong. Messages it sends
    // do not count, because a client can keep sending while it never reads what we send back.
    this.lastPongAt = this.openedAt;
    this.pingSentAt = 0; // 0 means no ping is waiting for an answer
    this.behindSince = 0; // when the send buffer went over the limit, 0 while it is not
    this.closing = false;

    ws.on('message', (data, isBinary) => {
      // Nothing a client sends may take the server down. Every check below should already
      // refuse bad input politely; this is the safety net for anything they miss.
      try {
        this.#onMessage(data, isBinary);
      } catch (error) {
        this.#logError('message', error);
        this.sendError('bad-request');
      }
    });
    ws.on('pong', () => {
      this.lastPongAt = this.now();
      this.pingSentAt = 0;
    });
    // Without this handler, a socket error would crash the whole process.
    ws.on('error', () => {});
    ws.on('close', () => {
      try {
        lobby.disconnected(this);
      } catch (error) {
        this.#logError('close', error);
      }
    });
  }

  get bufferedAmount() {
    return this.ws.bufferedAmount;
  }

  /**
   * Sends a JSON message. Returns false when it was not sent: the socket is not open, the value
   * cannot be written as JSON, or the receiver is too far behind (see #hasRoom).
   */
  sendJson(obj) {
    if (this.ws.readyState !== OPEN || !this.#hasRoom()) return false;
    let text;
    try {
      text = JSON.stringify(obj);
    } catch (error) {
      this.#logError('send', error);
      return false;
    }
    this.ws.send(text);
    return true;
  }

  /** Sends a binary frame, with the same rules as sendJson. */
  sendBinary(buf) {
    if (this.ws.readyState !== OPEN || !this.#hasRoom()) return false;
    this.ws.send(buf, { binary: true });
    return true;
  }

  sendError(code) {
    this.sendJson({ t: 'error', code });
  }

  /** Called by a timer: the hello deadline, receivers that stopped reading, and pings. */
  checkIdle(now) {
    if (this.closing) return;
    if (!this.helloDone && now - this.openedAt >= this.config.helloTimeoutMs) {
      this.closing = true;
      this.lobby.log('closed: no hello in time');
      this.ws.close(1008, 'hello timeout');
      return;
    }
    if (this.behindSince) {
      if (this.bufferedAmount <= this.config.maxBufferedBytes) this.behindSince = 0;
      else if (now - this.behindSince >= this.config.slowReaderMs) return this.#dropSlowReader();
    }
    if (this.pingSentAt) {
      if (now - this.pingSentAt >= this.config.pongWaitMs) {
        this.closing = true;
        this.ws.terminate();
      }
    } else if (now - this.lastPongAt >= this.config.idleMs) {
      this.pingSentAt = now;
      if (this.ws.readyState === OPEN) this.ws.ping();
    }
  }

  /**
   * Backpressure. ws keeps everything we send in memory until the network takes it, so a client
   * that stops reading would make us hold more and more. Above the limit, messages for it are
   * dropped; if it stays above the limit too long, checkIdle closes the socket.
   */
  #hasRoom() {
    if (this.bufferedAmount <= this.config.maxBufferedBytes) {
      this.behindSince = 0;
      return true;
    }
    const now = this.now();
    if (!this.behindSince) this.behindSince = now;
    else if (now - this.behindSince >= this.config.slowReaderMs) this.#dropSlowReader();
    return false;
  }

  #dropSlowReader() {
    if (this.closing) return;
    this.closing = true;
    this.lobby.log('closed: receiver too far behind');
    // A close frame would only queue behind everything else, so cut the socket at once.
    this.ws.terminate();
  }

  #logError(where, error) {
    try {
      this.lobby.log(`error while handling ${where}: ${error && error.name}: ${error && error.message}`);
    } catch {
      // Logging must never be the thing that fails.
    }
  }

  #onMessage(data, isBinary) {
    const now = this.now();

    // 1. Rate limit first, so a flood of junk costs us as little work as possible.
    const verdict = this.limiter.take(now);
    if (verdict === 'close') {
      this.sendError('rate');
      this.ws.close(1008, 'rate');
      return;
    }
    if (verdict === 'warn') this.sendError('rate');
    if (verdict !== 'ok') return;

    // 2. Size limit. (ws itself closes the socket if a frame is far bigger, see server.js.)
    if (data.length > this.config.maxMessageBytes) return this.sendError('too-big');

    // 3. Binary frames are fallback game traffic. The lobby drops them unless this player is in a
    //    started room, and nobody can be in a room before saying hello.
    if (isBinary) return this.lobby.relayBinary(this, data);

    // 4. Text frames are JSON control messages.
    let msg;
    try {
      msg = JSON.parse(data.toString('utf8'));
    } catch {
      return this.sendError('bad-request');
    }
    if (typeof msg !== 'object' || msg === null || typeof msg.t !== 'string') {
      return this.sendError('bad-request');
    }

    if (msg.t === 'hello') return this.#hello(msg);
    if (!this.helloDone) return this.sendError('bad-request'); // hello must come first
    if (msg.t === 'ping') {
      // n is sent back as it is, so only a plain number is accepted (see shapes.js).
      if (!isPingNumber(msg.n)) return this.sendError('bad-request');
      return this.sendJson({ t: 'pong', n: msg.n });
    }
    this.lobby.handle(this, msg);
  }

  #hello(msg) {
    if (msg.v !== PROTOCOL_VERSION) {
      this.sendError('version');
      this.ws.close(1002, 'version');
      return;
    }
    this.helloDone = true;
    this.sendJson({ t: 'welcome', v: PROTOCOL_VERSION });
  }
}
