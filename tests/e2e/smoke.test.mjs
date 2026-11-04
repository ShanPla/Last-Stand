import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir } from 'node:fs/promises';
import { startServer, launchChrome, openGame } from './harness.mjs';

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

test('page loads with no console errors and fits the desktop window', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport: { width: 1280, height: 720 } });
  await page.waitForTimeout(500);
  const size = await page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    return { width: canvas.width, height: canvas.height };
  });
  assert.equal(size.width, 1280);
  assert.equal(size.height, 720);
  await page.screenshot({ path: 'tests/e2e/screenshots/m0-desktop.png' });
  assert.deepEqual(problems, []);
  await context.close();
});

test('phone viewport fills the canvas at device pixel ratio', async () => {
  const { context, page, problems } = await openGame(browser, web.url, {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  await page.waitForTimeout(500);
  const size = await page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    return { width: canvas.width, height: canvas.height };
  });
  assert.equal(size.width, 780);
  assert.equal(size.height, 1688);
  await page.screenshot({ path: 'tests/e2e/screenshots/m0-phone.png' });
  assert.deepEqual(problems, []);
  await context.close();
});

test('shows a clear message when WebGL2 is missing', async () => {
  const context = await browser.newContext();
  const page = await context.newPage();
  await page.addInitScript(() => { delete window.WebGL2RenderingContext; });
  await page.goto(web.url);
  await page.waitForSelector('#webgl-error:not([hidden])');
  await page.screenshot({ path: 'tests/e2e/screenshots/m0-webgl-error.png' });
  await context.close();
});
