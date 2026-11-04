import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, countColorNearCenter } from './harness.mjs';

let web;
let browser;

const readState = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));

before(async () => {
  web = await startServer();
  browser = await launchChrome();
});

after(async () => {
  await browser?.close();
  web?.server.close();
});

test('keyboard moves the player, and enemies and shots appear', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport: { width: 1280, height: 720 } });
  await page.keyboard.down('d');
  await page.waitForTimeout(800);
  await page.keyboard.up('d');
  const state = await readState(page);
  assert.ok(state.x > 50, `player x should grow, got ${state.x}`);
  await page.waitForTimeout(2500);
  const later = await readState(page);
  assert.ok(later.enemies > 0, 'enemies should spawn');
  await page.screenshot({ path: 'tests/e2e/screenshots/m1-play-desktop.png' });
  // The player's outline must really be on screen, in its theme color.
  assert.ok(await countColorNearCenter(page, [95, 244, 255]) > 20, 'player should be visible at the center');
  assert.deepEqual(problems, []);
  await context.close();
});

test('level-up opens, ignores instant input, then accepts a choice', async () => {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', { viewport: { width: 1280, height: 720 } });
  // Leaves 1 XP to the next level, then spawns enemies around the player until a gem lands.
  await page.evaluate(() => { Module._ls_debug_command(4, 1); });
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 40000 });
  await page.keyboard.press('Enter');
  assert.equal((await readState(page)).phase, 1, 'a key press inside the input delay must not choose');
  await page.waitForTimeout(500);
  await page.screenshot({ path: 'tests/e2e/screenshots/m1-levelup-desktop.png' });
  await page.keyboard.press('Enter');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 0, null, { timeout: 3000 });
  assert.deepEqual(problems, []);
  await context.close();
});

test('touch drag moves the player, and a tap picks a card on a phone', async () => {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  const client = await context.newCDPSession(page);
  const touch = (type, points) => client.send('Input.dispatchTouchEvent', { type, touchPoints: points });
  await touch('touchStart', [{ x: 195, y: 500 }]);
  await touch('touchMove', [{ x: 280, y: 500 }]);
  await page.waitForTimeout(800);
  await touch('touchEnd', []);
  const state = await readState(page);
  assert.ok(state.x > 50, `touch should move the player, got x=${state.x}`);
  await page.screenshot({ path: 'tests/e2e/screenshots/m1-play-phone.png' });

  await page.evaluate(() => { Module._ls_debug_command(4, 1); });
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 40000 });
  await page.waitForTimeout(500);
  await page.screenshot({ path: 'tests/e2e/screenshots/m1-levelup-phone.png' });
  await touch('touchStart', [{ x: 195, y: 422 }]);
  await touch('touchEnd', []);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 0, null, { timeout: 3000 });
  assert.deepEqual(problems, []);
  await context.close();
});

for (const scale of [1, 1.25, 2]) {
  for (const holdMs of [0, 80]) {
    test(`mouse click picks a card at pixel ratio ${scale}, button held ${holdMs} ms`, async () => {
      const { context, page, problems } = await openGame(browser, web.url + '?debug', {
        viewport: { width: 1280, height: 720 }, deviceScaleFactor: scale,
      });
      await page.evaluate(() => { Module._ls_debug_command(4, 1); });
      await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 40000 });
      await page.waitForTimeout(500);
      await page.mouse.move(1050, 380);
      await page.waitForTimeout(100);
      await page.mouse.down();
      await page.waitForTimeout(holdMs);
      await page.mouse.up();
      await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 0, null, { timeout: 3000 });
      assert.deepEqual(problems, []);
      await context.close();
    });
  }
}
