// The difficulty selector on the title screen, tested in a real browser: every way to
// change it, that Play stays one press, what is saved, and that best runs are kept per level.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, waitSelected, findButton, clickButton, tapButton,
  installFakePad, padPress,
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

const EASY = 0;
const NORMAL = 1;
const HARD = 2;

const open = (options = desktop, query = '?debug') => openGame(browser, web.url + query, options);

const waitDifficulty = (page, wanted, timeout = 10000) =>
  page.waitForFunction((d) => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).difficulty === d, wanted, { timeout });

async function reopenTitle(page) {
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
}

// Opens the game with a saved text already in storage.
async function openWithSave(text, options = desktop) {
  const context = await browser.newContext(options);
  const page = await context.newPage();
  const problems = [];
  page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
  page.on('pageerror', (e) => problems.push(String(e)));
  await page.addInitScript((value) => { if (!sessionStorage.getItem('seeded')) { localStorage.setItem('laststand.save', value); sessionStorage.setItem('seeded', '1'); } }, text);
  await page.goto(web.url + '?debug');
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  return { context, page, problems };
}

// ---- Changing it ---------------------------------------------------------------------

test('the title starts on Play with Normal chosen, and the selector sits second in the focus order', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await waitSelected(page, 'PLAY');
  const ui = await readUi(page);
  assert.equal(ui.difficulty, NORMAL);
  const names = ui.buttons.map((b) => b.n);
  const top = (name) => ui.buttons.find((b) => b.n === name).y;
  assert.ok(names.includes('Difficulty') && names.includes('Difficulty <') && names.includes('Difficulty >'));
  assert.ok(top('PLAY') < top('Difficulty') && top('Difficulty') < top('Settings') && top('Settings') < top('How to Play'));
  // Left and right do nothing while Play has the focus.
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowLeft');
  await page.waitForTimeout(150);
  assert.equal((await readUi(page)).difficulty, NORMAL);
  assert.deepEqual(problems, []);
  await context.close();
});

test('keyboard: left and right change the level while the selector has focus, and wrap', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Difficulty');
  await page.keyboard.press('ArrowRight');
  await waitDifficulty(page, HARD);
  await page.keyboard.press('ArrowRight');
  await waitDifficulty(page, EASY);
  await page.keyboard.press('ArrowLeft');
  await waitDifficulty(page, HARD);
  await page.keyboard.press('ArrowLeft');
  await waitDifficulty(page, NORMAL);
  await page.keyboard.press('d');
  await waitDifficulty(page, HARD);
  await page.keyboard.press('a');
  await waitDifficulty(page, NORMAL);
  // Enter on the row steps forward, and does not start a run.
  await page.keyboard.press('Enter');
  await waitDifficulty(page, HARD);
  assert.equal((await readUi(page)).screen, 'title');
  // The focus moves on as normal.
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Map');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Settings');
  await page.keyboard.press('ArrowRight');
  await page.waitForTimeout(150);
  assert.equal((await readUi(page)).difficulty, HARD, 'left and right do nothing on other rows');
  assert.deepEqual(problems, []);
  await context.close();
});

test('mouse: a click on an arrow steps that way, and on the middle steps forward', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, NORMAL);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, EASY);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, HARD);
  await clickButton(page, 'Difficulty');
  await waitDifficulty(page, EASY);
  assert.equal((await readUi(page)).screen, 'title', 'none of this starts a run');
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, options] of [['portrait phone', phone], ['landscape phone', phoneLandscape]]) {
  test(`touch: a tap on an arrow changes the level on a ${name}`, async () => {
    const { context, page, problems } = await open(options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(200);
    await tapButton(page, 'Difficulty >');
    await waitDifficulty(page, HARD);
    await tapButton(page, 'Difficulty <');
    await waitDifficulty(page, NORMAL);
    await tapButton(page, 'Difficulty <');
    await waitDifficulty(page, EASY);
    await tapButton(page, 'Difficulty');
    await waitDifficulty(page, NORMAL);
    assert.equal((await readUi(page)).screen, 'title');
    for (const part of ['Difficulty <', 'Difficulty', 'Difficulty >']) {
      const b = await findButton(page, part);
      assert.ok(b.ch >= 40, `${part} is ${b.ch.toFixed(0)} CSS px tall`);
      assert.ok(b.cw >= 40, `${part} is ${b.cw.toFixed(0)} CSS px wide`);
    }
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('gamepad: the D-pad moves to the row and changes the level', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await installFakePad(page);
  await page.waitForTimeout(300);
  await padPress(page, 13);  // Down.
  await waitSelected(page, 'Difficulty');
  await padPress(page, 15);  // Right.
  await waitDifficulty(page, HARD);
  await padPress(page, 14);  // Left.
  await padPress(page, 14);
  await waitDifficulty(page, EASY);
  await padPress(page, 0);   // A steps forward and does not start.
  await waitDifficulty(page, NORMAL);
  assert.equal((await readUi(page)).screen, 'title');
  await padPress(page, 12);  // Up, back to Play.
  await waitSelected(page, 'PLAY');
  await padPress(page, 0);
  await waitScreen(page, 'playing');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Play stays one press ----------------------------------------------------------------

test('Play is one press with the chosen level: Enter, click and tap', async () => {
  // Keyboard.
  let session = await open();
  await waitScreen(session.page, 'title');
  await session.page.keyboard.press('ArrowDown');
  await session.page.keyboard.press('ArrowRight');
  await waitDifficulty(session.page, HARD);
  await session.page.keyboard.press('ArrowUp');
  await waitSelected(session.page, 'PLAY');
  await session.page.keyboard.press('Enter');
  await waitScreen(session.page, 'playing');
  assert.equal((await readState(session.page)).difficulty, HARD);
  assert.deepEqual(session.problems, []);
  await session.context.close();

  // A key press on Play with nothing changed starts a Normal run at once, as before.
  session = await open();
  await waitScreen(session.page, 'title');
  await session.page.keyboard.press('Enter');
  await waitScreen(session.page, 'playing');
  assert.equal((await readState(session.page)).difficulty, NORMAL);
  await session.context.close();

  // Mouse.
  session = await open();
  await waitScreen(session.page, 'title');
  await clickButton(session.page, 'Difficulty <');
  await waitDifficulty(session.page, EASY);
  await clickButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  assert.equal((await readState(session.page)).difficulty, EASY);
  await session.context.close();

  // Touch.
  session = await open(phone);
  await waitScreen(session.page, 'title');
  await session.page.waitForTimeout(200);
  await tapButton(session.page, 'Difficulty >');
  await waitDifficulty(session.page, HARD);
  await tapButton(session.page, 'PLAY');
  await waitScreen(session.page, 'playing');
  assert.equal((await readState(session.page)).difficulty, HARD);
  assert.deepEqual(session.problems, []);
  await session.context.close();
});

test('the title demo behind the menu stays Normal whatever is chosen', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  await page.waitForTimeout(300);
  assert.equal((await readState(page)).difficulty, NORMAL);
  assert.equal((await readUi(page)).demo, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('restart keeps the level, and so does going back to the title', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  await clickButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  // Restart from the pause menu.
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Restart');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).difficulty, HARD);
  // Restart from the game over screen.
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(2200);
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).difficulty, HARD);
  // Back to the title: the selector still shows it.
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(2200);
  await clickButton(page, 'Quit');
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).difficulty, HARD);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Saving --------------------------------------------------------------------------------

test('the chosen level persists across a reload', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  await page.waitForTimeout(300);
  await reopenTitle(page);
  assert.equal((await readUi(page)).difficulty, HARD);
  // One click at a time: two presses inside one slow frame would count as one.
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, NORMAL);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, EASY);
  await page.waitForTimeout(300);
  await reopenTitle(page);
  assert.equal((await readUi(page)).difficulty, EASY);
  // And a run started now is an Easy one.
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).difficulty, EASY);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a corrupt or out of range difficulty is safe', async () => {
  const cases = [
    ['difficulty=banana\n', NORMAL],
    ['difficulty=\n', NORMAL],
    ['difficulty=NaN\n', NORMAL],
    ['difficulty=99\n', HARD],
    ['difficulty=-5\n', EASY],
    ['difficulty=1e99\n', HARD],
    ['\u0000\u0001garbage{{{ difficulty=2', NORMAL],
    ['difficulty=0\n', EASY],
  ];
  for (const [text, expected] of cases) {
    const { context, page, problems } = await openWithSave(text);
    const ui = await readUi(page);
    assert.equal(ui.difficulty, expected, `save ${JSON.stringify(text)}`);
    await page.keyboard.press('Enter');
    await waitScreen(page, 'playing');
    assert.equal((await readState(page)).difficulty, expected);
    assert.deepEqual(problems, []);
    await context.close();
  }
});

test('an old save with one best run counts as Normal', async () => {
  const { context, page, problems } = await openWithSave('version=1\nmusic=0.5\nbestTime=300.00\nmostKills=250\nhighestLevel=9\nwins=1\nruns=6\n');
  const ui = await readUi(page);
  assert.equal(ui.difficulty, NORMAL);
  assert.equal(ui.bestKills, 250);
  assert.equal(ui.bestLevel, 9);
  assert.equal(ui.runs, 6);
  assert.deepEqual(ui.bests[NORMAL], [300, 250, 9, 1, 6]);
  assert.equal(ui.bests[EASY][4], 0);
  assert.equal(ui.bests[HARD][4], 0);
  // The title shows the best of the level that is selected.
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  assert.equal((await readUi(page)).bestKills, 0);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, NORMAL);
  assert.equal((await readUi(page)).bestKills, 250);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Best runs per level ---------------------------------------------------------------------

test('best runs are kept per level, and a new best only compares within the level', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');

  // A strong Normal run.
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.setKills, 300);
  await debug(page, CMD.setLevel, 12);
  await debug(page, CMD.setTime, 200);
  await page.waitForTimeout(200);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  let ui = await readUi(page);
  assert.equal(ui.newBest, 1);
  assert.equal(ui.summaryDifficulty, NORMAL);
  assert.equal(ui.bests[NORMAL][4], 1);
  assert.ok(ui.bests[NORMAL][1] >= 300);
  assert.equal(ui.bests[HARD][4], 0);

  // A much weaker Hard run is still a best, because Hard has none yet.
  await debug(page, CMD.showScreen, 0);
  await waitScreen(page, 'title');
  await page.waitForTimeout(300);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, EASY);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, HARD);
  await clickButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await debug(page, CMD.setKills, 20);
  await debug(page, CMD.setLevel, 3);
  await debug(page, CMD.setTime, 40);
  await page.waitForTimeout(200);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  ui = await readUi(page);
  assert.equal(ui.newBest, 1, 'the first Hard run is a best on Hard');
  assert.equal(ui.summaryDifficulty, HARD);
  assert.equal(ui.bests[HARD][4], 1);
  assert.ok(ui.bests[HARD][1] >= 20 && ui.bests[HARD][1] < 300);
  assert.ok(ui.bests[NORMAL][1] >= 300, 'the Normal best was not touched');
  assert.equal(ui.bests[NORMAL][4], 1);
  const hardKills = ui.bests[HARD][1];

  // A weaker second Hard run is not a best, even though it beats nothing on Easy.
  await page.waitForTimeout(2200);
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.setKills, 2);
  await page.waitForTimeout(200);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  ui = await readUi(page);
  assert.equal(ui.newBest, 0);
  assert.equal(ui.bests[HARD][1], hardKills);
  assert.equal(ui.bests[HARD][4], 2);

  // Wins are counted on their level too.
  await page.waitForTimeout(2200);
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.forceVictory);
  await waitScreen(page, 'over');
  ui = await readUi(page);
  assert.equal(ui.bests[HARD][3], 1);
  assert.equal(ui.bests[NORMAL][3], 0);

  // Everything survives a reload, and the title shows the level that is selected.
  await page.waitForTimeout(500);
  await reopenTitle(page);
  ui = await readUi(page);
  assert.equal(ui.difficulty, HARD);
  assert.equal(ui.bests[HARD][4], 3);
  assert.equal(ui.bests[NORMAL][4], 1);
  assert.equal(ui.bestKills, ui.bests[HARD][1]);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, NORMAL);
  assert.ok((await readUi(page)).bestKills >= 300);
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, EASY);
  assert.equal((await readUi(page)).bestKills, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Layout ----------------------------------------------------------------------------------

for (const [name, options] of [
  ['desktop', desktop],
  ['portrait phone', phone],
  ['landscape phone', phoneLandscape],
  ['very small portrait', { viewport: { width: 320, height: 480 } }],
  ['very small landscape', { viewport: { width: 480, height: 320 } }],
  ['nearly square', { viewport: { width: 700, height: 700 } }],
  ['ultra wide', { viewport: { width: 1920, height: 520 } }],
  ['very large', { viewport: { width: 2560, height: 1440 } }],
]) {
  test(`the title with the selector fits and nothing overlaps: ${name}`, async () => {
    const { context, page, problems } = await open(options);
    await waitScreen(page, 'title');
    await page.waitForTimeout(400);
    const ui = await readUi(page);
    const list = ui.buttons;
    for (const b of list) {
      assert.ok(b.x >= -0.5 && b.y >= -0.5 && b.x + b.w <= ui.width + 0.5 && b.y + b.h <= ui.height + 0.5, `"${b.n}" is inside the screen`);
    }
    for (let i = 0; i < list.length; i++) {
      for (let j = i + 1; j < list.length; j++) {
        const a = list[i];
        const b = list[j];
        const overlap = a.x < b.x + b.w - 1 && b.x < a.x + a.w - 1 && a.y < b.y + b.h - 1 && b.y < a.y + a.h - 1;
        assert.ok(!overlap, `"${a.n}" overlaps "${b.n}"`);
      }
    }
    const get = (n) => list.find((b) => b.n === n);
    assert.ok(get('Difficulty <').x + get('Difficulty <').w <= get('Difficulty').x + 1);
    assert.ok(get('Difficulty').x + get('Difficulty').w <= get('Difficulty >').x + 1);
    assert.ok(get('PLAY').y + get('PLAY').h <= get('Difficulty').y && get('Difficulty').y + get('Difficulty').h <= get('Settings').y);
    if (options.hasTouch) {
      for (const n of ['Difficulty <', 'Difficulty', 'Difficulty >']) assert.ok(get(n).ch >= 40, `${n} is tall enough for a thumb`);
    }
    // The row is no narrower than Play's column, so it lines up with the other buttons.
    assert.ok(Math.abs(get('Difficulty <').x - get('PLAY').x) < 1.5);
    assert.ok(Math.abs(get('Difficulty >').x + get('Difficulty >').w - (get('PLAY').x + get('PLAY').w)) < 1.5);
    assert.deepEqual(problems, []);
    await context.close();
  });
}
