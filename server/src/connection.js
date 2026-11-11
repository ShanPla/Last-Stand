// One Connection wraps one WebSocket. It runs each incoming frame through the same checks in
// order (rate, size, hello, JSON) before the lobby sees it, and keeps the idle ping state.
import { RateLimiter } from './limits.js';

export const PROTOCOL_VERSION = 1;
const OPEN = 1; // WebSocket.OPEN

export class Connection {
  /**
   * @param {import('ws').WebSocket} ws
   * @param {import('./lobby.js').Lobby} lobby
   * @param {ReturnType<import('./config.js').loadConfig>} config
   * @param {() => number} now steady clock in ms
   */
  constructor(ws, lobby, config, now) {
    this.ws = ws;
    this.lobby = lobby;
    this.config = config;
    this.now = now;
    this.limiter = new RateLimiter({
      perSec: config.ratePerSec,
      burst: config.rateBurst,
      closeAfterMs: config.rateCloseAfterMs,
    });
    this.helloDone = false;
    /** @type {import('./rooms.js').Room | null} */
    this.room = null;
    this.slot = -1;
    this.lastSeen = now();
    this.pingSentAt = 0; // 0 means no ping is waiting for an answer

    ws.on('message', (data, isBinary) => this.#onMessage(data, isBinary));
    ws.on('pong', () => {
      this.lastSeen = this.now();
      this.pingSentAt = 0;
    });
    // Without this handler, a socket error would crash the whole process.
    ws.on('error', () => {});
    ws.on('close', () => lobby.disconnected(this));
  }

  get bufferedAmount() {
    return this.ws.bufferedAmount;
  }

  sendJson(obj) {
    if (this.ws.readyState === OPEN) this.ws.send(JSON.stringify(obj));
  }

  sendBinary(buf) {
    if (this.ws.readyState === OPEN) this.ws.send(buf, { binary: true });
  }

  sendError(code) {
    this.sendJson({ t: 'error', code });
  }

  /** Called by a timer. Pings quiet sockets and drops the ones that never answer. */
  checkIdle(now) {
    if (this.pingSentAt) {
      if (now - this.pingSentAt >= this.config.pongWaitMs) this.ws.terminate();
    } else if (now - this.lastSeen >= this.config.idleMs) {
      this.pingSentAt = now;
      if (this.ws.readyState === OPEN) this.ws.ping();
    }
  }

  #onMessage(data, isBinary) {
    const now = this.now();
    this.lastSeen = now;

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
    if (msg.t === 'ping') return this.sendJson({ t: 'pong', n: msg.n });
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
