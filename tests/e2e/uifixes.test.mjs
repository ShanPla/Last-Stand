// Tests for fixes found in review of input, saving, sound, the loading screen and the offline worker.
// Each test here fails when its fix is taken out. Layout checks are in uifixeslayout.test.mjs.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { gzipSync } from 'node:zlib';
import { mkdir, mkdtemp, readFile, writeFile, copyFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  startServer, launchChrome, openGame, watchProblems, CMD, debug, readState, readUi, waitScreen, waitSelected, findButton, startGame,
} from './harness.mjs';

let web;
let browser;
const tempDirs = [];
const extraServers = [];

before(async () => {
  await mkdir('tests/e2e/screenshots', { recursive: true });
  web = await startServer();
  browser = await launchChrome();
});

after(async () => {
  await browser?.close();
  web?.server.close();
  for (const server of extraServers) server.close();
  for (const dir of tempDirs) await rm(dir, { recursive: true, force: true });
});

const desktop = { viewport: { width: 1280, height: 720 } };
const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };

const open = (options = desktop, query = '?debug') => openGame(browser, web.url + query, options);

// ---- Save: nothing lost to another tab or an unreadable store -----------------------------

const storedBestTime = (page) => page.evaluate(() => {
  const text = localStorage.getItem('laststand.save') ?? '';
  const match = text.match(/^bestTime=([0-9.]+)$/m);
  return match ? Number(match[1]) : 0;
});

async function dieAfter(page, seconds) {
  await debug(page, CMD.startRun, 0);
  await waitScreen(page, 'playing');
  await debug(page, CMD.setTime, seconds);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
}

test('a second tab with an old copy of the save does not take a best run back', async () => {
  const context = await browser.newContext(desktop);
  const a = await context.newPage();
  const b = await context.newPage();
  const problemsA = watchProblems(a);
  const problemsB = watchProblems(b);
  for (const page of [a, b]) {
    await page.goto(web.url + '?debug');
    await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
    await waitScreen(page, 'title');
  }
  // Both tabs loaded an empty save. Tab A then sets a record.
  await dieAfter(a, 300);
  assert.ok(await storedBestTime(a) >= 299, 'tab A did not save its record');
  // Tab B still thinks the best is zero, and finishes a short run.
  await dieAfter(b, 20);
  const stored = await storedBestTime(b);
  assert.ok(stored >= 299, `tab B wrote its own numbers over the record: best time is ${stored}`);
  // And tab B now shows the merged record too.
  await debug(b, CMD.showScreen, 0);
  await waitScreen(b, 'title');
  assert.ok((await readUi(b)).bestTime >= 299, 'tab B does not show the merged record');
  assert.deepEqual([...problemsA, ...problemsB], []);
  await context.close();
});

test('a save that cannot be read is not written over with defaults', async () => {
  const context = await browser.newContext(desktop);
  const seed = await context.newPage();
  await seed.goto(web.url);
  const good = 'version=1\nmusic=0.300\neffects=0.800\nshake=1.000\nflash=1.000\ndamageNumbers=1\nreducedMotion=0\ndifficulty=1\nmap=0\n'
    + 'bestTime=412.00\nmostKills=999\nhighestLevel=20\nwins=3\nruns=11\n';
  await seed.evaluate((text) => localStorage.setItem('laststand.save', text), good);
  await seed.close();

  const page = await context.newPage();
  // Reading fails while the game starts, as it does when storage is briefly blocked.
  await page.addInitScript(() => {
    const real = Storage.prototype.getItem;
    window.__breakStorage = true;
    Storage.prototype.getItem = function (key) {
      if (window.__breakStorage && key === 'laststand.save') throw new DOMException('blocked', 'SecurityError');
      return real.call(this, key);
    };
  });
  const problems = watchProblems(page);
  await page.goto(web.url + '?debug');
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).bestTime, 0, 'the game should start on defaults while the read fails');
  await dieAfter(page, 30);
  await page.waitForTimeout(300);
  await page.evaluate(() => { window.__breakStorage = false; });
  const text = await page.evaluate(() => localStorage.getItem('laststand.save'));
  assert.equal(text, good, 'the unreadable save was written over');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Input ------------------------------------------------------------------------------------

test('a cancelled touch clears the joystick and the player stops', async () => {
  const { context, page, problems } = await open(phone);
  await startGame(page);
  const cdp = await context.newCDPSession(page);
  const touch = (type, x, y) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchCancel' ? [] : [{ x, y, id: 1 }] });
  await touch('touchStart', 120, 500);
  await touch('touchMove', 220, 500);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).joystick === 1, null, { timeout: 3000 });
  // The system takes the touch away: there is a cancel and no end.
  await touch('touchCancel');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).joystick === 0, null, { timeout: 3000 });
  const first = await readState(page);
  await page.waitForTimeout(500);
  const second = await readState(page);
  assert.ok(Math.hypot(second.x - first.x, second.y - first.y) < 1, 'the player kept running with no finger down');
  // A new touch afterwards starts fresh.
  await touch('touchStart', 100, 400);
  await touch('touchMove', 100, 300);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).joystick === 1, null, { timeout: 3000 });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).joystick === 0, null, { timeout: 3000 });
  // raylib answers a touchcancel as if it could be cancelled, and Chrome notes that in the console.
  // It is raylib's handler and not the game's, so only that one message is allowed.
  assert.deepEqual(problems.filter((text) => !/Ignored attempt to cancel a touchcancel event/.test(text)), []);
  await context.close();
});

test('a slider drag ends when Settings is left, and does not jump when Settings opens again', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await debug(page, CMD.showScreen, 2);
  await waitScreen(page, 'settings');
  const music = await findButton(page, 'Music volume');
  await page.mouse.move(music.cx + music.cw * 0.12, music.cy);
  await page.mouse.down();
  await page.waitForTimeout(150);
  const dragged = (await readUi(page)).music;
  assert.ok(dragged > 0 && dragged < 1, `the press should set the slider, got ${dragged}`);
  // Leave while the button is still held.
  await page.keyboard.press('Escape');
  await waitScreen(page, 'title');
  await page.mouse.up();
  await page.waitForTimeout(150);

  // Open Settings with a click on the button, and keep the button held for a moment.
  const settingsButton = await findButton(page, 'Settings');
  await page.mouse.move(settingsButton.cx, settingsButton.cy);
  await page.mouse.down();
  await waitScreen(page, 'settings');
  await page.waitForTimeout(300);
  const held = (await readUi(page)).music;
  await page.mouse.up();
  assert.ok(Math.abs(held - dragged) < 0.011, `the slider jumped from ${dragged} to ${held} with no press on it`);
  assert.deepEqual(problems, []);
  await context.close();
});

// A synthetic key press, so two of them can land in the same frame.
const keyEvent = (page, keys) => page.evaluate((list) => {
  for (const [code, key, keyCode] of list) {
    window.dispatchEvent(new KeyboardEvent('keydown', { code, key, keyCode, which: keyCode, bubbles: true }));
    window.dispatchEvent(new KeyboardEvent('keyup', { code, key, keyCode, which: keyCode, bubbles: true }));
  }
}, keys);

async function openLevelUp(page) {
  await debug(page, CMD.levelUp);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1, null, { timeout: 5000 });
  await page.waitForTimeout(600);  // Past the short input delay.
}

test('a card pick and a reroll in the same frame make one choice, never a pick from unseen cards', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.holdSim, 0);
  for (let attempt = 0; attempt < 3; attempt++) {
    await openLevelUp(page);
    const before = await readState(page);
    assert.equal(before.rerolls, 2, 'a fresh run starts with two rerolls');
    // Enter picks the selected card and R rerolls. Both arrive in one frame.
    await keyEvent(page, [['Enter', 'Enter', 13], ['KeyR', 'r', 82]]);
    await page.waitForTimeout(400);
    const after = await readState(page);
    const picked = after.phase !== 1;
    const rerolled = after.rerolls < before.rerolls;
    assert.ok(!(picked && rerolled), 'the reroll was spent and a card was also picked');
    assert.ok(picked || rerolled, 'neither action happened');
    if (!picked) await keyEvent(page, [['Enter', 'Enter', 13]]);
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase !== 1, null, { timeout: 5000 });
    // A new run each time, so the rerolls are back.
    await debug(page, CMD.startRun, 0);
    await waitScreen(page, 'playing');
  }
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, options] of [['columns', desktop], ['stacked', phone]]) {
  test(`level-up focus skips a disabled reroll button (${name})`, async () => {
    const { context, page, problems } = await open(options);
    await startGame(page);
    await debug(page, CMD.holdSim, 0);
    await openLevelUp(page);
    // Use both rerolls, waiting out the input delay each time.
    for (let i = 0; i < 2; i++) {
      await page.keyboard.press('r');
      await page.waitForTimeout(700);
    }
    assert.equal((await readState(page)).rerolls, 0);
    for (let i = 0; i < 4; i++) {
      await page.keyboard.press('ArrowDown');
      await page.waitForTimeout(120);
      const selected = (await readUi(page)).selected;
      assert.ok(selected.startsWith('Card'), `focus went to "${selected}" after Down press ${i + 1}`);
    }
    // Enter still picks the focused card.
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase !== 1, null, { timeout: 5000 });
    assert.deepEqual(problems, []);
    await context.close();
  });
}

// ---- Sound -------------------------------------------------------------------------------------

// Makes the browser refuse to start sound while window.__refuseResume is true, the way it does
// when a press does not count as user activation. Wherever raylib's sound library makes its
// audio context, it asks to resume it, and this answers without doing so.
const refuseResume = (page) => page.addInitScript(() => {
  window.__refuseResume = true;
  const real = AudioContext.prototype.resume;
  AudioContext.prototype.resume = function () {
    if (window.__refuseResume) return Promise.resolve();
    return real.call(this);
  };
});

const audioState = (page) => page.evaluate(() => ({
  ui: JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])),
  context: (window.miniaudio?.devices ?? []).map((d) => d && d.webaudio && d.webaudio.state),
}));

test('sound is asked to start again on later presses until it really runs', async () => {
  const context = await browser.newContext(desktop);
  const page = await context.newPage();
  await refuseResume(page);
  const problems = watchProblems(page);
  await page.goto(web.url + '?debug');
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  // The first press does not start sound: the browser refuses.
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(700);
  let state = await audioState(page);
  assert.equal(state.ui.audio, 0, 'sound should not be running while the browser refuses');
  assert.ok(state.context.includes('suspended'), `the context should be suspended: ${state.context}`);
  // A later press, now accepted, must start it.
  await page.evaluate(() => { window.__refuseResume = false; });
  await page.keyboard.press('ArrowDown');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).audio === 1, null, { timeout: 5000 });
  state = await audioState(page);
  assert.ok(state.context.includes('running'), `the context is ${state.context}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('music on the title screen starts ducked, as in every other menu', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.keyboard.press('ArrowDown');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).audio === 1, null, { timeout: 5000 });
  await page.waitForTimeout(400);
  const { duck } = await readUi(page);
  assert.ok(duck > 0.2 && duck < 0.3, `the music gain on the title screen is ${duck}, not the ducked 0.25`);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- The loading screen ---------------------------------------------------------------------

test('the loading screen is really gone once the game runs', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.waitForTimeout(500);
  const state = await page.evaluate(() => {
    const screen = document.getElementById('loading-screen');
    return { hidden: screen.hidden, display: getComputedStyle(screen).display };
  });
  assert.equal(state.hidden, true);
  assert.equal(state.display, 'none', 'the invisible overlay is still in the page');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- The offline worker -----------------------------------------------------------------------

const buildDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../build/web');
const BASE = '/Last-Stand/';
const types = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
  '.webmanifest': 'application/manifest+json', '.png': 'image/png',
};

async function copySite() {
  const dir = await mkdtemp(path.join(tmpdir(), 'laststand-site-'));
  tempDirs.push(dir);
  const list = (await readFile(path.join(buildDir, 'site-files.txt'), 'utf8')).split(/\r?\n/).filter(Boolean);
  for (const file of list) {
    await mkdir(path.dirname(path.join(dir, file)), { recursive: true });
    await copyFile(path.join(buildDir, file), path.join(dir, file));
  }
  return { dir, list };
}

async function serveSite(dir) {
  const server = createServer(async (req, res) => {
    const pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    try {
      if (!pathname.startsWith(BASE)) throw new Error('outside the base path');
      const file = path.join(dir, pathname.slice(BASE.length) || 'index.html');
      res.writeHead(200, { 'Content-Type': types[path.extname(file)] ?? 'application/octet-stream', 'Content-Encoding': 'gzip' });
      res.end(gzipSync(await readFile(file)));
    } catch {
      res.writeHead(404);
      res.end();
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  extraServers.push(server);
  return `http://127.0.0.1:${server.address().port}${BASE}`;
}

async function pollUntil(page, check, arg, what, timeout = 30000) {
  const start = Date.now();
  for (;;) {
    if (await page.evaluate(check, arg)) return;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}`);
    await page.waitForTimeout(100);
  }
}

test('the worker lists a hash for every file it caches', async () => {
  const copy = await copySite();
  const worker = await readFile(path.join(copy.dir, 'sw.js'), 'utf8');
  const files = JSON.parse(worker.match(/const FILES = (\[.*\]);/)[1]);
  assert.equal(files.length, copy.list.length - 1, 'every site file but the worker itself is listed');
  for (const [file, hash] of files) {
    assert.match(hash, /^[0-9a-f]{64}$/, `no SHA-256 for ${file}`);
    assert.ok(copy.list.includes(file), `${file} is not in the site list`);
  }
});

test('an install that finds a file from another build fails, and the old version keeps working', async () => {
  const copy = await copySite();
  const url = await serveSite(copy.dir);
  const { context, page, problems } = await (async () => {
    const context = await browser.newContext(desktop);
    const page = await context.newPage();
    const problems = watchProblems(page);
    await page.goto(url);
    await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
    return { context, page, problems };
  })();
  await pollUntil(page, async (count) => {
    const registration = await navigator.serviceWorker.getRegistration();
    if (!registration || !registration.active || !navigator.serviceWorker.controller) return false;
    const keys = await caches.keys();
    return keys.length === 1 && (await (await caches.open(keys[0])).keys()).length === count;
  }, copy.list.length - 1, 'the offline cache to fill');
  const [oldKey] = await page.evaluate(() => caches.keys());

  // A host half way through an update: the new worker (new build id, new list of hashes)
  // is served together with a script of another build. Here the script is changed on disk.
  const workerFile = path.join(copy.dir, 'sw.js');
  const worker = await readFile(workerFile, 'utf8');
  await writeFile(workerFile, worker.replace(/laststand-[0-9a-f]+/, 'laststand-mixed'));
  const script = path.join(copy.dir, 'index.js');
  await writeFile(script, (await readFile(script, 'utf8')) + '\n// a different build\n');
  await page.evaluate(() => navigator.serviceWorker.getRegistration().then((r) => r.update())).catch(() => {});
  await page.waitForTimeout(2000);
  assert.deepEqual(await page.evaluate(() => caches.keys()), [oldKey], 'the mixed install replaced the working cache');

  // The old version still runs, offline too.
  await context.setOffline(true);
  await page.reload();
  await waitScreen(page, 'title');
  await context.setOffline(false);
  assert.deepEqual(problems, []);
  await context.close();
});
