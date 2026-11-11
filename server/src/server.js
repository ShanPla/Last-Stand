// Puts the pieces together: an HTTP server, the WebSocket upgrade with its checks, and one timer
// that handles room expiry and idle sockets. index.js starts it, and the tests start their own.
import { createServer } from 'node:http';
import { WebSocketServer } from 'ws';
import { DEFAULTS } from './config.js';
import { Connection } from './connection.js';
import { createHttpHandler } from './http.js';
import { Lobby } from './lobby.js';
import { isOriginAllowed } from './origin.js';
import { RoomRegistry } from './rooms.js';

/**
 * @param {Partial<typeof DEFAULTS>} [overrides] settings, see config.js. Anything left out uses
 *   the default.
 * @param {(line: string) => void} [log]
 */
export function createRelay(overrides = {}, log = (line) => console.log(line)) {
  const config = { ...DEFAULTS, ...overrides };
  const clock = () => performance.now();

  const registry = new RoomRegistry(config);
  // Room age uses the wall clock (Date.now) so it matches what a person would expect.
  const lobby = new Lobby(registry, log, Date.now);
  /** @type {Set<Connection>} */
  const connections = new Set();

  const http = createServer(createHttpHandler(config));
  const wss = new WebSocketServer({
    noServer: true, // we do the upgrade ourselves, so we can refuse with a proper HTTP status
    // ws closes the socket (code 1009) for any frame above this. Frames between our own limit
    // and this one get a polite `too-big` error instead. Bigger ones are just abuse.
    maxPayload: config.maxMessageBytes * 4,
    perMessageDeflate: false, // compression costs CPU, and our messages are tiny
  });

  http.on('upgrade', (req, socket, head) => {
    if (!isOriginAllowed(req.headers.origin, config)) {
      log('connection refused: origin');
      return refuse(socket, '403 Forbidden');
    }
    if (connections.size >= config.maxConnections) {
      log('connection refused: server busy');
      return refuse(socket, '503 Service Unavailable');
    }
    wss.handleUpgrade(req, socket, head, (ws) => {
      const conn = new Connection(ws, lobby, config, clock);
      connections.add(conn);
      ws.on('close', () => connections.delete(conn));
    });
  });

  // One timer for both chores. It must tick often enough for the shortest limit.
  const tickMs = Math.max(
    10,
    Math.min(5000, config.idleMs / 4, config.pongWaitMs / 4, config.roomMaxAgeMs / 4),
  );
  const timer = setInterval(() => {
    lobby.expireRooms();
    const now = clock();
    for (const conn of connections) conn.checkIdle(now);
  }, tickMs);
  timer.unref(); // the timer alone should never keep the process alive

  return {
    config,
    /** Starts listening. Pass 0 to let the system pick a free port. Resolves to the port. */
    listen(port = config.port) {
      return new Promise((resolve, reject) => {
        http.once('error', reject);
        http.listen(port, () => {
          http.off('error', reject);
          resolve(http.address().port);
        });
      });
    },
    roomCount: () => registry.count,
    connectionCount: () => connections.size,
    /** Stops cleanly: tells every client we are going away, then stops listening. */
    close() {
      clearInterval(timer);
      for (const conn of connections) conn.ws.close(1001, 'server stopping');
      return new Promise((resolve) => {
        wss.close();
        http.close(() => resolve());
        http.closeAllConnections();
        // Give polite closes a moment, then cut anything that is still hanging on.
        setTimeout(() => {
          for (const conn of connections) conn.ws.terminate();
        }, 1000).unref();
      });
    },
  };
}

/** Answers a refused upgrade with a short HTTP error and hangs up. */
function refuse(socket, status) {
  socket.write(`HTTP/1.1 ${status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  socket.destroy();
}
