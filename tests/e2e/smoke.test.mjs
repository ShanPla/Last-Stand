import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir } from 'node:fs/promises';
import { startServer, launchChrome, openGame, readUi, waitScreen } from './harness.mjs';

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

test('page loads with no console errors, shows the title, and fits the desktop window', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport: { width: 1280, height: 720 } });
  await waitScreen(page, 'title');
  await page.waitForTimeout(500);
  const size = await page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    return { width: canvas.width, height: canvas.height };
  });
  assert.equal(size.width, 1280);
  assert.equal(size.height, 720);
  const ui = await readUi(page);
  assert.equal(ui.screen, 'title');
  assert.deepEqual(ui.buttons.map((b) => b.n), ['PLAY', 'Difficulty <', 'Difficulty', 'Difficulty >', 'Map <', 'Map', 'Map >', 'Settings', 'How to Play']);
  assert.deepEqual(problems, []);
  await context.close();
});

test('phone viewport fills the canvas at device pixel ratio', async () => {
  const { context, page, problems } = await openGame(browser, web.url, {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  await waitScreen(page, 'title');
  await page.waitForTimeout(500);
  const size = await page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    return { width: canvas.width, height: canvas.height };
  });
  assert.equal(size.width, 780);
  assert.equal(size.height, 1688);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the page blocks scrolling, zoom gestures and the context menu', async () => {
  const { context, page, problems } = await openGame(browser, web.url, {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  const result = await page.evaluate(() => {
    const canvasStyle = getComputedStyle(document.getElementById('canvas'));
    const bodyStyle = getComputedStyle(document.body);
    const contextMenu = new MouseEvent('contextmenu', { cancelable: true, bubbles: true });
    document.getElementById('canvas').dispatchEvent(contextMenu);
    const space = new KeyboardEvent('keydown', { key: ' ', cancelable: true, bubbles: true });
    window.dispatchEvent(space);
    const arrow = new KeyboardEvent('keydown', { key: 'ArrowDown', cancelable: true, bubbles: true });
    window.dispatchEvent(arrow);
    const pinch = new Event('gesturestart', { cancelable: true, bubbles: true });
    document.dispatchEvent(pinch);
    return {
      canvasTouchAction: canvasStyle.touchAction,
      bodyOverflow: bodyStyle.overflow,
      menuBlocked: contextMenu.defaultPrevented,
      spaceBlocked: space.defaultPrevented,
      arrowBlocked: arrow.defaultPrevented,
      pinchBlocked: pinch.defaultPrevented,
      viewport: document.querySelector('meta[name=viewport]').content,
    };
  });
  assert.equal(result.canvasTouchAction, 'none');
  assert.equal(result.bodyOverflow, 'hidden');
  assert.ok(result.menuBlocked && result.spaceBlocked && result.arrowBlocked && result.pinchBlocked);
  assert.match(result.viewport, /user-scalable=no/);
  assert.match(result.viewport, /viewport-fit=cover/);
  assert.deepEqual(problems, []);
  await context.close();
});

test('shows a clear message when WebGL2 is missing', async () => {
  const context = await browser.newContext();
  const page = await context.newPage();
  await page.addInitScript(() => { delete window.WebGL2RenderingContext; });
  await page.goto(web.url);
  await page.waitForSelector('#webgl-error:not([hidden])');
  await context.close();
});
