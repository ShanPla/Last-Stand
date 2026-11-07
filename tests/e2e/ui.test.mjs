// Screens, input and saves, tested in a real browser: every screen is reached the way
// a player reaches it, and the result is read back from the game.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, waitSelected, findButton, clickButton, tapButton, startGame, watchProblems,
  installFakePad, padPress, stickHome
} from './harness.mjs';

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

const desktop = { viewport: { width: 1280, height: 720 } };
const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const phoneLandscape = { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };

const waitPhase = (page, wanted, timeout = 5000) =>
  page.waitForFunction((p) => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === p, wanted, { timeout });

async function open(options = desktop, query = '?debug') {
  return openGame(browser, web.url + query, options);
}

// Opens the level-up cards and waits until their input delay is over.
async function openCards(page) {
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.waitForTimeout(500);
}

const findPrefix = async (page, prefix) => {
  const ui = await readUi(page);
  const button = ui.buttons.find((b) => b.n.startsWith(prefix));
  assert.ok(button, `no button starting with "${prefix}" on ${ui.screen}`);
  return button;
};

// ---- Title ----------------------------------------------------------------------

test('Enter and Space each start a run with one press', async () => {
  for (const key of ['Enter', ' ']) {
    const { context, page, problems } = await open();
    await waitScreen(page, 'title');
    await waitSelected(page, 'PLAY');
    await page.keyboard.press(key);
    await waitScreen(page, 'playing');
    assert.equal((await readUi(page)).demo, 0);
    assert.deepEqual(problems, []);
    await context.close();
  }
});

test('a click on Play starts a run, and a tap on Play does too', async () => {
  let session = await open();
  await waitScreen(session.page, 'title');
  await clickButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  assert.deepEqual(session.problems, []);
  await session.context.close();

  session = await open(phone);
  await waitScreen(session.page, 'title');
  await tapButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  assert.deepEqual(session.problems, []);
  await session.context.close();
});

test('a tap that starts and ends between two frames still presses Play', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  const play = await findButton(page, 'PLAY');
  // Both events run inside one JavaScript task, so no frame can happen between them.
  await page.evaluate(({ x, y }) => {
    const canvas = document.getElementById('canvas');
    const touch = new Touch({ identifier: 7, target: canvas, clientX: x, clientY: y });
    canvas.dispatchEvent(new TouchEvent('touchstart', { bubbles: true, cancelable: true, touches: [touch], targetTouches: [touch], changedTouches: [touch] }));
    canvas.dispatchEvent(new TouchEvent('touchend', { bubbles: true, cancelable: true, touches: [], targetTouches: [], changedTouches: [touch] }));
  }, { x: play.cx, y: play.cy });
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});

test('tapping or clicking elsewhere on the title does not start a run', async () => {
  let session = await open();
  await waitScreen(session.page, 'title');
  await session.page.mouse.click(1100, 200);
  await session.page.mouse.click(700, 650);
  await session.page.waitForTimeout(600);
  assert.equal((await readUi(session.page)).screen, 'title');
  await session.context.close();

  session = await open(phone);
  await waitScreen(session.page, 'title');
  await session.page.touchscreen.tap(195, 60);
  await session.page.touchscreen.tap(60, 780);
  await session.page.waitForTimeout(600);
  assert.equal((await readUi(session.page)).screen, 'title');
  await session.context.close();
});

test('title menu: keyboard moves the selection, and hover follows the mouse', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  // The order is Play, Difficulty, Map, Mode, Settings, How to Play.
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Difficulty');
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Map');
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Mode');
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Settings');
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'How to Play');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await page.waitForTimeout(100);
  await waitSelected(page, 'PLAY');
  const howTo = await findButton(page, 'How to Play');
  await page.mouse.move(howTo.cx, howTo.cy);
  await page.waitForTimeout(150);
  await waitSelected(page, 'How to Play');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'howto');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('How to Play and Settings open from the title and return to it', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'How to Play');
  await waitScreen(page, 'howto');
  await clickButton(page, 'Back');
  await waitScreen(page, 'title');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await clickButton(page, 'Back');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Level-up -------------------------------------------------------------------

test('level-up by keyboard: the arrows move the selection and Enter chooses', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  assert.equal((await readUi(page)).screen, 'levelup');
  await page.waitForTimeout(500);
  await waitSelected(page, 'Card 1');
  await page.keyboard.press('ArrowRight');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Card 2');
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Card 3');
  await page.keyboard.press('ArrowDown');
  await page.waitForTimeout(100);
  await waitSelected(page, 'Reroll');
  await page.keyboard.press('ArrowUp');
  await page.waitForTimeout(100);
  await page.keyboard.press('Enter');
  await waitPhase(page, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('level-up by click and by tap', async () => {
  let session = await open();
  await startGame(session.page);
  await openCards(session.page);
  await clickButton(session.page, 'Card 2');
  await waitPhase(session.page, 0);
  assert.deepEqual(session.problems, []);
  await session.context.close();

  session = await open(phone);
  await waitScreen(session.page, 'title');
  await tapButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  await openCards(session.page);
  await tapButton(session.page, 'Card 3');
  await waitPhase(session.page, 0);
  assert.deepEqual(session.problems, []);
  await session.context.close();
});

test('a click inside the input delay does not choose a card', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await clickButton(page, 'Card 1');
  await page.waitForTimeout(100);
  assert.equal((await readState(page)).phase, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('reroll by key and by click, with a count that runs out', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  assert.equal((await readState(page)).rerolls, 2);
  await page.keyboard.press('r');
  await page.waitForTimeout(150);
  assert.equal((await readState(page)).rerolls, 2, 'R inside the input delay does nothing');
  await page.waitForTimeout(500);
  await page.keyboard.press('r');
  await page.waitForTimeout(150);
  assert.equal((await readState(page)).rerolls, 1);
  assert.match((await findPrefix(page, 'Reroll')).n, /1 left/);
  await page.waitForTimeout(500);
  await page.mouse.click(...(await (async () => { const b = await findPrefix(page, 'Reroll'); return [b.cx, b.cy]; })()));
  await page.waitForTimeout(150);
  assert.equal((await readState(page)).rerolls, 0);
  assert.equal((await findPrefix(page, 'No rerolls')).n, 'No rerolls left');
  await page.waitForTimeout(500);
  await page.keyboard.press('r');
  await page.waitForTimeout(150);
  assert.equal((await readState(page)).rerolls, 0);
  assert.equal((await readState(page)).phase, 1, 'still choosing');
  assert.deepEqual(problems, []);
  await context.close();
});

test('reroll works by tap on a phone', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await openCards(page);
  const reroll = await findPrefix(page, 'Reroll');
  await page.touchscreen.tap(reroll.cx, reroll.cy);
  await page.waitForTimeout(200);
  assert.equal((await readState(page)).rerolls, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Pause ----------------------------------------------------------------------

test('pause with Esc and with P, resume with the key and the button', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  for (const key of ['Escape', 'p']) {
    await page.keyboard.press(key);
    await waitScreen(page, 'paused');
    await waitSelected(page, 'Resume');
    const frozen = (await readState(page)).tick;
    await page.waitForTimeout(400);
    assert.equal((await readState(page)).tick, frozen, 'the simulation stands still while paused');
    await page.keyboard.press(key);
    await waitScreen(page, 'playing');
  }
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Resume');
  await waitScreen(page, 'playing');
  await page.waitForTimeout(300);
  assert.ok((await readState(page)).tick > 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the pause button works for a mouse click and a touch tap', async () => {
  let session = await open();
  await startGame(session.page);
  await clickButton(session.page, 'Pause');
  await waitScreen(session.page, 'paused');
  await session.context.close();

  session = await open(phone);
  await waitScreen(session.page, 'title');
  await tapButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  await tapButton(session.page, 'Pause');
  await waitScreen(session.page, 'paused');
  assert.deepEqual(session.problems, []);
  await session.context.close();
});

test('losing focus or hiding the tab pauses at once', async () => {
  let session = await open();
  await startGame(session.page);
  await session.page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await waitScreen(session.page, 'paused');
  await session.context.close();

  session = await open();
  await startGame(session.page);
  await session.page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await waitScreen(session.page, 'paused');
  assert.deepEqual(session.problems, []);
  await session.context.close();
});

test('losing focus on the title or in a menu does nothing unexpected', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await page.waitForTimeout(200);
  assert.equal((await readUi(page)).screen, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('restart starts a fresh run, and quit returns to the title', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.setKills, 77);
  await debug(page, CMD.setTime, 100);
  await page.waitForTimeout(200);
  assert.ok((await readState(page)).kills >= 77);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Restart');
  await waitScreen(page, 'playing');
  await page.waitForTimeout(200);
  const fresh = await readState(page);
  assert.ok(fresh.kills < 10 && fresh.tick < 120, `a new run starts from zero, got kills ${fresh.kills} tick ${fresh.tick}`);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Quit to Title');
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).demo, 1);
  await waitSelected(page, 'PLAY');
  assert.deepEqual(problems, []);
  await context.close();
});

test('settings opened from pause return to pause', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await findButton(page, 'Back to pause');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await clickButton(page, 'Back to pause');
  await waitScreen(page, 'paused');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Settings and saves ---------------------------------------------------------

test('two clicks that land between the same two frames both count', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  const numbers = await findButton(page, 'Damage numbers');
  const reduced = await findButton(page, 'Reduced motion');
  const before = await readUi(page);
  // Sent in one go, inside one browser task, so that no frame can run between them.
  await page.evaluate(([a, b]) => {
    const canvas = document.getElementById('canvas');
    for (const point of [a, b]) {
      canvas.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0, clientX: point[0], clientY: point[1] }));
      canvas.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, button: 0, clientX: point[0], clientY: point[1] }));
    }
  }, [[numbers.cx, numbers.cy], [reduced.cx, reduced.cy]]);
  await page.waitForTimeout(300);
  const after = await readUi(page);
  assert.equal(after.damageNumbers, before.damageNumbers ? 0 : 1, 'the first click counts');
  assert.equal(after.reducedMotion, before.reducedMotion ? 0 : 1, 'the second click counts');
  assert.deepEqual(problems, []);
  await context.close();
});

test('settings: sliders by mouse drag and keyboard, toggles, and they persist across a reload', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  const music = await findButton(page, 'Music volume');
  // Drag from the middle of the track to the far left edge, then far right.
  await page.mouse.move(music.cx + music.cw * 0.1, music.cy);
  await page.mouse.down();
  await page.mouse.move(music.cx - music.cw * 0.6, music.cy, { steps: 6 });
  await page.mouse.up();
  await page.waitForTimeout(100);
  assert.equal((await readUi(page)).music, 0, 'dragging past the left end gives zero');
  await page.mouse.move(music.cx, music.cy);
  await page.mouse.down();
  await page.mouse.move(music.cx + music.cw * 0.6, music.cy, { steps: 6 });
  await page.mouse.up();
  await page.waitForTimeout(100);
  assert.equal((await readUi(page)).music, 1);
  // Keyboard: move to Effects volume and lower it by two steps.
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Effects volume');
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await page.waitForTimeout(100);
  assert.ok(Math.abs((await readUi(page)).effects - 0.6) < 0.001, 'two steps of 0.1 below the 0.8 default');
  // Screen shake and flash: the same stepping, one row at a time.
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Screen shake');
  await page.keyboard.press('ArrowLeft');
  await page.waitForTimeout(100);
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Flash intensity');
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await page.waitForTimeout(100);
  const midway = await readUi(page);
  assert.ok(Math.abs(midway.shake - 0.9) < 0.001);
  assert.ok(Math.abs(midway.flash - 0.8) < 0.001);
  // Toggles by click.
  await clickButton(page, 'Damage numbers');
  await clickButton(page, 'Reduced motion');
  await page.waitForTimeout(200);
  const changed = await readUi(page);
  assert.equal(changed.damageNumbers, 0);
  assert.equal(changed.reducedMotion, 1);

  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  const after = await readUi(page);
  assert.equal(after.music, 1);
  assert.ok(Math.abs(after.effects - 0.6) < 0.001);
  assert.ok(Math.abs(after.shake - 0.9) < 0.001);
  assert.ok(Math.abs(after.flash - 0.8) < 0.001);
  assert.equal(after.damageNumbers, 0);
  assert.equal(after.reducedMotion, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('settings are usable by touch', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await tapButton(page, 'Settings');
  await waitScreen(page, 'settings');
  const music = await findButton(page, 'Music volume');
  await page.touchscreen.tap(music.cx + music.cw * 0.2, music.cy + music.ch * 0.15);
  await page.waitForTimeout(200);
  const value = (await readUi(page)).music;
  assert.ok(value > 0.5 && value <= 1, `a tap on the slider sets it, got ${value}`);
  await tapButton(page, 'Reduced motion');
  await page.waitForTimeout(150);
  assert.equal((await readUi(page)).reducedMotion, 1);
  await tapButton(page, 'Back');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a corrupt save is ignored, and out of range values are clamped', async () => {
  for (const text of ['\u0000\u0001garbage{{{ music=NaN', 'music=9000\neffects=-4\nshake=nan\nwins=1e99\nbestTime=abc\n', '', 'null']) {
    const context = await browser.newContext(desktop);
    const page = await context.newPage();
    const problems = watchProblems(page);
    await page.addInitScript((value) => { localStorage.setItem('laststand.save', value); }, text);
    await page.goto(web.url + '?debug');
    await waitScreen(page, 'title');
    const ui = await readUi(page);
    assert.ok(ui.music >= 0 && ui.music <= 1 && ui.effects >= 0 && ui.effects <= 1 && ui.shake >= 0 && ui.shake <= 1, `settings stay in range for ${JSON.stringify(text)}`);
    assert.ok(Number.isFinite(ui.bestTime) && ui.bestTime >= 0);
    await clickButton(page, 'PLAY');
    await waitScreen(page, 'playing');
    assert.deepEqual(problems, []);
    await context.close();
  }
  // The clamped values, spelled out for the one with big numbers.
  const context = await browser.newContext(desktop);
  const page = await context.newPage();
  await page.addInitScript(() => { localStorage.setItem('laststand.save', 'music=9000\neffects=-4\nwins=1e99\n'); });
  await page.goto(web.url + '?debug');
  await waitScreen(page, 'title');
  const ui = await readUi(page);
  assert.equal(ui.music, 1);
  assert.equal(ui.effects, 0);
  assert.ok(ui.wins <= 100000000);
  await context.close();
});

test('blocked browser storage does not break the game', async () => {
  const context = await browser.newContext(desktop);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.addInitScript(() => {
    Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('blocked', 'SecurityError'); } });
  });
  await page.goto(web.url + '?debug');
  await waitScreen(page, 'title');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await clickButton(page, 'Reduced motion');
  await clickButton(page, 'Back');
  await waitScreen(page, 'title');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  assert.deepEqual(problems, []);
  await context.close();
});

test('reduced motion starts on when the system asks for it and nothing is saved', async () => {
  const context = await browser.newContext({ ...desktop, reducedMotion: 'reduce' });
  const page = await context.newPage();
  await page.goto(web.url + '?debug');
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).reducedMotion, 1);
  await context.close();

  const plain = await browser.newContext({ ...desktop, reducedMotion: 'no-preference' });
  const page2 = await plain.newPage();
  await page2.goto(web.url + '?debug');
  await waitScreen(page2, 'title');
  assert.equal((await readUi(page2)).reducedMotion, 0);
  await plain.close();
});

// ---- Game over, victory and best run --------------------------------------------

test('game over: a different headline from victory, an input delay, and a fresh press restarts', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.fakeDamage, 5000);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  assert.equal((await readUi(page)).won, 0);
  await page.keyboard.press('Enter');
  await page.waitForTimeout(250);
  assert.equal((await readUi(page)).screen, 'over', 'a press inside the delay does not restart');
  await page.waitForTimeout(800);
  await waitSelected(page, 'RESTART');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.ok((await readState(page)).tick < 60);

  await debug(page, CMD.forceVictory);
  await waitScreen(page, 'over');
  assert.equal((await readUi(page)).won, 1, 'victory is a different screen');
  await page.waitForTimeout(1000);
  await clickButton(page, 'Quit');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a key held down from play does not restart the run, only a fresh press does', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await page.keyboard.down('Enter');
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(1300);
  assert.equal((await readUi(page)).screen, 'over');
  await page.keyboard.up('Enter');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a tap on a phone restarts after the delay, and not before it', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await tapButton(page, 'RESTART');
  await page.waitForTimeout(200);
  assert.equal((await readUi(page)).screen, 'over');
  await page.waitForTimeout(900);
  await tapButton(page, 'RESTART');
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});

test('best run is recorded at the end of a run and survives a reload', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.setKills, 123);
  await debug(page, CMD.setLevel, 8);
  await debug(page, CMD.setTime, 95);
  await page.waitForTimeout(200);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  const first = await readUi(page);
  assert.equal(first.newBest, 1);
  assert.ok(first.bestKills >= 123 && first.bestLevel >= 8 && first.bestTime >= 95 && first.runs === 1);

  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  const stored = await readUi(page);
  assert.ok(stored.bestKills >= 123 && stored.bestLevel >= 8 && stored.bestTime >= 95 && stored.runs === 1);

  // A weaker run does not lower the best, and does not claim to be one.
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  const second = await readUi(page);
  assert.equal(second.newBest, 0);
  assert.equal(second.bestKills, stored.bestKills);
  assert.equal(second.runs, 2);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a win is counted', async () => {
  const { context, page, problems } = await open();
  await startGame(page);
  await debug(page, CMD.forceVictory);
  await waitScreen(page, 'over');
  assert.equal((await readUi(page)).wins, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Touch joystick -------------------------------------------------------------

test('the touch joystick steers from its fixed place and moves the player', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await page.waitForTimeout(400);
  const client = await context.newCDPSession(page);
  const touch = (type, points) => client.send('Input.dispatchTouchEvent', { type, touchPoints: points });
  const before = await readState(page);
  const stick = await stickHome(page);
  await touch('touchStart', [{ x: stick.x, y: stick.y }]);
  await touch('touchMove', [{ x: stick.x + 80, y: stick.y }]);
  await page.waitForTimeout(700);
  assert.equal((await readUi(page)).joystick, 1, 'the joystick is active while a thumb is down');
  await page.screenshot({ path: 'tests/e2e/screenshots/screen-portrait-joystick.png' });
  await touch('touchEnd', []);
  await page.waitForTimeout(200);
  assert.equal((await readUi(page)).joystick, 0);
  const afterMove = await readState(page);
  assert.ok(afterMove.x - before.x > 40, `the player moved right, ${before.x} to ${afterMove.x}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a touch that starts on the pause button presses it and does not start the joystick', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await page.waitForTimeout(400);
  const client = await context.newCDPSession(page);
  const touch = (type, points) => client.send('Input.dispatchTouchEvent', { type, touchPoints: points });
  const pause = await findButton(page, 'Pause');
  const before = await readState(page);
  await touch('touchStart', [{ x: pause.cx, y: pause.cy }]);
  await touch('touchMove', [{ x: pause.cx - 90, y: pause.cy + 120 }]);
  await page.waitForTimeout(500);
  const ui = await readUi(page);
  assert.equal(ui.screen, 'paused');
  assert.equal(ui.joystick, 0);
  await touch('touchEnd', []);
  const afterState = await readState(page);
  assert.ok(Math.abs(afterState.x - before.x) < 6 && Math.abs(afterState.y - before.y) < 6, 'the player did not move');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a touch that starts on a menu button does not start the joystick either', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  const settings = await findButton(page, 'Settings');
  const client = await context.newCDPSession(page);
  await client.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: settings.cx, y: settings.cy }] });
  await page.waitForTimeout(200);
  assert.equal((await readUi(page)).joystick, 0);
  await client.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Layout at every size ---------------------------------------------------------

const insideScreen = (ui, margin = 0) => {
  for (const b of ui.buttons) {
    assert.ok(b.x >= margin - 0.5 && b.y >= margin - 0.5 && b.x + b.w <= ui.width - margin + 0.5 && b.y + b.h <= ui.height - margin + 0.5,
      `${ui.screen}: "${b.n}" at ${b.x.toFixed(0)},${b.y.toFixed(0)} ${b.w.toFixed(0)}x${b.h.toFixed(0)} leaves the ${ui.width}x${ui.height} screen`);
  }
};

const noOverlaps = (ui) => {
  const list = ui.buttons;
  for (let i = 0; i < list.length; i++) {
    for (let j = i + 1; j < list.length; j++) {
      const a = list[i];
      const b = list[j];
      const overlap = a.x < b.x + b.w - 1 && b.x < a.x + a.w - 1 && a.y < b.y + b.h - 1 && b.y < a.y + a.h - 1;
      assert.ok(!overlap, `${ui.screen}: "${a.n}" overlaps "${b.n}"`);
    }
  }
};

// Walks through every screen, and calls check on each.
async function visitEveryScreen(page, check) {
  await waitScreen(page, 'title');
  await page.waitForTimeout(350);
  await check(await readUi(page));
  await debug(page, CMD.showScreen, 3);
  await waitScreen(page, 'howto');
  await page.waitForTimeout(350);
  await check(await readUi(page));
  await debug(page, CMD.showScreen, 2);
  await waitScreen(page, 'settings');
  await page.waitForTimeout(350);
  await check(await readUi(page));
  await debug(page, CMD.showScreen, 0);
  await startGame(page);
  await debug(page, CMD.giveBuild, 1);
  await page.waitForTimeout(300);
  await check(await readUi(page));
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await page.waitForTimeout(350);
  await check(await readUi(page));
  await page.keyboard.press('Escape');
  await waitScreen(page, 'playing');
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.waitForTimeout(500);
  await check(await readUi(page));
  await page.keyboard.press('Enter');
  await waitPhase(page, 0);
  await debug(page, CMD.fakeDamage, 30000);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(1200);
  await check(await readUi(page));
}

for (const [name, options] of [['desktop', desktop], ['portrait phone', phone], ['landscape phone', phoneLandscape]]) {
  test(`every screen fits and nothing overlaps on a ${name}`, async () => {
    const { context, page, problems } = await open(options);
    const minTouch = options.hasTouch ? 40 : 0;
    await visitEveryScreen(page, async (ui) => {
      insideScreen(ui);
      noOverlaps(ui);
      // Big enough targets for a thumb: about 44 CSS pixels, a little less for the corner pause button.
      for (const b of ui.buttons) {
        assert.ok(b.ch >= minTouch * (b.n === 'Pause' ? 0.9 : 1) || b.n.startsWith('Card'), `${ui.screen}: "${b.n}" is only ${b.ch.toFixed(0)} CSS px tall`);
      }
    });
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('safe-area insets keep every screen inside the notch margins', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await debug(page, CMD.setSafeArea, 34);
  await page.waitForTimeout(200);
  const ratio = 2;
  await visitEveryScreen(page, async (ui) => {
    assert.ok(ui.safe.every((v) => Math.abs(v - 34 * ratio) < 1.5), `insets are read as 34 CSS px, got ${ui.safe}`);
    insideScreen(ui);
    for (const b of ui.buttons) {
      assert.ok(b.x >= ui.safe[0] - 0.5 && b.y >= ui.safe[1] - 0.5 && b.x + b.w <= ui.width - ui.safe[2] + 0.5 && b.y + b.h <= ui.height - ui.safe[3] + 0.5,
        `${ui.screen}: "${b.n}" enters the notch area`);
    }
  });
  assert.deepEqual(problems, []);
  await context.close();
});

test('safe-area insets in landscape keep the layout inside too', async () => {
  const { context, page, problems } = await open(phoneLandscape);
  await waitScreen(page, 'title');
  await debug(page, CMD.setSafeArea, 44);
  await page.waitForTimeout(200);
  await visitEveryScreen(page, async (ui) => {
    for (const b of ui.buttons) {
      assert.ok(b.x >= ui.safe[0] - 0.5 && b.y >= ui.safe[1] - 0.5 && b.x + b.w <= ui.width - ui.safe[2] + 0.5 && b.y + b.h <= ui.height - ui.safe[3] + 0.5,
        `${ui.screen}: "${b.n}" enters the notch area`);
    }
  });
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, viewport, ratio] of [
  ['very small portrait', { width: 320, height: 480 }, 1],
  ['very small landscape', { width: 480, height: 320 }, 1],
  ['nearly square', { width: 700, height: 700 }, 1],
  ['ultra wide', { width: 1920, height: 520 }, 1],
  ['very large', { width: 2560, height: 1440 }, 1],
]) {
  test(`layout holds at a ${name} size (${viewport.width}x${viewport.height})`, async () => {
    const { context, page, problems } = await open({ viewport, deviceScaleFactor: ratio });
    await visitEveryScreen(page, async (ui) => {
      insideScreen(ui);
      noOverlaps(ui);
    });
    assert.deepEqual(problems, []);
    await context.close();
  });
}

// ---- Transitions never block input -----------------------------------------------

test('input works during a screen fade, and the first frame of a new screen', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  // Open settings and press Escape as fast as the browser allows: the fade must not eat it.
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'settings');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'title');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await page.keyboard.down('d');
  await page.waitForTimeout(300);
  await page.keyboard.up('d');
  assert.ok((await readState(page)).x > 20, 'movement works right after the run-start fade');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Emulated gamepad ------------------------------------------------------------

// The fake pad (installFakePad, padPress) lives in harness.mjs. Buttons follow the standard layout:
// 0 is A, 1 is B, 3 is Y, 5 is the right shoulder, 9 is Start, 12-15 the D-pad.
test('a gamepad can navigate the title, start, pause, resume, and use the level-up cards', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await installFakePad(page);
  await page.waitForTimeout(300);
  await padPress(page, 13);  // D-pad down.
  await waitSelected(page, 'Difficulty');
  assert.equal((await readUi(page)).device, 'gamepad');
  await padPress(page, 13);
  await waitSelected(page, 'Map');
  await padPress(page, 13);
  await waitSelected(page, 'Mode');
  await padPress(page, 13);
  await waitSelected(page, 'Settings');
  await padPress(page, 12);  // D-pad up, four times.
  await padPress(page, 12);
  await padPress(page, 12);
  await padPress(page, 12);
  await padPress(page, 0);   // A starts.
  await waitScreen(page, 'playing');
  await padPress(page, 9);   // Start pauses.
  await waitScreen(page, 'paused');
  await padPress(page, 1);   // B resumes.
  await waitScreen(page, 'playing');
  await debug(page, CMD.levelUp);
  await waitPhase(page, 1);
  await page.waitForTimeout(500);
  await padPress(page, 15);  // Right.
  await waitSelected(page, 'Card 2');
  await padPress(page, 3);   // Y rerolls.
  await page.waitForTimeout(150);
  assert.equal((await readState(page)).rerolls, 1);
  await page.waitForTimeout(500);
  await padPress(page, 0);   // A chooses.
  await waitPhase(page, 0);
  assert.deepEqual(problems, []);
  await context.close();
});
