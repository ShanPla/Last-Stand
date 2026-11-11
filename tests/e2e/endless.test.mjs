// Endless mode, tested in a real browser: the Mode selector works by keyboard, mouse and
// touch, Play stays one press, the boss comes back after it dies, the game over screen shows
// the Endless figures, and Endless best runs are kept apart from Story's.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {
  startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, waitSelected, findButton, clickButton, tapButton,
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

const STORY = 0;
const ENDLESS = 1;
const NORMAL = 1;
const killBoss = 47;

const desktop = { viewport: { width: 1280, height: 720 } };
const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };
const sizes = [
  ['desktop', desktop],
  ['xga', { viewport: { width: 1024, height: 768 } }],
  ['phone', phone],
  ['phone-landscape', { viewport: { width: 844, height: 390 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true }],
  ['small-phone', { viewport: { width: 320, height: 568 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true }],
];

const open = (options = desktop) => openGame(browser, `${web.url}?debug`, options);

// Screenshots for a person to look at, only when a folder is given.
async function shot(page, name) {
  if (!process.env.LS_SHOT_DIR) return;
  await page.screenshot({ path: path.join(process.env.LS_SHOT_DIR, `endless-${name}.png`) });
}

const waitMode = (page, wanted) =>
  page.waitForFunction((m) => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).mode === m, wanted, { timeout: 10000 });

// Opens the game with a saved text already in storage, as an earlier visit would leave it.
async function openWithSave(options, text) {
  const context = await browser.newContext(options);
  const page = await context.newPage();
  const problems = [];
  page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
  page.on('pageerror', (e) => problems.push(String(e)));
  await page.addInitScript((value) => { if (!sessionStorage.getItem('seeded')) { localStorage.setItem('laststand.save', value); sessionStorage.setItem('seeded', '1'); } }, text);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  return { context, page, problems };
}

async function reopenTitle(page) {
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
}

test('keyboard: the Mode row sits after Map, left and right change it, and Play is one press', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  assert.equal((await readUi(page)).mode, STORY, 'Story is the default');
  for (const row of ['Difficulty', 'Map', 'Mode']) {
    await page.keyboard.press('ArrowDown');
    await waitSelected(page, row);
  }
  await page.keyboard.press('ArrowRight');
  await waitMode(page, ENDLESS);
  await page.keyboard.press('ArrowLeft');
  await waitMode(page, STORY);
  // Enter on the row steps forward, and does not start a run.
  await page.keyboard.press('Enter');
  await waitMode(page, ENDLESS);
  assert.equal((await readUi(page)).screen, 'title');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Co-op');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Settings');
  for (let i = 0; i < 5; i++) await page.keyboard.press('ArrowUp');
  await waitSelected(page, 'PLAY');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  const state = await readState(page);
  assert.equal(state.mode, ENDLESS, 'the run is an Endless run');
  assert.equal(state.bossesKilled, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('mouse: the arrows and the middle change the mode, and the choice is saved', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Mode >');
  await waitMode(page, ENDLESS);
  await clickButton(page, 'Mode <');
  await waitMode(page, STORY);
  await clickButton(page, 'Mode');
  await waitMode(page, ENDLESS);
  assert.equal((await readUi(page)).screen, 'title', 'none of this starts a run');
  await reopenTitle(page);
  assert.equal((await readUi(page)).mode, ENDLESS, 'the last mode used comes back after a reload');
  // One press from here plays Endless.
  await clickButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).mode, ENDLESS);
  assert.deepEqual(problems, []);
  await context.close();
});

test('touch: a tap changes the mode on a phone, and one tap on Play starts it', async () => {
  const { context, page, problems } = await open(phone);
  await waitScreen(page, 'title');
  await page.waitForTimeout(200);
  await tapButton(page, 'Mode >');
  await waitMode(page, ENDLESS);
  for (const part of ['Mode <', 'Mode', 'Mode >']) {
    const b = await findButton(page, part);
    assert.ok(b.ch >= 40, `${part} is ${b.ch.toFixed(0)} CSS px tall`);
    assert.ok(b.cw >= 40, `${part} is ${b.cw.toFixed(0)} CSS px wide`);
  }
  await tapButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).mode, ENDLESS);
  assert.deepEqual(problems, []);
  await context.close();
});

test('Endless: the boss returns after it dies, and the game over screen shows the Endless figures', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Mode >');
  await waitMode(page, ENDLESS);
  await clickButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await debug(page, CMD.holdSim, 1);
  await debug(page, CMD.setSeed, 5151);
  await debug(page, CMD.holdSim, 1);

  // The first boss, and its death: the run goes on.
  await debug(page, CMD.setTime, 599);
  await debug(page, CMD.runSteps, 90);
  let state = await readState(page);
  assert.equal(state.bossSpawned, 1, 'the first boss is out at 10:00');
  await debug(page, killBoss);
  await debug(page, CMD.runSteps, 1);
  state = await readState(page);
  assert.equal(state.phase, 0, 'killing the boss is not a win in Endless');
  assert.equal(state.bossesKilled, 1);
  assert.equal(state.bossSpawned, 0);
  assert.equal((await readUi(page)).screen, 'playing');

  // It returns at 20:00, and its bar shows again.
  await debug(page, CMD.setTime, 1199);
  await debug(page, CMD.runSteps, 90);
  state = await readState(page);
  assert.equal(state.bossSpawned, 1, 'the boss returns at 20:00');
  assert.ok(state.bossHp > 0.9, `the returning boss has its full bar, ${state.bossHp}`);
  await page.waitForTimeout(150);
  await shot(page, 'hud-return');

  // The clock past an hour.
  await debug(page, CMD.setTime, 3700);
  await page.waitForTimeout(150);
  await shot(page, 'hud-hour');

  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  await page.waitForTimeout(900);
  const ui = await readUi(page);
  assert.equal(ui.won, 0);
  assert.equal(ui.summaryMode, ENDLESS);
  assert.equal(ui.summaryBosses, 1);
  assert.ok(Math.abs(ui.summaryTime - 3700) < 2, `time survived ${ui.summaryTime}`);
  assert.equal(ui.newBestTime, 1, 'the first Endless run is a new best by time');
  assert.equal(ui.newBestKills, 0, 'Endless only compares time');
  assert.equal(ui.newBestLevel, 0);
  assert.equal(ui.endlessBests[NORMAL][4], 1, 'one Endless run on Normal');
  assert.equal(ui.endlessBests[NORMAL][3], 1, 'its most bosses killed');
  assert.equal(ui.bests[NORMAL][4], 0, 'no Story run was counted');
  await shot(page, 'gameover');

  // Back on the title, the best run shown is Endless's, because Endless is selected.
  await clickButton(page, 'Quit');
  await waitScreen(page, 'title');
  const title = await readUi(page);
  assert.equal(title.runs, 1);
  assert.ok(Math.abs(title.bestTime - 3700) < 2);
  assert.deepEqual(problems, []);
  await context.close();
});

test('Story: killing the boss still wins, and does not touch the Endless records', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.holdSim, 1);
  await debug(page, CMD.setTime, 599);
  await debug(page, CMD.runSteps, 90);
  await debug(page, killBoss);
  await debug(page, CMD.runSteps, 1);
  await debug(page, CMD.holdSim, 0);
  await waitScreen(page, 'over');
  const ui = await readUi(page);
  assert.equal(ui.won, 1);
  assert.equal(ui.summaryMode, STORY);
  assert.equal(ui.bests[NORMAL][3], 1, 'a Story win');
  assert.equal(ui.endlessBests[NORMAL][4], 0);
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, options] of sizes) {
  test(`title layout with three selectors fits at ${name}, best run included`, async () => {
    // An Endless best run, so the panel shows its full figures.
    const { context, page, problems } = await openWithSave(options,
      'version=1\nmode=1\nendless.bestTime=1834\nendless.mostKills=5120\nendless.highestLevel=31\nendless.bosses=2\nendless.runs=4\n');
    await page.waitForTimeout(400);
    const ui = await readUi(page);
    assert.equal(ui.mode, ENDLESS);
    const [left, top, right, bottom] = ui.safeRect;
    for (const b of ui.buttons) {
      assert.ok(b.x >= left - 1 && b.y >= top - 1 && b.x + b.w <= right + 1 && b.y + b.h <= bottom + 1, `${b.n} is inside the safe area`);
    }
    const best = ui.extents.find((e) => e.n === 'best run');
    assert.ok(best, 'the best run panel is shown');
    assert.ok(best.y + best.h <= bottom + 1, 'the best run panel is on screen');
    for (const b of ui.buttons) {
      const overlaps = b.x < best.x + best.w && best.x < b.x + b.w && b.y < best.y + best.h && best.y < b.y + b.h;
      assert.ok(!overlaps, `${b.n} does not cover the best run`);
    }
    await shot(page, `title-${name}`);
    assert.deepEqual(problems, []);
    await context.close();
  });
}
