// Entry point for `npm start`: reads the environment, starts the relay, and stops cleanly when
// the host asks (Render sends SIGTERM when it replaces or stops the service).
import { loadConfig } from './config.js';
import { createRelay } from './server.js';

const config = loadConfig(process.env);
const relay = createRelay(config);
const port = await relay.listen();
console.log(
  `relay listening on port ${port} (${config.production ? 'production' : 'development'}, ` +
    `${config.allowedOrigins.length} allowed origin(s))`,
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
