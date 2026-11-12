// Rejoin in online co-op, against the REAL relay server in server/, through the game's own
// screens. A guest who reloads comes back with the Rejoin button, a guest whose connection
// drops comes back by itself, the host's Play Again with nobody left goes solo with a notice,
// and a Rejoin for a room that has closed says so and goes away.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { waitScreen, clickButton, debug, readUi, watchProblems } from './harness.mjs';
import {
  useBrowser, readNet, waitNet, readCoop, waitCoop, startCoopRun, near, toCoop, gameUrl, shot, COOP_CMD as CMD,
} from './coophelpers.mjs';

useBrowser();

const phoneSize = { width: 390, height: 844 };
const desktopSize = { width: 1280, height: 720 };

// Lets the guest level up once and pick a card, so there is a level and a build to keep.
async function levelGuest(a, b) {
  await debug(a.page, CMD.levelUpSlot, 1);
  await waitCoop(b.page, (c) => c.players[1].cards === 1, "the guest's cards");
  await b.page.waitForTimeout(900);  // The cards ignore presses for a moment after they open.
  await b.page.keyboard.press('Enter');
  return waitCoop(a.page, (c) => c.players[1].cards === 0 && c.players[1].level === 2, 'the guest at level 2');
}

// Both machines agree on where both soldiers stand.
async function agree(a, b, what) {
  await a.page.waitForTimeout(600);
  const [hostSees, guestSees] = await Promise.all([readCoop(a.page), readCoop(b.page)]);
  for (const slot of [0, 1]) {
    assert.ok(near(hostSees.players[slot], guestSees.players[slot], 4), `${what}, soldier ${slot}: ${JSON.stringify([hostSees.players[slot], guestSees.players[slot]])}`);
  }
  return { hostSees, guestSees };
}

// The page's WebSocket, found through a ping it sends (one goes out every 2 seconds).
async function catchSocket(page) {
  await page.evaluate(() => {
    const realSend = WebSocket.prototype.send;
    WebSocket.prototype.send = function (data) { window.lastSocket = this; return realSend.call(this, data); };
  });
  await page.waitForFunction(() => !!window.lastSocket, null, { timeout: 6000 });
}

test('co-op rejoin: a guest who reloads the page presses Rejoin and is back in the same slot, level and build', async () => {
  const { relay, a, b, code } = await startCoopRun();
  const leveled = await levelGuest(a, b);
  const before = leveled.players[1];

  // The guest's page reloads. The host keeps the soldier, down and still.
  await b.page.reload();
  await waitScreen(b.page, 'title', 30000);
  const away = await waitCoop(a.page, (c) => c.players[1].connected === 0 && c.players[1].downed === 1, 'the guest away and down');
  assert.match(away.notice, /left the run/);

  // The co-op menu offers the run back, first and selected.
  await toCoop(b.page);
  const menu = await waitNet(b.page, (n) => n.rejoin === code, 'the saved seat');
  assert.equal(menu.sub, 'coop-menu');
  const ui = await readUi(b.page);
  assert.ok(ui.buttons.some((button) => button.n === `Rejoin ${code}`), `buttons: ${ui.buttons.map((button) => button.n)}`);
  assert.ok(ui.selected.startsWith('Rejoin'), `selected: ${ui.selected}`);
  await shot(b.page, 'rejoin-menu-desktop');
  await b.page.setViewportSize(phoneSize);
  await b.page.waitForTimeout(400);
  await shot(b.page, 'rejoin-menu-phone');
  await b.page.setViewportSize(desktopSize);
  await b.page.waitForTimeout(300);

  await clickButton(b.page, `Rejoin ${code}`);
  const back = await waitCoop(b.page, (c) => c.role === 'guest' && c.screen === 'playing' && c.slot === 1, 'the guest back in the run', 15000);
  assert.equal(back.count, 2);
  const hostBack = await waitCoop(a.page, (c) => c.players[1].connected === 1, 'the host to see the guest back');
  assert.match(hostBack.notice, /is back/);
  const { hostSees, guestSees } = await agree(a, b, 'after the rejoin');
  for (const seen of [hostSees.players[1], guestSees.players[1]]) {
    assert.equal(seen.level, before.level, 'the same level');
    assert.equal(seen.xp, before.xp, 'the same XP');
    assert.equal(seen.weapons, before.weapons, 'the same weapons');
    assert.equal(seen.downed, 1, 'still down until a teammate helps');
  }
  assert.equal(guestSees.run, hostSees.run, 'the same run');

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

for (const [label, extra] of [['default transport', ''], ['transport=relay', '&transport=relay']]) {
  test(`co-op rejoin: a guest whose connection drops reconnects by itself, without pressing anything (${label})`, async () => {
    const { relay, a, b } = await startCoopRun(extra);
    await catchSocket(b.page);
    // Keeps the guest from reconnecting for a moment, so the Reconnecting state can be seen.
    await b.page.evaluate(() => {
      window.RealWebSocket = window.WebSocket;
      window.WebSocket = function () { throw new Error('blocked for the test'); };
    });
    await b.page.evaluate(() => window.lastSocket.close());
    const cut = await waitCoop(b.page, (c) => c.reconnecting === 1, 'Reconnecting');
    assert.equal(cut.screen, 'playing', 'the run stays on screen');
    await waitCoop(a.page, (c) => c.players[1].connected === 0, 'the host to see the guest away');
    if (!extra) {
      await shot(b.page, 'rejoin-reconnecting-desktop');
      await b.page.setViewportSize(phoneSize);
      await b.page.waitForTimeout(400);
      await shot(b.page, 'rejoin-reconnecting-phone');
      await b.page.setViewportSize(desktopSize);
    }
    assert.equal((await readCoop(b.page)).reconnecting, 1);

    await b.page.evaluate(() => { window.WebSocket = window.RealWebSocket; });
    await waitCoop(b.page, (c) => c.reconnecting === 0 && c.role === 'guest' && c.screen === 'playing' && c.slot === 1, 'the guest reconnected', 20000);
    const hostBack = await waitCoop(a.page, (c) => c.players[1].connected === 1, 'the host to see the guest back');
    assert.match(hostBack.notice, /is back/);
    // Snapshots flow again, and the two agree.
    await waitCoop(b.page, (c) => c.snapIn > 0 && c.hostAway === 0, 'snapshots again');
    const { guestSees } = await agree(a, b, 'after reconnecting');
    if (extra) assert.equal(guestSees.transport, 'relay');
    assert.equal((await readNet(b.page)).sub, 'coop-host', 'the co-op screens never took over');

    assert.deepEqual(a.problems, []);
    assert.deepEqual(b.problems, []);
    await a.context.close();
    await b.context.close();
    relay.stop();
  });
}

test('co-op rejoin: Play Again with the guest gone goes solo with a notice, and a stale Rejoin then says the room closed', async () => {
  const { relay, a, b, code } = await startCoopRun();
  // The guest closes the tab. Its seat is still saved in that browser.
  await b.page.close();
  await waitCoop(a.page, (c) => c.players[1].connected === 0, 'the guest gone');

  await debug(a.page, CMD.forceDeath, 0);
  await waitScreen(a.page, 'over');
  await a.page.waitForTimeout(1200);  // The buttons ignore presses for a moment.
  await clickButton(a.page, 'PLAY AGAIN');
  const solo = await waitCoop(a.page, (c) => c.role === 'none' && c.screen === 'playing', 'a single-player run');
  assert.equal(solo.notice, 'Everyone else left. Playing solo.');
  assert.equal(solo.count, 1);
  await a.page.waitForTimeout(300);
  await shot(a.page, 'rejoin-solo-notice');

  // The guest opens the game again in the same browser: the Rejoin button is there, and pressing
  // it says the room has closed, and the button goes away.
  const page = await b.context.newPage();
  const problems = watchProblems(page);
  await page.goto(gameUrl(relay.ws));
  await waitScreen(page, 'title', 30000);
  await toCoop(page);
  await waitNet(page, (n) => n.rejoin === code, 'the saved seat');
  await clickButton(page, `Rejoin ${code}`);
  const refused = await waitNet(page, (n) => n.sub === 'coop-menu' && /Room closed/.test(n.notice), 'the room-closed notice', 15000);
  assert.equal(refused.rejoin, '');
  await page.waitForTimeout(200);
  const ui = await readUi(page);
  assert.ok(!ui.buttons.some((button) => button.n.startsWith('Rejoin')), `buttons: ${ui.buttons.map((button) => button.n)}`);
  await shot(page, 'rejoin-room-closed');
  assert.deepEqual(problems, []);
  assert.deepEqual(a.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});
