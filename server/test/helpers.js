// Shared test tools: start a real relay on a random port, and talk to it with real WebSocket
// clients. Each test file starts its own relay so files cannot disturb each other.
import { WebSocket } from 'ws';
import { createRelay } from '../src/server.js';

export const DEV_ORIGIN = 'http://localhost:5173';

/**
 * @param {object} [overrides] settings for the relay, see src/config.js
 * @returns {Promise<{relay: any, port: number, url: string, logs: string[], stop: () => Promise<void>}>}
 */
export async function startRelay(overrides = {}) {
  const logs = [];
  const relay = createRelay(overrides, (line) => logs.push(line));
  const port = await relay.listen(0);
  return {
    relay,
    port,
    url: `ws://127.0.0.1:${port}`,
    logs,
    stop: () => relay.close(),
  };
}

export class Client {
  /** Opens a connection. Rejects if the server refuses the handshake. */
  static open(url, { origin = DEV_ORIGIN, autoPong = true } = {}) {
    return new Promise((resolve, reject) => {
      const options = { autoPong };
      if (origin) options.origin = origin;
      const ws = new WebSocket(url, options);
      const client = new Client(ws);
      ws.once('open', () => resolve(client));
      ws.once('unexpected-response', (req, res) => reject(Object.assign(new Error('refused'), { status: res.statusCode })));
      ws.once('error', (err) => reject(err));
    });
  }

  /** Opens a connection and completes the hello exchange. */
  static async connect(url, options) {
    const client = await Client.open(url, options);
    client.sendJson({ t: 'hello', v: 1 });
    const welcome = await client.nextJson();
    if (welcome.t !== 'welcome') throw new Error(`expected welcome, got ${JSON.stringify(welcome)}`);
    return client;
  }

  constructor(ws) {
    this.ws = ws;
    this.queue = [];
    this.waiters = [];
    this.closeInfo = null;
    this.closedPromise = new Promise((resolve) => {
      ws.once('close', (code, reason) => {
        this.closeInfo = { code, reason: reason.toString() };
        resolve(this.closeInfo);
        this.#wake();
      });
    });
    ws.on('message', (data, isBinary) => {
      this.queue.push({ isBinary, data: isBinary ? data : data.toString() });
      this.#wake();
    });
    ws.on('error', () => {});
  }

  #wake() {
    const waiters = this.waiters;
    this.waiters = [];
    for (const w of waiters) w();
  }

  sendJson(obj) {
    this.ws.send(JSON.stringify(obj));
  }

  sendText(text) {
    this.ws.send(text);
  }

  sendBinary(bytes) {
    this.ws.send(Buffer.from(bytes), { binary: true });
  }

  /** Waits for the next frame of any kind. */
  async next(timeoutMs = 2000) {
    const deadline = Date.now() + timeoutMs;
    while (this.queue.length === 0) {
      if (this.closeInfo) throw new Error('socket closed while waiting for a message');
      const left = deadline - Date.now();
      if (left <= 0) throw new Error('timed out waiting for a message');
      await new Promise((resolve) => {
        const timer = setTimeout(resolve, left);
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
    return this.queue.shift();
  }

  async nextJson(timeoutMs) {
    const frame = await this.next(timeoutMs);
    if (frame.isBinary) throw new Error('expected JSON, got a binary frame');
    return JSON.parse(frame.data);
  }

  async nextBinary(timeoutMs) {
    const frame = await this.next(timeoutMs);
    if (!frame.isBinary) throw new Error(`expected binary, got ${frame.data}`);
    return frame.data;
  }

  /** Waits until the next JSON message with this type, skipping others. */
  async nextOfType(type, timeoutMs) {
    for (;;) {
      const msg = await this.nextJson(timeoutMs);
      if (msg.t === type) return msg;
    }
  }

  /**
   * Proves nothing else is on its way: sends a ping and collects everything that arrives before
   * the pong. Messages are handled in order, so anything the server sent earlier shows up first.
   * @returns {Promise<any[]>} the frames received before the pong
   */
  async settle() {
    const n = Math.floor(Math.random() * 1e9);
    this.sendJson({ t: 'ping', n });
    const seen = [];
    for (;;) {
      const frame = await this.next();
      if (!frame.isBinary) {
        const msg = JSON.parse(frame.data);
        if (msg.t === 'pong' && msg.n === n) return seen;
        seen.push(msg);
      } else {
        seen.push(frame.data);
      }
    }
  }

  close() {
    this.ws.close();
    return this.closedPromise;
  }
}

/** Connects a host who creates a room. Resolves to the client and the room code. */
export async function makeRoom(url, name = 'Ana') {
  const host = await Client.connect(url);
  host.sendJson({ t: 'create', name });
  const room = await host.nextJson();
  return { host, code: room.code, room };
}

/** Connects a player and joins a room. Returns the client and the first room message. */
export async function joinRoom(url, code, name) {
  const client = await Client.connect(url);
  client.sendJson({ t: 'join', code, name });
  const room = await client.nextJson();
  return { client, room };
}

/** Makes a started room with `count` players (host first). Everything queued is already read. */
export async function startedRoom(url, count = 2) {
  const { host, code } = await makeRoom(url, 'P0');
  const clients = [host];
  for (let i = 1; i < count; i++) {
    const { client } = await joinRoom(url, code, `P${i}`);
    clients.push(client);
  }
  host.sendJson({ t: 'start' });
  for (const c of clients) {
    await c.nextOfType('started'); // skips the lobby updates on the way
  }
  return { clients, code };
}

/** Sleeps for the given milliseconds. */
export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
