// Checks of the flat wreck in the real page: a crushed car really changes on screen, its picture
// is the same however often it is painted, it hides nothing any more, a car over a chunk's
// edge lines up, and the moment of the crush neither drops a frame nor breaks a setting.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, startGame, readState, debug, CMD } from './harness.mjs';

// Debug command numbers. They match DebugCommand in src/core/App.cpp.
const kShowcase = 17;
const kMovePlayerX = 18;
const kCarCover = 44;
const kCrushNearestCar = 45;
const kBossBesideCar = 46;

// Scenes. They match src/components/WorldView/WorldView.h.
const kSceneEmpty = -105;
const kSceneBehind = -106;
const kSceneCarOfSprite = -120;
const kSceneCarOverEdge = -130;

const kCity = 0;
const kWasteland = 1;
const kBusSide = 5;
const kBusEnd = 6;
const kTankerSide = 7;
const kChunkWorldSize = 640;

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

async function openRun(viewport = { width: 1280, height: 720 }) {
  const opened = await openGame(browser, `${web.url}?debug`, { viewport });
  await startGame(opened.page);
  return opened;
}

const readWorld = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));
const readEffects = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])));
const waitFrames = (page, count) => page.evaluate((n) => new Promise((resolve) => {
  const step = () => (n-- <= 0 ? resolve() : requestAnimationFrame(step));
  step();
}), count);

// A scene held still: flames too, and no particles left from anything before.
async function openScene(page, map, seed, scene, time = 0) {
  await debug(page, CMD.setMap, map);
  await debug(page, CMD.setSeed, seed);
  await debug(page, CMD.setReducedMotion, 1);
  await debug(page, kShowcase, scene);
  await debug(page, CMD.setTime, time);
  await page.waitForTimeout(600);
}

async function crushAndSettle(page) {
  await debug(page, kCrushNearestCar, 0);
  await page.waitForTimeout(1300);
}

async function boxBytes(page, box) {
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

function changedPixels(a, b) {
  let changed = 0;
  for (let i = 0; i < a.length; i += 4) {
    if (Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]) > 6) changed++;
  }
  return changed;
}

// The car's picture in page pixels. The soldier stands to its left, so its left is trimmed.
function carBox(world) {
  const [left, top, right, bottom] = world.car.map((v) => v * world.pixelScale);
  const trim = (right - left) * 0.3;
  return { x: Math.round(left + trim), y: Math.round(top), w: Math.round(right - left - trim), h: Math.round(bottom - top) };
}

for (const [name, map, sprites] of [['the city', kCity, [0, 1]], ['the desert', kWasteland, [2, 3, 4, kBusSide, kBusEnd, kTankerSide]]]) {
  test(`in ${name}, every kind of car looks different once flat, and paints the same again later`, async () => {
    const { context, page, problems } = await openRun();
    for (const sprite of sprites) {
      await openScene(page, map, 8, kSceneCarOfSprite - sprite);
      const world = await readWorld(page);
      assert.ok(world.cars > 0, `sprite ${sprite}: the scene must show a car`);
      // The tanker is so long that another car's middle may be nearer the soldier, so its
      // box is measured from the soldier, who stands at its left end.
      const box = sprite === kTankerSide ? { x: 680, y: 270, w: 240, h: 100 } : carBox(world);
      const before = await boxBytes(page, box);
      await crushAndSettle(page);
      const after = await readWorld(page);
      assert.equal(after.crushed, 1, `sprite ${sprite}: one car must be flat`);
      assert.ok(after.cars < world.cars, `sprite ${sprite}: the flat car must leave the list of cars that cover things`);
      const flat = await boxBytes(page, box);
      const changed = changedPixels(before, flat);
      const total = before.length / 4;
      assert.ok(changed > total * 0.1, `sprite ${sprite}: the picture must really change: ${changed} of ${total} pixels`);

      // Walk far enough for every chunk to be thrown away, then come back to find it painted
      // again, from nothing, a piece of it after the walk. The picture must be the same.
      const home = await readState(page);
      const paintsBefore = await page.evaluate(() => Module._ls_ground_whole_paints());
      for (let i = 1; i <= 4; ++i) {
        await debug(page, kMovePlayerX, Math.round(home.x + i * 8 * kChunkWorldSize));
        await waitFrames(page, 4);
      }
      // The scene stands the soldier on the same spot, exactly. The move commands round it.
      await debug(page, kShowcase, kSceneCarOfSprite - sprite);
      await waitFrames(page, 6);
      assert.ok(await page.evaluate(() => Module._ls_ground_whole_paints()) > paintsBefore, 'the chunks must really be painted again');
      assert.equal(changedPixels(flat, await boxBytes(page, box)), 0, `sprite ${sprite}: the flat car must look the same when painted again`);
    }
    assert.deepEqual(problems, []);
    await context.close();
  });
}

for (const [name, map] of [['the city', kCity], ['the desert', kWasteland]]) {
  test(`in ${name}, painting a flat car changes nothing else, whole or in pieces, on any seed`, async () => {
    const { context, page, problems } = await openRun({ width: 640, height: 360 });
    let cars = 0;
    let overEdge = 0;
    for (const seed of [3, 1234]) {
      const report = await page.evaluate(([m, s]) => JSON.parse(Module.ccall('ls_check_flat_cars', 'string', ['number', 'number', 'number'], [m, s, 2])), [map, seed]);
      cars += report.cars;
      overEdge += report.overEdge;
      assert.equal(report.unchanged, 0, `seed ${seed}: a car painted the same once flat`);
      assert.equal(report.outside, 0, `seed ${seed}: ${report.outside} pixels changed away from the car`);
      assert.equal(report.stillMarked, 0, `seed ${seed}: ${report.stillMarked} pixels of a flat car still cover figures`);
      assert.equal(report.seams, 0, `seed ${seed}: ${report.seams} chunks started from a different picture of the same car`);
      assert.equal(report.missed, 0, `seed ${seed}: ${report.missed} chunks should show a flat car and never painted it`);
      assert.equal(report.stagedDifferent, 0, `seed ${seed}: painting in pieces differs by ${report.stagedDifferent} pixels`);
    }
    assert.ok(cars >= 10, `too few cars to prove anything: ${cars}`);
    assert.ok(overEdge > 0, 'at least one car must cross a chunk edge, or the seam check proves nothing');
    assert.deepEqual(problems, []);
    await context.close();
  });

  test(`in ${name}, a flat car hides nothing: a zombie behind it shows whole, with no silhouette`, async () => {
    const { context, page, problems } = await openRun();
    await openScene(page, map, 8, kSceneBehind);
    const whole = await readWorld(page);
    assert.equal(whole.silhouettes, 1, 'the zombie behind the whole car must be a silhouette, or this proves nothing');
    const box = carBox(whole);
    await crushAndSettle(page);
    const flat = await readWorld(page);
    assert.equal(flat.silhouettes, 0, 'nothing is covered, so nothing needs a silhouette');

    // Cars covering things or not makes no difference at all to this picture any more.
    const covered = await boxBytes(page, box);
    await debug(page, kCarCover, 0);
    await waitFrames(page, 4);
    const uncovered = await boxBytes(page, box);
    await debug(page, kCarCover, 1);
    assert.equal(changedPixels(covered, uncovered), 0, 'a flat car must leave no depth behind');
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('flattening a burning wreck puts its fire out, and adds no other light', async () => {
  const { context, page, problems } = await openRun();
  const report = await page.evaluate(() => JSON.parse(Module.ccall('ls_check_wreck_fires', 'string', ['number', 'number', 'number'], [1, 8, 8])));
  assert.ok(report.fewer > 0, `no chunk in this search lost a fire, so nothing was checked: ${JSON.stringify(report)}`);
  assert.equal(report.more, 0, `flat cars must not add lights: ${JSON.stringify(report)}`);
  assert.ok(report.flat < report.whole);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a car over a chunk edge goes flat on both chunks in the same frame', async () => {
  const { context, page, problems } = await openRun();
  for (const map of [kCity, kWasteland]) {
    await openScene(page, map, 8, kSceneCarOverEdge);
    await crushAndSettle(page);
    const world = await readWorld(page);
    assert.equal(world.crushed, 1);
    assert.ok(world.flatSwap >= 2, `both chunks must change together, and ${world.flatSwap} did`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('a crush is painted a piece per frame, and the same seed starts whole again in a new run', async () => {
  const { context, page, problems } = await openRun();
  for (const map of [kCity, kWasteland]) {
    await openScene(page, map, 8, kSceneEmpty);
    const world = await readWorld(page);
    const box = carBox(world);
    const whole = await boxBytes(page, box);
    await waitFrames(page, 30);  // Anything still being painted ahead of the camera finishes.

    // Counts pieces of painting per frame from here on, up to the end of the swap.
    await page.evaluate(() => {
      window.pieces = [];
      let last = Module._ls_ground_paints();
      const watch = () => {
        const now = Module._ls_ground_paints();
        window.pieces.push(now - last);
        last = now;
        if (window.pieces.length < 40) requestAnimationFrame(watch);
      };
      requestAnimationFrame(watch);
    });
    const wholeBefore = await page.evaluate(() => Module._ls_ground_whole_paints());
    await debug(page, kCrushNearestCar, 0);
    await page.waitForFunction(() => window.pieces.length >= 40);
    const pieces = await page.evaluate(() => window.pieces);
    assert.ok(Math.max(...pieces) <= 1, `at most one piece per frame: ${pieces.join(',')}`);
    assert.ok(pieces.reduce((a, b) => a + b, 0) >= 4, `the repaint must really run: ${pieces.join(',')}`);
    assert.equal(await page.evaluate(() => Module._ls_ground_whole_paints()), wholeBefore, 'no chunk may be painted whole for a crush');
    await page.waitForTimeout(1300);
    const flatChange = changedPixels(whole, await boxBytes(page, box));
    assert.ok(flatChange > 200, `the car must have changed: ${flatChange} pixels`);

    // The very same seed in a new run: the car must be whole again, and solid.
    await openScene(page, map, 8, kSceneEmpty);
    assert.equal((await readWorld(page)).crushed, 0);
    // Not quite zero: the soldier's spinning blades may have moved a step before the clock
    // stopped, and one can cross this box. A flat car left behind would change far more.
    const leftover = changedPixels(whole, await boxBytes(page, box));
    assert.ok(leftover < flatChange / 4, `a new run must not show the old flat car: ${leftover} pixels against ${flatChange}`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('the real boss walks onto a real car: dust, metal and a small shake, in a real step', async () => {
  const { context, page, problems } = await openRun();
  for (const map of [kCity, kWasteland]) {
    await debug(page, CMD.setMap, map);
    await debug(page, CMD.setSeed, 8);
    await debug(page, CMD.resetFxCounters, 0);
    await debug(page, kBossBesideCar, 0);
    const before = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])).particlesSpawned);
    await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])).crushed > 0, null, { timeout: 8000 });
    await waitFrames(page, 3);
    const fx = await readEffects(page);
    assert.ok(fx.particlesSpawned > before + 20, `dust and metal must fly: ${fx.particlesSpawned - before}`);
    assert.ok(fx.shakeEvents > 0, 'a crush shakes the screen');
    // Only the crush itself: no big jolt on top of what the boss's own arrival would do.
    assert.ok(fx.shakePeak <= 2, `the shake must stay small: ${fx.shakePeak} pixels`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('the settings rule the crush: no shake with reduced motion or shake off, fewer particles', async () => {
  const { context, page, problems } = await openRun();
  const crushOne = async () => {
    await debug(page, kShowcase, kSceneEmpty);
    await debug(page, CMD.resetFxCounters, 0);
    const before = (await readEffects(page)).particlesSpawned;
    await debug(page, kCrushNearestCar, 0);
    await waitFrames(page, 3);
    const fx = await readEffects(page);
    return { spawned: fx.particlesSpawned - before, shakes: fx.shakeEvents, flashes: fx.flashesShown };
  };
  await debug(page, CMD.setMap, kCity);
  await debug(page, CMD.setReducedMotion, 0);
  await debug(page, CMD.setSeed, 8);
  const full = await crushOne();
  assert.ok(full.spawned > 20, `full motion: ${full.spawned} particles`);
  assert.equal(full.shakes, 1, 'one crush, one small shake');
  assert.equal(full.flashes, 0, 'a crush never flashes the screen');

  await debug(page, CMD.setSeed, 8);
  await debug(page, CMD.setShake, 0);
  assert.equal((await crushOne()).shakes, 0, 'the shake setting at zero stops it');
  await debug(page, CMD.setShake, 100);

  await debug(page, CMD.setSeed, 8);
  await debug(page, CMD.setReducedMotion, 1);
  const reduced = await crushOne();
  assert.equal(reduced.shakes, 0, 'reduced motion stops it');
  assert.ok(reduced.spawned < full.spawned * 0.6, `reduced motion cuts particles: ${reduced.spawned} of ${full.spawned}`);
  assert.ok(reduced.spawned > 0, 'but the crush must still be seen');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a crush works the same on the half-size ground of a weak device', async () => {
  const { context, page, problems } = await openRun();
  await debug(page, CMD.setQuality, 0);
  for (const map of [kCity, kWasteland]) {
    await openScene(page, map, 8, kSceneEmpty);
    const world = await readWorld(page);
    const box = carBox(world);
    const whole = await boxBytes(page, box);
    await crushAndSettle(page);
    assert.equal((await readWorld(page)).crushed, 1);
    assert.ok(changedPixels(whole, await boxBytes(page, box)) > 100, 'the car must have changed');
  }
  assert.deepEqual(problems, []);
  await context.close();
});
