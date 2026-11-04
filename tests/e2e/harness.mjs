// Shared browser test setup: serves the web build with gzip, as a real host
// would, and opens it in the installed Chrome.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { gzipSync } from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../build/web');
const chromePath = process.env.CHROME_PATH
  ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const types = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
  '.webmanifest': 'application/manifest+json', '.png': 'image/png',
};

export async function startServer(port = 0) {
  const sent = { raw: 0, compressed: 0 };
  const server = createServer(async (req, res) => {
    const urlPath = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path.join(root, urlPath === '/' ? 'index.html' : urlPath);
    try {
      const data = await readFile(file);
      const body = gzipSync(data);
      sent.raw += data.length;
      sent.compressed += body.length;
      res.writeHead(200, {
        'Content-Type': types[path.extname(file)] ?? 'application/octet-stream',
        'Content-Encoding': 'gzip',
      });
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end();
    }
  });
  await new Promise((resolve) => server.listen(port, '127.0.0.1', resolve));
  return { server, sent, url: `http://127.0.0.1:${server.address().port}/` };
}

export async function launchChrome() {
  return chromium.launch({
    executablePath: chromePath,
    headless: true,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
}

// Opens the game and records console errors and uncaught exceptions.
export async function openGame(browser, url, contextOptions = {}) {
  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  const problems = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') problems.push(msg.text());
  });
  page.on('pageerror', (error) => problems.push(String(error)));
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  return { context, page, problems };
}

// Counts screenshot pixels close to an RGB color, inside a centered box. A real
// pixel check, because state queries can pass while nothing is visible on screen.
export async function countColorNearCenter(page, [r, g, b], boxFraction = 0.2) {
  const base64 = (await page.screenshot()).toString('base64');
  return page.evaluate(async ({ base64, r, g, b, boxFraction }) => {
    const blob = await (await fetch(`data:image/png;base64,${base64}`)).blob();
    const image = await createImageBitmap(blob);
    const canvas = new OffscreenCanvas(image.width, image.height);
    const context = canvas.getContext('2d');
    context.drawImage(image, 0, 0);
    const w = Math.floor(image.width * boxFraction);
    const h = Math.floor(image.height * boxFraction);
    const data = context.getImageData((image.width - w) >> 1, (image.height - h) >> 1, w, h).data;
    let count = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (Math.abs(data[i] - r) < 24 && Math.abs(data[i + 1] - g) < 24 && Math.abs(data[i + 2] - b) < 24) count++;
    }
    return count;
  }, { base64, r, g, b, boxFraction });
}

// ---- Helpers for the screens -----------------------------------------------------

// Debug command numbers. They match DebugCommand in src/core/App.cpp.
export const CMD = {
  stressEnemies: 1, stressProjectiles: 2, setSeed: 3, grantXp: 4, startRun: 5, forceDeath: 6, forceVictory: 7,
  showScreen: 8, setSafeArea: 9, giveBuild: 10, setHp: 11, setTime: 12, setKills: 13, setLevel: 14, fakeDamage: 15, levelUp: 16,
  setQuality: 20, setShake: 21, setFlash: 22, setDamageNumbers: 23, setReducedMotion: 24, pushEvent: 25, resetFxCounters: 26, runSteps: 27,
};

export const readState = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));

// Screen name, settings, best run, and button rectangles. Rectangles are also given in
// CSS pixels (cx, cy are the center), because that is what the mouse and touch use.
export const readUi = (page) => page.evaluate(() => {
  const ui = JSON.parse(Module.ccall('ls_query_ui', 'string', [], []));
  const sx = innerWidth / ui.width;
  const sy = innerHeight / ui.height;
  for (const b of ui.buttons) { b.cx = (b.x + b.w / 2) * sx; b.cy = (b.y + b.h / 2) * sy; b.cw = b.w * sx; b.ch = b.h * sy; }
  return ui;
});

export const debug = (page, command, value = 0) => page.evaluate(([c, v]) => { Module._ls_debug_command(c, v); }, [command, value]);

export async function waitScreen(page, screen, timeout = 8000) {
  await page.waitForFunction((name) => {
    try { return JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).screen === name; } catch { return false; }
  }, screen, { timeout });
}

// The button list is from the frame that just drew, so right after a screen change it
// can still belong to the old screen. This waits a moment for the new one.
export async function findButton(page, name, timeout = 2000) {
  const start = Date.now();
  for (;;) {
    const ui = await readUi(page);
    const button = ui.buttons.find((b) => b.n === name);
    if (button) return button;
    if (Date.now() - start > timeout) {
      throw new Error(`no button "${name}" on screen "${ui.screen}": ${ui.buttons.map((b) => b.n).join(', ')}`);
    }
    await page.waitForTimeout(40);
  }
}

// Waits until the control with keyboard or gamepad focus has this name (or starts with it).
export async function waitSelected(page, name, timeout = 3000) {
  try {
    await page.waitForFunction((wanted) => {
      try { return JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).selected.startsWith(wanted); } catch { return false; }
    }, name, { timeout });
  } catch {
    const ui = await readUi(page);
    throw new Error(`expected "${name}" to be selected on ${ui.screen}, but "${ui.selected}" is`);
  }
}

export async function clickButton(page, name) {
  const b = await findButton(page, name);
  await page.mouse.click(b.cx, b.cy);
}

export async function tapButton(page, name) {
  const b = await findButton(page, name);
  await page.touchscreen.tap(b.cx, b.cy);
}

// Waits for the title screen, then presses Enter to start a run.
export async function startGame(page) {
  await waitScreen(page, 'title');
  await page.waitForTimeout(150);
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
}
