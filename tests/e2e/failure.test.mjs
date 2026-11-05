// What a player sees when something goes wrong: a script that does not load, a game
// file that does not download or does not compile, a crash, and a lost graphics context.
// Each must end in a clear message on screen, never in a page that says Loading forever
// or in a frozen canvas.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, waitScreen } from './harness.mjs';

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

const viewport = { width: 800, height: 600 };

// Opens the page without waiting for the game, because in these tests it never starts.
async function openBroken(setup) {
  const context = await browser.newContext({ viewport });
  const page = await context.newPage();
  await setup(page);
  await page.goto(web.url);
  return { context, page };
}

// Waits until the loading screen has faded out and been hidden, which is where a crash in
// the middle of play starts from. The page hides it 250 ms after the fade begins. A crash
// before that would leave the hidden flag untouched, so a message that forgot to show the
// screen again would still pass.
async function waitForLoadingScreenGone(page) {
  await page.waitForFunction(() => document.getElementById('loading-screen').hidden === true, null, { timeout: 15000 });
}

// Waits for the fatal message and returns what the player can see.
async function waitForMessage(page, pattern) {
  await page.waitForFunction((source) => {
    const screen = document.getElementById('loading-screen');
    // The screen fades in over a fraction of a second, so wait until it is fully shown.
    return screen.classList.contains('is-error') && new RegExp(source).test(document.getElementById('loading-label').textContent) &&
      !screen.hidden && getComputedStyle(screen).opacity === '1';
  }, pattern.source, { timeout: 15000 });
  return page.evaluate(() => {
    const screen = document.getElementById('loading-screen');
    const label = document.getElementById('loading-label');
    const rect = label.getBoundingClientRect();
    return {
      text: label.textContent,
      screenShown: !screen.hidden && !screen.classList.contains('is-done') && getComputedStyle(screen).opacity === '1' && getComputedStyle(screen).pointerEvents !== 'none',
      labelVisible: getComputedStyle(label).display !== 'none' && rect.width > 0 && rect.height > 0,
      barHidden: getComputedStyle(document.querySelector('.loading-track')).display === 'none',
      role: screen.getAttribute('role'),
    };
  });
}

test('a game script that fails to load shows a message instead of Loading forever', async () => {
  const { context, page } = await openBroken((p) => p.route('**/index.js', (route) => route.abort('failed')));
  const shown = await waitForMessage(page, /could not be loaded/);
  assert.ok(shown.screenShown, 'the message must be on screen');
  assert.ok(shown.labelVisible);
  assert.ok(shown.barHidden, 'a bar that will never fill must not be shown');
  assert.equal(shown.role, 'alert');
  assert.match(shown.text, /reload/i);
  assert.doesNotMatch(shown.text, /^Loading/);
  await context.close();
});

test('a game file that fails to download says the download failed', async () => {
  const { context, page } = await openBroken((p) => p.route('**/index.wasm', (route) => route.fulfill({ status: 404, body: 'gone' })));
  const shown = await waitForMessage(page, /Download failed/);
  assert.ok(shown.screenShown);
  assert.ok(shown.labelVisible);
  assert.match(shown.text, /connection/i);
  assert.match(shown.text, /reload/i);
  await context.close();
});

test('a game file that downloads whole but will not compile does not claim a failed download', async () => {
  const { context, page } = await openBroken((p) => p.route('**/index.wasm', (route) => route.fulfill({
    status: 200,
    contentType: 'application/wasm',
    body: Buffer.from('this is not a webassembly module'),
  })));
  const shown = await waitForMessage(page, /could not start/);
  assert.ok(shown.screenShown);
  assert.ok(shown.labelVisible);
  assert.doesNotMatch(shown.text, /download/i, 'the file arrived, so the message must not blame the download');
  assert.match(shown.text, /reload/i);
  await context.close();
});

test('a crash while playing covers the frozen canvas with a message', async () => {
  const { context, page } = await openGame(browser, `${web.url}?debug`, { viewport });
  await waitScreen(page, 'title');
  await waitForLoadingScreenGone(page);
  // Emscripten calls Module.onAbort from its abort(), for example when memory cannot grow.
  assert.equal(await page.evaluate(() => typeof Module.onAbort), 'function');
  await page.evaluate(() => Module.onAbort('test abort'));
  const shown = await waitForMessage(page, /stopped unexpectedly/);
  assert.ok(shown.screenShown, 'the message must cover the game after the loading screen has faded');
  assert.ok(shown.labelVisible);
  assert.match(shown.text, /reload/i);
  await context.close();
});

test('a WebAssembly trap in the frame loop also shows the message', async () => {
  const { context, page } = await openGame(browser, `${web.url}?debug`, { viewport });
  await waitScreen(page, 'title');
  await waitForLoadingScreenGone(page);
  // The smallest module that traps: one function that runs "unreachable". Thrown from a
  // timer, so it reaches the page as an uncaught error, as a trap in the game's own loop does.
  await page.evaluate(() => {
    const bytes = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 4, 1, 96, 0, 0, 3, 2, 1, 0, 7, 5, 1, 1, 116, 0, 0, 10, 5, 1, 3, 0, 0, 11]);
    const trap = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.t;
    setTimeout(trap, 0);
  });
  const shown = await waitForMessage(page, /stopped unexpectedly/);
  assert.ok(shown.screenShown);
  await context.close();
});

test('a lost WebGL context shows a message instead of a blank canvas', async () => {
  const { context, page } = await openGame(browser, `${web.url}?debug`, { viewport });
  await waitScreen(page, 'title');
  await waitForLoadingScreenGone(page);
  const lost = await page.evaluate(() => {
    const gl = document.getElementById('canvas').getContext('webgl2');
    const extension = gl.getExtension('WEBGL_lose_context');
    if (!extension) return false;
    extension.loseContext();
    return true;
  });
  assert.ok(lost, 'the browser must offer WEBGL_lose_context for this test');
  const shown = await waitForMessage(page, /graphics stopped working/);
  assert.ok(shown.screenShown);
  assert.ok(shown.labelVisible);
  assert.match(shown.text, /reload/i);
  await context.close();
});

test('a healthy start shows no error screen', async () => {
  const { context, page, problems } = await openGame(browser, web.url, { viewport });
  await waitScreen(page, 'title');
  assert.equal(await page.evaluate(() => document.getElementById('loading-screen').classList.contains('is-error')), false);
  assert.deepEqual(problems, []);
  await context.close();
});
