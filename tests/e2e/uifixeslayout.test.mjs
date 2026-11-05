// Layout checks for How to Play, Pause and Level-up at many window shapes, and the font
// memory check. Nothing may overlap or run off the screen. Screenshots are left to look at.
// Kept apart from uifixes.test.mjs so that each file runs well inside the test time limit.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir } from 'node:fs/promises';
import {
  startServer, launchChrome, openGame, CMD, debug, readUi, waitScreen,
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

const desktop = { viewport: { width: 1280, height: 720 } };
const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const open = (options = desktop) => openGame(browser, web.url + '?debug', options);

const rectOf = (ui, name) => ui.buttons.find((b) => b.n === name);
const extentOf = (ui, name) => ui.extents.find((b) => b.n === name);
const startsWith = (ui, prefix) => ui.buttons.find((b) => b.n.startsWith(prefix));
const insideBox = (inner, outer, slack = 1) => inner.x >= outer.x - slack && inner.y >= outer.y - slack
  && inner.x + inner.w <= outer.x + outer.w + slack && inner.y + inner.h <= outer.y + outer.h + slack;
const overlaps = (a, b) => a.x < b.x + b.w - 1 && a.x + a.w > b.x + 1 && a.y < b.y + b.h - 1 && a.y + a.h > b.y + 1;
const label = (v) => `${v.viewport.width}x${v.viewport.height}`;

const shapes = [
  { viewport: { width: 1024, height: 768 } },   // 4:3 landscape
  { viewport: { width: 768, height: 1024 } },   // 4:3 portrait
  { viewport: { width: 1280, height: 720 } },
  { viewport: { width: 1920, height: 1080 } },
  { viewport: { width: 600, height: 600 } },    // square
  { ...phone },
  { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true },
  { viewport: { width: 320, height: 568 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true },
];

for (const shape of shapes) {
  test(`how to play, pause and level-up lay out cleanly at ${label(shape)}`, async () => {
    const { context, page, problems } = await open(shape);
    const size = label(shape);
    const shot = (name) => page.screenshot({ path: `tests/e2e/screenshots/uifix-${size}-${name}.png` });
    await waitScreen(page, 'title');

    // How to Play: each tip's text stays inside its panel, panels never overlap, and
    // nothing reaches the Back button or leaves the screen.
    await debug(page, CMD.showScreen, 3);
    await waitScreen(page, 'howto');
    await page.waitForTimeout(300);
    let ui = await readUi(page);
    const screen = { x: 0, y: 0, w: ui.width, h: ui.height };
    const back = rectOf(ui, 'Back');
    for (let i = 1; i <= 8; i++) {
      const panel = extentOf(ui, `Tip ${i}`);
      const text = extentOf(ui, `Tip ${i} text`);
      assert.ok(panel && text, `tip ${i} is not on screen`);
      assert.ok(insideBox(text, panel), `tip ${i} text runs out of its panel: ${JSON.stringify({ text, panel })}`);
      assert.ok(insideBox(panel, screen), `tip ${i} panel leaves the screen`);
      assert.ok(!overlaps(panel, back), `tip ${i} covers Back`);
      for (let j = i + 1; j <= 8; j++) assert.ok(!overlaps(panel, extentOf(ui, `Tip ${j}`)), `tips ${i} and ${j} overlap`);
    }
    await shot('howto');

    // Pause with a full build: the build list is inside its panel, clear of the buttons.
    await debug(page, CMD.showScreen, 0);
    // On a busy machine the first Enter can arrive before the title is ready, so it is tried again.
    await waitScreen(page, 'title');
    for (let attempt = 0; attempt < 4 && (await readUi(page)).screen !== 'playing'; attempt++) {
      await page.waitForTimeout(250);
      await page.keyboard.press('Enter');
      await page.waitForTimeout(400);
    }
    await waitScreen(page, 'playing');
    await debug(page, CMD.holdSim, 1);
    await debug(page, CMD.giveBuild, 0);
    await page.waitForTimeout(200);
    await debug(page, CMD.showScreen, 1);
    await waitScreen(page, 'paused');
    await page.waitForTimeout(300);
    ui = await readUi(page);
    const build = extentOf(ui, 'Build');
    const content = extentOf(ui, 'Build content');
    assert.ok(build && content, 'the build panel is not on screen');
    assert.ok(content.y + content.h <= build.y + build.h + 1, `the build list runs past its panel: ${JSON.stringify({ content, build })}`);
    assert.ok(insideBox(build, screen), 'the build panel leaves the screen');
    for (const name of ['Resume', 'Settings', 'Restart', 'Quit to Title']) {
      const button = rectOf(ui, name);
      assert.ok(button, `no ${name} button`);
      assert.ok(!overlaps(button, build), `${name} is under the build panel`);
      assert.ok(insideBox(button, screen), `${name} leaves the screen`);
    }
    await shot('pause');
    await page.keyboard.press('Escape');
    await waitScreen(page, 'playing');

    // Level-up with a full build: cards, reroll and the build strip all fit on screen.
    await debug(page, CMD.holdSim, 0);
    await debug(page, CMD.levelUp);
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 5000 });
    await page.waitForTimeout(500);
    ui = await readUi(page);
    const strip = extentOf(ui, 'Build strip');
    const reroll = startsWith(ui, 'Reroll') ?? startsWith(ui, 'No rerolls');
    assert.ok(strip && reroll, 'no build strip or reroll button');
    assert.ok(insideBox(strip, screen), `the build strip leaves the screen: ${JSON.stringify(strip)} in ${ui.width}x${ui.height}`);
    assert.ok(insideBox(reroll, screen), 'the reroll button leaves the screen');
    assert.ok(!overlaps(strip, reroll), 'the build strip is under the reroll button');
    for (let i = 1; i <= 3; i++) {
      const card = rectOf(ui, `Card ${i}`);
      assert.ok(insideBox(card, screen), `card ${i} leaves the screen`);
      assert.ok(!overlaps(card, strip) && !overlaps(card, reroll), `card ${i} overlaps the strip or reroll`);
    }
    await shot('levelup');
    assert.deepEqual(problems, []);
    await context.close();
  });
}

// ---- Font memory --------------------------------------------------------------------------

test('big font sizes are only baked when a window needs them', async () => {
  const sizes = {};
  for (const [name, options] of [['phone-portrait', phone], ['desktop-720p', desktop], ['full-hd', { viewport: { width: 1920, height: 1080 } }],
    ['small-window', { viewport: { width: 600, height: 400 } }]]) {
    const { context, page, problems } = await open(options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(800);
    sizes[name] = (await readUi(page)).fontBytes;
    assert.deepEqual(problems, []);
    await context.close();
  }
  console.log('font atlas bytes:', JSON.stringify(sizes));
  // With all eleven sizes baked up front the atlases came to about 15 MB on every device.
  for (const [name, bytes] of Object.entries(sizes)) assert.ok(bytes > 0 && bytes < 8 * 1024 * 1024, `${name}: ${bytes}`);
  assert.ok(sizes['small-window'] < sizes['full-hd'], 'a small window should hold less than a big one');
});
