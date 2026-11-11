// Shared setup for the co-op screen tests (coopscreens, coopscreens-input, coopscreens-layout).
// They are three files so that each one finishes well inside the test time limit.
// There is no real server yet, so the game's fake lobby is driven with debug commands.
import { before, after } from 'node:test';
import { mkdir } from 'node:fs/promises';
import { startServer, launchChrome, openGame, readUi, debug, waitScreen, tapButton, clickButton } from './harness.mjs';

// Debug commands for the fake lobby. They match DebugOp in src/components/Net/Net.h.
export const NET = {
  reset: 100, wakeMs: 101, latencyMs: 102, nextError: 103, room: 104, guestRoom: 105, readyMask: 106,
  hostLeft: 107, loseServer: 108, wakeSeconds: 109, phase: 110,
};
export const PHASE = { offline: 0, waking: 1, connecting: 2, inLobby: 3, starting: 4, error: 5 };
export const ERR = { none: 0, unreachable: 1, notFound: 2, full: 3, started: 4, badName: 5, hostLeft: 6, version: 7, other: 8 };

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

// Call once at the top of a test file. Starts the page server and Chrome, and closes them after.
export function useBrowser() {
  before(async () => {
    await mkdir('tests/e2e/screenshots', { recursive: true });
    rig.web = await startServer();
    rig.browser = await launchChrome();
  });
  after(async () => {
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
export async function open(options = desktop, query = '?debug') {
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
