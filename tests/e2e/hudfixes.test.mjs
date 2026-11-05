// Checks of things a player reported: the XP bar must show progress on every screen shape, the
// best run must be visible on the title screen, and the touch joystick must stay in one place.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, startGame, waitScreen, debug, CMD, readUi, readState, stickHome, findButton } from './harness.mjs';

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
const shapes = [
  phone,
  { viewport: { width: 393, height: 852 }, deviceScaleFactor: 3, hasTouch: true, isMobile: true },
  { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true },
  { viewport: { width: 1280, height: 720 } },
  { viewport: { width: 1920, height: 1080 } },
  { viewport: { width: 1024, height: 768 } },
  { viewport: { width: 768, height: 1024 } },
  { viewport: { width: 360, height: 640 }, deviceScaleFactor: 3, hasTouch: true, isMobile: true },
];
const label = (o) => `${o.viewport.width}x${o.viewport.height}@${o.deviceScaleFactor ?? 1}`;
const overlaps = (a, b) => a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
const extent = (ui, name) => ui.extents.find((e) => e.n === name);

// Measures how much of the top of the screen is in the XP bar's color.
async function xpBar(page, options) {
  const ratio = options.deviceScaleFactor ?? 1;
  const shot = await page.screenshot({ clip: { x: 0, y: 0, width: options.viewport.width, height: 40 } });
  return page.evaluate(async (base64) => {
    const blob = await (await fetch(`data:image/png;base64,${base64}`)).blob();
    const image = await createImageBitmap(blob);
    const context = new OffscreenCanvas(image.width, image.height).getContext('2d');
    context.drawImage(image, 0, 0);
    const data = context.getImageData(0, 0, image.width, image.height).data;
    let rows = 0;
    let widest = 0;
    for (let y = 0; y < image.height; y++) {
      let run = 0;
      for (let x = 0; x < image.width; x++) {
        const i = (y * image.width + x) * 4;
        if (Math.abs(data[i] - 96) < 14 && Math.abs(data[i + 1] - 204) < 14 && Math.abs(data[i + 2] - 176) < 14) run++;
      }
      if (run > image.width * 0.05) {
        rows++;
        widest = Math.max(widest, run);
      }
    }
    return { rows, widest, width: image.width };
  }, shot.toString('base64')).then((r) => ({ ...r, ratio }));
}

for (const options of shapes) {
  test(`the XP bar shows how full it is at ${label(options)}`, async () => {
    const { context, page, problems } = await openGame(browser, `${web.url}?debug`, options);
    await startGame(page);
    await debug(page, CMD.holdSim, 1);
    // The state doesn't report XP, so these set how far the bar is from full: a level needs 5.
    await debug(page, CMD.grantXp, 4);
    await page.waitForTimeout(200);
    const low = await xpBar(page, options);
    await debug(page, CMD.grantXp, 1);
    await page.waitForTimeout(200);
    const high = await xpBar(page, options);
    assert.ok(low.rows / low.ratio >= 2, `${label(options)}: the fill is only ${low.rows} device rows tall`);
    assert.ok(high.widest > low.widest * 2, `${label(options)}: more XP must fill more, ${low.widest} then ${high.widest}`);
    assert.ok(high.widest > high.width * 0.6, `${label(options)}: 4 of 5 should fill most of the width, got ${high.widest} of ${high.width}`);
    assert.deepEqual(problems, []);
    await context.close();
  });
}

for (const options of shapes) {
  test(`the best run is on the title screen at ${label(options)}`, async () => {
    const { context, page, problems } = await openGame(browser, web.url, options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(400);
    const ui = await readUi(page);
    const best = extent(ui, 'best run');
    assert.ok(best, `${label(options)}: the best run is not shown`);
    const ratio = options.deviceScaleFactor ?? 1;
    assert.ok(best.x >= 0 && best.y >= 0 && best.x + best.w <= options.viewport.width * ratio + 1 && best.y + best.h <= options.viewport.height * ratio + 1,
      `${label(options)}: the best run leaves the screen: ${JSON.stringify(best)}`);
    for (const button of ui.buttons) {
      assert.ok(!overlaps(best, button), `${label(options)}: the best run covers ${button.n}`);
    }
    assert.deepEqual(problems, []);
    await context.close();
  });
}

for (const options of [phone, shapes[2]]) {
  test(`the touch joystick stays in its place at ${label(options)}`, async () => {
    const { context, page, problems } = await openGame(browser, `${web.url}?debug`, options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(150);
    const play = await findButton(page, 'PLAY');
    await page.touchscreen.tap(play.cx, play.cy);
    await waitScreen(page, 'playing');
    await debug(page, CMD.setHp, 100);

    // It is on screen before any thumb lands, clear of the buttons and the hint.
    const home = await stickHome(page);
    let ui = await readUi(page);
    const box = extent(ui, 'joystick');
    const ratio = options.deviceScaleFactor;
    assert.ok(box.x >= 0 && box.y >= 0 && box.y + box.h <= options.viewport.height * ratio + 1, 'the stick is on screen');
    for (const name of ['Dash', 'Pause']) {
      assert.ok(!overlaps(box, ui.buttons.find((b) => b.n === name)), `the stick covers ${name}`);
    }
    if (extent(ui, 'controls hint')) assert.ok(!overlaps(box, extent(ui, 'controls hint')), 'the stick covers the hint');
    await page.screenshot({ path: `tests/e2e/screenshots/stick-${label(options)}.png` });

    const session = await context.newCDPSession(page);
    const touch = (type, points) => session.send('Input.dispatchTouchEvent', { type, touchPoints: points });

    // A touch far from the stick does nothing.
    const far = { x: options.viewport.width * 0.7, y: options.viewport.height * 0.35, id: 1 };
    await touch('touchStart', [far]);
    await touch('touchMove', [{ ...far, x: far.x - 60 }]);
    await page.waitForTimeout(200);
    assert.equal((await readUi(page)).joystick, 0, 'a touch away from the stick must not steer');
    await touch('touchEnd', []);
    await page.waitForTimeout(100);

    // A thumb on the stick steers, and pushing far past its edge doesn't move it.
    const start = await readState(page);
    await touch('touchStart', [{ x: home.x, y: home.y, id: 2 }]);
    await touch('touchMove', [{ x: home.x + home.radius * 3, y: home.y, id: 2 }]);
    await page.waitForTimeout(500);
    ui = await readUi(page);
    assert.equal(ui.joystick, 1);
    assert.ok((await readState(page)).x - start.x > 30, 'the player runs the way the thumb pushed');
    const during = await stickHome(page);
    assert.ok(Math.abs(during.x - home.x) < 1 && Math.abs(during.y - home.y) < 1, `the stick moved from ${JSON.stringify(home)} to ${JSON.stringify(during)}`);
    await page.screenshot({ path: `tests/e2e/screenshots/stick-${label(options)}-pushed.png` });
    await touch('touchEnd', []);
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('a keyboard or mouse player never sees the stick', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  await startGame(page);
  await page.waitForTimeout(300);
  assert.equal(extent(await readUi(page), 'joystick'), undefined);
  assert.deepEqual(problems, []);
  await context.close();
});
