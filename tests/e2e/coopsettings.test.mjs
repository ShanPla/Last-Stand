// The run settings in the co-op lobby: everyone sees the map, difficulty and mode, the host can
// change them with the same selectors as the title, and a guest sees them read-only and live.
//
// The first test uses the REAL relay in server/ with two browser contexts, so the settings really
// travel host -> server -> guest, and the started run is checked against them. The layout tests
// use the game's fake lobby (see coophelpers.mjs) at the five sizes of the brief.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { debug, waitScreen, clickButton, waitSelected, readUi } from './harness.mjs';
import {
  NET, PHASE, sizes, useBrowser, open, waitNet, frameUi, toCoop, host, shot, hasButton, extentOf, checkLayout, isTouch, press,
  startRelay, openPlayer, joinByCode, waitCoop,
} from './coophelpers.mjs';

useBrowser();

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

// The indexes the game saves. They match Difficulty, maps::MapId and RunMode.
const desktopOptions = {};

const settingsOf = async (page) => {
  const ui = await readUi(page);
  return { difficulty: ui.difficulty, map: ui.map, mode: ui.mode };
};

// One press of a selector arrow with the mouse or a finger, then a wait until the host's saved choice has
// moved. Without the wait the next press can come before the game has drawn a frame, and a read
// right after would still show the old value.
async function step(page, options, label, key, direction = 1) {
  const before = (await settingsOf(page))[key];
  await press(page, options, `${label} ${direction > 0 ? '>' : '<'}`);
  const start = Date.now();
  for (;;) {
    const now = (await settingsOf(page))[key];
    if (now !== before) return now;
    if (Date.now() - start > 4000) throw new Error(`"${label}" did not change from ${before} after a press`);
    await page.waitForTimeout(40);
  }
}

// Steps one way until the saved choice has this value.
async function stepTo(page, options, label, key, target) {
  for (let i = 0; i < 6 && (await settingsOf(page))[key] !== target; i++) await step(page, options, label, key);
  assert.equal((await settingsOf(page))[key], target, `${label} reaches ${target}`);
}

// The fake lobby's debug command takes the three settings packed in one number.
const pack = (map, difficulty, mode) => map + difficulty * 16 + mode * 256;

// A guest's copy of what the host chose, from the lobby it has been sent.
const seenBy = (page) => waitNet(page, (n) => n.settingsKnown === 1, 'the settings to reach the lobby');
const guestShows = (page, want) => waitNet(
  page,
  (n) => n.settingsKnown === 1 && n.setDifficulty === want.difficulty && n.setMap === want.map && n.setMode === want.mode,
  `the guest lobby to show ${JSON.stringify(want)}`,
);

test('settings travel from the host to a guest through the real relay, and the run uses them', async () => {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws);
  const b = await openPlayer(relay.ws);
  await toCoop(a.page);
  const room = await host(a.page);
  await toCoop(b.page);
  await joinByCode(b.page, room.code);
  await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the joined lobby');

  // The host announces its saved choice as soon as the room is up, so the guest knows at once.
  const start = await settingsOf(a.page);
  await seenBy(b.page);
  await guestShows(b.page, { difficulty: start.difficulty, map: start.map, mode: start.mode });

  // The guest has no controls for them: no buttons, only read-only rows that are not focusable.
  let guestUi = await frameUi(b.page);
  for (const name of ['Difficulty', 'Map', 'Mode', 'Difficulty <', 'Difficulty >']) assert.ok(!hasButton(guestUi, name), `a guest has a "${name}" control`);
  for (const name of ['Difficulty', 'Map', 'Mode']) assert.ok(extentOf(guestUi, `coop setting ${name}`), `the guest shows ${name}`);
  await b.page.keyboard.press('ArrowDown');           // Any key makes keyboard focus show.
  await waitSelected(b.page, 'Leave');                 // Ready was first, so the next is Leave: nothing between them.
  await b.page.keyboard.press('ArrowUp');
  await b.page.keyboard.press('ArrowUp');
  await waitSelected(b.page, 'Copy');                  // And above Ready is the code, not a setting.

  // The host steps each setting. After each press the saved choice moves and the guest follows.
  const want = { ...start };
  for (let i = 0; i < 3; i++) {
    want.difficulty = await step(a.page, desktopOptions, 'Difficulty', 'difficulty');
    await guestShows(b.page, want);
  }
  want.map = await step(a.page, desktopOptions, 'Map', 'map');
  await guestShows(b.page, want);
  want.mode = await step(a.page, desktopOptions, 'Mode', 'mode');
  await guestShows(b.page, want);
  // The arrows going back, too.
  want.difficulty = await step(a.page, desktopOptions, 'Difficulty', 'difficulty', -1);
  await guestShows(b.page, want);

  // Settle on Hard, Wasteland, Endless, so the run is not just the defaults.
  const target = { difficulty: 2, map: 1, mode: 1 };
  await stepTo(a.page, desktopOptions, 'Difficulty', 'difficulty', target.difficulty);
  await stepTo(a.page, desktopOptions, 'Map', 'map', target.map);
  await stepTo(a.page, desktopOptions, 'Mode', 'mode', target.mode);
  await guestShows(b.page, target);
  assert.deepEqual(await settingsOf(a.page), target);

  // A guest who joins late gets the current choice with the room.
  const c = await openPlayer(relay.ws);
  await toCoop(c.page);
  await joinByCode(c.page, room.code);
  await waitNet(c.page, (n) => n.phase === PHASE.inLobby && n.count === 3, 'the third player in the lobby');
  await guestShows(c.page, target);
  await c.context.close();
  await waitNet(a.page, (n) => n.count === 2, 'the third player to leave');

  await shot(a.page, 'settings-real-host');
  await shot(b.page, 'settings-real-guest');

  // Start. Both players are in a run made from the host's choice.
  await clickButton(b.page, 'Ready');
  await clickButton(a.page, 'Ready');
  await waitNet(a.page, (n) => n.canStart === 1, 'Start to be enabled');
  await clickButton(a.page, 'Start');
  const [hostRun, guestRun] = await Promise.all([
    waitCoop(a.page, (r) => r.role === 'host' && r.screen === 'playing', 'the host in the run'),
    waitCoop(b.page, (r) => r.role === 'guest' && r.screen === 'playing' && r.count === 2, 'the guest in the run'),
  ]);
  for (const [who, run] of [['host', hostRun], ['guest', guestRun]]) {
    assert.deepEqual([run.runDifficulty, run.runMap, run.runMode], [target.difficulty, target.map, target.mode], `the ${who}'s run settings`);
  }

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('the host choice in the lobby is the title choice afterwards, and the keyboard order is top to bottom', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  await page.waitForTimeout(150);
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowUp');                // Selection starts on Ready, so Down then Up is still Ready.
  await waitSelected(page, 'Ready');
  for (const name of ['Mode', 'Map', 'Difficulty', 'Copy']) {
    await page.keyboard.press('ArrowUp');
    await waitSelected(page, name);
  }
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Difficulty');
  const before = await settingsOf(page);
  await page.keyboard.press('ArrowRight');             // Right on a selector steps it, as on the title.
  await waitNet(page, (n) => n.setDifficulty === (before.difficulty + 1) % 4, 'the arrow key to change the difficulty');
  assert.equal((await settingsOf(page)).difficulty, (before.difficulty + 1) % 4);
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Map');
  await page.keyboard.press('Enter');                  // Enter steps forward.
  await waitNet(page, (n) => n.setMap === (before.map + 1) % 2, 'Enter to change the map');

  // The map shows through the screen too, and leaving keeps the choice. The title shows the same one.
  const chosen = await settingsOf(page);
  await press(page, {}, 'Leave');
  await waitScreen(page, 'coop-menu');
  await press(page, {}, 'Back');
  await waitScreen(page, 'title');
  assert.deepEqual(await settingsOf(page), chosen);
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, options] of sizes) {
  test(`the lobby settings lay out cleanly for host and guest at ${name}`, async () => {
    const { context, page, problems } = await open(options);
    const touch = isTouch(options);
    await toCoop(page, options);
    await host(page, options);
    const copyOverlap = [['coop code', 'Copy'], ['coop code', 'Copied']];

    // The host: three selectors, each with its two arrows, plus every old control.
    await debug(page, NET.room, 4);
    await debug(page, NET.readyMask, 0b1111);
    let ui = await frameUi(page);
    for (const label of ['Difficulty', 'Map', 'Mode']) {
      for (const part of [`${label} <`, label, `${label} >`]) assert.ok(hasButton(ui, part), `host has "${part}": ${ui.buttons.map((b) => b.n)}`);
    }
    for (const label of ['Copy', 'Ready', 'Start', 'Leave']) assert.ok(hasButton(ui, label), `host has ${label}`);
    checkLayout(ui, `host lobby settings ${name}`, copyOverlap);
    if (touch) {
      for (const b of ui.buttons.filter((x) => /^(Difficulty|Map|Mode)( [<>])?$/.test(x.n))) {
        assert.ok(b.ch >= 43.5 && b.cw >= 43.5, `"${b.n}" is ${b.cw.toFixed(0)}x${b.ch.toFixed(0)} CSS pixels at ${name}`);
      }
    }
    await shot(page, `settings-${name}-host`);

    // The longest names, to check the text fits: Hell, Wasteland, Endless.
    await stepTo(page, options, 'Difficulty', 'difficulty', 3);
    await stepTo(page, options, 'Map', 'map', 1);
    await stepTo(page, options, 'Mode', 'mode', 1);
    ui = await frameUi(page);
    checkLayout(ui, `host lobby long names ${name}`, copyOverlap);
    await shot(page, `settings-${name}-host-long`);

    // The host sent the choice to the (fake) room once, and the lobby carries it.
    const net = await waitNet(page, (n) => n.settingsKnown === 1 && n.setDifficulty === 3 && n.setMap === 1 && n.setMode === 1, 'the host settings');
    assert.equal(net.canStart, 1);

    // A guest: read-only rows, nothing focusable, and the same controls as before otherwise.
    await debug(page, NET.guestRoom, 4);
    await debug(page, NET.readyMask, 0b0011);
    await debug(page, NET.roomSettings, pack(0, 1, 0));   // City, Normal, Story.
    ui = await frameUi(page);
    for (const label of ['Difficulty', 'Map', 'Mode']) {
      assert.ok(!hasButton(ui, label) && !hasButton(ui, `${label} <`) && !hasButton(ui, `${label} >`), `the guest has a ${label} control`);
      assert.ok(extentOf(ui, `coop setting ${label}`), `the guest shows ${label}`);
    }
    assert.ok(!hasButton(ui, 'Start'));
    for (const label of ['Copy', 'Ready', 'Leave']) assert.ok(hasButton(ui, label), `guest has ${label}`);
    checkLayout(ui, `guest lobby settings ${name}`, copyOverlap);
    await shot(page, `settings-${name}-guest`);

    // The longest again: Hell (3), Wasteland (1), Endless (1), then nothing from the host yet.
    await debug(page, NET.roomSettings, pack(1, 3, 1));
    ui = await frameUi(page);
    checkLayout(ui, `guest lobby long names ${name}`, copyOverlap);
    await shot(page, `settings-${name}-guest-long`);
    let guestNet = await waitNet(page, (n) => n.setDifficulty === 3 && n.setMap === 1 && n.setMode === 1, 'the guest to see the change');
    assert.equal(guestNet.settingsKnown, 1);
    await debug(page, NET.roomSettings, -1);
    ui = await frameUi(page);
    checkLayout(ui, `guest lobby no settings ${name}`, copyOverlap);
    await shot(page, `settings-${name}-guest-none`);
    guestNet = await waitNet(page, (n) => n.settingsKnown === 0, 'the settings to clear');

    // A guest cannot change anything by tapping or clicking where the rows are.
    const row = extentOf(ui, 'coop setting Map');
    const sx = ui.width ? (await page.evaluate(() => innerWidth)) / ui.width : 1;
    const sy = ui.height ? (await page.evaluate(() => innerHeight)) / ui.height : 1;
    const before = await settingsOf(page);
    if (touch) await page.touchscreen.tap((row.x + row.w / 2) * sx, (row.y + row.h / 2) * sy);
    else await page.mouse.click((row.x + row.w / 2) * sx, (row.y + row.h / 2) * sy);
    await page.waitForTimeout(200);
    assert.deepEqual(await settingsOf(page), before, 'a click on a guest row changed the saved choice');

    assert.deepEqual(problems, []);
    await context.close();
  });
}

// A relay from before this feature answers the unknown `settings` message with bad-request. The
// host must carry on as if nothing happened: still in its room, no error screen, and the choice
// is only sent once, not over and over.
test('a relay that does not know the settings message changes nothing for the host', async () => {
  const { WebSocketServer } = createRequire(path.join(repoRoot, 'server/package.json'))('ws');
  const received = [];
  // The page wakes the relay with a plain request to /healthz first, so the stand-in answers that too.
  const http = createServer((req, res) => {
    res.writeHead(200, { 'Access-Control-Allow-Origin': '*', 'Content-Type': 'text/plain' });
    res.end('ok');
  });
  const wss = new WebSocketServer({ server: http });
  await new Promise((resolve) => http.listen(0, '127.0.0.1', resolve));
  wss.on('connection', (ws) => {
    ws.on('message', (data, isBinary) => {
      if (isBinary) return;
      const msg = JSON.parse(data.toString());
      received.push(msg.t);
      if (msg.t === 'hello') ws.send(JSON.stringify({ t: 'welcome', v: 1 }));
      else if (msg.t === 'create') {
        ws.send(JSON.stringify({ t: 'room', code: 'K7QXM', you: 0, host: 0, started: false, players: [{ id: 0, name: msg.name, ready: false }] }));
      } else if (msg.t === 'ping') ws.send(JSON.stringify({ t: 'pong', n: msg.n }));
      else ws.send(JSON.stringify({ t: 'error', code: 'bad-request' }));  // Including `settings`, which this relay predates.
    });
  });
  const sent = () => received.filter((t) => t === 'settings').length;
  try {
    const a = await openPlayer(`ws://127.0.0.1:${http.address().port}`);
    await toCoop(a.page);
    await host(a.page);
    await a.page.waitForTimeout(1200);
    const net = await waitNet(a.page, (n) => n.phase === PHASE.inLobby, 'still in the room');
    assert.equal(net.sub, 'coop-host');
    assert.equal(net.settingsKnown, 0, 'the old relay never passes settings back');
    assert.equal(sent(), 1, `settings sent ${sent()} times: ${received}`);
    // The host's own controls still work, and each change is one more send. Normal to Hell is
    // two steps, so two more (and nothing in between, whatever the frame rate).
    const from = (await settingsOf(a.page)).difficulty;
    await stepTo(a.page, desktopOptions, 'Difficulty', 'difficulty', 3);
    await a.page.waitForTimeout(400);
    assert.equal(sent(), 1 + ((3 - from + 4) % 4), `settings sent ${sent()} times: ${received}`);
    assert.equal((await waitNet(a.page, (n) => n.phase === PHASE.inLobby, 'the room')).sub, 'coop-host');
    assert.deepEqual(a.problems, []);
    await a.context.close();
  } finally {
    wss.clients.forEach((client) => client.terminate());  // The server's close() would wait for these forever.
    wss.close();
    http.closeAllConnections();
    await new Promise((resolve) => http.close(resolve));
  }
});
