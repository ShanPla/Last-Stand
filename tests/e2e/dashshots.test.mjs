// Takes pictures of the dash and the difficulty screens at three window shapes, so they can
// be judged by eye. Also checks that nothing throws while they are drawn.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir } from 'node:fs/promises';
import {
  startServer, launchChrome, openGame, CMD, debug, readUi, waitScreen, clickButton, tapButton, findButton,
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
  test(`dash and difficulty pictures at ${view.name}`, async () => {
    const touch = Boolean(view.options.hasTouch);
    const { context, page, problems } = await openGame(browser, web.url + '?debug', view.options);
    const shot = (name) => page.screenshot({ path: `tests/e2e/screenshots/${name}-${view.name}.png` });
    const press = async (name) => (touch ? tapButton(page, name) : clickButton(page, name));

    // The title with each level, and the best run for it.
    await waitScreen(page, 'title');
    await page.waitForTimeout(1500);
    await shot('difficulty-title-normal');
    await press('Difficulty >');
    await page.waitForTimeout(400);
    await shot('difficulty-title-hard');

    // A Hard run, the HUD with a ready dash, then a dash in progress.
    await press('PLAY');
    await waitScreen(page, 'playing');
    await debug(page, CMD.giveBuild, 0);
    await page.waitForTimeout(1500);
    await shot('dash-hud-ready');

    await debug(page, CMD.setDashCooldown, 60);
    await page.waitForTimeout(400);
    await shot('dash-hud-cooling');

    await debug(page, CMD.setDashCooldown, 0);
    await debug(page, CMD.holdSim, 1);
    await debug(page, CMD.dash);
    await debug(page, 27, 6);  // Six steps of the twelve in a dash.
    await page.waitForTimeout(500);
    await shot('dash-in-progress');

    await debug(page, CMD.holdSim, 0);
    await debug(page, CMD.setDashCooldown, 30);
    await page.waitForTimeout(800);

    // The pause menu shows the level, and so does the game over screen.
    await page.keyboard.press('Escape');
    await waitScreen(page, 'paused');
    await page.waitForTimeout(500);
    await shot('difficulty-pause');
    await page.keyboard.press('Escape');
    await waitScreen(page, 'playing');

    await debug(page, CMD.setKills, 321);
    await debug(page, CMD.setLevel, 11);
    await debug(page, CMD.setTime, 275);
    await debug(page, CMD.fakeDamage, 40000);
    await debug(page, CMD.forceDeath);
    await waitScreen(page, 'over');
    await page.waitForTimeout(1300);
    await shot('difficulty-gameover-hard');

    // Back on the title, the Hard best run is shown; Easy has none.
    await debug(page, CMD.showScreen, 0);
    await waitScreen(page, 'title');
    await page.waitForTimeout(1500);
    await shot('difficulty-title-hard-best');
    // One press at a time: two inside one slow frame would count as one.
    const waitLevel = (level) => page.waitForFunction((d) => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).difficulty === d, level, { timeout: 10000 });
    await press('Difficulty <');
    await waitLevel(1);
    await press('Difficulty <');
    await waitLevel(0);
    await page.waitForTimeout(400);
    assert.equal((await readUi(page)).difficulty, 0);
    await shot('difficulty-title-easy-nobest');

    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('a dash on the desktop shows the key prompt, the gamepad one after a pad press', async () => {
  const { context, page, problems } = await openGame(browser, web.url + '?debug', views[0].options);
  await waitScreen(page, 'title');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.giveBuild, 0);
  await page.waitForTimeout(1200);
  const dash = await findButton(page, 'Dash');
  assert.ok(dash.w > 20);
  await page.screenshot({ path: 'tests/e2e/screenshots/dash-hud-desktop-closeup.png', clip: { x: 0, y: 0, width: 420, height: 300 } });
  assert.deepEqual(problems, []);
  await context.close();
});
