import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { joinRoom, makeRoom, startRelay } from './helpers.js';

describe('signaling', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  async function threeInARoom() {
    const { host, code } = await makeRoom(s.url, 'P0');
    const { client: p1 } = await joinRoom(s.url, code, 'P1');
    const { client: p2 } = await joinRoom(s.url, code, 'P2');
    for (const c of [host, p1, p2]) await c.settle();
    return { host, p1, p2 };
  }

  test('a signal reaches only its target, with from set and data unchanged', async () => {
    const { host, p1, p2 } = await threeInARoom();
    const data = { kind: 'offer', sdp: 'v=0\r\no=- 1 2 IN IP4 127.0.0.1', nested: { a: [1, 2, 3] } };
    host.sendJson({ t: 'signal', to: 2, data });
    assert.deepEqual(await p2.nextJson(), { t: 'signal', from: 0, data });
    // The others saw nothing, and the sender got nothing back.
    assert.deepEqual(await p1.settle(), []);
    assert.deepEqual(await host.settle(), []);
    for (const c of [host, p1, p2]) await c.close();
  });

  test('a signal can go from any player to any other, and from is the sender slot', async () => {
    const { host, p1, p2 } = await threeInARoom();
    p2.sendJson({ t: 'signal', to: 1, data: { candidate: 'x' } });
    assert.deepEqual(await p1.nextJson(), { t: 'signal', from: 2, data: { candidate: 'x' } });
    p1.sendJson({ t: 'signal', to: 0, data: { answer: true } });
    assert.deepEqual(await host.nextJson(), { t: 'signal', from: 1, data: { answer: true } });
    for (const c of [host, p1, p2]) await c.close();
  });

  test('from cannot be faked by the sender', async () => {
    const { host, p1, p2 } = await threeInARoom();
    host.sendJson({ t: 'signal', to: 1, from: 2, data: { a: 1 } });
    assert.equal((await p1.nextJson()).from, 0);
    for (const c of [host, p1, p2]) await c.close();
  });

  test('a signal to an empty slot or to yourself is dropped without breaking anything', async () => {
    const { host, p1, p2 } = await threeInARoom();
    host.sendJson({ t: 'signal', to: 3, data: { a: 1 } });
    host.sendJson({ t: 'signal', to: 0, data: { a: 1 } });
    assert.deepEqual(await host.settle(), []);
    assert.deepEqual(await p1.settle(), []);
    assert.deepEqual(await p2.settle(), []);
    for (const c of [host, p1, p2]) await c.close();
  });

  test('a signal with a bad target or no data is bad-request', async () => {
    const { host, p1, p2 } = await threeInARoom();
    for (const msg of [
      { t: 'signal', to: 'x', data: {} },
      { t: 'signal', to: 4, data: {} },
      { t: 'signal', to: -1, data: {} },
      { t: 'signal', to: 1.5, data: {} },
      { t: 'signal', to: 1 },
      { t: 'signal', to: 1, data: null },
      { t: 'signal', to: 1, data: 'text' },
    ]) {
      host.sendJson(msg);
      assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' }, JSON.stringify(msg));
    }
    assert.deepEqual(await p1.settle(), []);
    for (const c of [host, p1, p2]) await c.close();
  });

  test('signals do not cross between rooms', async () => {
    const a = await makeRoom(s.url, 'A0');
    const { client: a1 } = await joinRoom(s.url, a.code, 'A1');
    const b = await makeRoom(s.url, 'B0');
    const { client: b1 } = await joinRoom(s.url, b.code, 'B1');
    for (const c of [a.host, a1, b.host, b1]) await c.settle();
    a.host.sendJson({ t: 'signal', to: 1, data: { for: 'A1' } });
    assert.deepEqual(await a1.nextJson(), { t: 'signal', from: 0, data: { for: 'A1' } });
    assert.deepEqual(await b1.settle(), []);
    assert.deepEqual(await b.host.settle(), []);
    for (const c of [a.host, a1, b.host, b1]) await c.close();
  });
});
