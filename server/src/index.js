// Entry point for `npm start`: reads the environment, starts the relay, and stops cleanly when
// the host asks (Render sends SIGTERM when it replaces or stops the service).
import { loadConfig } from './config.js';
import { createRelay } from './server.js';

// Last resort. Every message, timer and upgrade handler already catches its own errors, so this
// should never run. If it does, the error is logged and the relay keeps going: the rooms of every
// other player are worth more than a clean slate. That is safe enough here because the state is
// small and simple, and if a throw left one WebSocket half-read, that socket stops answering
// pings and the idle timer closes it. But a server that keeps failing has a real bug, so after
// several failures in a minute it exits and lets the host (Render) start a fresh copy.
const CRASH_WINDOW_MS = 60_000;
const CRASH_LIMIT = 5;
let crashTimes = [];
function lastResort(kind, error) {
  const now = Date.now();
  crashTimes = crashTimes.filter((t) => now - t < CRASH_WINDOW_MS);
  crashTimes.push(now);
  console.error(`${kind}: ${(error && error.stack) || error}`);
  if (crashTimes.length >= CRASH_LIMIT) {
    console.error(`${CRASH_LIMIT} unexpected errors within a minute, exiting so the host restarts us`);
    process.exit(1);
  }
}
process.on('uncaughtException', (error) => lastResort('uncaught exception', error));
process.on('unhandledRejection', (reason) => lastResort('unhandled rejection', reason));

const config = loadConfig(process.env);
const relay = createRelay(config);
const port = await relay.listen();
console.log(
  `relay listening on port ${port} (${config.production ? 'production' : 'development'}, ` +
    `${config.allowedOrigins.length} allowed origin(s))`,
);
const ip = relay.ipSource;
console.log(
  ip.kind === 'off'
    ? 'per-address limits are off: set CLIENT_IP_HEADER to turn them on (see PROTOCOL.md)'
    : `per-address limits read the address from ${ip.kind === 'header' ? `the ${ip.name} header` : 'the socket'}`,
);

let stopping = false;
async function shutdown(signal) {
  if (stopping) return;
  stopping = true;
  console.log(`${signal} received, closing`);
  // If something refuses to close, do not hang forever: Render would kill us anyway.
  setTimeout(() => process.exit(1), 5000).unref();
  await relay.close();
  process.exit(0);
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
