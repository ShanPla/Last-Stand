// Hell, the fourth level, tested in a real browser: it can be chosen and played, it brings more
// zombies than Hard, its best run is kept apart from the others, and old saves still load.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, readState, readUi, debug, CMD, waitScreen, clickButton } from './harness.mjs';

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

const HARD = 2;
const HELL = 3;
const desktop = { viewport: { width: 1280, height: 720 } };

const open = (options = desktop) => openGame(browser, `${web.url}?debug`, options);

const waitDifficulty = (page, wanted) =>
  page.waitForFunction((d) => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).difficulty === d, wanted, { timeout: 10000 });

async function reopenTitle(page) {
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
}

// Opens the game with a saved text already in storage, as an earlier visit would leave it.
async function openWithSave(text) {
  const context = await browser.newContext(desktop);
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

// Plays the saved level with a fixed seed for a while, and reports how many zombies are out.
async function zombiesAfter(level) {
  const { context, page, problems } = await openWithSave(`version=1
difficulty=${level}
`);
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  await debug(page, CMD.setSeed, 4242);
  await debug(page, CMD.setTime, 400);
  await debug(page, CMD.runSteps, 300);
  const state = await readState(page);
  assert.equal(state.difficulty, level);
  assert.deepEqual(problems, []);
  await context.close();
  return state.enemies;
}

test('Hell is one step after Hard, plays as a Hell run, and Play stays one press', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('ArrowRight');
  await waitDifficulty(page, HARD);
  await page.keyboard.press('ArrowRight');
  await waitDifficulty(page, HELL);
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'playing');
  assert.equal((await readState(page)).difficulty, HELL);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a Hell run has more zombies than a Hard run of the same seed', async () => {
  const hard = await zombiesAfter(HARD);
  const hell = await zombiesAfter(HELL);
  assert.ok(hard > 20, `Hard had ${hard} zombies, so the comparison means something`);
  assert.ok(hell > hard * 1.3, `Hell had ${hell} zombies and Hard had ${hard}`);
});

test('the Hell best run is kept apart, survives a reload, and shows on the title', async () => {
  const { context, page, problems } = await open();
  await waitScreen(page, 'title');
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HARD);
  await clickButton(page, 'Difficulty >');
  await waitDifficulty(page, HELL);
  await clickButton(page, 'PLAY');
  await waitScreen(page, 'playing');
  await debug(page, CMD.setKills, 123);
  await debug(page, CMD.setLevel, 7);
  await debug(page, CMD.setTime, 150);
  await page.waitForTimeout(200);
  await debug(page, CMD.forceDeath);
  await waitScreen(page, 'over');
  let ui = await readUi(page);
  assert.equal(ui.summaryDifficulty, HELL);
  assert.equal(ui.newBest, 1);
  assert.equal(ui.bests[HELL][4], 1);
  assert.ok(ui.bests[HELL][1] >= 123);
  for (const level of [0, 1, HARD]) assert.equal(ui.bests[level][4], 0, `level ${level} was not touched`);

  await page.waitForTimeout(500);
  await reopenTitle(page);
  ui = await readUi(page);
  assert.equal(ui.difficulty, HELL);
  assert.equal(ui.runs, 1, 'the title shows the Hell figures while Hell is selected');
  assert.ok(ui.bestKills >= 123);
  // Hard has nothing, and the title shows that.
  await clickButton(page, 'Difficulty <');
  await waitDifficulty(page, HARD);
  assert.equal((await readUi(page)).runs, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a save with Hell data and a save from before Hell both load', async () => {
  for (const [text, hellRuns, hardRuns] of [
    ['version=1\ndifficulty=3\nhell.bestTime=200.00\nhell.mostKills=50\nhell.highestLevel=5\nhell.wins=0\nhell.runs=4\nhard.runs=2\n', 4, 2],
    ['version=1\ndifficulty=2\nhard.bestTime=90.00\nhard.mostKills=30\nhard.runs=9\n', 0, 9],
  ]) {
    const { context, page, problems } = await openWithSave(text);
    const ui = await readUi(page);
    assert.equal(ui.bests[HELL][4], hellRuns);
    assert.equal(ui.bests[HARD][4], hardRuns);
    await page.keyboard.press('Enter');
    await waitScreen(page, 'playing');
    assert.deepEqual(problems, []);
    await context.close();
  }
});
