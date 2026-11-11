// Plain HTTP side of the server. It only has to answer the health check.
import { isOriginAllowed } from './origin.js';

/**
 * Render calls /healthz to see that the service is alive. The browser also calls it to wake a
 * sleeping free instance before it opens the WebSocket.
 * @param {ReturnType<import('./config.js').loadConfig>} config
 */
export function createHttpHandler(config) {
  return (req, res) => {
    const path = (req.url ?? '').split('?')[0];
    if (path === '/healthz' && (req.method === 'GET' || req.method === 'HEAD')) {
      const headers = { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' };
      // Let our own page read the answer, so it can tell "awake" from "still waking".
      const origin = req.headers.origin;
      if (origin && isOriginAllowed(origin, config)) {
        headers['Access-Control-Allow-Origin'] = origin;
        headers.Vary = 'Origin';
      }
      res.writeHead(200, headers);
      res.end('ok');
      return;
    }
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    res.end('not found');
  };
}
