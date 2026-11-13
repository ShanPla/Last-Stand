// All settings in one place. Defaults match PROTOCOL.md. Every limit can be lowered with an
// environment variable (or an object passed to createRelay), so tests do not have to wait
// minutes or open thousands of sockets.

/** Defaults taken from the Limits table in PROTOCOL.md. */
export const DEFAULTS = Object.freeze({
  port: 8787,
  allowedOrigins: [],
  production: false,
  maxMessageBytes: 16 * 1024,
  ratePerSec: 150,
  rateBurst: 300,
  rateCloseAfterMs: 10_000,
  maxRooms: 200,
  maxConnections: 1000,
  idleMs: 60_000,
  pongWaitMs: 10_000,
  roomMaxAgeMs: 3 * 60 * 60 * 1000,
  // A socket that has not sent a good hello by then is closed.
  helloTimeoutMs: 10_000,
  // A receiver this far behind gets no more messages until it catches up...
  maxBufferedBytes: 1024 * 1024,
  // ...and is closed when it stays that far behind for this long.
  slowReaderMs: 10_000,
  // Generous, because a whole school or cafe can share one address.
  maxConnectionsPerIp: 20,
  maxRoomsPerIp: 10,
  // Where the client address comes from, see clientIpSource below. Empty means the default.
  clientIpHeader: '',
});

// Environment variable name for each numeric limit.
const NUMBER_ENV = {
  maxMessageBytes: 'MAX_MESSAGE_BYTES',
  ratePerSec: 'RATE_PER_SEC',
  rateBurst: 'RATE_BURST',
  rateCloseAfterMs: 'RATE_CLOSE_AFTER_MS',
  maxRooms: 'MAX_ROOMS',
  maxConnections: 'MAX_CONNECTIONS',
  idleMs: 'IDLE_MS',
  pongWaitMs: 'PONG_WAIT_MS',
  roomMaxAgeMs: 'ROOM_MAX_AGE_MS',
  helloTimeoutMs: 'HELLO_TIMEOUT_MS',
  maxBufferedBytes: 'MAX_BUFFERED_BYTES',
  slowReaderMs: 'SLOW_READER_MS',
  maxConnectionsPerIp: 'MAX_CONNECTIONS_PER_IP',
  maxRoomsPerIp: 'MAX_ROOMS_PER_IP',
};

/** Reads a positive number, or returns the fallback when it is missing or not a number. */
function positiveNumber(text, fallback) {
  const n = Number(text);
  return text !== undefined && text !== '' && Number.isFinite(n) && n > 0 ? n : fallback;
}

/** Turns "https://a.com, https://b.com/" into a clean list without trailing slashes. */
export function parseOrigins(text) {
  return String(text ?? '')
    .split(',')
    .map((s) => s.trim().replace(/\/+$/, ''))
    .filter(Boolean);
}

/**
 * Builds the settings object from environment variables.
 * @param {Record<string, string | undefined>} env usually process.env
 */
export function loadConfig(env = process.env) {
  const config = {
    ...DEFAULTS,
    port: Number.isInteger(Number(env.PORT)) && env.PORT !== '' && env.PORT !== undefined
      ? Number(env.PORT)
      : DEFAULTS.port,
    allowedOrigins: parseOrigins(env.ALLOWED_ORIGINS),
    production: env.NODE_ENV === 'production',
    clientIpHeader: String(env.CLIENT_IP_HEADER ?? '').trim().toLowerCase(),
  };
  for (const [key, name] of Object.entries(NUMBER_ENV)) {
    config[key] = positiveNumber(env[name], DEFAULTS[key]);
  }
  return config;
}

/**
 * Says where the per-address limits read the client address from.
 * - `CLIENT_IP_HEADER` set to a header name (for example `cf-connecting-ip`): that header. This
 *   is only safe when the proxy in front of the server always overwrites the header, because a
 *   client can send any header it likes. Never use `x-forwarded-for` behind a proxy that only
 *   appends to it.
 * - `CLIENT_IP_HEADER=socket`: the TCP peer address, for a server that faces the internet itself.
 * - Not set: the TCP peer address while developing. In production the TCP peer is usually the
 *   host's proxy, so every player would seem to share one address and the limits would turn into
 *   a tiny cap for the whole server. So in production they stay off until this is set.
 * @param {{clientIpHeader?: string, production?: boolean}} config
 * @returns {{kind: 'header', name: string} | {kind: 'socket'} | {kind: 'off'}}
 */
export function clientIpSource(config) {
  const name = String(config.clientIpHeader ?? '').trim().toLowerCase();
  if (name === 'socket') return { kind: 'socket' };
  if (name) return { kind: 'header', name };
  return config.production ? { kind: 'off' } : { kind: 'socket' };
}

/**
 * The client address for one upgrade request, or null when the per-address limits do not apply
 * to it. A configured header that is missing also gives null, not the socket address: behind a
 * proxy the socket address is the proxy's, and counting every such request under it would turn
 * the per-address limits into one tiny limit for everybody. IPv4 addresses written in IPv6 form
 * (`::ffff:1.2.3.4`) are shortened, so one client always gets one key.
 * @param {import('node:http').IncomingMessage} req
 * @param {ReturnType<typeof clientIpSource>} source
 */
export function clientIp(req, source) {
  if (source.kind === 'off') return null;
  let ip;
  if (source.kind === 'header') {
    const value = req.headers[source.name];
    ip = String(Array.isArray(value) ? value[0] : value ?? '').split(',')[0].trim().slice(0, 64);
  } else {
    ip = String(req.socket?.remoteAddress ?? '');
  }
  if (ip.startsWith('::ffff:')) ip = ip.slice(7);
  return ip || null;
}
