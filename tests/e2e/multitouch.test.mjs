// Two fingers at once on a phone, tested with real multi-touch events. The normal way to
// play is two thumbs: one steers, the other presses Dash or Pause. The steering thumb must
// stay the steering thumb, whatever the other finger does and whichever has the lower id.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, findButton,
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

const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };

// Starts a run with a tap on Play, makes the player hard to kill, and keeps the dash from
// being ready, so a finger resting on the Dash button never dashes and moves the player.
async function startRun() {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', phone);
  await waitScreen(page, 'title');
  await page.waitForTimeout(150);
  const play = await findButton(page, 'PLAY');
  await page.touchscreen.tap(play.cx, play.cy);
  await waitScreen(page, 'playing');
  await debug(page, CMD.setHp, 100);
  await debug(page, CMD.setDashCooldown, 90);
  const dash = await findButton(page, 'Dash');
  const pause = await findButton(page, 'Pause');
  const session = await context.newCDPSession(page);
  return { context, page, problems, dash, pause, session };
}

// A finger is [id, x, y]. For touchStart and touchMove CDP wants every finger that is down.
// For touchEnd it wants only the fingers that lift, and the rest stay down.
const send = (session, type, fingers) =>
  session.send('Input.dispatchTouchEvent', { type, touchPoints: fingers.map(([id, x, y]) => ({ x, y, id })) });

const joystick = async (page) => (await readUi(page)).joystick;

// How far the player moved sideways over a short time, without touching anything.
async function driftX(page, ms = 500) {
  const before = await readState(page);
  await page.waitForTimeout(ms);
  const after = await readState(page);
  return after.x - before.x;
}

test('a thumb resting on Dash does not stop the other thumb steering', async () => {
  const { context, page, problems, dash, session } = await startRun();
  const dashFinger = [1, dash.cx, dash.cy];
  await send(session, 'touchStart', [dashFinger]);
  await page.waitForTimeout(150);
  const start = await readState(page);
  // The steering thumb lands after the Dash thumb, and drags left.
  await send(session, 'touchStart', [dashFinger, [2, 220, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [dashFinger, [2, 150, 500]]);
  await page.waitForTimeout(500);
  assert.equal(await joystick(page), 1, 'the steering thumb drives the joystick');
  const moved = (await readState(page)).x - start.x;
  assert.ok(moved < -40, `the player ran left, moved ${moved.toFixed(1)}`);
  // Lifting the Dash thumb while steering must not break anything either.
  await send(session, 'touchEnd', [dashFinger]);
  await page.waitForTimeout(100);
  assert.equal(await joystick(page), 1, 'still steering after the Dash thumb lifted');
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});

test('lifting the steering thumb while a thumb rests on Dash stops the player, and does not steer toward Dash', async () => {
  const { context, page, problems, dash, session } = await startRun();
  await send(session, 'touchStart', [[2, 220, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [[2, 150, 500]]);
  await page.waitForTimeout(300);
  assert.equal(await joystick(page), 1, 'steering to begin with');
  // The Dash thumb lands and stays, then the steering thumb lifts.
  const dashFinger = [1, dash.cx, dash.cy];
  await send(session, 'touchStart', [[2, 150, 500], dashFinger]);
  await page.waitForTimeout(150);
  await send(session, 'touchEnd', [[2, 150, 500]]);
  await page.waitForTimeout(250);
  assert.equal(await joystick(page), 0, 'no steering finger is down, so no joystick');
  const drift = await driftX(page, 600);
  assert.ok(Math.abs(drift) < 10, `the player stood still, drifted ${drift.toFixed(1)}`);
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a Dash finger with the lower touch id landing mid-drag does not take over the joystick', async () => {
  const { context, page, problems, dash, session } = await startRun();
  // The steering finger has the higher id, so a list sorted by id puts Dash first.
  const steer = (x) => [7, x, 500];
  await send(session, 'touchStart', [steer(220)]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [steer(150)]);
  await page.waitForTimeout(300);
  assert.equal(await joystick(page), 1);
  const dashFinger = [1, dash.cx, dash.cy];
  await send(session, 'touchStart', [dashFinger, steer(150)]);
  const before = await readState(page);
  await page.waitForTimeout(600);
  const after = await readState(page);
  assert.equal(await joystick(page), 1, 'still steering');
  assert.ok(after.x - before.x < -20, `kept running left, moved ${(after.x - before.x).toFixed(1)}`);
  // The player went left, not toward the Dash button at the lower right.
  assert.ok(after.y - before.y < 30, `and did not run down toward Dash, moved ${(after.y - before.y).toFixed(1)}`);
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});

test('after the steering thumb lifts, a new finger away from the buttons steers again', async () => {
  const { context, page, problems, dash, session } = await startRun();
  const dashFinger = [1, dash.cx, dash.cy];
  await send(session, 'touchStart', [dashFinger]);
  await page.waitForTimeout(100);
  await send(session, 'touchStart', [dashFinger, [2, 220, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [dashFinger, [2, 150, 500]]);
  await page.waitForTimeout(250);
  assert.equal(await joystick(page), 1);
  // Steering thumb lifts; the Dash thumb stays.
  await send(session, 'touchEnd', [[2, 150, 500]]);
  await page.waitForTimeout(250);
  assert.equal(await joystick(page), 0, 'the joystick ended with its finger');
  // A fresh finger lands in the open and steers right.
  const start = await readState(page);
  await send(session, 'touchStart', [dashFinger, [3, 150, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [dashFinger, [3, 220, 500]]);
  await page.waitForTimeout(500);
  assert.equal(await joystick(page), 1, 'a new finger steers');
  assert.ok((await readState(page)).x - start.x > 30, 'and the player ran right');
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the Pause button still works while a thumb is steering', async () => {
  const { context, page, problems, pause, session } = await startRun();
  await send(session, 'touchStart', [[2, 220, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [[2, 150, 500]]);
  await page.waitForTimeout(250);
  assert.equal(await joystick(page), 1);
  const pauseFinger = [1, pause.cx, pause.cy];
  await send(session, 'touchStart', [[2, 150, 500], pauseFinger]);
  await page.waitForTimeout(50);
  await send(session, 'touchEnd', [pauseFinger]);
  await waitScreen(page, 'paused');
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the Dash button still dashes with a tap while a thumb is steering', async () => {
  const { context, page, problems, dash, session } = await startRun();
  await debug(page, CMD.setDashCooldown, 0);
  await send(session, 'touchStart', [[2, 220, 500]]);
  await page.waitForTimeout(100);
  await send(session, 'touchMove', [[2, 150, 500]]);
  await page.waitForTimeout(250);
  const dashFinger = [1, dash.cx, dash.cy];
  await send(session, 'touchStart', [[2, 150, 500], dashFinger]);
  await page.waitForTimeout(50);
  await send(session, 'touchEnd', [dashFinger]);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).dashCooldown > 0, null, { timeout: 5000 });
  assert.equal(await joystick(page), 1, 'steering carried on through the tap');
  await send(session, 'touchEnd', []);
  assert.deepEqual(problems, []);
  await context.close();
});
