// The map selector on the title screen.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, readUi, waitScreen, clickButton, tapButton, findButton } from './harness.mjs';

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

const waitForMap = (page, map) => page.waitForFunction((want) => {
  try { return JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).map === want; } catch { return false; }
}, map, { timeout: 8000 });

test('the map starts on the city, a click changes it, Play is still one press, and it persists', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport: { width: 1280, height: 720 } });
  await waitScreen(page, 'title');
  await page.waitForTimeout(400);
  assert.equal((await readUi(page)).map, 0);
  await clickButton(page, 'Map >');
  await waitForMap(page, 1);
  await page.reload();
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).map, 1, 'the choice is saved');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a tap on the map selector works on a phone and fits the screen', async () => {
  const { context, page, problems } = await openGame(browser, web.url, {
    viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true,
  });
  await waitScreen(page, 'title');
  await page.waitForTimeout(400);
  await tapButton(page, 'Map >');
  await waitForMap(page, 1);
  const map = await findButton(page, 'Map');
  assert.ok(map.y >= 0 && map.y + map.h <= (await readUi(page)).height, 'the row is on screen');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a corrupt saved map is safe', async () => {
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const page = await context.newPage();
  await page.addInitScript(() => { localStorage.setItem('laststand.save', 'version=1\nmap=9000\n'); });
  const problems = [];
  page.on('pageerror', (error) => problems.push(String(error)));
  await page.goto(web.url);
  await waitScreen(page, 'title');
  const map = await findButton(page, 'Map');
  assert.ok(map, 'the selector still draws, with an out-of-range saved map');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});
