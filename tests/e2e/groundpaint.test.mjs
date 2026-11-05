// Checks of two fixes for slow frames. Ground ahead of the camera is painted in pieces over
// several frames, and that must give the very same picture as painting it whole. The audio
// device opens while the game loads, so the first press has no device to wait for.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, watchProblems, debug, CMD, waitScreen, readUi } from './harness.mjs';

const kShowcase = 17;
const kEmptyWorld = -1;
const kMovePlayerX = 18;
const kMovePlayerY = 19;
const kChunkWorldSize = 640;
const kNightSeconds = 420;
const kMaps = [[0, 'the city'], [1, 'the wasteland']];

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

async function open(options = { viewport: { width: 640, height: 360 } }, initScript) {
  const context = await browser.newContext(options);
  if (initScript) await context.addInitScript(initScript);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  return { context, page, problems };
}

const waitFrames = (page, count) => page.evaluate((n) => new Promise((resolve) => {
  const step = () => (n-- <= 0 ? resolve() : requestAnimationFrame(step));
  step();
}), count);

const paintCounts = (page) => page.evaluate(() => ({
  whole: Module._ls_ground_whole_paints(),
  chunks: JSON.parse(Module.ccall('ls_query_world', 'string', [], [])).chunkPaints,
}));

// An empty world on this map and seed, with nothing moving, at the player's start.
async function emptyWorld(page, map, seed) {
  await debug(page, CMD.setMap, map);
  await debug(page, CMD.setSeed, seed);
  await debug(page, CMD.setReducedMotion, 1);  // Holds flames still, so pictures can be compared.
  await debug(page, kShowcase, kEmptyWorld);
  await debug(page, CMD.setTime, kNightSeconds);  // The same light in every picture.
  await waitFrames(page, 12);
  await page.waitForTimeout(500);  // The fade in from black at the start of a run has to end first.
}

// Differing pixels between two screenshots, worked out inside the page. Only the ground counts:
// the soldier in the middle stands differently after a walk, and the debug overlay's frame
// times along the bottom differ between any two frames.
function changedPixels(page, first, second) {
  return page.evaluate(async ([one, two]) => {
    const read = async (data) => {
      const blob = await (await fetch(`data:image/png;base64,${data}`)).blob();
      const image = await createImageBitmap(blob);
      const canvas = new OffscreenCanvas(image.width, image.height);
      const context = canvas.getContext('2d');
      context.drawImage(image, 0, 0);
      return context.getImageData(0, 0, image.width, image.height).data;
    };
    const a = await read(one);
    const b = await read(two);
    const columns = innerWidth * devicePixelRatio;
    const rows = innerHeight * devicePixelRatio;
    const lastByte = Math.max(0, a.length - columns * 4 * 70 * devicePixelRatio);
    let changed = 0;
    for (let i = 0; i < lastByte; i += 4) {
      const x = (i / 4) % columns;
      const y = Math.floor(i / 4 / columns);
      if (x > columns / 3 && x < columns * 2 / 3 && y > rows / 3) continue;
      if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) changed++;
    }
    return changed;
  }, [first.toString('base64'), second.toString('base64')]);
}

for (const [map, name] of kMaps) {
  test(`painting ${name} in pieces gives exactly the same pixels and lights as painting it whole`, async () => {
    const { context, page, problems } = await open();
    for (const seed of [12345, 777, 90210]) {
      const report = await page.evaluate(([m, s]) => JSON.parse(Module.ccall('ls_check_staged_paint', 'string', ['number', 'number', 'number'], [m, s, 2])), [map, seed]);
      assert.equal(report.chunks, 25);
      assert.equal(report.pixels, 0, `seed ${seed}: ${report.pixels} pixels differ`);
      assert.equal(report.lights, 0, `seed ${seed}: ${report.lights} chunks have other lights`);
    }
    assert.deepEqual(problems, []);
    await context.close();
  });

  test(`walking through ${name} paints ahead in pieces, and shows what jumping there shows`, async () => {
    const target = Math.round(kChunkWorldSize * 2.4);

    // Jumping means every chunk on screen is painted whole, in one frame.
    const first = await open();
    await emptyWorld(first.page, map, 4321);
    await debug(first.page, kMovePlayerX, target);
    await debug(first.page, kMovePlayerY, 40);
    await waitFrames(first.page, 8);
    const jumped = await first.page.screenshot();
    assert.deepEqual(first.problems, []);
    await first.context.close();

    // A fresh page, so nothing is painted yet, and a walk there at about walking speed. That
    // gives the chunks ahead of the camera time to be painted a piece at a time.
    const { context, page, problems } = await open();
    await emptyWorld(page, map, 4321);
    await debug(page, kMovePlayerY, 40);
    await waitFrames(page, 60);
    const start = await paintCounts(page);
    for (let x = 0; x < target; x += 8) {
      await debug(page, kMovePlayerX, x);
      await waitFrames(page, 2);
    }
    await debug(page, kMovePlayerX, target);
    await waitFrames(page, 8);
    const walked = await page.screenshot();
    const end = await paintCounts(page);

    assert.ok(end.chunks > start.chunks, 'the walk should have painted new chunks');
    assert.equal(end.whole, start.whole, 'at walking speed, no chunk should have to be painted whole');
    assert.equal(await changedPixels(page, jumped, walked), 0, 'walking and jumping show different ground');
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('a change of map while a chunk is half painted leaves no trace of the old map', async () => {
  const { context, page, problems } = await open();
  await emptyWorld(page, 1, 555);
  const target = kChunkWorldSize;
  await debug(page, kMovePlayerX, target);
  await waitFrames(page, 8);
  const untouched = await page.screenshot();

  await emptyWorld(page, 0, 555);
  // A step that starts a piece-by-piece paint, and the map changes on the very next frame.
  await waitFrames(page, 60);
  await debug(page, kMovePlayerX, 300);
  await waitFrames(page, 1);
  await emptyWorld(page, 1, 555);
  await debug(page, kMovePlayerX, target);
  await waitFrames(page, 8);
  const after = await page.screenshot();

  assert.equal(await changedPixels(page, untouched, after), 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the audio device opens while loading, and the first press only has to start it', async () => {
  // Counts audio contexts, each of which costs the browser real time to set up.
  const counter = () => {
    const Real = window.AudioContext;
    window.__audioContexts = 0;
    window.AudioContext = function (...args) {
      window.__audioContexts++;
      return new Real(...args);
    };
    window.AudioContext.prototype = Real.prototype;
  };
  const context = await browser.newContext({ viewport: { width: 640, height: 360 } });
  await context.addInitScript(counter);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');

  const opened = await page.evaluate(() => window.__audioContexts);
  assert.ok(opened > 0, 'the device should already be open on the title screen');
  assert.equal((await readUi(page)).audio, 0, 'nothing may sound before the first press');

  await page.keyboard.press('ArrowDown');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).audio === 1, null, { timeout: 5000 });
  assert.equal(await page.evaluate(() => window.__audioContexts), opened, 'the press should not set up any more audio');
  assert.deepEqual(problems, []);
  await context.close();
});
