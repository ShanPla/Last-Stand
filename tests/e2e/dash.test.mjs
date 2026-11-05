// The dash, tested in a real browser: every way to press it, what it must not do, and
// where its button sits on a phone.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, findButton, startGame, installFakePad, padPress,
} from './harness.mjs';

let web;
let browser;

before(async () => {
  web = await startServer();
  browser = await launchChrome();
});

after(async () => {
  await browser?.close();
  web?.server.close();
});

const desktop = { viewport: { width: 1280, height: 720 } };
const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const phoneLandscape = { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };

const open = (options = desktop) => openGame(browser, web.url + '?debug', options);
const waitPhase = (page, wanted, timeout = 5000) =>
  page.waitForFunction((p) => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === p, wanted, { timeout });

// Waits until a dash has started (its cooldown is running), and returns the state then.
const waitDash = (page, timeout = 10000) =>
  page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).dashCooldown > 0, null, { timeout });
const waitDashOver = (page, timeout = 10000) =>
  page.waitForFunction(() => {
    const s = JSON.parse(Module.ccall('ls_query_state', 'string', [], []));
    return s.dashCooldown > 0 && s.dashLeft === 0;
  }, null, { timeout });

// Waits a moment, then says whether no dash happened.
async function assertNoDash(page, ms = 700) {
  await page.waitForTimeout(ms);
  const state = await readState(page);
  assert.equal(state.dashCooldown, 0, 'no dash was started');
  assert.equal(state.dashLeft, 0);
}

const resetCooldown = (page) => debug(page, CMD.setDashCooldown, 0);

// ---- Keyboard ---------------------------------------------------------------------

test('Shift dashes, and the dash covers a few strides in the facing direction', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.waitForTimeout(300);
  const before = await readState(page);
  await page.keyboard.press('Shift');
  await waitDash(page);
  await waitDashOver(page);
  const after = await readState(page);
  const moved = Math.hypot(after.x - before.x, after.y - before.y);
  // 170 world units, less a little for the steps that ran before the first reading.
  assert.ok(moved > 150 && moved < 190, `dash moved ${moved.toFixed(1)}`);
  assert.ok(after.dashCooldown > 2.5 && after.dashCooldown <= 4, `cooldown ${after.dashCooldown}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a dash goes where the player is moving', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.setHp, 100);
  const before = await readState(page);
  await page.keyboard.down('s');
  await page.waitForTimeout(150);
  await page.keyboard.press('Shift');
  await waitDash(page);
  await waitDashOver(page);
  await page.keyboard.up('s');
  const after = await readState(page);
  assert.ok(after.y - before.y > 150, 'dashed down');
  assert.ok(Math.abs(after.x - before.x) < 30, 'and not sideways');
  assert.ok(Math.abs(after.dashY) > 0.9);
  await context.close();
  assert.deepEqual(problems, []);
});

test('Space dashes during play, and Enter does not', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.press('Enter');
  await assertNoDash(page, 500);
  await page.keyboard.press(' ');
  await waitDash(page);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a press while the dash recharges does nothing and is not remembered', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.press('Shift');
  await waitDash(page);
  await waitDashOver(page);
  const first = await readState(page);
  await page.keyboard.press('Shift');
  await page.keyboard.press(' ');
  await page.waitForTimeout(300);
  const second = await readState(page);
  assert.equal(second.dashLeft, 0, 'no second dash started');
  assert.ok(second.dashCooldown < first.dashCooldown, 'the cooldown kept counting down, not restarting');
  // Once recharged, the earlier presses must not fire on their own.
  await debug(page, CMD.setDashCooldown, 3);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).dashCooldown === 0, null, { timeout: 3000 });
  await assertNoDash(page, 500);
  // And now it works again.
  await page.keyboard.press('Shift');
  await waitDash(page);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the Space that starts a run does not dash, and neither does the Space that closes a card', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.waitForTimeout(200);
  await page.keyboard.press(' ');
  await waitScreen(page, 'playing');
  await assertNoDash(page, 800);

  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.waitForTimeout(500);
  await page.keyboard.press(' ');
  await waitPhase(page, 0);
  await assertNoDash(page, 800);

  // The same on the game over screen: Space restarts and does not dash.
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(2200);
  await page.keyboard.press(' ');
  await waitScreen(page, 'playing');
  await assertNoDash(page, 800);

  // And resuming from pause with Space.
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await page.waitForTimeout(300);
  await page.keyboard.press(' ');
  await waitScreen(page, 'playing');
  await assertNoDash(page, 800);
  assert.deepEqual(problems, []);
  await context.close();
});

test('no dash starts on the level-up screen, and the cooldown stands still there', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.keyboard.press('Shift');
  await page.waitForTimeout(200);
  await page.keyboard.press('Shift');
  assert.equal((await readState(page)).dashCooldown, 0);

  await page.waitForTimeout(500);
  await page.keyboard.press('Enter');
  await waitPhase(page, 0);
  await assertNoDash(page, 400);

  // A recharging dash does not recharge while the cards are open.
  await page.keyboard.press('Shift');
  await waitDash(page);
  await debug(page, CMD.setDashCooldown, 90);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  const frozen = (await readState(page)).dashCooldown;
  await page.waitForTimeout(1200);
  assert.equal((await readState(page)).dashCooldown, frozen);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a dash press while paused is thrown away', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await page.keyboard.press('Shift');
  await page.waitForTimeout(200);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'playing');
  await assertNoDash(page, 600);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Mouse, gamepad -------------------------------------------------------------------

test('a click on the dash indicator dashes, on a desktop', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  const dash = await findButton(page, 'Dash');
  assert.ok(dash.cw >= 30 && dash.ch >= 30);
  await page.mouse.click(dash.cx, dash.cy);
  await waitDash(page);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the gamepad right shoulder dashes, and A does not', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await installFakePad(page);
  await page.waitForTimeout(300);
  await padPress(page, 0);  // A starts the run.
  await waitScreen(page, 'playing');
  await assertNoDash(page, 600);
  await padPress(page, 0);
  await assertNoDash(page, 400);
  await padPress(page, 5);  // Right shoulder.
  await waitDash(page);
  assert.equal((await readUi(page)).device, 'gamepad');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Touch ----------------------------------------------------------------------------

for (const [name, options] of [['portrait phone', phone], ['landscape phone', phoneLandscape]]) {
  test(`the touch dash button on a ${name}: big, in a lower corner, clear of the pause button, and a tap dashes`, async () => {
    const { context, page, problems } = await open(options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(150);
    await tapStartIfNeeded(page);
    // The button list comes from the last frame drawn, which can still be the title screen's.
    await findButton(page, 'Dash');
    const ui = await readUi(page);
    const dash = ui.buttons.find((b) => b.n === 'Dash');
    assert.ok(dash, 'a Dash button is on screen');
    assert.ok(dash.cw >= 60 && dash.ch >= 60, `big enough for a thumb: ${dash.cw.toFixed(0)}x${dash.ch.toFixed(0)} CSS px`);
    assert.ok(dash.cx > options.viewport.width * 0.6 && dash.cy > options.viewport.height * 0.55, 'in the lower right');
    const pause = ui.buttons.find((b) => b.n === 'Pause');
    const apart = dash.x + dash.w <= pause.x || pause.x + pause.w <= dash.x || dash.y + dash.h <= pause.y || pause.y + pause.h <= dash.y;
    assert.ok(apart, 'clear of the pause button');
    assert.ok(dash.x + dash.w <= ui.width && dash.y + dash.h <= ui.height, 'on screen');

    const before = await readState(page);
    await page.touchscreen.tap(dash.cx, dash.cy);
    await waitDash(page);
    await waitDashOver(page);
    const after = await readState(page);
    assert.ok(Math.hypot(after.x - before.x, after.y - before.y) > 120, 'the dash moved the player');
    assert.equal((await readUi(page)).joystick, 0, 'the tap did not start the joystick');
    assert.deepEqual(problems, []);
    await context.close();
  });
}

// On a phone Enter is not available, so the run is started with a tap on Play.
async function tapStartIfNeeded(page) {
  const ui = await readUi(page);
  if (ui.screen === 'title') {
    const play = await findButton(page, 'PLAY');
    await page.touchscreen.tap(play.cx, play.cy);
    await waitScreen(page, 'playing');
  }
}

test('a tap that starts and ends between two frames still dashes', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await page.waitForTimeout(150);
  await tapStartIfNeeded(page);
  await page.waitForTimeout(300);
  const dash = await findButton(page, 'Dash');
  await page.evaluate(({ x, y }) => {
    const canvas = document.getElementById('canvas');
    const touch = new Touch({ identifier: 9, target: canvas, clientX: x, clientY: y });
    canvas.dispatchEvent(new TouchEvent('touchstart', { bubbles: true, cancelable: true, touches: [touch], targetTouches: [touch], changedTouches: [touch] }));
    canvas.dispatchEvent(new TouchEvent('touchend', { bubbles: true, cancelable: true, touches: [], targetTouches: [], changedTouches: [touch] }));
  }, { x: dash.cx, y: dash.cy });
  await waitDash(page);
  assert.deepEqual(problems, []);
  await context.close();
});

test('touching the dash button does not start the joystick, and dragging elsewhere still does', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await page.waitForTimeout(150);
  await tapStartIfNeeded(page);
  await resetCooldown(page);
  const dash = await findButton(page, 'Dash');
  // A finger down on the button, held, with a drag: no joystick.
  const session = await context.newCDPSession(page);
  await session.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: dash.cx, y: dash.cy, id: 1 }] });
  await page.waitForTimeout(200);
  await session.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: dash.cx - 40, y: dash.cy - 40, id: 1 }] });
  await page.waitForTimeout(200);
  assert.equal((await readUi(page)).joystick, 0, 'no joystick from the button');
  await session.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await page.waitForTimeout(200);

  await session.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: 120, y: 500, id: 2 }] });
  await page.waitForTimeout(100);
  await session.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: 180, y: 500, id: 2 }] });
  await page.waitForTimeout(250);
  assert.equal((await readUi(page)).joystick, 1, 'the joystick starts elsewhere');
  await session.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  assert.deepEqual(problems, []);
  await context.close();
});

test('the dash button stays inside the safe area, and clear of the boss bar and the hint', async () => {
  for (const [options, inset] of [[phone, 34], [phoneLandscape, 44]]) {
    const { context, page, problems } = await open(options);
    await waitScreen(page, 'title');
    await debug(page, CMD.setSafeArea, inset);
    await page.waitForTimeout(200);
    await tapStartIfNeeded(page);
    await page.waitForTimeout(200);
    const ui = await readUi(page);
    const dash = ui.buttons.find((b) => b.n === 'Dash');
    assert.ok(dash.x + dash.w <= ui.width - ui.safe[2] + 0.5, 'right edge inside the inset');
    assert.ok(dash.y + dash.h <= ui.height - ui.safe[3] + 0.5, 'bottom edge inside the inset');
    // With the boss on screen the bar must not run under the button.
    await debug(page, CMD.setTime, 599);
    await page.waitForTimeout(2500);
    const after = await readUi(page);
    const again = after.buttons.find((b) => b.n === 'Dash');
    assert.equal(again.x, dash.x, 'the button did not move');
    assert.deepEqual(problems, []);
    await context.close();
  }
});

// ---- Everything else ---------------------------------------------------------------------

test('a held dash key does not dash again by itself', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.down('Shift');
  await waitDash(page);
  await page.waitForTimeout(800);
  const state = await readState(page);
  assert.ok(state.dashCooldown > 2.5, 'one dash only');
  await page.keyboard.up('Shift');
  assert.deepEqual(problems, []);
  await context.close();
});
