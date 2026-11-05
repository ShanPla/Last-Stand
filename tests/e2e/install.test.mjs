// Tests for the installable web app: manifest, icons, offline worker, install button.
// The site is served under a sub-path, /Last-Stand/, the way GitHub Pages serves it,
// so any root-relative URL would fail here.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { gzipSync } from 'node:zlib';
import { mkdir, mkdtemp, readFile, writeFile, copyFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { launchChrome, openGame, readUi, waitScreen, startGame, watchProblems } from './harness.mjs';

const buildDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../build/web');
const BASE = '/Last-Stand/';
const types = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
  '.webmanifest': 'application/manifest+json', '.png': 'image/png',
};

let browser;
const servers = [];
const tempDirs = [];

// Copies exactly the files the build lists for the web host, like the deploy step does.
async function copySite() {
  const dir = await mkdtemp(path.join(tmpdir(), 'laststand-site-'));
  tempDirs.push(dir);
  const list = (await readFile(path.join(buildDir, 'site-files.txt'), 'utf8')).split(/\r?\n/).filter(Boolean); // CMake writes CRLF on Windows
  for (const file of list) {
    await mkdir(path.dirname(path.join(dir, file)), { recursive: true });
    await copyFile(path.join(buildDir, file), path.join(dir, file));
  }
  return { dir, list };
}

// Serves a folder under BASE, gzipped like a real host, and records every request.
async function serveSite(dir) {
  const log = [];
  const server = createServer(async (req, res) => {
    const pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const entry = { pathname, status: 0 }; // 0 while the request is still being answered
    log.push(entry);
    try {
      if (!pathname.startsWith(BASE)) throw new Error('outside the base path');
      const file = path.join(dir, pathname.slice(BASE.length) || 'index.html');
      const body = gzipSync(await readFile(file));
      entry.status = 200;
      res.writeHead(200, {
        'Content-Type': types[path.extname(file)] ?? 'application/octet-stream',
        'Content-Encoding': 'gzip',
      });
      res.end(body);
    } catch {
      entry.status = 404;
      res.writeHead(404);
      res.end();
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  servers.push(server);
  return { log, url: `http://127.0.0.1:${server.address().port}${BASE}`, origin: `http://127.0.0.1:${server.address().port}` };
}

let site;     // one shared copy of the real build, served under /Last-Stand/
let siteFiles;

before(async () => {
  await mkdir('tests/e2e/screenshots', { recursive: true });
  const copy = await copySite();
  siteFiles = copy.list;
  site = await serveSite(copy.dir);
  browser = await launchChrome();
});

after(async () => {
  await browser?.close();
  for (const server of servers) server.close();
  for (const dir of tempDirs) await rm(dir, { recursive: true, force: true });
});

// ---- Helpers -----------------------------------------------------------------

const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const landscapePhone = { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const desktop = { viewport: { width: 1280, height: 720 } };

// Polls from the test, because waitForFunction does not wait for async checks.
async function pollUntil(page, check, arg, what, timeout = 30000) {
  const start = Date.now();
  for (;;) {
    if (await page.evaluate(check, arg)) return;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}`);
    await page.waitForTimeout(100);
  }
}

// Waits until the worker is active, controls the page, and has cached every file.
const waitForOffline = (page, fileCount) => pollUntil(page, async (count) => {
  const registration = await navigator.serviceWorker.getRegistration();
  if (!registration || !registration.active || !navigator.serviceWorker.controller) return false;
  const keys = await caches.keys();
  if (keys.length !== 1) return false;
  return (await (await caches.open(keys[0])).keys()).length === count;
}, fileCount, 'the offline cache to fill');

// Fires the event Chrome sends when the game can be installed. Headless Chrome never
// sends it, so the test sends one that behaves the same way.
const offerInstall = (page) => page.evaluate(() => {
  window.__installPrompts = 0;
  const event = new Event('beforeinstallprompt', { cancelable: true });
  event.prompt = () => { window.__installPrompts++; return Promise.resolve(); };
  event.userChoice = Promise.resolve({ outcome: 'dismissed' });
  window.dispatchEvent(event);
  return event.defaultPrevented;
});

const installBoxHidden = (page) => page.evaluate(() => document.getElementById('install-app').hidden);

async function waitInstallBox(page, hidden, timeout = 3000) {
  await page.waitForFunction((want) => document.getElementById('install-app').hidden === want, hidden, { timeout });
}

function pngSize(bytes) {
  assert.deepEqual([...bytes.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 'not a PNG');
  assert.equal(bytes.subarray(12, 16).toString('ascii'), 'IHDR');
  return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
}

// ---- Manifest and icons --------------------------------------------------------

test('the manifest is valid and every icon exists and is a PNG of its stated size', async () => {
  const manifestUrl = `${site.url}manifest.webmanifest`;
  const response = await fetch(manifestUrl);
  assert.equal(response.status, 200);
  const manifest = await response.json();
  assert.equal(manifest.name, 'Last Stand');
  assert.ok(manifest.short_name);
  assert.ok(['standalone', 'fullscreen'].includes(manifest.display));
  assert.equal(manifest.orientation, 'any');
  assert.equal(manifest.background_color, '#14131a');
  assert.equal(manifest.theme_color, '#14131a');
  // Relative, so the manifest works under any sub-path.
  assert.match(manifest.start_url, /^\.\//);
  assert.match(manifest.scope, /^\.\//);
  assert.equal(new URL(manifest.start_url, manifestUrl).href, site.url);
  assert.equal(new URL(manifest.scope, manifestUrl).href, site.url);

  const seen = new Set();
  for (const icon of manifest.icons) {
    const url = new URL(icon.src, manifestUrl);
    const icons = await fetch(url);
    assert.equal(icons.status, 200, `${icon.src} is missing`);
    assert.equal(icons.headers.get('content-type'), 'image/png');
    const [w, h] = pngSize(Buffer.from(await icons.arrayBuffer()));
    assert.equal(icon.type, 'image/png');
    assert.equal(icon.sizes, `${w}x${h}`, `${icon.src} is ${w}x${h}`);
    seen.add(`${icon.purpose}-${w}`);
  }
  for (const purpose of ['any', 'maskable']) {
    for (const size of [192, 512]) assert.ok(seen.has(`${purpose}-${size}`), `no ${purpose} icon at ${size}`);
  }
});

// The built page is minified, so it is read through the browser's DOM, not with text patterns.
async function readPageLinks() {
  const context = await browser.newContext(desktop);
  const page = await context.newPage();
  await page.goto(site.url, { waitUntil: 'domcontentloaded' });
  const result = await page.evaluate(() => ({
    manifest: document.querySelector('link[rel=manifest]')?.getAttribute('href'),
    touchIcon: document.querySelector('link[rel=apple-touch-icon]')?.getAttribute('href'),
    icons: [...document.querySelectorAll('link[rel=icon]')].map((l) => [l.getAttribute('href'), l.getAttribute('sizes')]),
    webApp: document.querySelector('meta[name=apple-mobile-web-app-capable]')?.content,
    title: document.querySelector('meta[name=apple-mobile-web-app-title]')?.content,
    urls: [...document.querySelectorAll('[href],[src]')].map((e) => e.getAttribute('href') ?? e.getAttribute('src')),
  }));
  await context.close();
  return result;
}

test('the page links the manifest, favicons and iPhone home screen tags', async () => {
  const links = await readPageLinks();
  assert.equal(links.manifest, 'manifest.webmanifest');
  assert.equal(links.touchIcon, 'icons/apple-touch-icon.png');
  assert.equal(links.webApp, 'yes');
  assert.equal(links.title, 'Last Stand');
  assert.deepEqual(links.icons, [['icons/favicon-32.png', '32x32'], ['icons/favicon-48.png', '48x48']]);
  const touch = await fetch(`${site.url}icons/apple-touch-icon.png`);
  assert.deepEqual(pngSize(Buffer.from(await touch.arrayBuffer())), [180, 180]);
  for (const [href, sizes] of links.icons) {
    const icon = await fetch(new URL(href, site.url));
    const size = Number(sizes.split('x')[0]);
    assert.deepEqual(pngSize(Buffer.from(await icon.arrayBuffer())), [size, size]);
  }
});

test('every URL in the page, manifest and worker is relative', async () => {
  const pageUrls = (await readPageLinks()).urls;
  const manifest = await (await fetch(`${site.url}manifest.webmanifest`)).json();
  const worker = await (await fetch(`${site.url}sw.js`)).text();
  const scripts = await Promise.all(['GameCanvas/GameCanvas.js', 'LoadingScreen/LoadingScreen.js', 'OfflineCache/OfflineCache.js']
    .map(async (f) => (await fetch(`${site.url}components/${f}`)).text()));
  const urls = [
    ...pageUrls,
    manifest.id, manifest.start_url, manifest.scope, ...manifest.icons.map((i) => i.src),
    ...JSON.parse(worker.match(/const FILES = (\[.*\]);/)[1]),
    // Quoted file names the page scripts load: loadWasm('index.wasm', ...) and register('sw.js', ...).
    ...[...scripts.join('\n').matchAll(/(?:loadWasm|register)\('([^']*)'/g)].map((m) => m[1]),
  ];
  assert.ok(pageUrls.length >= 10 && urls.length > 40, `only ${urls.length} URLs were found`);
  for (const url of urls) {
    // Anything but a data URL must not start with a slash or a scheme.
    if (url.startsWith('data:')) continue;
    assert.doesNotMatch(url, /^(\/|[a-z]+:)/i, `not relative: ${url}`);
  }
});

test('the site list names real files, including the new ones', async () => {
  for (const wanted of ['index.html', 'index.js', 'index.wasm', 'manifest.webmanifest', 'sw.js',
    'components/InstallApp/InstallApp.js', 'components/InstallApp/InstallApp.css', 'icons/icon-512.png']) {
    assert.ok(siteFiles.includes(wanted), `${wanted} is not in site-files.txt`);
  }
  assert.ok(!siteFiles.some((f) => /CMake|build\.ninja|\.a$|^_deps|site-files/.test(f)), 'build system files listed');
});

// ---- The game under a sub-path ---------------------------------------------------

test('the game runs under a sub-path with no console errors and no missing files', async () => {
  const start = site.log.length;
  const { context, page, problems } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await page.waitForTimeout(500);
  assert.deepEqual(problems, []);
  const missing = site.log.slice(start).filter((r) => r.status === 404);
  assert.deepEqual(missing, []);
  await context.close();
});

// ---- Service worker -----------------------------------------------------------------

test('the worker registers after the game has loaded, and a second load works offline', async () => {
  const start = site.log.length;
  const { context, page, problems } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await waitForOffline(page, siteFiles.length - 1);

  // The worker must not compete with the first download: sw.js is requested only
  // after the wasm file has been.
  const requests = site.log.slice(start).map((r) => r.pathname.slice(BASE.length));
  assert.ok(requests.indexOf('index.wasm') >= 0 && requests.indexOf('sw.js') > requests.indexOf('index.wasm'),
    `order was ${requests.join(', ')}`);

  await context.setOffline(true);
  const servedByWorker = [];
  page.on('response', (r) => { if (r.fromServiceWorker()) servedByWorker.push(path.basename(new URL(r.url()).pathname)); });
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  assert.ok(servedByWorker.includes('index.wasm'), `served by the worker: ${servedByWorker.join(', ')}`);
  assert.deepEqual(problems, []);
  // The game really runs offline, not just the title.
  await startGame(page);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a new build replaces the old cache, and a build that fails to install changes nothing', async () => {
  const copy = await copySite();
  const own = await serveSite(copy.dir);
  const { context, page, problems } = await openGame(browser, own.url, desktop);
  await waitForOffline(page, copy.list.length - 1);
  const [oldKey] = await page.evaluate(() => caches.keys());

  const workerFile = path.join(copy.dir, 'sw.js');
  const original = await readFile(workerFile, 'utf8');

  // A broken build: one listed file does not exist, so the new worker cannot install.
  await writeFile(workerFile, original
    .replace(/laststand-[0-9a-f]+/, 'laststand-broken')
    .replace('"index.html",', '"index.html","does-not-exist.js",'));
  await page.evaluate(() => navigator.serviceWorker.getRegistration().then((r) => r.update())).catch(() => {});
  await page.waitForTimeout(1500);
  assert.deepEqual(await page.evaluate(() => caches.keys()), [oldKey], 'the failed install touched the cache');
  await context.setOffline(true);
  await page.reload();
  await waitScreen(page, 'title');
  await context.setOffline(false);

  // A good new build: new cache name, and the old cache is deleted.
  await writeFile(workerFile, original.replace(/laststand-[0-9a-f]+/, 'laststand-newbuild'));
  await page.evaluate(() => navigator.serviceWorker.getRegistration().then((r) => r.update()));
  await pollUntil(page, async () => (await caches.keys()).join() === 'laststand-newbuild', null, 'the new cache to replace the old', 20000);
  assert.notEqual(oldKey, 'laststand-newbuild');

  await context.setOffline(true);
  await page.reload();
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the game works when service workers are blocked or missing', async () => {
  // Blocked by the browser, as some privacy settings do.
  const blocked = await openGame(browser, site.url, { ...desktop, serviceWorkers: 'block' });
  await waitScreen(blocked.page, 'title');
  await blocked.page.waitForTimeout(2500);
  // Blocking is done by Playwright, and it announces its own block as a warning. That one
  // message is the test setup, not the game, so it is the only one allowed here. It may or
  // may not arrive before the check, so it is filtered out rather than required.
  assert.deepEqual(blocked.problems.filter((text) => text !== 'warning: Service Worker registration blocked by Playwright'), []);
  await blocked.context.close();

  // The API is missing altogether, as on plain http outside localhost.
  const context = await browser.newContext(desktop);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.addInitScript(() => { Object.defineProperty(navigator, 'serviceWorker', { value: undefined }); });
  await page.goto(site.url);
  await waitScreen(page, 'title');
  await page.waitForTimeout(2500);
  assert.deepEqual(problems, []);
  assert.equal(await installBoxHidden(page), true);
  await context.close();
});

// ---- Install button -------------------------------------------------------------------

test('the install button is hidden until the browser offers an install', async () => {
  const { context, page, problems } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await page.waitForTimeout(1000);
  assert.equal(await installBoxHidden(page), true);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the install button shows on the title only, and never during play', async () => {
  const { context, page, problems } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  assert.equal(await offerInstall(page), true, 'the browser banner is not suppressed');
  await waitInstallBox(page, false);
  await startGame(page);
  await waitInstallBox(page, true);
  await page.waitForTimeout(1200); // two more checks of the timer, still hidden
  assert.equal(await installBoxHidden(page), true);
  assert.deepEqual(problems, []);
  await context.close();
});

test('clicking Install keeps focus on the canvas, asks the browser once, and Enter still starts a run', async () => {
  const { context, page, problems } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await offerInstall(page);
  await waitInstallBox(page, false);
  await page.click('#install-app-button');
  assert.equal(await page.evaluate(() => window.__installPrompts), 1);
  assert.equal(await page.evaluate(() => document.activeElement && document.activeElement.id), 'canvas');
  // The click must not leak into the game, which would press a title button.
  assert.equal((await readUi(page)).screen, 'title');
  await waitInstallBox(page, true);
  await startGame(page); // presses Enter
  assert.deepEqual(problems, []);
  await context.close();
});

test('pressing Enter while the button is showing starts a run, even without clicking', async () => {
  const { context, page } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await offerInstall(page);
  await waitInstallBox(page, false);
  await startGame(page);
  await context.close();
});

test('a tap on Install does not press the title button under the finger', async () => {
  const { context, page } = await openGame(browser, site.url, phone);
  await waitScreen(page, 'title');
  await offerInstall(page);
  await waitInstallBox(page, false);
  const box = await page.locator('#install-app-button').boundingBox();
  assert.ok(box.height >= 44 && box.width >= 44, 'touch target is too small');
  await page.touchscreen.tap(box.x + box.width / 2, box.y + box.height / 2);
  await page.waitForTimeout(400);
  assert.equal(await page.evaluate(() => window.__installPrompts), 1);
  assert.equal((await readUi(page)).screen, 'title');
  await context.close();
});

test('Dismiss hides the button for good, and keeps focus on the canvas', async () => {
  const { context, page } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await offerInstall(page);
  await waitInstallBox(page, false);
  await page.click('#install-app-dismiss');
  await waitInstallBox(page, true);
  assert.equal(await page.evaluate(() => document.activeElement && document.activeElement.id), 'canvas');
  await page.reload();
  await waitScreen(page, 'title');
  await offerInstall(page);
  await page.waitForTimeout(1200);
  assert.equal(await installBoxHidden(page), true, 'it came back after being dismissed');
  await startGame(page);
  await context.close();
});

test('the button hides once the app is installed', async () => {
  const { context, page } = await openGame(browser, site.url, desktop);
  await waitScreen(page, 'title');
  await offerInstall(page);
  await waitInstallBox(page, false);
  await page.evaluate(() => window.dispatchEvent(new Event('appinstalled')));
  await waitInstallBox(page, true);
  await context.close();
});

for (const [name, options] of [['desktop', desktop], ['phone-portrait', phone], ['phone-landscape', landscapePhone]]) {
  test(`the install button does not cover the title buttons (${name})`, async () => {
    const { context, page } = await openGame(browser, site.url, options);
    await waitScreen(page, 'title');
    await offerInstall(page);
    await waitInstallBox(page, false);
    await page.waitForTimeout(300);
    const box = await page.evaluate(() => {
      const r = document.getElementById('install-app').getBoundingClientRect();
      return { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
    });
    const ui = await readUi(page);
    assert.ok(ui.buttons.length >= 3);
    for (const b of ui.buttons) {
      const overlaps = b.cx - b.cw / 2 < box.right && b.cx + b.cw / 2 > box.left
        && b.cy - b.ch / 2 < box.bottom && b.cy + b.ch / 2 > box.top;
      assert.ok(!overlaps, `the install button covers "${b.n}"`);
    }
    const view = options.viewport;
    assert.ok(box.left >= 0 && box.top >= 0 && box.right <= view.width && box.bottom <= view.height, 'it is partly off screen');
    await page.screenshot({ path: `tests/e2e/screenshots/install-${name}.png` });
    await context.close();
  });
}
