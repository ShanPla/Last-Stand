// Rejoin: a player of a started run who lost the connection, reloaded the page or left comes
// back to the seat the room kept for them, proved by the secret token they got with `started`.
import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { Client, joinRoom, makeRoom, sleep, startRelay } from './helpers.js';

/** A started room with `count` players. Returns the clients (host first), the code, and each token. */
async function startedWithTokens(url, count = 2) {
  const { host, code } = await makeRoom(url, 'P0');
  const clients = [host];
  for (let i = 1; i < count; i++) clients.push((await joinRoom(url, code, `P${i}`)).client);
  host.sendJson({ t: 'start' });
  const tokens = [];
  for (const c of clients) tokens.push((await c.nextOfType('started')).token);
  for (const c of clients) await c.settle();
  return { clients, code, tokens };
}

/** A fresh socket that asks for a seat back. Returns it and the first answer. */
async function rejoin(url, code, token) {
  const client = await Client.connect(url);
  client.sendJson({ t: 'rejoin', code, token });
  return { client, first: await client.nextJson() };
}

/** Waits for the next room message in which player `id` has the given away state. */
async function roomWhere(client, id, away) {
  for (;;) {
    const room = await client.nextOfType('room');
    const p = room.players.find((q) => q.id === id);
    if (p && !!p.away === away) return room;
  }
}

describe('rejoin', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('each player gets a different token, only in their own started message, never in room messages', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    host.sendJson({ t: 'start' });
    const seen = { host: await host.settle(), ben: await ben.settle() };
    const tokenOf = (list) => list.filter((m) => m.t === 'started').map((m) => m.token);
    assert.equal(tokenOf(seen.host).length, 1);
    assert.equal(tokenOf(seen.ben).length, 1);
    assert.notEqual(tokenOf(seen.host)[0], tokenOf(seen.ben)[0]);
    const others = JSON.stringify([...seen.host, ...seen.ben].filter((m) => m.t !== 'started'));
    assert.ok(!others.includes(tokenOf(seen.host)[0]) && !others.includes(tokenOf(seen.ben)[0]), 'no token in any room message');
    await ben.close();
    await host.close();
  });

  test('a guest who disconnects keeps the seat as away, and comes back to the same slot and name', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 3);
    const [host, p1, p2] = clients;
    await p1.close();
    for (const c of [host, p2]) {
      const room = await roomWhere(c, 1, true);
      assert.deepEqual(room.players, [
        { id: 0, name: 'P0', ready: false },
        { id: 1, name: 'P1', ready: false, away: true },
        { id: 2, name: 'P2', ready: false },
      ]);
    }
    const { client: back, first } = await rejoin(s.url, code.toLowerCase(), tokens[1]);
    assert.equal(first.t, 'room');
    assert.equal(first.you, 1);
    assert.equal(first.started, true);
    assert.deepEqual(first.players.map((p) => `${p.id}:${p.name}:${!!p.away}`), ['0:P0:false', '1:P1:false', '2:P2:false']);
    assert.deepEqual(await back.nextJson(), { t: 'started', token: tokens[1] });
    // The others are told the player is back, and get no started of their own.
    for (const c of [host, p2]) {
      const room = await roomWhere(c, 1, false);
      assert.equal(room.players.length, 3);
      assert.ok(!(await c.settle()).some((m) => m.t === 'started'));
    }
    for (const c of [back, p2, host]) await c.close();
  });

  test('leave in a started room also keeps the seat, and the same socket can then rejoin it', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 2);
    const [host, ben] = clients;
    ben.sendJson({ t: 'leave' });
    const room = await roomWhere(host, 1, true);
    assert.equal(room.players.length, 2);
    // The seat is not free: nobody new can take it, and the room is still started.
    const late = await Client.connect(s.url);
    late.sendJson({ t: 'join', code, name: 'Late' });
    assert.deepEqual(await late.nextJson(), { t: 'error', code: 'started' });
    // The same socket, now in no room, can rejoin.
    ben.sendJson({ t: 'rejoin', code, token: tokens[1] });
    assert.equal((await ben.nextJson()).you, 1);
    assert.equal((await ben.nextJson()).t, 'started');
    for (const c of [late, ben, host]) await c.close();
  });

  test('leave before the start still frees the seat, and there is nothing to rejoin', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    ben.sendJson({ t: 'leave' });
    assert.deepEqual((await host.nextOfType('room')).players.map((p) => p.id), [0]);
    // Not started: there is no seat to come back to, whatever the token.
    ben.sendJson({ t: 'rejoin', code, token: 'AAAAAAAAAAAAAAAAAAAAAA' });
    assert.deepEqual(await ben.nextJson(), { t: 'error', code: 'bad-token' });
    for (const c of [ben, host]) await c.close();
  });

  test('a wrong token, a token from another room and the host token are refused with bad-token', async () => {
    const a = await startedWithTokens(s.url, 2);
    const b = await startedWithTokens(s.url, 2);
    await a.clients[1].close();
    await roomWhere(a.clients[0], 1, true);
    for (const token of ['nope', '', b.tokens[1], a.tokens[0], 'x'.repeat(100)]) {
      const { client, first } = await rejoin(s.url, a.code, token);
      assert.deepEqual(first, { t: 'error', code: 'bad-token' }, `token ${token.slice(0, 10)}`);
      // A refused socket is still in no room and can try again.
      client.sendJson({ t: 'rejoin', code: a.code, token: a.tokens[1] });
      assert.equal((await client.nextJson()).you, 1);
      await client.close();
      await roomWhere(a.clients[0], 1, true);
    }
    for (const c of [...a.clients, ...b.clients]) await c.close();
  });

  test('bad requests: no code or no token, a socket already in a room, an unknown room, before hello', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 2);
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'rejoin', code });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendJson({ t: 'rejoin', token: tokens[1] });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    c.sendJson({ t: 'rejoin', code: 'ZZZZZ', token: tokens[1] });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'not-found' });
    clients[1].sendJson({ t: 'rejoin', code, token: tokens[1] });
    assert.deepEqual(await clients[1].nextJson(), { t: 'error', code: 'bad-request' });
    const raw = await Client.open(s.url);
    raw.sendJson({ t: 'rejoin', code, token: tokens[1] });
    assert.deepEqual(await raw.nextJson(), { t: 'error', code: 'bad-request' });
    for (const x of [raw, c, ...clients]) await x.close();
  });

  test('the right token on a seat that still has a socket moves the player to the new socket', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 2);
    const [host, oldBen] = clients;
    const { client: newBen, first } = await rejoin(s.url, code, tokens[1]);
    assert.equal(first.you, 1);
    assert.deepEqual(await newBen.nextJson(), { t: 'started', token: tokens[1] });
    // The old socket is told it was replaced, and is now in no room.
    assert.deepEqual(await oldBen.nextOfType('closed'), { t: 'closed', reason: 'replaced' });
    oldBen.sendJson({ t: 'ready', ready: true });
    assert.deepEqual(await oldBen.nextJson(), { t: 'error', code: 'not-in-room' });
    // The host sees the player go away and come back, so it knows to connect to them again.
    await roomWhere(host, 1, true);
    await roomWhere(host, 1, false);
    // The old socket closing later does not take the seat from the new one.
    await oldBen.close();
    assert.deepEqual(await host.settle(), []);
    host.sendBinary([1, 42]);
    assert.ok(Buffer.from([0, 42]).equals(await newBen.nextBinary()));
    for (const c of [newBen, host]) await c.close();
  });

  test('a wrong token on a seat that has a socket changes nothing', async () => {
    const { clients, code } = await startedWithTokens(s.url, 2);
    const { client, first } = await rejoin(s.url, code, 'AAAAAAAAAAAAAAAAAAAAAA');
    assert.deepEqual(first, { t: 'error', code: 'bad-token' });
    assert.deepEqual(await clients[0].settle(), []);
    assert.deepEqual(await clients[1].settle(), []);
    for (const c of [client, ...clients]) await c.close();
  });

  test('when the room closes the kept seats go with it, and their tokens find nothing', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 2);
    await clients[1].close();
    await roomWhere(clients[0], 1, true);
    await clients[0].close();
    for (let i = 0; i < 50 && s.relay.roomCount() > 0; i++) await sleep(10);
    const { client, first } = await rejoin(s.url, code, tokens[1]);
    assert.deepEqual(first, { t: 'error', code: 'not-found' });
    await client.close();
  });

  test('frames and signals to an away seat are dropped, and both work again after the rejoin', async () => {
    const { clients, code, tokens } = await startedWithTokens(s.url, 3);
    const [host, p1, p2] = clients;
    await p1.close();
    await roomWhere(host, 1, true);
    await roomWhere(p2, 1, true);
    host.sendBinary([1, 1, 2, 3]);
    host.sendJson({ t: 'signal', to: 1, data: { kind: 'offer' } });
    host.sendBinary([255, 9]);
    assert.ok(Buffer.from([0, 9]).equals(await p2.nextBinary()), 'a broadcast still reaches the players who are here');
    assert.deepEqual(await host.settle(), []);

    const { client: back } = await rejoin(s.url, code, tokens[1]);
    await back.nextOfType('started');
    await roomWhere(host, 1, false);
    host.sendJson({ t: 'signal', to: 1, data: { kind: 'offer', sdp: 'x' } });
    assert.deepEqual(await back.nextJson(), { t: 'signal', from: 0, data: { kind: 'offer', sdp: 'x' } });
    back.sendJson({ t: 'signal', to: 0, data: { kind: 'answer' } });
    assert.deepEqual(await host.nextOfType('signal'), { t: 'signal', from: 1, data: { kind: 'answer' } });
    back.sendBinary([0, 7]);
    assert.ok(Buffer.from([1, 7]).equals(await host.nextBinary()));
    for (const c of [back, p2, host]) await c.close();
  });

  test('rejoin counts against the rate limit like any other message, and logs say nothing private', async () => {
    const limited = await startRelay({ ratePerSec: 5, rateBurst: 5 });
    const { clients, code, tokens } = await startedWithTokens(limited.url, 2);
    await clients[1].close();
    await roomWhere(clients[0], 1, true);
    const c = await Client.connect(limited.url);
    for (let i = 0; i < 8; i++) c.sendJson({ t: 'rejoin', code, token: 'wrong' });
    const answers = [];
    for (let i = 0; i < 5; i++) answers.push((await c.nextJson()).code);
    assert.ok(answers.includes('rate'), `answers: ${answers}`);
    await sleep(1500);
    await c.settle();
    c.sendJson({ t: 'rejoin', code, token: tokens[1] });
    assert.equal((await c.nextOfType('room')).you, 1);
    const text = limited.logs.join('\n');
    assert.ok(text.includes('player rejoined'));
    assert.ok(!text.includes(code) && !text.includes(tokens[1]) && !text.includes('P1'));
    for (const x of [c, clients[0]]) await x.close();
    await limited.stop();
  });
});
