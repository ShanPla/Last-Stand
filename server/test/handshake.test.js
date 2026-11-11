import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { Client, startRelay } from './helpers.js';

describe('hello, version and ping', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('a good hello is answered with welcome', async () => {
    const c = await Client.open(s.url);
    c.sendJson({ t: 'hello', v: 1 });
    assert.deepEqual(await c.nextJson(), { t: 'welcome', v: 1 });
    await c.close();
  });

  test('a wrong version gets error version and the socket closes', async () => {
    const c = await Client.open(s.url);
    c.sendJson({ t: 'hello', v: 2 });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'version' });
    const info = await c.closedPromise;
    assert.ok(info.code > 1000);
  });

  test('a hello with no version is a version error too', async () => {
    const c = await Client.open(s.url);
    c.sendJson({ t: 'hello' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'version' });
    await c.closedPromise;
  });

  test('anything before hello is a bad-request and does not work', async () => {
    const c = await Client.open(s.url);
    c.sendJson({ t: 'create', name: 'Ana' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    assert.equal(s.relay.roomCount(), 0);
    await c.close();
  });

  test('bad JSON, non-objects and unknown types are bad-request', async () => {
    const c = await Client.connect(s.url);
    c.sendText('{not json');
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendText('42');
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendText('null');
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendJson({ nothing: true });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendJson({ t: 'dance' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    await c.close();
  });

  test('ping is answered with a pong carrying the same n', async () => {
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'ping', n: 17 });
    assert.deepEqual(await c.nextJson(), { t: 'pong', n: 17 });
    c.sendJson({ t: 'ping', n: 18 });
    assert.deepEqual(await c.nextJson(), { t: 'pong', n: 18 });
    await c.close();
  });

  test('room actions before joining a room are not-in-room', async () => {
    const c = await Client.connect(s.url);
    for (const msg of [
      { t: 'ready', ready: true },
      { t: 'start' },
      { t: 'leave' },
      { t: 'signal', to: 1, data: {} },
      { t: 'relay', on: true },
    ]) {
      c.sendJson(msg);
      assert.deepEqual(await c.nextJson(), { t: 'error', code: 'not-in-room' }, msg.t);
    }
    await c.close();
  });
});
