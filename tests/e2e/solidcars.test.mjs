// Checks of the solid cars in the real page: the painters and the simulation agree about
// where every car is, a car covers what stands behind it without ever fully hiding it, and
// the player really is stopped by one.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, startGame, readState } from './harness.mjs';

// Debug command numbers. They match DebugCommand in src/core/App.cpp.
const kSetSeed = 3;
const kSetTime = 12;
const kShowcase = 17;
const kMovePlayerX = 18;
const kMovePlayerY = 19;
const kSetQuality = 20;
const kSetMap = 28;
const kShowCarBoxes = 43;
const kCarCover = 44;

// Scenes. They match kSceneCarEmpty and the rest in src/components/WorldView/WorldView.h.
const kSceneEmpty = -105;
const kSceneBehind = -106;
const kSceneFront = -107;

const kCity = 0;
const kWasteland = 1;
const kDay = 0;
const kNight = 400;

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

const command = (page, id, value) => page.evaluate(([c, v]) => Module._ls_debug_command(c, v), [id, value]);
const readWorld = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));

// A picture of a scene, as raw RGBA bytes of a box, with the world held still.
async function sceneBytes(page, scene, time, box) {
  await command(page, kShowcase, scene);
  await command(page, kSetTime, time);
  await page.waitForTimeout(300);
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

// The pixels that differ between two pictures, and how much they differ on average.
function change(a, b) {
  let changed = 0;
  let total = 0;
  for (let i = 0; i < a.length; i += 4) {
    const d = Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]);
    if (d > 6) {
      changed++;
      total += d;
    }
  }
  return { changed, strength: changed ? total / changed : 0 };
}

// How strongly a covered figure still shows: 1 is not covered at all, and 0 is fully hidden.
// Taken only over the pixels the figure touches when nothing covers it.
function showing(empty, covered, uncovered) {
  const difference = (a, b, i) => Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]);
  let shown = 0;
  let full = 0;
  for (let i = 0; i < empty.length; i += 4) {
    const figure = difference(empty, uncovered, i);
    if (figure <= 6) continue;
    full += figure;
    shown += Math.min(figure, difference(empty, covered, i));
  }
  return full > 0 ? shown / full : 1;
}

// The car's picture in page pixels, trimmed so that only car shows: its lower half.
function carBodyBox(world) {
  const [left, top, right, bottom] = world.car.map((v) => v * world.pixelScale);
  const x = Math.round(left + (right - left) * 0.2);
  const w = Math.round((right - left) * 0.6);
  const y = Math.round(top);
  const h = Math.round((bottom - top) * 0.75);
  return { x, y, w, h };
}

async function openSeed(page, map, seed) {
  await command(page, kSetMap, map);
  await command(page, kSetSeed, seed);
}

test('every car the painters mark has a solid box, and every box has its car, on both maps', async () => {
  const { context, page, problems } = await openRun();
  for (const map of [kCity, kWasteland]) {
    for (const seed of [3, 1234]) {
      const report = await page.evaluate(([m, s]) => JSON.parse(Module.ccall('ls_check_car_marks', 'string', ['number', 'number', 'number'], [m, s, 3])), [map, seed]);
      assert.ok(report.cars > 0, `map ${map} seed ${seed} must have cars to check`);
      assert.equal(report.strayPixels, 0, `map ${map} seed ${seed}: car pixels were painted where there is no box`);
      assert.ok(report.fewestMarked >= 150, `map ${map} seed ${seed}: a box has too little car painted in it: ${report.fewestMarked}`);
    }
  }
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, map] of [['the city', kCity], ['the desert', kWasteland]]) {
  for (const [when, time] of [['by day', kDay], ['at night', kNight]]) {
    test(`in ${name} ${when}, a car covers a zombie behind it but still shows its silhouette`, async () => {
      const { context, page, problems } = await openRun();
      await openSeed(page, map, 8);
      await sceneBytes(page, kSceneEmpty, time, { x: 0, y: 0, w: 4, h: 4 });
      const world = await readWorld(page);
      assert.ok(world.cars > 0, 'the scene must show a car');
      const box = carBodyBox(world);

      const empty = await sceneBytes(page, kSceneEmpty, time, box);
      const behindPicture = await sceneBytes(page, kSceneBehind, time, box);
      const silhouettes = (await readWorld(page)).silhouettes;
      const frontPicture = await sceneBytes(page, kSceneFront, time, box);
      await command(page, kCarCover, 0);
      const behindUncovered = await sceneBytes(page, kSceneBehind, time, box);
      const frontUncovered = await sceneBytes(page, kSceneFront, time, box);
      await command(page, kCarCover, 1);

      const behind = change(empty, behindPicture);
      const uncovered = change(empty, behindUncovered);
      assert.equal(silhouettes, 1, 'the zombie behind the car must be drawn as a silhouette');
      assert.ok(uncovered.changed > 20, `the zombie must overlap the car, or this proves nothing: ${uncovered.changed} pixels`);
      // A car that covers nothing shows the figure at full strength, and one that hides it
      // completely shows no change at all. The silhouette is in between.
      assert.ok(behind.changed > 20, `the silhouette must show on the car: ${behind.changed} pixels changed`);
      const share = showing(empty, behindPicture, behindUncovered);
      assert.ok(share < 0.85, `the car must cover the zombie: it still shows at ${share}`);
      assert.ok(share > 0.2, `the zombie must not vanish behind the car: it shows at ${share}`);
      // A zombie in front of the car owes nothing to the cover. Only the soldier's long
      // shadow, which a car does cut off, can differ a little.
      assert.ok(change(empty, frontPicture).changed > 20, 'a zombie in front must show on the car');
      const frontShare = showing(empty, frontPicture, frontUncovered);
      assert.ok(frontShare > 0.8, `a car must not cover a zombie in front of it: it shows at ${frontShare}`);
      assert.deepEqual(problems, []);
      await context.close();
    });
  }
}

test('half resolution ground, for weak devices, still covers a zombie behind a car', async () => {
  const { context, page, problems } = await openRun();
  await openSeed(page, kCity, 8);
  await command(page, kSetQuality, 0);
  await sceneBytes(page, kSceneEmpty, kDay, { x: 0, y: 0, w: 4, h: 4 });
  const box = carBodyBox(await readWorld(page));
  const empty = await sceneBytes(page, kSceneEmpty, kDay, box);
  const covered = await sceneBytes(page, kSceneBehind, kDay, box);
  await command(page, kCarCover, 0);
  const uncovered = await sceneBytes(page, kSceneBehind, kDay, box);
  const share = showing(empty, covered, uncovered);
  assert.ok(share < 0.85 && share > 0.2, `the zombie must show faintly: it shows at ${share}`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the solid box sits on the painted car, at phone and desktop sizes', async () => {
  for (const viewport of [{ width: 1280, height: 720 }, { width: 390, height: 844 }]) {
    const { context, page, problems } = await openRun(viewport);
    await openSeed(page, kCity, 8);
    await command(page, kShowCarBoxes, 1);
    await sceneBytes(page, kSceneEmpty, kDay, { x: 0, y: 0, w: 4, h: 4 });
    const world = await readWorld(page);
    assert.ok(world.cars > 0);
    // The box runs along the car's lower body, so its picture must hold the outline's color.
    const box = world.carBox.map((v) => v * world.pixelScale);
    const area = { x: Math.round(box[0]), y: Math.round(box[1]), w: Math.max(4, Math.round(box[2] - box[0])), h: Math.max(4, Math.round(box[3] - box[1])) };
    const withBoxes = await sceneBytes(page, kSceneEmpty, kDay, area);
    await command(page, kShowCarBoxes, 0);
    const without = await sceneBytes(page, kSceneEmpty, kDay, area);
    assert.ok(change(withBoxes, without).changed > 10, 'the debug outline must land where the query says the car is');
    assert.deepEqual(problems, []);
    await context.close();
  }
});

test('the player walking at a car is stopped by it in the real game', async () => {
  const { context, page, problems } = await openRun();
  await openSeed(page, kWasteland, 8);
  // The scene gives the position of a car, and then a real run goes on around it.
  await sceneBytes(page, kSceneEmpty, kDay, { x: 0, y: 0, w: 4, h: 4 });
  const { worldBox } = await readWorld(page);
  await command(page, kSetSeed, 8);
  const y = Math.round((worldBox[1] + worldBox[3]) / 2);
  await command(page, kMovePlayerX, Math.round(worldBox[0]) - 60);
  await command(page, kMovePlayerY, y);
  await page.keyboard.down('ArrowRight');
  await page.waitForTimeout(1200);
  const state = await readState(page);
  await page.keyboard.up('ArrowRight');
  assert.ok(state.x <= worldBox[0] - 13, `he must stop at the car's side, at ${worldBox[0]}, and is at ${state.x}`);
  assert.ok(state.x >= worldBox[0] - 40, `he must have walked up to the car, at ${worldBox[0]}, and is at ${state.x}`);
  assert.deepEqual(problems, []);
  await context.close();
});
