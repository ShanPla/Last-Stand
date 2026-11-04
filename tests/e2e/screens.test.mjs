// Takes a picture of every screen at three window shapes, so the layout can be
// judged by eye. Also checks that nothing throws while each screen is drawn.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir } from 'node:fs/promises';
import {
  startServer, launchChrome, openGame, CMD, debug, readState, readUi, waitScreen, clickButton, tapButton, startGame,
} from './harness.mjs';

let web;
let browser;

before(async () => {
  await mkdir('tests/e2e/screenshots', { recursive: true });
  web = await startServer();
  browser = await launchChrome();
});

after(async () => {
  await browser?.close();
  web?.server.close();
});

const views = [
  { name: 'desktop', options: { viewport: { width: 1280, height: 720 } } },
  { name: 'portrait', options: { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true } },
  { name: 'landscape', options: { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true } },
];

for (const view of views) {
  test(`screens at ${view.name}`, async () => {
    const { context, page, problems } = await openGame(browser, web.url + '?debug', view.options);
    const shot = (name) => page.screenshot({ path: `tests/e2e/screenshots/screen-${view.name}-${name}.png` });

    await waitScreen(page, 'title');
    await page.waitForTimeout(1800);
    await shot('title');

    await debug(page, CMD.showScreen, 3);
    await waitScreen(page, 'howto');
    await page.waitForTimeout(400);
    await shot('howto');

    await debug(page, CMD.showScreen, 2);
    await waitScreen(page, 'settings');
    await page.waitForTimeout(400);
    await shot('settings');

    await debug(page, CMD.showScreen, 0);
    await startGame(page);
    await page.waitForTimeout(1200);
    await shot('play-start');

    // A full build with one gun dry and one reloading.
    await debug(page, CMD.giveBuild, 1);
    await debug(page, CMD.setKills, 187);
    await debug(page, CMD.setLevel, 9);
    await debug(page, CMD.setTime, 252);
    await page.waitForTimeout(500);
    await shot('hud-build');

    await debug(page, CMD.setHp, 18);
    await page.waitForTimeout(700);
    await shot('hud-lowhp');
    await debug(page, CMD.setHp, 100);

    await page.keyboard.press('Escape');
    await waitScreen(page, 'paused');
    await page.waitForTimeout(500);
    await shot('pause');
    await page.keyboard.press('Escape');
    await waitScreen(page, 'playing');

    await debug(page, CMD.levelUp);
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 5000 });
    await page.waitForTimeout(700);
    await shot('levelup');

    // Choose the first card and carry on.
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase !== 1, null, { timeout: 5000 });

    await debug(page, CMD.setTime, 599);
    await page.waitForTimeout(2500);
    await shot('hud-boss');

    await debug(page, CMD.fakeDamage, 42000);
    await debug(page, CMD.forceDeath);
    await waitScreen(page, 'over');
    await page.waitForTimeout(1300);
    await shot('gameover');

    await page.keyboard.press('Enter');
    await waitScreen(page, 'playing');
    await debug(page, CMD.fakeDamage, 98000);
    await debug(page, CMD.forceVictory);
    await waitScreen(page, 'over');
    await page.waitForTimeout(1300);
    await shot('victory');

    assert.deepEqual(problems, []);
    await context.close();
  });
}
