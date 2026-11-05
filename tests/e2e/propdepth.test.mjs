// Checks of the standing map objects in the real page: a wall, a tree or a lamp covers a
// zombie behind it but never hides it completely, a zombie in front draws over it, every
// standing pixel the painters mark is listed, and a flat car still covers nothing.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, startGame } from './harness.mjs';

// Debug command numbers. They match DebugCommand in src/core/App.cpp.
const kSetSeed = 3;
const kSetTime = 12;
const kShowcase = 17;
const kSetQuality = 20;
const kSetMap = 28;
const kCarCover = 44;
const kCrushNearestCar = 45;

// Scenes. They match kScenePropFirst and kSceneCarBehind in src/components/WorldView/WorldView.h.
const kScenePropFirst = -160;
const kSceneCarBehind = -106;
// Lamps stand along every city street. The desert only has them at gas stations, so its
// thin standing thing to check is the telephone pole.
const kKinds = { city: [['wall', 0], ['tree', 1], ['lamp', 2]], desert: [['wall', 0], ['tree', 1], ['telephone pole', 3]] };
const sceneOf = (kind, variant) => kScenePropFirst - 3 * kind - variant;

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

async function openRun() {
  const opened = await openGame(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
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
  return boxBytes(page, box);
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

const difference = (a, b, i) => Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]);

function changedPixels(a, b) {
  let changed = 0;
  for (let i = 0; i < a.length; i += 4) if (difference(a, b, i) > 6) changed++;
  return changed;
}

// What the cover does to a zombie behind it. Hidden pixels are the ones where turning cover
// off changes the picture. On those, the faint silhouette must still differ from the empty
// scene, and less strongly than the whole zombie does. The zombie's shadow on the ground is
// hidden too, with no silhouette, so not every hidden pixel can show.
function coverage(empty, covered, uncovered) {
  let hidden = 0;
  let shown = 0;
  let stillShow = 0;
  let faint = 0;
  let whole = 0;
  for (let i = 0; i < empty.length; i += 4) {
    if (difference(covered, uncovered, i) <= 6) continue;
    hidden++;
    const shows = difference(empty, covered, i);
    if (shows > 6) shown++;
    faint += shows;
    whole += difference(empty, uncovered, i);
  }
  stillShow = hidden ? shown / hidden : 0;
  return { hidden, shown, stillShow, strength: whole ? faint / whole : 1 };
}

// In page pixels, the part of the scene's object where its zombie stands, with room for the
// zombie, and none for the soldier further to the left.
function propBox(world) {
  const scale = world.pixelScale;
  const [spotX, spotY] = world.spot;
  const top = Math.min(world.prop[1], spotY - 40);
  const x = Math.max(0, Math.round((spotX - 24) * scale));
  const y = Math.max(0, Math.round((top - 10) * scale));
  return { x, y, w: Math.round(48 * scale), h: Math.round((spotY + 24 - top + 10) * scale) };
}

async function openSeed(page, map, seed) {
  await command(page, kSetMap, map);
  await command(page, kSetSeed, seed);
}

test('every standing pixel the painters mark is in a listed cover, on both maps', async () => {
  const { context, page, problems } = await openRun();
  const kinds = { walls: 0, trees: 0, lamps: 0 };
  for (const map of [kCity, kWasteland]) {
    for (const seed of [3, 1234]) {
      const report = await page.evaluate(([m, s]) => JSON.parse(Module.ccall('ls_check_cover_marks', 'string', ['number', 'number', 'number'], [m, s, 3])), [map, seed]);
      const where = `map ${map} seed ${seed}: ${JSON.stringify(report)}`;
      assert.ok(report.marked > 1000, `standing objects must be marked, or this proves nothing: ${where}`);
      assert.equal(report.strayPixels, 0, `a marked pixel outside every cover would hide a zombie with no silhouette: ${where}`);
      assert.ok(report.busiest < report.capacity, `the busiest chunk must fit in its list: ${where}`);
      for (const kind of Object.keys(kinds)) kinds[kind] += report[kind];
    }
  }
  assert.ok(kinds.walls > 0 && kinds.trees > 0 && kinds.lamps > 0, `walls, trees and lamps must all be listed: ${JSON.stringify(kinds)}`);
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, map, kinds] of [['the city', kCity, kKinds.city], ['the desert', kWasteland, kKinds.desert]]) {
  for (const [kindName, kind] of kinds) {
    for (const [when, time] of kindName === 'wall' ? [['by day', kDay], ['at night', kNight]] : [['by day', kDay]]) {
      test(`in ${name} ${when}, a ${kindName} covers a zombie behind it, shows its silhouette, and a zombie in front draws over it`, async () => {
        const { context, page, problems } = await openRun();
        await openSeed(page, map, 8);
        await sceneBytes(page, sceneOf(kind, 0), time, { x: 0, y: 0, w: 4, h: 4 });
        const world = await readWorld(page);
        assert.ok(world.prop[2] > world.prop[0], `the scene must find a ${kindName}`);
        const box = propBox(world);

        const empty = await sceneBytes(page, sceneOf(kind, 0), time, box);
        const behind = await sceneBytes(page, sceneOf(kind, 1), time, box);
        const behindWorld = await readWorld(page);
        const front = await sceneBytes(page, sceneOf(kind, 2), time, box);
        const frontWorld = await readWorld(page);
        await command(page, kCarCover, 0);
        const behindUncovered = await sceneBytes(page, sceneOf(kind, 1), time, box);
        const frontUncovered = await sceneBytes(page, sceneOf(kind, 2), time, box);
        await command(page, kCarCover, 1);

        assert.equal(behindWorld.silhouettes, 1, 'the zombie behind must be drawn as a silhouette');
        assert.equal(behindWorld.coversDropped, 0);
        assert.ok(changedPixels(empty, behindUncovered) > 40, 'the zombie must overlap the object, or this proves nothing');
        const cover = coverage(empty, behind, behindUncovered);
        assert.ok(cover.hidden > 12, `the ${kindName} must cover part of the zombie behind it: ${JSON.stringify(cover)}`);
        assert.ok(cover.shown >= 20 && cover.stillShow > 0.15, `a covered zombie must still show as a silhouette: ${JSON.stringify(cover)}`);
        assert.ok(cover.strength > 0.08 && cover.strength < 0.75, `the silhouette must show, fainter than the zombie: ${JSON.stringify(cover)}`);

        // A zombie in front owes nothing to the cover.
        assert.equal(frontWorld.silhouettes, 0, 'a zombie in front needs no silhouette');
        assert.ok(changedPixels(empty, front) > 40, 'a zombie in front must show over the object');
        assert.ok(changedPixels(front, frontUncovered) <= 4, `a ${kindName} must not cover a zombie in front of it`);
        assert.deepEqual(problems, []);
        await context.close();
      });
    }
  }
}

test('half resolution ground, for weak devices, still covers a zombie behind a wall', async () => {
  const { context, page, problems } = await openRun();
  await openSeed(page, kCity, 8);
  await command(page, kSetQuality, 0);
  await sceneBytes(page, sceneOf(0, 0), kDay, { x: 0, y: 0, w: 4, h: 4 });
  const box = propBox(await readWorld(page));
  const empty = await sceneBytes(page, sceneOf(0, 0), kDay, box);
  const covered = await sceneBytes(page, sceneOf(0, 1), kDay, box);
  await command(page, kCarCover, 0);
  const uncovered = await sceneBytes(page, sceneOf(0, 1), kDay, box);
  await command(page, kCarCover, 1);
  const cover = coverage(empty, covered, uncovered);
  assert.ok(cover.hidden > 12 && cover.shown >= 20 && cover.strength > 0.08, `the wall must cover the zombie and show its silhouette: ${JSON.stringify(cover)}`);
  assert.deepEqual(problems, []);
  await context.close();
});

for (const [name, map] of [['the city', kCity], ['the desert', kWasteland]]) {
  test(`in ${name} at night, a flat crushed car still covers nothing`, async () => {
    const { context, page, problems } = await openRun();
    await openSeed(page, map, 8);
    await command(page, kShowcase, kSceneCarBehind);
    await command(page, kSetTime, kNight);
    await page.waitForTimeout(500);
    const whole = await readWorld(page);
    assert.equal(whole.silhouettes, 1, 'the zombie behind the whole car must be a silhouette, or this proves nothing');
    const [left, top, right, bottom] = whole.car.map((v) => v * whole.pixelScale);
    const box = { x: Math.round(left), y: Math.round(top), w: Math.round(right - left), h: Math.round(bottom - top) };
    await command(page, kCrushNearestCar, 0);
    await page.waitForTimeout(1300);
    const flat = await readWorld(page);
    assert.equal(flat.crushed, 1);
    assert.equal(flat.silhouettes, 0, 'nothing is covered, so nothing needs a silhouette');
    const covered = await boxBytes(page, box);
    await command(page, kCarCover, 0);
    await page.waitForTimeout(200);
    const uncovered = await boxBytes(page, box);
    await command(page, kCarCover, 1);
    assert.equal(changedPixels(covered, uncovered), 0, 'a flat car must leave no depth behind');
    assert.deepEqual(problems, []);
    await context.close();
  });
}
