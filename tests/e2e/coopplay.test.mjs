// Online co-op during the run, against the REAL relay server in server/, through the real
// browser networking and the game's own screens. Two players are two browser contexts: the
// host runs the game, the guest draws the host's snapshots and predicts its own soldier.
import { test, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:net';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { openGame, waitScreen, clickButton, debug, readUi, CMD as BASE_CMD } from './harness.mjs';
import { PHASE, useBrowser, readNet, waitNet, toCoop, typeText, host, webUrl, desktop } from './coophelpers.mjs';

// The game's debug commands, with the co-op one this file adds. See DebugCommand in App.cpp.
const CMD = { ...BASE_CMD, levelUpSlot: 48 };

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

const readCoop = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_coop', 'string', [], [])));

async function waitCoop(page, check, what, timeout = 10000) {
  const start = Date.now();
  for (;;) {
    const coop = await readCoop(page);
    if (check(coop)) return coop;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}: ${JSON.stringify(coop)}`);
    await page.waitForTimeout(50);
  }
}

async function openPlayer(server, extra) {
  const session = await openGame(rig.browser, `${webUrl()}?server=${server}&debug${extra}`, desktop);
  await waitScreen(session.page, 'title');
  return session;
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

// Two players through the real lobby: host, join, ready, start, and both in the run.
async function startCoopRun(extra = '') {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws, extra);
  const b = await openPlayer(relay.ws, extra);
  await toCoop(a.page);
  const room = await host(a.page);
  await toCoop(b.page);
  await join(b.page, room.code);
  await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the joined lobby');
  await clickButton(b.page, 'Ready');
  await clickButton(a.page, 'Ready');
  await waitNet(a.page, (n) => n.canStart === 1, 'Start to be enabled');
  await clickButton(a.page, 'Start');
  const [hostRun, guestRun] = await Promise.all([
    waitCoop(a.page, (c) => c.role === 'host' && c.screen === 'playing', 'the host in the run'),
    waitCoop(b.page, (c) => c.role === 'guest' && c.screen === 'playing' && c.count === 2, 'the guest in the run'),
  ]);
  return { relay, a, b, hostRun, guestRun };
}

const near = (p, q, tolerance) => Math.hypot(p.x - q.x, p.y - q.y) <= tolerance;

// Holds a key on each page at the same time, as two people would.
async function holdKeys(pairs, ms) {
  await Promise.all(pairs.map(([page, key]) => page.keyboard.down(key)));
  await pairs[0][0].waitForTimeout(ms);
  await Promise.all(pairs.map(([page, key]) => page.keyboard.up(key)));
}

for (const [label, extra] of [['default transport', ''], ['transport=relay', '&transport=relay']]) {
  test(`co-op run: both players move, and the guest sees what the host has (${label})`, async () => {
    const { relay, a, b, hostRun, guestRun } = await startCoopRun(extra);
    assert.equal(hostRun.slot, 0);
    assert.equal(guestRun.slot, 1);
    assert.equal(hostRun.count, 2);
    const bestRunsBefore = (await readUi(a.page)).bests.map((row) => row[4]);

    // Zombies around both players, kept topped up. The stress mode also keeps everyone at full
    // health, so nobody goes down while the test looks.
    await debug(a.page, CMD.stressEnemies, 150);
    await a.page.waitForTimeout(600);

    // Real key presses: the host walks right, the guest walks down, both at once.
    const start = await readCoop(a.page);
    await holdKeys([[a.page, 'd'], [b.page, 's']], 900);
    await a.page.waitForTimeout(800);

    const [hostSees, guestSees] = await Promise.all([readCoop(a.page), readCoop(b.page)]);
    assert.ok(hostSees.players[0].x - start.players[0].x > 120, `the host walked right: ${JSON.stringify([start.players[0], hostSees.players[0]])}`);
    assert.ok(hostSees.players[1].y - start.players[1].y > 120, `the host saw the guest walk down: ${JSON.stringify([start.players[1], hostSees.players[1]])}`);
    // Both machines agree on where both soldiers stand.
    assert.ok(near(hostSees.players[0], guestSees.players[0], 4), `host soldier: ${JSON.stringify([hostSees.players[0], guestSees.players[0]])}`);
    assert.ok(near(hostSees.players[1], guestSees.players[1], 4), `guest soldier: ${JSON.stringify([hostSees.players[1], guestSees.players[1]])}`);
    // The guest draws the zombies near it, as many as the host has near the guest.
    const nearGuest = hostSees.near[1];
    assert.ok(nearGuest > 40, `zombies near the guest: ${nearGuest}`);
    assert.ok(Math.abs(guestSees.enemies - nearGuest) <= Math.max(6, nearGuest * 0.15), `guest draws ${guestSees.enemies}, host has ${nearGuest} near it`);
    assert.equal(guestSees.kills, hostSees.kills);

    // Snapshots: about 20 a second, never over 8 KB, and far under the relay's 150 messages a second.
    const hostNumbers = await waitCoop(a.page, (c) => c.snapOut > 0, 'a second of snapshot counts');
    const guestNumbers = await waitCoop(b.page, (c) => c.snapIn > 0, 'a second of snapshots arriving');
    console.log(`[${label}] host: ${hostNumbers.snapOut} snapshots/s, ${hostNumbers.msgOut} msgs/s out, ${hostNumbers.bytesOut} B/s out, snapshot ${hostNumbers.snapBytes} B (avg ${hostNumbers.snapAvg}, max ${hostNumbers.snapMax}); guest: ${guestNumbers.snapIn} snapshots/s in, ${guestNumbers.msgOut} msgs/s out, ${guestNumbers.inputsIn} inputs/s, corrections ${guestNumbers.corrections}, jumps ${guestNumbers.jumps}, transport ${guestNumbers.transport}, ping ${guestNumbers.ping} ms`);
    assert.ok(hostNumbers.snapOut >= 15 && hostNumbers.snapOut <= 22, `snapshots out per second: ${hostNumbers.snapOut}`);
    assert.ok(guestNumbers.snapIn >= 12 && guestNumbers.snapIn <= 22, `snapshots in per second: ${guestNumbers.snapIn}`);
    assert.ok(hostNumbers.snapMax > 200 && hostNumbers.snapMax <= 8192, `biggest snapshot: ${hostNumbers.snapMax}`);
    assert.ok(hostNumbers.msgOut < 60, `host messages per second: ${hostNumbers.msgOut}`);
    // About 30 inputs a second, fewer when the page draws fewer frames than that.
    assert.ok(guestNumbers.msgOut >= 12 && guestNumbers.msgOut < 60, `guest messages per second: ${guestNumbers.msgOut}`);
    assert.ok(hostNumbers.inputsIn >= 12, `inputs reaching the host per second: ${hostNumbers.inputsIn}`);
    if (extra) assert.equal(guestNumbers.transport, 'relay');

    // The network overlay, on the guest, with F3.
    await b.page.keyboard.press('F3');
    await waitCoop(b.page, (c) => c.overlay === 1, 'the overlay to show');
    await b.page.screenshot({ path: `tests/e2e/screenshots/coop-play-guest${extra ? '-relay' : ''}.png` });
    await a.page.screenshot({ path: `tests/e2e/screenshots/coop-play-host${extra ? '-relay' : ''}.png` });
    await b.page.keyboard.press('F3');

    // A crowd of 2,000 zombies and 2,000 shots: the snapshots fill up, and stop at 8 KB.
    await debug(a.page, CMD.stressEnemies, 2000);
    await debug(a.page, CMD.stressProjectiles, 2000);
    await a.page.waitForTimeout(1500);
    const crowded = await waitCoop(a.page, (c) => c.snapMax > 6000, 'full snapshots', 6000);
    const crowdedGuest = await readCoop(b.page);
    console.log(`[${label}] crowded host: ${crowded.snapOut} snapshots/s, snapshot ${crowded.snapBytes} B (max ${crowded.snapMax}), ${crowded.bytesOut} B/s out; guest draws ${crowdedGuest.enemies} zombies, ${crowdedGuest.snapIn} snapshots/s in`);
    assert.ok(crowded.snapMax <= 8192, `biggest snapshot: ${crowded.snapMax}`);
    assert.ok(crowdedGuest.enemies > 100, `the guest still draws the nearest zombies: ${crowdedGuest.enemies}`);
    await debug(a.page, CMD.stressProjectiles, 0);

    // The run ends for both. A co-op run is not a best run.
    await debug(a.page, CMD.stressEnemies, 0);
    await debug(a.page, CMD.forceDeath, 0);
    await waitScreen(a.page, 'over');
    await waitScreen(b.page, 'over');
    const bestRunsAfter = (await readUi(a.page)).bests.map((row) => row[4]);
    assert.deepEqual(bestRunsAfter, bestRunsBefore, 'co-op runs are kept out of the best runs');

    // The host plays again in the same room, and the guest follows.
    await a.page.waitForTimeout(1200);  // The buttons ignore presses for a moment.
    await clickButton(a.page, 'PLAY AGAIN');
    await waitCoop(a.page, (c) => c.screen === 'playing' && c.run === 2, 'the host in the second run');
    await waitCoop(b.page, (c) => c.screen === 'playing' && c.run === 2, 'the guest in the second run');

    assert.deepEqual(a.problems, []);
    assert.deepEqual(b.problems, []);
    await a.context.close();
    await b.context.close();
    relay.stop();
  });
}

test('co-op run: the host hiding the tab pauses everyone, and only the host can pause', async () => {
  const { relay, a, b } = await startCoopRun();
  // The guest's Esc opens its own menu, and the run goes on.
  const before = await readCoop(a.page);
  await b.page.keyboard.press('Escape');
  await waitScreen(b.page, 'paused');
  await a.page.waitForTimeout(500);
  assert.ok((await readCoop(a.page)).tick > before.tick + 15, 'the host kept running');
  await b.page.keyboard.press('Escape');
  await waitScreen(b.page, 'playing');

  // The host's tab is hidden: everyone sees that the host is away.
  await a.page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { value: true, configurable: true });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await waitCoop(b.page, (c) => c.hostAway === 1, 'Host is away');
  await waitScreen(a.page, 'paused');
  const frozen = (await readCoop(a.page)).tick;
  await a.page.waitForTimeout(400);
  assert.equal((await readCoop(a.page)).tick, frozen, 'the run stands still');
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-play-host-away.png' });

  // Back again: the host's pause menu is open, so everyone is paused until the host resumes.
  await a.page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { value: false, configurable: true });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await waitCoop(b.page, (c) => c.hostAway === 0 && c.hostPaused === 1, 'Paused by the host');
  await a.page.keyboard.press('Escape');
  await waitScreen(a.page, 'playing');
  await waitCoop(b.page, (c) => c.hostAway === 0 && c.hostPaused === 0, 'the run going again');

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('co-op run: the host leaving ends the guest\'s run with a message', async () => {
  const { relay, a, b } = await startCoopRun('&transport=relay');
  await b.page.waitForTimeout(500);
  await a.page.close();
  const net = await waitNet(b.page, (n) => /host left/i.test(n.notice), 'the host-left notice', 10000);
  assert.equal(net.sub, 'coop-menu');
  const coop = await readCoop(b.page);
  assert.equal(coop.role, 'none');
  assert.equal((await readUi(b.page)).screen, 'coop-menu');
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-play-host-left.png' });
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('co-op run: a downed player is shown to both and revived by the other, and a guest picks a card over the running game', async () => {
  const { relay, a, b } = await startCoopRun();
  // The host goes down. Both screens show it.
  await debug(a.page, CMD.setHp, 0);
  await waitCoop(a.page, (c) => c.players[0].downed === 1, 'the host down');
  await waitCoop(b.page, (c) => c.players[0].downed === 1, 'the guest to see the host down');
  // The guest walks over and stands by the host until they are back up.
  await b.page.keyboard.down('d');
  await b.page.waitForTimeout(250);
  await b.page.keyboard.up('d');
  await waitCoop(a.page, (c) => c.players[0].downed === 1, 'still down while being helped');
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-play-revive.png' });
  await waitCoop(a.page, (c) => c.players[0].downed === 0 && c.players[0].hp > 0, 'the host revived', 8000);
  await waitCoop(b.page, (c) => c.players[0].downed === 0, 'the guest to see the host back up');

  // The guest levels up: their cards open, the run goes on, and the pick reaches the host.
  const before = await readCoop(a.page);
  await debug(a.page, CMD.levelUpSlot, 1);
  await waitCoop(b.page, (c) => c.players[1].cards === 1, "the guest's cards");
  await waitScreen(b.page, 'playing');
  await b.page.waitForTimeout(900);  // The cards ignore presses for a moment after they open.
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-play-cards.png' });
  assert.ok((await readCoop(a.page)).tick > before.tick + 30, "the run did not pause for the guest's cards");
  await b.page.keyboard.press('Enter');
  await waitCoop(a.page, (c) => c.players[1].cards === 0 && c.players[1].level === 2, "the host to take the guest's card");
  await waitCoop(b.page, (c) => c.players[1].cards === 0, "the guest's cards to close");

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('co-op run: a guest who leaves stays down, and the host is told', async () => {
  const { relay, a, b } = await startCoopRun();
  // The co-op marks at phone sizes, upright and on its side, for looking at.
  await b.page.keyboard.press('F3');
  for (const [name, size] of [['phone', { width: 390, height: 844 }], ['phone-landscape', { width: 844, height: 390 }]]) {
    await b.page.setViewportSize(size);
    await b.page.waitForTimeout(500);
    await b.page.screenshot({ path: `tests/e2e/screenshots/coop-play-${name}.png` });
  }
  await b.page.close();
  const hostSees = await waitCoop(a.page, (c) => c.players[1].connected === 0 && c.players[1].downed === 1, 'the guest down and gone', 10000);
  assert.match(hostSees.notice, /left the run/);
  assert.equal(hostSees.screen, 'playing', 'the run goes on for the host');
  await a.page.screenshot({ path: 'tests/e2e/screenshots/coop-play-guest-left.png' });
  assert.deepEqual(a.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});
