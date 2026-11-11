import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { Client, joinRoom, makeRoom, startRelay, startedRoom } from './helpers.js';

describe('binary fallback', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('binary frames are dropped until the host has started the room', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    host.sendBinary([1, 9, 9, 9]);
    ben.sendBinary([0, 8, 8, 8]);
    // Each settle proves that socket's earlier frames were handled; the second host settle
    // proves nothing was forwarded to it from Ben.
    assert.deepEqual(await host.settle(), []);
    assert.deepEqual(await ben.settle(), []);
    assert.deepEqual(await host.settle(), []);
    // After start the same frame goes through.
    host.sendJson({ t: 'start' });
    await host.nextOfType('started');
    await ben.nextOfType('started');
    host.sendBinary([1, 9, 9, 9]);
    assert.ok(Buffer.from([0, 9, 9, 9]).equals(await ben.nextBinary()));
    await ben.close();
    await host.close();
  });

  test('binary frames from a connection outside any room are dropped', async () => {
    const { clients } = await startedRoom(s.url, 2);
    const stranger = await Client.connect(s.url);
    stranger.sendBinary([0, 1, 2, 3]);
    assert.deepEqual(await stranger.settle(), []);
    assert.deepEqual(await clients[0].settle(), []);
    for (const c of [stranger, ...clients]) await c.close();
  });

  test('a frame to one target arrives with the sender id in byte 0 and the payload unchanged', async () => {
    const { clients } = await startedRoom(s.url, 3);
    const [p0, p1, p2] = clients;
    p0.sendBinary([2, 10, 20, 30, 40]);
    const got = await p2.nextBinary();
    assert.ok(Buffer.from([0, 10, 20, 30, 40]).equals(got), `got ${[...got]}`);
    assert.deepEqual(await p1.settle(), []);
    assert.deepEqual(await p0.settle(), []);
    p2.sendBinary([1, 5]);
    assert.ok(Buffer.from([2, 5]).equals(await p1.nextBinary()));
    for (const c of clients) await c.close();
  });

  test('target 255 reaches every other player and not the sender', async () => {
    const { clients } = await startedRoom(s.url, 4);
    const [p0, p1, p2, p3] = clients;
    p1.sendBinary([255, 7, 7]);
    for (const c of [p0, p2, p3]) assert.ok(Buffer.from([1, 7, 7]).equals(await c.nextBinary()));
    assert.deepEqual(await p1.settle(), []);
    for (const c of clients) await c.close();
  });

  test('the payload is passed through byte for byte, including every byte value', async () => {
    const { clients } = await startedRoom(s.url, 2);
    const payload = Buffer.from(Array.from({ length: 256 }, (_, i) => i));
    clients[1].sendBinary(Buffer.concat([Buffer.from([0]), payload]));
    const got = await clients[0].nextBinary();
    assert.equal(got[0], 1);
    assert.ok(got.subarray(1).equals(payload));
    for (const c of clients) await c.close();
  });

  test('a frame that is only the target byte arrives as just the sender id', async () => {
    const { clients } = await startedRoom(s.url, 2);
    clients[0].sendBinary([1]);
    assert.ok(Buffer.from([0]).equals(await clients[1].nextBinary()));
    for (const c of clients) await c.close();
  });

  test('frames to empty slots, to yourself, or to a nonsense id are dropped', async () => {
    const { clients } = await startedRoom(s.url, 2);
    const [p0, p1] = clients;
    for (const target of [2, 3, 0, 4, 100, 254]) p0.sendBinary([target, 1, 2]);
    assert.deepEqual(await p0.settle(), []);
    assert.deepEqual(await p1.settle(), []);
    // And the connection still works afterwards.
    p0.sendBinary([1, 42]);
    assert.ok(Buffer.from([0, 42]).equals(await p1.nextBinary()));
    for (const c of clients) await c.close();
  });

  test('frames never cross into another room', async () => {
    const a = await startedRoom(s.url, 2);
    const b = await startedRoom(s.url, 2);
    a.clients[0].sendBinary([255, 1]);
    assert.ok(Buffer.from([0, 1]).equals(await a.clients[1].nextBinary()));
    assert.deepEqual(await b.clients[0].settle(), []);
    assert.deepEqual(await b.clients[1].settle(), []);
    for (const c of [...a.clients, ...b.clients]) await c.close();
  });

  test('a player who left no longer receives frames', async () => {
    const { clients } = await startedRoom(s.url, 3);
    const [p0, p1, p2] = clients;
    p2.sendJson({ t: 'leave' });
    await p0.nextOfType('room');
    await p1.nextOfType('room');
    p0.sendBinary([2, 1]);
    assert.deepEqual(await p2.settle(), []); // p2 is out of the room
    assert.deepEqual(await p1.settle(), []);
    for (const c of clients) await c.close();
  });

  test('relay on is accepted in a room, and needs a boolean', async () => {
    const { clients } = await startedRoom(s.url, 2);
    clients[1].sendJson({ t: 'relay', on: true });
    assert.deepEqual(await clients[1].settle(), []); // no error
    clients[1].sendJson({ t: 'relay', on: 'yes' });
    assert.deepEqual(await clients[1].nextJson(), { t: 'error', code: 'bad-request' });
    for (const c of clients) await c.close();
  });
});
