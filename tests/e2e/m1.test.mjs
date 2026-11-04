// The first playable milestone, rechecked now that a title screen comes first:
// movement, level-up input, and clicks at several pixel ratios.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame, countColorNearCenter, readState, readUi, debug, CMD, startGame, waitScreen, findButton, tapButton,
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

const phase = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase);
const waitPhase = (page, wanted, timeout = 5000) =>
  page.waitForFunction((p) => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === p, wanted, { timeout });

test('keyboard moves the player, and enemies and shots appear', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport: { width: 1280, height: 720 } });
  await startGame(page);
  await page.keyboard.down('d');
  await page.waitForTimeout(800);
  await page.keyboard.up('d');
  const state = await readState(page);
  assert.ok(state.x > 50, `player x should grow, got ${state.x}`);
  await page.waitForTimeout(2500);
  const later = await readState(page);
  assert.ok(later.enemies > 0, 'enemies should spawn');
  // The player's outline must really be on screen, in its theme color.
  assert.ok(await countColorNearCenter(page, [95, 244, 255]) > 20, 'player should be visible at the center');
  assert.deepEqual(problems, []);
  await context.close();
});

test('level-up opens, ignores instant input, then accepts a choice', async () => {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', { viewport: { width: 1280, height: 720 } });
  await startGame(page);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.keyboard.press('Enter');
  assert.equal(await phase(page), 1, 'a key press inside the input delay must not choose');
  await page.waitForTimeout(500);
  await page.keyboard.press('Enter');
  await waitPhase(page, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('touch drag moves the player, and a tap picks a card on a phone', async () => {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  await waitScreen(page, 'title');
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await page.waitForTimeout(600);
  const client = await context.newCDPSession(page);
  const touch = (type, points) => client.send('Input.dispatchTouchEvent', { type, touchPoints: points });
  await touch('touchStart', [{ x: 195, y: 500 }]);
  await touch('touchMove', [{ x: 280, y: 500 }]);
  await page.waitForTimeout(800);
  await touch('touchEnd', []);
  const state = await readState(page);
  assert.ok(state.x > 50, `touch should move the player, got x=${state.x}`);

  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.waitForTimeout(500);
  await tapButton(page, 'Card 2');
  await waitPhase(page, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

for (const scale of [1, 1.25, 2]) {
  for (const holdMs of [0, 80]) {
    test(`mouse click picks a card at pixel ratio ${scale}, button held ${holdMs} ms`, async () => {
      const { context, page, problems } = await openGame(browser, web.url + '?debug', {
        viewport: { width: 1280, height: 720 }, deviceScaleFactor: scale,
      });
      await startGame(page);
      await debug(page, CMD.levelUp);
      await waitPhase(page, 1);
      await page.waitForTimeout(500);
      const card = await findButton(page, 'Card 3');
      await page.mouse.move(card.cx, card.cy);
      await page.waitForTimeout(100);
      await page.mouse.down();
      await page.waitForTimeout(holdMs);
      await page.mouse.up();
      await waitPhase(page, 0);
      assert.equal((await readUi(page)).screen, 'playing');
      assert.deepEqual(problems, []);
      await context.close();
    });
  }
}
