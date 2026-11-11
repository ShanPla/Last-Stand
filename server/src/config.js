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
  };
  for (const [key, name] of Object.entries(NUMBER_ENV)) {
    config[key] = positiveNumber(env[name], DEFAULTS[key]);
  }
  return config;
}
