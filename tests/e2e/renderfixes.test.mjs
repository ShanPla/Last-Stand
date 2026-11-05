// Checks of renderer fixes that plain play can't show on demand: warnings from zombies
// outside the picture, the soldier's gun and flash, ghost lights from an old seed, and the
// watcher that cuts effects on a slow device.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { startServer, launchChrome, watchProblems, readState, debug, CMD, waitScreen } from './harness.mjs';

// Debug command numbers and test scenes. They match App.cpp and WorldView.h.
const kShowcase = 17;
const kEmptyWorld = -1;
const kSceneChargerOffLeft = -100;
const kSceneChargerAway = -101;
const kSceneBossOffLeft = -102;
const kSceneOnlyMines = -103;
const kSceneWalkerOffLeft = -104;
const kMovePlayerX = 18;
const kMovePlayerY = 19;

const kRifle = 0;
const kSniper = 3;
const kDemoGuns = [0, 1, 2, 4, 8];  // Rifle, Shotgun, SMG, Flamethrower, Tesla: all the title demo holds.
const kNightSeconds = 420;
const kLowSunSeconds = 120;  // When shadows are at their longest.

const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true };
const laptop = { viewport: { width: 1280, height: 720 } };
const tiny = { viewport: { width: 320, height: 180 } };  // These tests count frames, so they should be cheap to draw.

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

// Replaces the page's clock, so the game sees exactly the frame times a test chooses, on any
// machine and under any load. Every animation frame moves time on by window.__frameMs. All the
// callbacks waiting on one frame share it, or a test's own waiting would speed up the clock.
function fakeClock(frameMs) {
  return `
    window.__frameMs = ${frameMs};
    {
      let fake = 1000;
      let waiting = [];
      const real = window.requestAnimationFrame.bind(window);
      performance.now = () => fake;
      window.requestAnimationFrame = (callback) => {
        if (waiting.length === 0) {
          real(() => {
            fake += window.__frameMs;
            const callbacks = waiting;
            waiting = [];
            for (const run of callbacks) run(fake);
          });
        }
        waiting.push(callback);
        return 0;
      };
    }`;
}

async function open(options, initScript) {
  const context = await browser.newContext(options);
  if (initScript) await context.addInitScript(initScript);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  return { context, page, problems };
}

const readWorld = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));
const readEffects = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])));
const waitFrames = (page, count) => page.evaluate((n) => new Promise((resolve) => {
  const step = () => (n-- <= 0 ? resolve() : requestAnimationFrame(step));
  step();
}), count);

// Starts a run through the debug path. A key press can fall between two slow frames and be
// lost, and these tests aren't about the title screen.
async function startRun(page, seed) {
  await waitScreen(page, 'title');
  await debug(page, CMD.startRun, seed);
  await waitScreen(page, 'playing');
}

// Compares two screenshots. The work happens inside the page, because sending millions of
// bytes back to this test takes far longer than the check itself. warm counts pixels that
// turned the bright orange of a warning line, and changed counts pixels that differ by more
// than the rounding between two passes of see-through light.
function comparePictures(page, first, second, skip = { left: 0, top: 0, right: 0, bottom: 0 }) {
  return page.evaluate(async ([before, after, skipped]) => {
    const read = async (data) => {
      const blob = await (await fetch(`data:image/png;base64,${data}`)).blob();
      const image = await createImageBitmap(blob);
      const canvas = new OffscreenCanvas(image.width, image.height);
      const context = canvas.getContext('2d');
      context.drawImage(image, 0, 0);
      return context.getImageData(0, 0, image.width, image.height).data;
    };
    const a = await read(before);
    const b = await read(after);
    let warm = 0;
    let changed = 0;
    const box = { left: Infinity, top: Infinity, right: -1, bottom: -1 };
    const columns = innerWidth * devicePixelRatio;
    // The debug overlay's frame times sit along the bottom, and differ between any two frames.
    const lastByte = Math.max(0, a.length - columns * 4 * 70 * devicePixelRatio);
    for (let i = 0; i < lastByte; i += 4) {
      const gap = Math.abs(b[i] - a[i]) + Math.abs(b[i + 1] - a[i + 1]) + Math.abs(b[i + 2] - a[i + 2]);
      const x = (i / 4) % columns;
      const y = Math.floor(i / 4 / columns);
      if (x >= skipped.left && x < skipped.right && y >= skipped.top && y < skipped.bottom) continue;
      if (gap > 12) {
        changed++;
        box.left = Math.min(box.left, x);
        box.right = Math.max(box.right, x);
        box.top = Math.min(box.top, y);
        box.bottom = Math.max(box.bottom, y);
      }
      if (gap > 40 && b[i] > 150 && b[i] > b[i + 2] + 70 && b[i + 1] < b[i]) warm++;
    }
    return { warm, changed, box };
  }, [first.toString('base64'), second.toString('base64'), skip]);
}

async function showScene(page, scene) {
  await debug(page, kShowcase, scene);
  await waitFrames(page, 4);
}

// ---- Warnings from outside the picture -------------------------------------------

for (const [name, options] of [['a tall phone', phone], ['a laptop', laptop]]) {
  test(`on ${name}, a Charger outside the picture still shows its warning line`, async () => {
    const { context, page, problems } = await open(options);
    await startRun(page, 77);
    // The camera has to be drawn once, for the scene to know where the left edge is.
    await showScene(page, kSceneChargerAway);
    const away = await readWorld(page);
    assert.equal(away.telegraphing, 1, 'the Charger must be telegraphing');
    assert.equal(away.telegraphsShown, 0, 'a line pointing out of the picture has nothing to show');
    const without = await page.screenshot();

    await showScene(page, kSceneChargerOffLeft);
    const across = await readWorld(page);
    assert.equal(across.telegraphsShown, 1, 'a line crossing the picture must be drawn');
    const withLine = await page.screenshot();
    if (process.env.RENDERFIXES_SHOTS) await writeFile(`${process.env.RENDERFIXES_SHOTS}/charger-${options.viewport.width}.png`, withLine);
    const { warm } = await comparePictures(page, without, withLine);
    assert.ok(warm > 100, `the warning line must really be on screen, found ${warm} warm pixels`);
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('a boss outside the picture still shows its dash warning', async () => {
  const { context, page, problems } = await open(phone);
  await startRun(page, 78);
  await showScene(page, kEmptyWorld);
  await debug(page, CMD.setTime, kNightSeconds);  // No long shadow at night, so the cull is at its tightest.
  await waitFrames(page, 4);
  const without = await page.screenshot();
  await showScene(page, kSceneBossOffLeft);
  const world = await readWorld(page);
  assert.equal(world.telegraphing, 1);
  assert.equal(world.telegraphsShown, 1);
  const withLine = await page.screenshot();
  if (process.env.RENDERFIXES_SHOTS) await writeFile(`${process.env.RENDERFIXES_SHOTS}/boss-${phone.viewport.width}.png`, withLine);
  const { warm } = await comparePictures(page, without, withLine);
  assert.ok(warm > 60, `the dash line must really be on screen, found ${warm} warm pixels`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('2,000 zombies with no warnings make the telegraph list empty, and it refills', async () => {
  const { context, page, problems } = await open(laptop);
  await startRun(page, 79);
  await showScene(page, 2000);
  const crowd = await readWorld(page);
  assert.equal(crowd.telegraphing, 0, 'a crowd that is only walking warns of nothing');
  await showScene(page, kSceneChargerOffLeft);
  assert.equal((await readWorld(page)).telegraphing, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a zombie just past the left edge still throws its long shadow into the picture', async () => {
  const { context, page, problems } = await open(laptop);
  await startRun(page, 82);
  await showScene(page, kEmptyWorld);
  await debug(page, CMD.setTime, kLowSunSeconds);
  await waitFrames(page, 4);
  await page.waitForTimeout(500);  // The fade in from black at the start of a run has to end first.
  const empty = await page.screenshot();
  await showScene(page, kSceneWalkerOffLeft);
  const withShadow = await page.screenshot();
  const { changed, box } = await comparePictures(page, empty, withShadow);
  assert.ok(changed >= 10, `the shadow must reach into the picture, found ${changed} changed pixels`);
  assert.ok(box.right < 120, `only the left edge should change, but the change reaches ${box.right}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('standing still on a tall phone stops painting ground', async () => {
  const { context, page, problems } = await open(phone);
  await startRun(page, 83);
  await showScene(page, kEmptyWorld);
  await waitFrames(page, 60);
  const settled = (await readWorld(page)).chunkPaints;
  await waitFrames(page, 30);
  assert.equal((await readWorld(page)).chunkPaints, settled, 'a pool too small for the view would repaint chunks forever');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- The soldier's gun ------------------------------------------------------------

// The showcase fires each gun in turn, so moving its clock picks the gun: the Sniper, which
// neither a new run nor the title demo owns.
async function holdSniperInShowcase(page) {
  await debug(page, kShowcase, 0);
  await debug(page, CMD.setTime, 4);
  await page.waitForFunction((gun) => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])).heldGun === gun, kSniper, { timeout: 30000 });
}

test('a new run and the title demo forget the gun from the last run', async () => {
  const { context, page, problems } = await open(laptop);
  await startRun(page, 90);
  await holdSniperInShowcase(page);

  await debug(page, CMD.startRun, 1234);
  await waitFrames(page, 3);
  assert.equal((await readWorld(page)).heldGun, kRifle, 'a new run starts with the gun it owns');

  await holdSniperInShowcase(page);
  await debug(page, CMD.showScreen, 0);
  await waitFrames(page, 3);
  const inDemo = (await readWorld(page)).heldGun;
  assert.ok(kDemoGuns.includes(inDemo), `the title demo must hold a gun it owns, not gun ${inDemo}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('dropping a mine fires no flash from a gun', async () => {
  // A steady 60 frames a second, as a flash would last long enough here to be counted.
  const { context, page, problems } = await open(tiny, fakeClock(1000 / 60));
  await startRun(page, 80);
  await showScene(page, kSceneOnlyMines);
  const before = (await readWorld(page)).muzzleFrames;
  await debug(page, CMD.holdSim, 0);  // Any such command hands control back to the simulation.
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).shots > 0, null, { timeout: 30000 });
  await waitFrames(page, 5);
  assert.equal((await readWorld(page)).muzzleFrames, before, 'a mine must not flash the gun');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the muzzle flash shows on a device too slow to draw it twice', async () => {
  // 18 frames a second: one frame is longer than the whole flash.
  const { context, page, problems } = await open(tiny, fakeClock(55));
  await startRun(page, 81);
  await debug(page, CMD.setQuality, 1);
  await debug(page, CMD.stressEnemies, 30);
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])).muzzleFrames > 0, null, { timeout: 60000 });
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Ghost lights -----------------------------------------------------------------

async function nightGroundOnSeed(page, seed, x, y) {
  await debug(page, CMD.setSeed, seed);
  await debug(page, CMD.setReducedMotion, 1);  // Holds flames still, so pictures can be compared.
  await debug(page, kShowcase, kEmptyWorld);
  await debug(page, kMovePlayerX, x);
  await debug(page, kMovePlayerY, y);
  await debug(page, CMD.setTime, kNightSeconds);
  await waitFrames(page, 12);
  await page.waitForTimeout(500);  // The fade in from black at the start of a run has to end first.
}

test('a new seed shows exactly what a fresh start on that seed shows, with no ghost lights', async () => {
  const options = { viewport: { width: 640, height: 360 } };
  const first = await open(options);
  await startRun(first.page, 5);
  // Walking between these fills all of the chunk pool. A new seed only reuses as many slots
  // as it needs, so seed 5's chunks around the last spot stay behind in the rest, and every
  // lamp and flame in them must stay out of the picture when seed 6 takes over.
  await nightGroundOnSeed(first.page, 5, -3000, 100);
  for (const x of [1200, 200]) {
    await debug(first.page, kMovePlayerX, x);
    await waitFrames(first.page, 30);
  }
  await nightGroundOnSeed(first.page, 6, 200, 100);
  const afterOldSeed = await first.page.screenshot();

  const fresh = await open(options);
  await startRun(fresh.page, 6);
  await nightGroundOnSeed(fresh.page, 6, 200, 100);
  const clean = await fresh.page.screenshot();

  if (process.env.RENDERFIXES_SHOTS) {
    await writeFile(`${process.env.RENDERFIXES_SHOTS}/ghost-after.png`, afterOldSeed);
    await writeFile(`${process.env.RENDERFIXES_SHOTS}/ghost-clean.png`, clean);
  }
  // The soldier's blades stop wherever they had turned to, so the area around him is skipped.
  const { changed, box } = await comparePictures(fresh.page, afterOldSeed, clean, { left: 220, top: 110, right: 420, bottom: 200 });
  assert.equal(changed, 0, `the old seed must leave nothing behind, but pixels differ in ${JSON.stringify(box)}`);
  assert.deepEqual(first.problems, []);
  assert.deepEqual(fresh.problems, []);
  await first.context.close();
  await fresh.context.close();
});

// ---- The frame-rate watcher -------------------------------------------------------

const quality = async (page) => (await readEffects(page)).quality;

// Waits until this many fake seconds of a run have passed at the current frame length.
async function playFor(page, seconds) {
  const frameMs = await page.evaluate(() => window.__frameMs);
  await waitFrames(page, Math.ceil(seconds * 1000 / frameMs));
}

test('a screen capped at 30 Hz keeps the full effects for good', async () => {
  const { context, page, problems } = await open(tiny, fakeClock(1000 / 30));
  await startRun(page, 91);
  await playFor(page, 6);
  const state = await readState(page);
  assert.ok(state.tick > 0, 'the run must really have been playing');
  assert.ok(Math.abs(state.frameMs - 1000 / 30) < 2, `the game must see the chosen frame time, saw ${state.frameMs}`);
  assert.equal(await quality(page), 1, 'a capped screen is not a slow device');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a device that runs the menus smoothly and fights at 30 frames a second loses the extras', async () => {
  const { context, page, problems } = await open(tiny, fakeClock(1000 / 60));
  await waitFrames(page, 20);
  await startRun(page, 92);
  await page.evaluate(() => { window.__frameMs = 1000 / 30; });
  await playFor(page, 6);
  assert.equal(await quality(page), 0, 'menus at 60 prove the screen can do better');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a device always under 26 frames a second loses the extras, even with no fast frames to compare', async () => {
  const { context, page, problems } = await open(tiny, fakeClock(50));
  await startRun(page, 93);
  await playFor(page, 6);
  assert.equal(await quality(page), 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('long gaps from a hidden tab or a stall never count against the device', async () => {
  const { context, page, problems } = await open(tiny, fakeClock(1000 / 60));
  await waitFrames(page, 20);
  await startRun(page, 94);
  for (let i = 0; i < 6; ++i) {
    // One frame arrives two seconds late, then the screen runs normally again.
    await page.evaluate(() => { window.__frameMs = 2000; });
    await waitFrames(page, 1);
    await page.evaluate(() => { window.__frameMs = 1000 / 60; });
    await waitFrames(page, 30);
  }
  assert.equal(await quality(page), 1);
  assert.deepEqual(problems, []);
  await context.close();
});
