import assert from 'node:assert/strict';
import { describe, test } from 'node:test';
import { loadConfig, parseOrigins } from '../src/config.js';
import { isOriginAllowed } from '../src/origin.js';
import { Client, startRelay } from './helpers.js';

const SITE = 'https://shanpla.github.io';

describe('origin check on a real server', () => {
  test('an allowed origin connects', async () => {
    const s = await startRelay({ allowedOrigins: [SITE], production: true });
    const c = await Client.connect(s.url, { origin: SITE });
    await c.close();
    await s.stop();
  });

  test('an origin that is not listed is refused with 403 and logged', async () => {
    const s = await startRelay({ allowedOrigins: [SITE], production: true });
    await assert.rejects(Client.open(s.url, { origin: 'https://evil.example' }), { status: 403 });
    assert.ok(s.logs.some((l) => l.includes('refused')));
    assert.equal(s.relay.connectionCount(), 0);
    await s.stop();
  });

  test('a lookalike origin is refused', async () => {
    const s = await startRelay({ allowedOrigins: [SITE], production: true });
    await assert.rejects(Client.open(s.url, { origin: `${SITE}.evil.example` }), { status: 403 });
    await assert.rejects(Client.open(s.url, { origin: 'http://shanpla.github.io' }), { status: 403 });
    await s.stop();
  });

  test('localhost on any port is allowed when not in production', async () => {
    const s = await startRelay({ allowedOrigins: [SITE], production: false });
    for (const origin of ['http://localhost:5173', 'http://127.0.0.1:8080', 'http://localhost']) {
      const c = await Client.connect(s.url, { origin });
      await c.close();
    }
    await s.stop();
  });

  test('localhost is refused in production', async () => {
    const s = await startRelay({ allowedOrigins: [SITE], production: true });
    await assert.rejects(Client.open(s.url, { origin: 'http://localhost:5173' }), { status: 403 });
    await assert.rejects(Client.open(s.url, { origin: 'http://127.0.0.1:5173' }), { status: 403 });
    await s.stop();
  });

  test('a lookalike of localhost is refused even in development', async () => {
    const s = await startRelay({ production: false });
    await assert.rejects(Client.open(s.url, { origin: 'http://localhost.evil.example' }), { status: 403 });
    await assert.rejects(Client.open(s.url, { origin: 'https://localhost:5173' }), { status: 403 });
    await s.stop();
  });

  test('a missing Origin header is allowed in development and refused in production', async () => {
    const dev = await startRelay({ production: false });
    const c = await Client.connect(dev.url, { origin: null });
    await c.close();
    await dev.stop();

    const prod = await startRelay({ production: true, allowedOrigins: [SITE] });
    await assert.rejects(Client.open(prod.url, { origin: null }), { status: 403 });
    await prod.stop();
  });

  test('the logged refusal never contains the origin', async () => {
    const s = await startRelay({ production: true });
    await assert.rejects(Client.open(s.url, { origin: 'https://secret-site.example' }), { status: 403 });
    assert.ok(s.logs.length > 0);
    assert.ok(!s.logs.join('\n').includes('secret-site'));
    await s.stop();
  });
});

describe('origin rules and config parsing', () => {
  test('isOriginAllowed ignores a trailing slash', () => {
    const config = { allowedOrigins: [SITE], production: true };
    assert.equal(isOriginAllowed(`${SITE}/`, config), true);
  });

  test('parseOrigins splits on commas, trims and drops empties and trailing slashes', () => {
    assert.deepEqual(parseOrigins(' https://a.com/ , ,https://b.com'), ['https://a.com', 'https://b.com']);
    assert.deepEqual(parseOrigins(undefined), []);
  });

  test('loadConfig reads the environment and ignores bad numbers', () => {
    const config = loadConfig({
      PORT: '9000',
      NODE_ENV: 'production',
      ALLOWED_ORIGINS: 'https://a.com',
      MAX_ROOMS: '5',
      RATE_PER_SEC: 'lots',
    });
    assert.equal(config.port, 9000);
    assert.equal(config.production, true);
    assert.deepEqual(config.allowedOrigins, ['https://a.com']);
    assert.equal(config.maxRooms, 5);
    assert.equal(config.ratePerSec, 150);
    assert.equal(loadConfig({}).port, 8787);
    assert.equal(loadConfig({}).production, false);
  });
});
