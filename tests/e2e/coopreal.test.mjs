// The co-op lobby against the REAL relay server in server/, through the real browser layer
// (web/components/NetClient) and the game's web backend. Two players are two browser contexts
// that click through the real screens. Nothing here uses the fake lobby, except one test that
// checks the ?fakenet flag still selects it.
//
// The run itself does not start yet: Start only moves both lobbies to Starting.
import { test, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:net';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { openGame, waitScreen, clickButton, debug } from './harness.mjs';
import {
  NET, PHASE, ERR, useBrowser, readNet, waitNet, frameUi, toCoop, typeText, host, hasButton, webUrl, desktop,
} from './coophelpers.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const rig = useBrowser();
const relays = [];

async function freePort() {
  return new Promise((resolve) => {
    const probe = createServer();
    probe.listen(0, '127.0.0.1', () => {
      const { port } = probe.address();
      probe.close(() => resolve(port));
    });
  });
}

// Runs the real server the way Render does: node server/src/index.js, with PORT set.
async function startRelay() {
  const port = await freePort();
  const child = spawn(process.execPath, [path.join(repo, 'server/src/index.js')], {
    env: { ...process.env, PORT: String(port) },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await new Promise((resolve, reject) => {
    child.once('exit', (code) => reject(new Error(`relay exited early with code ${code}`)));
    child.stdout.on('data', (chunk) => { if (String(chunk).includes('relay listening')) resolve(); });
  });
  const relay = { port, ws: `ws://127.0.0.1:${port}`, child, stop: () => child.kill() };
  relays.push(relay);
  return relay;
}

after(() => relays.forEach((relay) => relay.stop()));

// Opens the game pointed at a relay. Real backend, so no ?fakenet. ?debug is only added by
// tests that need a debug command, and it does not change which backend is used.
async function openPlayer(server, extra = '') {
  const session = await openGame(rig.browser, `${webUrl()}?server=${server}${extra}`, desktop);
  await waitScreen(session.page, 'title');
  return session;
}

async function setName(page, name) {
  await clickButton(page, 'Your name');
  await typeText(page, name);
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
}

async function join(page, code) {
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, code);
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await clickButton(page, 'Join');
}

const noRetry = (ui) => assert.ok(!hasButton(ui, 'Retry'), 'Retry would not help here');

// ---- The lobby, once with WebRTC allowed and once forced through the server -------

for (const [label, extra] of [['default transport', ''], ['transport=relay', '&transport=relay']]) {
  test(`two players: host, join, ready, start (${label})`, async () => {
    const relay = await startRelay();
    const a = await openPlayer(relay.ws, extra);
    const b = await openPlayer(relay.ws, extra);

    // The host's flow, by real clicks.
    await toCoop(a.page);
    await setName(a.page, 'Ana');
    const room = await host(a.page);
    assert.match(room.code, /^[A-HJKMNP-Y2-9]{5}$/, 'a 5-character code, never with Z');
    assert.equal(room.you, 0);
    assert.equal(room.host, 0);
    assert.equal(room.players[0].name, 'Ana');
    assert.equal(room.players[0].transport, 'local');

    // The other player joins with that code.
    await toCoop(b.page);
    await setName(b.page, 'Ben');
    await join(b.page, room.code.toLowerCase());
    await waitScreen(b.page, 'coop-host');
    const guest = await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the joined lobby');
    assert.equal(guest.code, room.code);
    assert.equal(guest.you, 1);
    assert.equal(guest.host, 0);
    const hostSees = await waitNet(a.page, (n) => n.count === 2, 'the host to see the guest');
    for (const net of [guest, hostSees]) assert.deepEqual(net.players.map((p) => p.name), ['Ana', 'Ben']);

    // Ready. Start is for the host only, and only once everyone is ready.
    assert.ok(!hasButton(await frameUi(b.page), 'Start'), 'a guest has no Start');
    await clickButton(b.page, 'Ready');
    await waitNet(a.page, (n) => n.players[1]?.ready === 1, 'the host to see the guest ready');
    assert.equal((await readNet(a.page)).canStart, 0, 'the host is not ready yet');
    await clickButton(a.page, 'Ready');
    await waitNet(a.page, (n) => n.canStart === 1, 'Start to be enabled');
    await waitNet(b.page, (n) => n.players[0]?.ready === 1, 'the guest to see the host ready');
    assert.equal((await readNet(b.page)).canStart, 0, 'only the host can start');

    // Transport and the numbers the debug overlay will show.
    const seen = await waitNet(a.page, (n) => n.serverRtt >= 0, 'the server round trip', 8000);
    assert.ok(seen.serverRtt < 1000);
    const peer = seen.players[1];
    assert.ok(['pending', 'webrtc', 'relay'].includes(peer.transport), `unexpected transport ${peer.transport}`);
    assert.equal(typeof peer.rtt, 'number');
    assert.equal(typeof peer.msgsIn, 'number');
    assert.equal(typeof peer.msgsOut, 'number');
    if (extra) assert.equal(peer.transport, 'relay', 'transport=relay forces the server path');

    // Start: the server marks the room started and both players move on.
    await clickButton(a.page, 'Start');
    const [hostEnd, guestEnd] = await Promise.all([
      waitNet(a.page, (n) => n.phase === PHASE.starting, 'the host to reach Starting'),
      waitNet(b.page, (n) => n.phase === PHASE.starting, 'the guest to reach Starting'),
    ]);
    assert.equal(hostEnd.started, 1);
    assert.equal(guestEnd.started, 1);
    // Once the room has started, pings flow and the pair has a measured round trip.
    if (extra) {
      const measured = await waitNet(a.page, (n) => n.players[1]?.rtt >= 0, 'a measured round trip to the guest', 9000);
      assert.equal(measured.players[1].transport, 'relay');
    }

    assert.deepEqual(a.problems, []);
    assert.deepEqual(b.problems, []);
    await a.context.close();
    await b.context.close();
    relay.stop();
  });
}

// ---- Errors through the real server --------------------------------------------------

test('a wrong code is not-found, and a room that has started refuses new players', async () => {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws);
  await toCoop(a.page);
  await join(a.page, 'AAAAA');
  const net = await waitNet(a.page, (n) => n.phase === PHASE.error, 'the error');
  assert.equal(net.error, ERR.notFound);
  assert.equal(net.sub, 'coop-join');
  noRetry(await frameUi(a.page));

  // A room that has started: the server checks that before it checks whether the room is full.
  const hostSession = await openPlayer(relay.ws);
  const guest = await openPlayer(relay.ws);
  await toCoop(hostSession.page);
  const room = await host(hostSession.page);
  await toCoop(guest.page);
  await join(guest.page, room.code);
  await waitNet(guest.page, (n) => n.phase === PHASE.inLobby, 'the guest in the lobby');
  await clickButton(guest.page, 'Ready');
  await clickButton(hostSession.page, 'Ready');
  await waitNet(hostSession.page, (n) => n.canStart === 1, 'Start to be enabled');
  await clickButton(hostSession.page, 'Start');
  await waitNet(hostSession.page, (n) => n.phase === PHASE.starting, 'Starting');
  await clickButton(a.page, 'Back');
  await waitScreen(a.page, 'coop-menu');
  await join(a.page, room.code);
  const refused = await waitNet(a.page, (n) => n.phase === PHASE.error, 'the started error');
  assert.equal(refused.error, ERR.started);
  noRetry(await frameUi(a.page));

  for (const session of [a, hostSession, guest]) {
    assert.deepEqual(session.problems, []);
    await session.context.close();
  }
  relay.stop();
});

test('an unreachable server: waking, Retry after the timeout, then server unreachable with Retry', async () => {
  const dead = await freePort();  // Nothing listens here, so every connection is refused.
  const { context, page, problems } = await openPlayer(`ws://127.0.0.1:${dead}`, '&debug&wakebudget=10000');
  await toCoop(page);
  await clickButton(page, 'Host Co-op');
  await waitScreen(page, 'coop-host');
  await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
  assert.ok(!hasButton(await frameUi(page), 'Retry'), 'no Retry at the start of the wait');
  assert.ok(hasButton(await frameUi(page), 'Cancel'));
  // The wait really counts up, and past the timeout Retry appears.
  await waitNet(page, (n) => n.wake > 0.4, 'the wake timer to run');
  await debug(page, NET.wakeSeconds, 71);
  assert.ok(hasButton(await frameUi(page), 'Retry'), 'Retry appears after the wake timeout');

  // The budget ends: the error is server unreachable, and trying again is offered.
  const failed = await waitNet(page, (n) => n.phase === PHASE.error, 'the unreachable error', 20000);
  assert.equal(failed.error, ERR.unreachable);
  const ui = await frameUi(page);
  assert.ok(hasButton(ui, 'Retry') && hasButton(ui, 'Back'));

  // Retry goes back to waiting. Back returns to the menu with a clean lobby.
  await clickButton(page, 'Retry');
  await waitNet(page, (n) => n.phase === PHASE.waking || n.phase === PHASE.connecting, 'the wait to start again');
  await waitNet(page, (n) => n.phase === PHASE.error, 'the second error', 20000);
  await clickButton(page, 'Back');
  await waitScreen(page, 'coop-menu');
  assert.equal((await readNet(page)).phase, PHASE.offline);

  // The only console errors are the refused connections themselves.
  for (const problem of problems) assert.match(problem, /ERR_CONNECTION_REFUSED|WebSocket connection|Failed to fetch/, problem);
  await context.close();
});

// ---- Losing the host and losing the server -----------------------------------------

test('the host closes its tab: the guest gets the host-left notice', async () => {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws);
  const b = await openPlayer(relay.ws);
  await toCoop(a.page);
  const room = await host(a.page);
  await toCoop(b.page);
  await join(b.page, room.code);
  await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the lobby');
  await a.page.close();
  const net = await waitNet(b.page, (n) => /host left/i.test(n.notice), 'the host-left notice', 8000);
  assert.equal(net.sub, 'coop-menu');
  assert.equal(net.phase, PHASE.offline, 'the lobby is cleared after the notice');
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('the server goes away: the guest is told the connection was lost', async () => {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws);
  const b = await openPlayer(relay.ws);
  await toCoop(a.page);
  const room = await host(a.page);
  await toCoop(b.page);
  await join(b.page, room.code);
  await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the lobby');
  relay.stop();
  const net = await waitNet(b.page, (n) => /Lost the connection/.test(n.notice), 'the lost-connection notice', 10000);
  assert.equal(net.sub, 'coop-menu');
  await waitNet(a.page, (n) => /Lost the connection/.test(n.notice), 'the same notice for the host', 10000);
  await a.context.close();
  await b.context.close();
});

// ---- Choosing the backend and waking on load -----------------------------------------

test('the server is woken when the page loads, but a local page names no server and wakes nothing', async () => {
  const relay = await startRelay();
  const named = await openPlayer(relay.ws);
  await named.page.waitForFunction(() => LastStandNet.shared().wakeState() === 'awake', null, { timeout: 8000 });
  const plain = await openGame(rig.browser, webUrl(), desktop);
  await waitScreen(plain.page, 'title');
  await plain.page.waitForTimeout(500);
  assert.equal(await plain.page.evaluate(() => LastStandNet.shared().wakeState()), 'idle', 'nothing is woken on a local page');
  assert.deepEqual(plain.problems, [], 'and no request to a server that is not there');
  assert.deepEqual(named.problems, []);
  await named.context.close();
  await plain.context.close();
  relay.stop();
});

test('?fakenet keeps the fake lobby even when a server is named', async () => {
  const relay = await startRelay();
  const { context, page, problems } = await openPlayer(relay.ws, '&fakenet&debug');
  await debug(page, NET.wakeMs, 30);
  await debug(page, NET.latencyMs, 30);
  await toCoop(page);
  const room = await host(page);
  assert.equal(room.code, 'K7QXM', 'the fake lobby always has this code');
  assert.deepEqual(problems, []);
  await context.close();
  relay.stop();
});
