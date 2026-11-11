// Origin check. Browsers always send an Origin header on a WebSocket handshake and page
// scripts cannot fake it, so this stops other websites from using our relay.
// (It does not stop a non-browser program, which can send any header. That is fine here,
// because the relay holds no secrets and has its own limits.)

const LOCAL_ORIGIN = /^http:\/\/(localhost|127\.0\.0\.1)(:\d{1,5})?$/;

/**
 * @param {string | undefined} origin the Origin header, if any
 * @param {{allowedOrigins: string[], production: boolean}} config
 * @returns {boolean}
 */
export function isOriginAllowed(origin, config) {
  if (!origin) {
    // No header means the caller is not a browser (a test script, wscat). Only allow it
    // while developing.
    return !config.production;
  }
  const clean = origin.replace(/\/+$/, '');
  if (config.allowedOrigins.includes(clean)) return true;
  return !config.production && LOCAL_ORIGIN.test(clean);
}
