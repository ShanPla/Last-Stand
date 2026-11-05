// Checks of the pixel world renderer: the ground is repeatable, and the view survives
// any window shape and any amount of walking.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startServer, launchChrome, openGame as openPage, startGame, watchProblems, debug, CMD } from './harness.mjs';

// Debug command numbers. Edit these here if the app's numbering changes.
const kSetSeed = 3;
const kShowcase = 17;      // Value -1: an empty world, with the clock stopped.
const kSetRunTime = 12;    // In seconds.
const kMovePlayerX = 18;
const kMovePlayerY = 19;
const kSetQuality = 20;

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

// The game opens on the title screen, and these checks are about the world in a real run.
async function openGame(browserToUse, url, options) {
  const opened = await openPage(browserToUse, url, options);
  await startGame(opened.page);
  return opened;
}

const command = (page, id, value) => page.evaluate(([c, v]) => Module._ls_debug_command(c, v), [id, value]);
const readState = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));

// Reads a box of the screenshot as raw RGBA bytes, so pictures can be compared.
async function readBox(page, box) {
  const base64 = (await page.screenshot()).toString('base64');
  return page.evaluate(async ({ base64, box }) => {
    const blob = await (await fetch(`data:image/png;base64,${base64}`)).blob();
    const image = await createImageBitmap(blob);
    const canvas = new OffscreenCanvas(image.width, image.height);
    const context = canvas.getContext('2d');
    context.drawImage(image, 0, 0);
    return Array.from(context.getImageData(box.x, box.y, box.w, box.h).data);
  }, { base64, box });
}

// The share of pixels that differ by more than a small amount.
function difference(a, b) {
  let changed = 0;
  for (let i = 0; i < a.length; i += 4) {
    if (Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]) > 12) changed++;
  }
  return changed / (a.length / 4);
}

// Away from the HUD and the soldier in the middle of the screen.
const groundBox = { x: 40, y: 400, w: 400, h: 240 };

async function groundAt(page, seed, x, y) {
  await command(page, kSetSeed, seed);
  await command(page, kShowcase, -1);
  await command(page, kMovePlayerX, x);
  await command(page, kMovePlayerY, y);
  await page.waitForTimeout(400);
  return readBox(page, groundBox);
}

test('the same seed gives the same ground at the same place, and another seed differs', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  const first = await groundAt(page, 1234, 2000, -1500);
  // Walk away so the chunks get replaced, then come back and paint them again.
  await groundAt(page, 1234, -9000, 7000);
  const again = await groundAt(page, 1234, 2000, -1500);
  assert.equal(difference(first, again), 0, 'the same seed must paint exactly the same pixels');
  const other = await groundAt(page, 4321, 2000, -1500);
  assert.ok(difference(first, other) > 0.2, 'a different seed must paint a different city');
  assert.deepEqual(problems, []);
  await context.close();
});

test('nearby places on the map differ, so the city does not visibly repeat', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  const a = await groundAt(page, 77, 0, 0);
  // One block away on the road grid, where a repeating tile would line up exactly.
  const b = await groundAt(page, 77, 640, 0);
  const c = await groundAt(page, 77, 0, 640);
  assert.ok(difference(a, b) > 0.2, 'blocks side by side must differ');
  assert.ok(difference(a, c) > 0.2, 'blocks above each other must differ');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the picture changes from early to late in a run', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  await command(page, kSetSeed, 99);
  await command(page, kShowcase, -1);
  await page.waitForTimeout(300);
  const early = await readBox(page, groundBox);
  await command(page, kSetRunTime, 420);
  await page.waitForTimeout(300);
  const late = await readBox(page, groundBox);
  assert.ok(difference(early, late) > 0.8, 'night must look different from the afternoon');
  const average = (bytes) => {
    let sum = 0;
    for (let i = 0; i < bytes.length; i += 4) sum += bytes[i] + bytes[i + 1] + bytes[i + 2];
    return sum / (bytes.length / 4) / 3;
  };
  assert.ok(average(late) < average(early) * 0.7, 'night must be darker than the afternoon');
  assert.deepEqual(problems, []);
  await context.close();
});

test('walking far in every direction keeps drawing ground and raises no errors', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 640, height: 360 } });
  await command(page, kSetSeed, 5);
  await command(page, kShowcase, -1);
  const places = [[60000, 0], [-60000, 0], [0, 60000], [0, -60000], [-140000, -140000], [140000, 140000]];
  for (const [x, y] of places) {
    await command(page, kMovePlayerX, x);
    await command(page, kMovePlayerY, y);
    await page.waitForTimeout(200);
    const state = await readState(page);
    assert.equal(state.x, x);
    const box = await readBox(page, { x: 0, y: 200, w: 200, h: 150 });
    const colors = new Set();
    for (let i = 0; i < box.length; i += 4) colors.add((box[i] << 16) | (box[i + 1] << 8) | box[i + 2]);
    assert.ok(colors.size > 8, `the ground at ${x},${y} must have detail, found ${colors.size} colors`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

// The ground is painted in square chunks, and the game keeps their size in one place. The
// test reads it from there, so it cannot drift from the game by keeping its own copy. If
// the line is reworded the test fails loudly rather than guessing.
function readChunkSize() {
  const header = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../src/components/Maps/Maps.h');
  const match = /kChunkWorldSize\s*=\s*([\d.]+)f?\s*;/.exec(readFileSync(header, 'utf8'));
  assert.ok(match, 'could not find kChunkWorldSize in Maps.h');
  return { size: Number(match[1]), header };
}

test('real walking with the keyboard crosses chunk borders with no errors', async () => {
  const { size: chunkSize, header } = readChunkSize();
  assert.ok(chunkSize >= 64 && chunkSize <= 4096, `chunk size ${chunkSize} looks wrong`);
  // The built game is only as new as its last build. A chunk size changed since then would
  // make the test walk to borders that the old build does not have.
  const built = statSync(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../build/web/index.wasm')).mtimeMs;
  assert.ok(built >= statSync(header).mtimeMs, 'Maps.h is newer than the web build: rebuild the game before running this test');

  // ?debug is only used to cause a level-up on demand. All movement below is real keys.
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 640, height: 360 } });
  const chunkOf = (value) => Math.floor(value / chunkSize);
  const start = await readState(page);
  assert.equal(chunkOf(start.x), 0);
  assert.equal(chunkOf(start.y), 0);

  // Waits, by polling the game's own state, until the level-up cards are showing (true) or
  // gone (false).
  const waitCards = (showing, timeout) => page.waitForFunction(
    (wanted) => (JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).phase === 1) === wanted, showing, { timeout });

  // Answers the level-up cards with the keyboard, as a player would. The cards ignore input
  // for a moment after they open, so Enter is pressed again until one is taken. A screen
  // that never accepts a choice fails here at once, not after the walk's long timeout.
  let levelUpsAnswered = 0;
  const answerCards = async () => {
    for (let attempt = 0; attempt < 20; attempt++) {
      await page.keyboard.press('Enter');
      try {
        await waitCards(false, 400);
        levelUpsAnswered++;
        return;
      } catch { /* Still showing the cards: press again. */ }
    }
    assert.fail('the level-up cards never accepted a choice');
  };

  // Holds a key until the player is past a limit on one axis. Straight after the key goes
  // down, a level-up is forced open and answered, and the walk must carry on afterwards.
  // Any level-up that comes by itself on the way is answered the same way.
  const walk = async (key, axis, past) => {
    await page.keyboard.down(key);
    try {
      await debug(page, CMD.levelUp);
      await waitCards(true, 5000);
      await answerCards();
      for (;;) {
        // Returns when the limit is passed, or when the cards open again.
        await page.waitForFunction(([name, limit, greater]) => {
          const state = JSON.parse(Module.ccall('ls_query_state', 'string', [], []));
          return state.phase === 1 || (greater ? state[name] > limit : state[name] < limit);
        }, [axis, past.limit, past.greater], { timeout: 20000 });
        const state = await readState(page);
        if (state.phase === 1) {
          await answerCards();
          continue;
        }
        return state;
      }
    } finally {
      await page.keyboard.up(key);
    }
  };

  const east = await walk('d', 'x', { limit: chunkSize + 60, greater: true });
  assert.equal(chunkOf(east.x), 1, 'walking right must enter the next chunk');
  const south = await walk('s', 'y', { limit: chunkSize + 60, greater: true });
  assert.equal(chunkOf(south.y), 1, 'walking down must enter the next chunk');
  const west = await walk('a', 'x', { limit: -50, greater: false });
  assert.equal(chunkOf(west.x), -1, 'walking left must cross back, into a chunk with a negative index');
  assert.ok(levelUpsAnswered >= 3, `a level-up was forced in each of the 3 walks, but only ${levelUpsAnswered} were answered`);
  assert.equal((await readState(page)).phase, 0, 'the game must be playing again at the end');
  assert.deepEqual(problems, []);
  await context.close();
});

test('at a whole number scale, every art pixel is an even, unblurred block', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  await command(page, kSetSeed, 31);
  await command(page, kShowcase, 40);
  await page.waitForTimeout(400);
  // 1280x720 uses a scale of 2, so each aligned 2x2 block must be a single color.
  const box = { x: 200, y: 200, w: 880, h: 400 };
  const data = await readBox(page, box);
  let mixed = 0;
  const at = (x, y) => (y * box.w + x) * 4;
  for (let y = 0; y + 1 < box.h; y += 2) {
    for (let x = 0; x + 1 < box.w; x += 2) {
      const a = at(x, y);
      for (const b of [at(x + 1, y), at(x, y + 1), at(x + 1, y + 1)]) {
        if (data[a] !== data[b] || data[a + 1] !== data[b + 1] || data[a + 2] !== data[b + 2]) { mixed++; break; }
      }
    }
  }
  assert.equal(mixed, 0, `${mixed} art pixels were not even blocks`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('every sprite fits in the atlas', async () => {
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const page = await context.newPage();
  const logs = [];
  page.on('console', (message) => logs.push(message.text()));
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  const ready = logs.find((line) => line.includes('world renderer ready'));
  assert.ok(ready, 'the renderer must report its startup');
  const rows = /using (\d+) of (\d+) rows/.exec(ready);
  assert.ok(+rows[1] > 0, 'the atlas must not overflow');
  assert.ok(!logs.some((line) => line.includes('atlas is full')));
  await context.close();
});

test('resizing keeps working, and spawns stay out of view at every window shape', async () => {
  const views = [];
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const page = await context.newPage();
  const problems = watchProblems(page);
  page.on('console', (message) => {
    const match = /world view: target (\d+)x(\d+), scale ([\d.]+), sees (\d+) of (\d+)/.exec(message.text());
    if (match) views.push({ width: +match[1], height: +match[2], scale: +match[3], sees: +match[4], spawn: +match[5] });
  });
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });

  const shapes = [[390, 844], [844, 390], [800, 800], [2000, 800], [500, 1400], [1920, 1080], [320, 240], [1366, 768], [1280, 720]];
  for (const [width, height] of shapes) {
    await page.setViewportSize({ width, height });
    await page.waitForTimeout(250);
    const size = await page.evaluate(() => ({ w: document.getElementById('canvas').width, h: document.getElementById('canvas').height }));
    assert.equal(size.w, width);
    assert.equal(size.h, height);
    const box = await readBox(page, { x: 0, y: Math.floor(height * 0.75), w: Math.floor(width / 4), h: Math.floor(height / 8) });
    const colors = new Set();
    for (let i = 0; i < box.length; i += 4) colors.add((box[i] << 16) | (box[i + 1] << 8) | box[i + 2]);
    assert.ok(colors.size > 4, `the world must fill a ${width}x${height} window`);
  }
  assert.ok(views.length >= shapes.length - 1, `each new shape must report its view, got ${views.length}`);
  for (const view of views) {
    assert.ok(view.sees < view.spawn, `a ${view.width}x${view.height} target sees ${view.sees}, past the spawn circle at ${view.spawn}`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('the low quality setting still draws the world', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  await command(page, kSetSeed, 21);
  await command(page, kShowcase, -1);
  await command(page, kSetQuality, 0);
  await command(page, kSetRunTime, 420);
  await page.waitForTimeout(400);
  const box = await readBox(page, groundBox);
  const colors = new Set();
  for (let i = 0; i < box.length; i += 4) colors.add((box[i] << 16) | (box[i + 1] << 8) | box[i + 2]);
  assert.ok(colors.size > 8);
  assert.deepEqual(problems, []);
  await context.close();
});
