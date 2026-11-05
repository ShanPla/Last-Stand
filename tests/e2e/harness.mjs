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
  // Connections kept open after a test would stop the server closing, and then the test
  // file would never exit. Close them all whenever the server closes.
  const closeServer = server.close.bind(server);
  server.close = (callback) => {
    server.closeAllConnections();
    return closeServer(callback);
  };
  return { server, sent, url: `http://127.0.0.1:${server.address().port}/` };
}

export async function launchChrome() {
  return chromium.launch({
    executablePath: chromePath,
    headless: true,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
}

// The only console warnings a healthy run prints. Native code writes its stderr and its
// own warnings to the console as warnings, so any other one is a real fault, such as a
// shader that failed to compile. Keep this list short and give each entry its reason.
const KNOWN_WARNINGS = [
  // Chrome's own note that the audio output uses a ScriptProcessorNode. It is not our error.
  /^The ScriptProcessorNode is deprecated/,
  // Chrome's note that sound waits for a press. The device opens during loading on purpose, so
  // the press itself doesn't freeze the page, and it stays silent until then.
  /^The AudioContext was not allowed to start/,
  // The renderer reports its view size and its start-up cost on purpose, once each.
  /^world view: target \d+x\d+, scale [\d.]+, sees \d+ of \d+$/,
  /^world renderer ready: /,
];

// Collects console errors, unexpected console warnings and uncaught exceptions on a page
// into the array, and returns it. Tests assert that it stays empty.
export function watchProblems(page, problems = []) {
  page.on('console', (msg) => {
    const text = msg.text();
    if (msg.type() === 'error') problems.push(text);
    else if (msg.type() === 'warning' && !KNOWN_WARNINGS.some((known) => known.test(text))) problems.push(`warning: ${text}`);
  });
  page.on('pageerror', (error) => problems.push(String(error)));
  return problems;
}

// Opens the game and records console errors, unexpected warnings and uncaught exceptions.
export async function openGame(browser, url, contextOptions = {}) {
  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  const problems = watchProblems(page);
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
  setQuality: 20, setShake: 21, setFlash: 22, setDamageNumbers: 23, setReducedMotion: 24, pushEvent: 25, resetFxCounters: 26, runSteps: 27, setMap: 28,
};

// Dash, held-still simulation and cooldown, kept apart from the list above.
CMD.dash = 40;
CMD.holdSim = 41;
CMD.setDashCooldown = 42;

export const readState = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));

// Screen name, settings, best run, and button rectangles. Rectangles are also given in
// CSS pixels (cx, cy are the center), because that is what the mouse and touch use.
// The middle of the touch joystick, in page pixels. It only reports itself while it is drawn,
// which is during play on a touch screen, so this waits for it.
export async function stickHome(page) {
  const box = await page.evaluate(async () => {
    for (let i = 0; i < 100; i++) {
      const ui = JSON.parse(Module.ccall('ls_query_ui', 'string', [], []));
      const found = ui.extents.find((e) => e.n === 'joystick');
      if (found) return { ...found, ratio: window.devicePixelRatio };
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    return null;
  });
  if (!box) throw new Error('the joystick never appeared');
  return { x: (box.x + box.w / 2) / box.ratio, y: (box.y + box.h / 2) / box.ratio, radius: box.w / 2 / box.ratio };
}

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
export async function findButton(page, name, timeout = 6000) {
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
export async function waitSelected(page, name, timeout = 8000) {
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

// ---- Emulated gamepad ------------------------------------------------------------

// A fake pad is installed in the page, so a gamepad is tested without hardware. Buttons
// follow the standard layout: 0 is A, 1 is B, 3 is Y, 5 is the right shoulder, 9 is Start,
// 12-15 the D-pad.
export async function installFakePad(page) {
  await page.evaluate(() => {
    const buttons = Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 }));
    const pad = { id: 'Fake Pad (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard', axes: [0, 0, 0, 0], buttons, timestamp: 0 };
    window.__pad = pad;
    navigator.getGamepads = () => [pad, null, null, null];
    const event = new Event('gamepadconnected');
    event.gamepad = pad;
    window.dispatchEvent(event);
  });
}

export const padPress = async (page, index, holdMs = 90) => {
  await page.evaluate((i) => { window.__pad.buttons[i].pressed = true; window.__pad.buttons[i].value = 1; window.__pad.timestamp++; }, index);
  await page.waitForTimeout(holdMs);
  await page.evaluate((i) => { window.__pad.buttons[i].pressed = false; window.__pad.buttons[i].value = 0; window.__pad.timestamp++; }, index);
  await page.waitForTimeout(60);
};
