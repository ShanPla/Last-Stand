// Shared setup for the co-op screen tests (coopscreens, coopscreens-input, coopscreens-layout).
// They are three files so that each one finishes well inside the test time limit.
// These tests use the game's fake lobby (the ?fakenet flag) and drive it with debug commands. The real
// server is tested in coopreal.test.mjs.
//
// The second half (startRelay to startCoopRun) is for the co-op run tests (coopplay, cooprejoin),
// which use the REAL relay server in server/ with two players in two browser contexts.
import { before, after } from 'node:test';
import { mkdir } from 'node:fs/promises';
import { createServer } from 'node:net';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startServer, launchChrome, openGame, readUi, debug, waitScreen, tapButton, clickButton, CMD } from './harness.mjs';

// Debug commands for the fake lobby. They match DebugOp in src/components/Net/Net.h.
export const NET = {
  reset: 100, wakeMs: 101, latencyMs: 102, nextError: 103, room: 104, guestRoom: 105, readyMask: 106,
  hostLeft: 107, loseServer: 108, wakeSeconds: 109, phase: 110, rejoinSeat: 111, awayMask: 112, roomSettings: 113,
};
export const PHASE = { offline: 0, waking: 1, connecting: 2, inLobby: 3, starting: 4, error: 5, reconnecting: 6 };
export const ERR = {
  none: 0, unreachable: 1, notFound: 2, full: 3, started: 4, badName: 5, hostLeft: 6, version: 7, other: 8, runEnded: 9, roomClosed: 10, replaced: 11,
};

export const desktop = { viewport: { width: 1280, height: 720 } };
export const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
export const phoneLandscape = { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
export const smallPhone = { viewport: { width: 320, height: 568 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
export const sizes = [
  ['1280x720', desktop],
  ['1024x768', { viewport: { width: 1024, height: 768 } }],
  ['390x844', phone],
  ['844x390', phoneLandscape],
  ['320x568', smallPhone],
];

const rig = { web: null, browser: null };
const relays = [];

// Call once at the top of a test file. Starts the page server and Chrome, and closes them after.
export function useBrowser() {
  before(async () => {
    await mkdir('tests/e2e/screenshots', { recursive: true });
    rig.web = await startServer();
    rig.browser = await launchChrome();
  });
  after(async () => {
    relays.forEach((relay) => relay.stop());
    await rig.browser?.close();
    rig.web?.server.close();
  });
  return rig;
}

export const readNet = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_net', 'string', [], [])));

export async function waitNet(page, check, what, timeout = 8000) {
  const start = Date.now();
  for (;;) {
    const net = await readNet(page);
    if (check(net)) return net;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}: ${JSON.stringify(net)}`);
    await page.waitForTimeout(40);
  }
}

// The button and extent lists come from the frame that just drew, so right after a change they can
// still belong to the old screen. A short wait lets a few frames pass first.
export async function frameUi(page) {
  await page.waitForTimeout(130);
  return readUi(page);
}

export async function fast(page) {
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, 30);
  await debug(page, NET.latencyMs, 30);
}

// Opens the game on the title, with a fast fake server so tests do not sit and wait.
export async function open(options = desktop, query = '?debug&fakenet') {
  const session = await openGame(rig.browser, rig.web.url + query, options);
  await waitScreen(session.page, 'title');
  await fast(session.page);
  return session;
}

export const webUrl = () => rig.web.url;
export const newContext = (options) => rig.browser.newContext(options);

export const isTouch = (options) => options.hasTouch === true;
export const press = (page, options, name) => (isTouch(options) ? tapButton(page, name) : clickButton(page, name));

export async function toCoop(page, options = desktop) {
  await press(page, options, 'Co-op');
  await waitScreen(page, 'coop-menu');
  await page.waitForTimeout(150);
}

// Types into the field that is being edited, once the game has seen the field open.
export async function typeText(page, text) {
  await waitNet(page, (n) => n.editing === 1, 'the field to open');
  await page.waitForTimeout(150);
  await page.keyboard.type(text);
  await page.waitForTimeout(150);
}

// Taps an on-screen key and waits until the game has taken it, because a slow frame can merge two taps.
export async function tapKey(page, label, field, expected) {
  await tapButton(page, `key ${label}`);
  await waitNet(page, (n) => n[field] === expected, `${field} to be "${expected}" after key ${label}`);
}

export async function host(page, options = desktop) {
  await press(page, options, 'Host Co-op');
  await waitScreen(page, 'coop-host');
  return waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
}

export const shot = (page, name) => page.screenshot({ path: `tests/e2e/screenshots/coop-${name}.png` });
export const names = (ui) => ui.buttons.map((b) => b.n);
export const hasButton = (ui, name) => ui.buttons.some((b) => b.n === name);
export const extentOf = (ui, name) => ui.extents.find((e) => e.n === name);

export const overlaps = (a, b) => a.x < b.x + b.w - 1 && a.x + a.w > b.x + 1 && a.y < b.y + b.h - 1 && a.y + a.h > b.y + 1;

// Everything a screen draws must be inside the safe area, and nothing may cover anything else.
// Extents such as the code plate may hold a button on purpose, so those pairs are named.
export function checkLayout(ui, where, allowed = []) {
  const fail = (message) => { throw new Error(`${where}: ${message}`); };
  const [left, top, right, bottom] = ui.safeRect;
  for (const b of ui.buttons) {
    if (!(b.x >= left - 1 && b.y >= top - 1 && b.x + b.w <= right + 1 && b.y + b.h <= bottom + 1)) {
      fail(`"${b.n}" leaves the safe area ${JSON.stringify({ b, safe: [left, top, right, bottom] })}`);
    }
  }
  const mine = ui.extents.filter((e) => e.n.startsWith('coop') || e.n === 'best run');
  for (const e of mine) {
    if (!(e.x >= left - 1 && e.y >= top - 1 && e.x + e.w <= right + 1 && e.y + e.h <= bottom + 1)) {
      fail(`"${e.n}" leaves the safe area ${JSON.stringify(e)}`);
    }
  }
  for (let i = 0; i < ui.buttons.length; i++) {
    for (let j = i + 1; j < ui.buttons.length; j++) {
      if (overlaps(ui.buttons[i], ui.buttons[j])) fail(`"${ui.buttons[i].n}" overlaps "${ui.buttons[j].n}"`);
    }
  }
  for (const e of mine) {
    for (const b of ui.buttons) {
      if (allowed.some(([extent, button]) => extent === e.n && button === b.n)) continue;
      if (overlaps(e, b)) fail(`"${e.n}" overlaps "${b.n}" ${JSON.stringify({ e, b })}`);
    }
  }
  for (let i = 0; i < mine.length; i++) {
    for (let j = i + 1; j < mine.length; j++) {
      if (overlaps(mine[i], mine[j])) fail(`"${mine[i].n}" overlaps "${mine[j].n}"`);
    }
  }
}

// ---- Co-op runs over the real relay -------------------------------------------------

// The game's debug commands, with the co-op one. See DebugCommand in App.cpp.
export const COOP_CMD = { ...CMD, levelUpSlot: 48 };

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

async function freePort() {
  return new Promise((resolve) => {
    const probe = createServer();
    probe.listen(0, '127.0.0.1', () => {
      const { port } = probe.address();
      probe.close(() => resolve(port));
    });
  });
}

// Runs the real server the way Render does. Stopped by useBrowser's after() if a test does not.
export async function startRelay() {
  const port = await freePort();
  const child = spawn(process.execPath, [path.join(repo, 'server/src/index.js')], {
    env: { ...process.env, PORT: String(port) },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await new Promise((resolve, reject) => {
    child.once('exit', (code) => reject(new Error(`relay exited early with code ${code}`)));
    child.stdout.on('data', (chunk) => { if (String(chunk).includes('relay listening')) resolve(); });
  });
  const relay = { port, ws: `ws://127.0.0.1:${port}`, child, stop: () => child.kill() };
  relays.push(relay);
  return relay;
}

export const readCoop = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_coop', 'string', [], [])));

export async function waitCoop(page, check, what, timeout = 10000) {
  const start = Date.now();
  for (;;) {
    const coop = await readCoop(page);
    if (check(coop)) return coop;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}: ${JSON.stringify(coop)}`);
    await page.waitForTimeout(50);
  }
}

export const gameUrl = (server, extra = '') => `${rig.web.url}?server=${server}&debug${extra}`;

export async function openPlayer(server, extra = '', options = desktop) {
  const session = await openGame(rig.browser, gameUrl(server, extra), options);
  await waitScreen(session.page, 'title');
  return session;
}

export async function joinByCode(page, code) {
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, code);
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await clickButton(page, 'Join');
}

// Two players through the real lobby: host, join, ready, start, and both in the run.
export async function startCoopRun(extra = '') {
  const relay = await startRelay();
  const a = await openPlayer(relay.ws, extra);
  const b = await openPlayer(relay.ws, extra);
  await toCoop(a.page);
  const room = await host(a.page);
  await toCoop(b.page);
  await joinByCode(b.page, room.code);
  await waitNet(b.page, (n) => n.phase === PHASE.inLobby && n.count === 2, 'the joined lobby');
  await clickButton(b.page, 'Ready');
  await clickButton(a.page, 'Ready');
  await waitNet(a.page, (n) => n.canStart === 1, 'Start to be enabled');
  await clickButton(a.page, 'Start');
  const [hostRun, guestRun] = await Promise.all([
    waitCoop(a.page, (c) => c.role === 'host' && c.screen === 'playing', 'the host in the run'),
    waitCoop(b.page, (c) => c.role === 'guest' && c.screen === 'playing' && c.count === 2, 'the guest in the run'),
  ]);
  return { relay, a, b, hostRun, guestRun, code: room.code };
}

export const near = (p, q, tolerance) => Math.hypot(p.x - q.x, p.y - q.y) <= tolerance;
