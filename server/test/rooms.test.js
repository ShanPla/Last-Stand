import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { RoomRegistry } from '../src/rooms.js';
import { Client, joinRoom, makeRoom, sleep, startRelay } from './helpers.js';

const CODE_PATTERN = /^[ABCDEFGHJKMNPQRSTUVWXY23456789]{5}$/;

describe('rooms and the lobby', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('create makes a room with the creator as host in slot 0', async () => {
    const { host, room, code } = await makeRoom(s.url, 'Ana');
    assert.match(code, CODE_PATTERN);
    assert.deepEqual(room, {
      t: 'room',
      code,
      you: 0,
      host: 0,
      started: false,
      players: [{ id: 0, name: 'Ana', ready: false }],
    });
    await host.close();
  });

  test('join by code works, in any letter case, and everyone gets the new room', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben, room } = await joinRoom(s.url, code.toLowerCase(), 'Ben');
    assert.equal(room.you, 1);
    assert.equal(room.host, 0);
    assert.deepEqual(room.players.map((p) => p.name), ['Ana', 'Ben']);
    const hostView = await host.nextJson();
    assert.equal(hostView.you, 0);
    assert.deepEqual(hostView.players.map((p) => p.id), [0, 1]);
    await ben.close();
    await host.close();
  });

  test('names are trimmed', async () => {
    const { host, code } = await makeRoom(s.url, '  Ana  ');
    const { client, room } = await joinRoom(s.url, ` ${code} `, ' Ben Two ');
    assert.deepEqual(room.players.map((p) => p.name), ['Ana', 'Ben Two']);
    await client.close();
    await host.close();
  });

  test('joining a room that does not exist is not-found', async () => {
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'join', code: 'ZZZZZ', name: 'Ben' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'not-found' });
    c.sendJson({ t: 'join', code: 'x', name: 'Ben' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'not-found' });
    await c.close();
  });

  test('join without a code is bad-request', async () => {
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'join', name: 'Ben' });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    await c.close();
  });

  test('bad names are refused on create and on join', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const c = await Client.connect(s.url);
    const bad = ['', '   ', 'a'.repeat(13), 'Ben!', '<b>', 'Ünï', 7, null, undefined];
    for (const name of bad) {
      c.sendJson({ t: 'create', name });
      assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-name' }, `create ${String(name)}`);
      c.sendJson({ t: 'join', code, name });
      assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-name' }, `join ${String(name)}`);
    }
    // 12 characters is the longest allowed.
    c.sendJson({ t: 'join', code, name: 'a'.repeat(12) });
    assert.equal((await c.nextJson()).t, 'room');
    await c.close();
    await host.close();
  });

  test('the room is full at 4 players', async () => {
    const { host, code } = await makeRoom(s.url, 'P0');
    const others = [];
    for (let i = 1; i < 4; i++) others.push((await joinRoom(s.url, code, `P${i}`)).client);
    const extra = await Client.connect(s.url);
    extra.sendJson({ t: 'join', code, name: 'P4' });
    assert.deepEqual(await extra.nextJson(), { t: 'error', code: 'full' });
    for (const c of [extra, ...others, host]) await c.close();
  });

  test('nobody can join after start', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    host.sendJson({ t: 'start' });
    await host.nextOfType('started');
    const late = await Client.connect(s.url);
    late.sendJson({ t: 'join', code, name: 'Late' });
    assert.deepEqual(await late.nextJson(), { t: 'error', code: 'started' });
    await late.close();
    await host.close();
  });

  test('start sends the final room then started to everyone', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    host.sendJson({ t: 'start' });
    for (const c of [host, ben]) {
      const room = await c.nextJson();
      assert.equal(room.t, 'room');
      assert.equal(room.started, true);
      const started = await c.nextJson();
      assert.equal(started.t, 'started');
      assert.match(started.token, /^[A-Za-z0-9_-]{22}$/, 'each player gets a rejoin token');
    }
    // Starting twice is an error and does not send a second started.
    host.sendJson({ t: 'start' });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'started' });
    await ben.close();
    await host.close();
  });

  test('only the host can start', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    ben.sendJson({ t: 'start' });
    assert.deepEqual(await ben.nextJson(), { t: 'error', code: 'not-host' });
    // The room did not start: someone else can still join.
    const { room } = await joinRoom(s.url, code, 'Cy');
    assert.equal(room.started, false);
    await ben.close();
    await host.close();
  });

  test('ready changes are sent to everyone in a new room message', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    ben.sendJson({ t: 'ready', ready: true });
    for (const c of [host, ben]) {
      const room = await c.nextJson();
      assert.equal(room.t, 'room');
      assert.deepEqual(room.players.map((p) => p.ready), [false, true]);
    }
    ben.sendJson({ t: 'ready', ready: false });
    const again = await host.nextJson();
    assert.deepEqual(again.players.map((p) => p.ready), [false, false]);
    ben.sendJson({ t: 'ready', ready: 'yes' });
    await ben.nextJson(); // the room message from the second ready
    assert.deepEqual(await ben.nextJson(), { t: 'error', code: 'bad-request' });
    await ben.close();
    await host.close();
  });

  test('leave frees the slot, tells the others, and keeps the socket open', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    ben.sendJson({ t: 'leave' });
    const room = await host.nextJson();
    assert.deepEqual(room.players.map((p) => p.name), ['Ana']);
    // Ben can still talk, and is no longer in a room.
    ben.sendJson({ t: 'ready', ready: true });
    const reply = await ben.nextJson();
    assert.deepEqual(reply, { t: 'error', code: 'not-in-room' });
    await ben.close();
    await host.close();
  });

  test('a player who disconnects is removed like one who leaves', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    await ben.close();
    const room = await host.nextJson();
    assert.deepEqual(room.players.map((p) => p.name), ['Ana']);
    await host.close();
  });

  test('a freed slot is reused by the next player, lowest first', async () => {
    const { host, code } = await makeRoom(s.url, 'P0');
    const { client: p1 } = await joinRoom(s.url, code, 'P1');
    const { client: p2 } = await joinRoom(s.url, code, 'P2');
    await joinRoom(s.url, code, 'P3');
    await host.settle(); // read the old lobby updates so they are not mistaken for new ones
    // Slots 1 and 2 empty out; the next two joiners take 1 then 2, not 4 or 5.
    p2.sendJson({ t: 'leave' });
    p1.sendJson({ t: 'leave' });
    // The host sees the room shrink to 2 players once both leaves are processed.
    for (let room = await host.nextOfType('room'); room.players.length !== 2; ) {
      room = await host.nextOfType('room');
    }
    const { room: first } = await joinRoom(s.url, code, 'Q1');
    assert.equal(first.you, 1);
    const { room: second } = await joinRoom(s.url, code, 'Q2');
    assert.equal(second.you, 2);
    assert.deepEqual(second.players.map((p) => `${p.id}:${p.name}`), ['0:P0', '1:Q1', '2:Q2', '3:P3']);
  });

  test('a player cannot create or join while already in a room', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    host.sendJson({ t: 'create', name: 'Ana' });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    host.sendJson({ t: 'join', code, name: 'Ana' });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    await host.close();
  });

  test('when the host leaves, everyone else gets closed with host-left and the room is gone', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    const { client: cy } = await joinRoom(s.url, code, 'Cy');
    await host.settle();
    await ben.settle();
    host.sendJson({ t: 'leave' });
    for (const c of [ben, cy]) {
      const msg = await c.nextOfType('closed');
      assert.deepEqual(msg, { t: 'closed', reason: 'host-left' });
    }
    // The host chose to leave, so it is not told the room closed.
    assert.deepEqual(await host.settle(), []);
    const late = await Client.connect(s.url);
    late.sendJson({ t: 'join', code, name: 'Late' });
    assert.deepEqual(await late.nextJson(), { t: 'error', code: 'not-found' });
    // Ben is free again and can make a new room.
    ben.sendJson({ t: 'create', name: 'Ben' });
    assert.equal((await ben.nextOfType('room')).you, 0);
    for (const c of [late, ben, cy, host]) await c.close();
  });

  test('when the host disconnects, everyone else gets closed with host-left', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.close();
    assert.deepEqual(await ben.nextOfType('closed'), { t: 'closed', reason: 'host-left' });
    await ben.close();
  });
});

describe('room codes', () => {
  test('codes are 5 characters from the safe alphabet and never repeat among live rooms', () => {
    const registry = new RoomRegistry({ maxRooms: 100_000, roomMaxAgeMs: 1 });
    const seen = new Set();
    for (let i = 0; i < 3000; i++) {
      const { code } = registry.create(0);
      assert.match(code, CODE_PATTERN);
      seen.add(code);
    }
    assert.equal(seen.size, 3000);
    assert.equal(registry.count, 3000);
    // Every character of the alphabet should show up, so none is left out by mistake.
    const used = new Set([...seen].join(''));
    assert.equal(used.size, 30);
  });
});

describe('room count', () => {
  test('rooms are counted, logged, and deleted when the host leaves', async () => {
    const s = await startRelay();
    assert.equal(s.relay.roomCount(), 0);
    const a = await makeRoom(s.url, 'Ana');
    const b = await makeRoom(s.url, 'Bo');
    assert.equal(s.relay.roomCount(), 2);
    a.host.sendJson({ t: 'leave' });
    await a.host.settle();
    assert.equal(s.relay.roomCount(), 1);
    await b.host.close(); // disconnect of an alone host empties the room too
    for (let i = 0; i < 50 && s.relay.roomCount() > 0; i++) await sleep(10);
    assert.equal(s.relay.roomCount(), 0);
    assert.equal(s.logs.filter((l) => l.startsWith('room created')).length, 2);
    assert.equal(s.logs.filter((l) => l.startsWith('room closed')).length, 2);
    await a.host.close();
    await s.stop();
  });

  test('logs never contain names or room codes', async () => {
    const s = await startRelay();
    const { host, code } = await makeRoom(s.url, 'Zelda');
    await host.close();
    for (let i = 0; i < 50 && s.relay.roomCount() > 0; i++) await sleep(10);
    const text = s.logs.join('\n');
    assert.ok(text.length > 0);
    assert.ok(!text.includes('Zelda'));
    assert.ok(!text.includes(code));
    assert.ok(!text.includes('127.0.0.1'));
    await s.stop();
  });
});
