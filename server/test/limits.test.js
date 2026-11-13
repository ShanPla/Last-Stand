import assert from 'node:assert/strict';
import { describe, test } from 'node:test';
import { RateLimiter } from '../src/limits.js';
import { Client, joinRoom, makeRoom, sleep, startRelay, startedRoom } from './helpers.js';

describe('RateLimiter on its own, with a fake clock', () => {
  const make = () => new RateLimiter({ perSec: 10, burst: 20, closeAfterMs: 5000 });

  test('allows a burst up to the bucket size, then refuses', () => {
    const rl = make();
    for (let i = 0; i < 20; i++) assert.equal(rl.take(0), 'ok', `message ${i}`);
    assert.notEqual(rl.take(0), 'ok');
  });

  test('refills at the steady rate', () => {
    const rl = make();
    for (let i = 0; i < 20; i++) rl.take(0);
    assert.notEqual(rl.take(0), 'ok');
    // 500 ms refills 5 tokens.
    for (let i = 0; i < 5; i++) assert.equal(rl.take(500), 'ok');
    assert.notEqual(rl.take(500), 'ok');
  });

  test('never holds more than the burst, however long it was quiet', () => {
    const rl = make();
    assert.equal(rl.take(0), 'ok');
    for (let i = 0; i < 20; i++) assert.equal(rl.take(60_000), 'ok');
    assert.notEqual(rl.take(60_000), 'ok');
  });

  test('warns once per second and drops the rest', () => {
    const rl = make();
    for (let i = 0; i < 20; i++) rl.take(0);
    assert.equal(rl.take(0), 'warn');
    assert.equal(rl.take(0), 'drop');
    assert.equal(rl.take(10), 'drop');
    // Over a second later it warns again, once. The bucket has refilled by then (10 tokens), so
    // the first 10 messages pass, then one warning, then silence.
    const later = Array.from({ length: 30 }, () => rl.take(1100));
    const oks = later.filter((v) => v === 'ok').length;
    assert.ok(oks === 10 || oks === 11, `ok count ${oks}`); // 11 when a leftover fraction rounds up
    assert.equal(later.filter((v) => v === 'warn').length, 1);
    assert.equal(later.filter((v) => v === 'drop').length, 29 - oks);
  });

  test('closes after being over the limit for the whole window, but not if the client calms down', () => {
    const rl = make();
    for (let i = 0; i < 20; i++) rl.take(0);
    // A flood that never stops: 100 messages per second against a refill of 10.
    let closed = false;
    for (let t = 0; t <= 6000; t += 10) closed ||= rl.take(t) === 'close';
    assert.equal(closed, true);

    const calm = make();
    for (let i = 0; i < 20; i++) calm.take(0);
    for (let t = 0; t <= 4000; t += 10) calm.take(t); // 4 s of flooding, under the 5 s limit
    for (let i = 0; i < 20; i++) calm.take(8000); // a pause, then another burst and flood
    closed = false;
    for (let t = 8000; t <= 11_000; t += 10) closed ||= calm.take(t) === 'close';
    assert.equal(closed, false);
  });
});

describe('limits on a real server', () => {
  test('a message over the size limit gets too-big and is dropped, and the socket survives', async () => {
    const s = await startRelay({ maxMessageBytes: 200 });
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'create', name: 'a'.repeat(300) }); // valid JSON, but 300+ bytes
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'too-big' });
    assert.equal(s.relay.roomCount(), 0); // it was dropped, not handled
    c.sendBinary(Buffer.alloc(201));
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'too-big' });
    // Exactly at the limit is fine.
    // Unknown fields are ignored, so a pad field makes a valid ping of exactly 200 bytes.
    const filler = 'x'.repeat(200 - JSON.stringify({ t: 'ping', n: 1, pad: '' }).length);
    c.sendText(JSON.stringify({ t: 'ping', n: 1, pad: filler }));
    assert.equal((await c.nextJson()).t, 'pong');
    await c.close();
    await s.stop();
  });

  test('a frame far over the limit closes the socket (ws hard cap)', async () => {
    const s = await startRelay({ maxMessageBytes: 200 });
    const c = await Client.connect(s.url);
    c.sendBinary(Buffer.alloc(5000));
    const info = await c.closedPromise;
    assert.equal(info.code, 1009);
    await s.stop();
  });

  test('a burst is allowed, then extra messages get one rate error and are dropped', async () => {
    const s = await startRelay({ ratePerSec: 5, rateBurst: 10, rateCloseAfterMs: 60_000 });
    const c = await Client.connect(s.url); // the hello used 1 token, 9 remain
    for (let i = 0; i < 9; i++) c.sendJson({ t: 'ping', n: i });
    for (let i = 0; i < 9; i++) assert.deepEqual(await c.nextJson(), { t: 'pong', n: i });

    // Now the bucket is empty. Send a flood; nothing gets through and one error comes back.
    for (let i = 0; i < 30; i++) c.sendJson({ t: 'ping', n: 100 + i });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'rate' });
    await sleep(100);
    const extra = [];
    while (c.queue.length) extra.push(await c.nextJson());
    assert.ok(extra.every((m) => m.t === 'pong'), `unexpected: ${JSON.stringify(extra)}`);
    assert.ok(extra.length <= 2, `a few may pass as tokens refill, got ${extra.length}`);
    assert.ok(!c.closeInfo, 'a short flood does not close the socket');

    // After a pause the bucket refills and the connection works again.
    await sleep(1200);
    c.sendJson({ t: 'ping', n: 999 });
    assert.deepEqual(await c.nextJson(), { t: 'pong', n: 999 });
    await c.close();
    await s.stop();
  });

  test('a flood that goes on too long closes the socket', async () => {
    const s = await startRelay({ ratePerSec: 5, rateBurst: 5, rateCloseAfterMs: 300 });
    const c = await Client.connect(s.url);
    const timer = setInterval(() => {
      if (c.ws.readyState === 1) c.sendJson({ t: 'ping', n: 1 });
    }, 10);
    const info = await Promise.race([c.closedPromise, sleep(4000).then(() => null)]);
    clearInterval(timer);
    assert.ok(info, 'socket should have been closed');
    assert.equal(info.code, 1008);
    await s.stop();
  });

  test('rate limits are per connection', async () => {
    const s = await startRelay({ ratePerSec: 5, rateBurst: 5, rateCloseAfterMs: 60_000 });
    const noisy = await Client.connect(s.url);
    for (let i = 0; i < 20; i++) noisy.sendJson({ t: 'ping', n: i });
    await sleep(100);
    const calm = await Client.connect(s.url);
    calm.sendJson({ t: 'ping', n: 1 });
    assert.deepEqual(await calm.nextJson(), { t: 'pong', n: 1 });
    await noisy.close();
    await calm.close();
    await s.stop();
  });

  test('create is refused with server-full at the room limit, and works again after one closes', async () => {
    const s = await startRelay({ maxRooms: 2 });
    const a = await makeRoom(s.url, 'A');
    const b = await makeRoom(s.url, 'B');
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'create', name: 'C' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'server-full' });
    assert.equal(s.relay.roomCount(), 2);
    // Joining an existing room is still allowed when the server is full of rooms.
    const joiner = await joinRoom(s.url, a.code, 'J');
    assert.equal(joiner.room.you, 1);
    a.host.sendJson({ t: 'leave' });
    await a.host.settle();
    c.sendJson({ t: 'create', name: 'C' });
    assert.equal((await c.nextJson()).t, 'room');
    for (const x of [a.host, b.host, c, joiner.client]) await x.close();
    await s.stop();
  });

  test('a connection over the limit is refused with 503, and the count drops when one closes', async () => {
    const s = await startRelay({ maxConnections: 2 });
    const a = await Client.connect(s.url);
    const b = await Client.connect(s.url);
    await assert.rejects(Client.open(s.url), { status: 503 });
    assert.ok(s.logs.some((l) => l.includes('refused')));
    assert.equal(s.relay.connectionCount(), 2);
    await a.close();
    for (let i = 0; i < 50 && s.relay.connectionCount() > 1; i++) await sleep(10);
    const again = await Client.connect(s.url);
    await again.close();
    await b.close();
    await s.stop();
  });
});

describe('time based limits, with lowered values', () => {
  test('a room older than the limit is closed with reason expired and deleted', async () => {
    const s = await startRelay({ roomMaxAgeMs: 300 });
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    assert.equal(s.relay.roomCount(), 1);
    for (const c of [host, ben]) {
      assert.deepEqual(await c.nextOfType('closed', 3000), { t: 'closed', reason: 'expired' });
    }
    assert.equal(s.relay.roomCount(), 0);
    assert.ok(s.logs.some((l) => l.includes('expired')));
    // The code is dead, and both players can start fresh.
    const late = await Client.connect(s.url);
    late.sendJson({ t: 'join', code, name: 'Late' });
    assert.deepEqual(await late.nextJson(), { t: 'error', code: 'not-found' });
    ben.sendJson({ t: 'ready', ready: true });
    assert.deepEqual(await ben.nextJson(), { t: 'error', code: 'not-in-room' });
    for (const c of [host, ben, late]) await c.close();
    await s.stop();
  });

  test('a room younger than the limit is left alone', async () => {
    const s = await startRelay({ roomMaxAgeMs: 60_000, idleMs: 60_000 });
    const { host } = await makeRoom(s.url, 'Ana');
    await sleep(200);
    assert.equal(s.relay.roomCount(), 1);
    assert.deepEqual(await host.settle(), []);
    await host.close();
    await s.stop();
  });

  test('an expired room closes even after it has started', async () => {
    const s = await startRelay({ roomMaxAgeMs: 400 });
    const { clients } = await startedRoom(s.url, 2);
    assert.deepEqual(await clients[1].nextOfType('closed', 3000), { t: 'closed', reason: 'expired' });
    for (const c of clients) await c.close();
    await s.stop();
  });

  test('a silent socket is pinged and kept if it answers', async () => {
    const s = await startRelay({ idleMs: 150, pongWaitMs: 300 });
    const c = await Client.connect(s.url); // auto-answers pings, like a browser
    await sleep(900);
    assert.ok(!c.closeInfo, 'socket that answers pings must stay open');
    c.sendJson({ t: 'ping', n: 1 });
    assert.deepEqual(await c.nextJson(), { t: 'pong', n: 1 });
    await c.close();
    await s.stop();
  });

  test('a silent socket that never answers the ping is closed, and its room cleaned up', async () => {
    const s = await startRelay({ idleMs: 150, pongWaitMs: 150 });
    const { host, code } = await makeRoom(s.url, 'Ana');
    const dead = await Client.connect(s.url, { autoPong: false });
    dead.sendJson({ t: 'join', code, name: 'Dead' });
    await dead.nextJson();
    // Keep the host alive with chatter while the other socket stays silent.
    const timer = setInterval(() => host.ws.readyState === 1 && host.sendJson({ t: 'ping', n: 1 }), 50);
    const info = await Promise.race([dead.closedPromise, sleep(4000).then(() => null)]);
    clearInterval(timer);
    assert.ok(info, 'silent socket should have been closed');
    // Its slot was freed.
    let room;
    do room = await host.nextOfType('room'); while (room.players.length !== 1);
    assert.equal(s.relay.connectionCount(), 1);
    await host.close();
    await s.stop();
  });

  test('a chatty socket that answers pings stays open', async () => {
    const s = await startRelay({ idleMs: 200, pongWaitMs: 100 });
    const c = await Client.connect(s.url); // auto-answers pings, like a browser
    for (let i = 0; i < 12; i++) {
      c.sendJson({ t: 'ping', n: i });
      assert.deepEqual(await c.nextJson(), { t: 'pong', n: i });
      await sleep(60);
    }
    assert.ok(!c.closeInfo);
    await c.close();
    await s.stop();
  });

  test('sending does not stand in for answering pings: a socket that never answers is closed', async () => {
    // A client could keep sending while it never reads anything we send. Only a pong proves it
    // reads, so chatting alone no longer keeps it open.
    const s = await startRelay({ idleMs: 150, pongWaitMs: 150 });
    const c = await Client.connect(s.url, { autoPong: false });
    const timer = setInterval(() => c.ws.readyState === 1 && c.sendJson({ t: 'ping', n: 1 }), 30);
    const info = await Promise.race([c.closedPromise, sleep(4000).then(() => null)]);
    clearInterval(timer);
    assert.ok(info, 'a socket that never answers pings should be closed even while it sends');
    await s.stop();
  });
});

describe('shutdown', () => {
  test('close tells every client the server is going away and stops listening', async () => {
    const s = await startRelay();
    const a = await Client.connect(s.url);
    const { host } = await makeRoom(s.url, 'Ana');
    await s.stop();
    assert.equal((await a.closedPromise).code, 1001);
    assert.equal((await host.closedPromise).code, 1001);
    await assert.rejects(fetch(`http://127.0.0.1:${s.port}/healthz`));
  });
});
