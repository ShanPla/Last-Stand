import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { startRelay } from './helpers.js';

describe('http', () => {
  let s;
  before(async () => {
    s = await startRelay({ allowedOrigins: ['https://shanpla.github.io'], production: true });
  });
  after(() => s.stop());
  const http = (path, init) => fetch(`http://127.0.0.1:${s.port}${path}`, init);

  test('GET /healthz answers 200 with the body ok', async () => {
    const res = await http('/healthz');
    assert.equal(res.status, 200);
    assert.equal(await res.text(), 'ok');
  });

  test('HEAD /healthz answers 200', async () => {
    const res = await http('/healthz', { method: 'HEAD' });
    assert.equal(res.status, 200);
  });

  test('/healthz ignores a query string', async () => {
    const res = await http('/healthz?wake=1');
    assert.equal(res.status, 200);
  });

  test('any other path answers 404', async () => {
    for (const path of ['/', '/index.html', '/healthz/extra', '/health']) {
      const res = await http(path);
      assert.equal(res.status, 404, path);
    }
  });

  test('POST /healthz answers 404', async () => {
    const res = await http('/healthz', { method: 'POST' });
    assert.equal(res.status, 404);
  });

  test('healthz lets an allowed page read the answer, and nobody else', async () => {
    const good = await http('/healthz', { headers: { Origin: 'https://shanpla.github.io' } });
    assert.equal(good.headers.get('access-control-allow-origin'), 'https://shanpla.github.io');
    const bad = await http('/healthz', { headers: { Origin: 'https://evil.example' } });
    assert.equal(bad.status, 200);
    assert.equal(bad.headers.get('access-control-allow-origin'), null);
  });
});
