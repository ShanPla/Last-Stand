// Checks of the desert highway map: it paints from the seed alone, looks nothing like the
// city, survives any amount of walking, and switching maps leaves nothing behind.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { startServer, launchChrome, openGame, startGame } from './harness.mjs';

// Debug command numbers. They match DebugCommand in src/core/App.cpp.
const kSetSeed = 3;
const kShowcase = 17;  // Value -1: an empty world, with the clock stopped.
const kSetRunTime = 12;
const kMovePlayerX = 18;
const kMovePlayerY = 19;
const kSetQuality = 20;
const kSetMap = 28;

const kCity = 0;
const kWasteland = 1;

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

async function openRun(options = { viewport: { width: 1280, height: 720 } }) {
  const opened = await openGame(browser, `${web.url}?debug`, options);
  await startGame(opened.page);
  return opened;
}

const command = (page, id, value) => page.evaluate(([c, v]) => Module._ls_debug_command(c, v), [id, value]);

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

function colorCount(bytes) {
  const colors = new Set();
  for (let i = 0; i < bytes.length; i += 4) colors.add((bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2]);
  return colors.size;
}

function averageColor(bytes) {
  let [r, g, b] = [0, 0, 0];
  for (let i = 0; i < bytes.length; i += 4) { r += bytes[i]; g += bytes[i + 1]; b += bytes[i + 2]; }
  const n = bytes.length / 4;
  return [r / n, g / n, b / n];
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

test('the same seed gives the same desert at the same place, and another seed differs', async () => {
  const { context, page, problems } = await openRun();
  await command(page, kSetMap, kWasteland);
  const first = await groundAt(page, 1234, 2000, -1500);
  // Walk away so the chunks get replaced, then come back and paint them again.
  await groundAt(page, 1234, -9000, 7000);
  const again = await groundAt(page, 1234, 2000, -1500);
  assert.equal(difference(first, again), 0, 'the same seed must paint exactly the same pixels');
  const other = await groundAt(page, 4321, 2000, -1500);
  assert.ok(difference(first, other) > 0.2, 'a different seed must paint a different desert');
  assert.deepEqual(problems, []);
  await context.close();
});

test('nearby places differ, so the desert does not visibly repeat', async () => {
  const { context, page, problems } = await openRun();
  await command(page, kSetMap, kWasteland);
  const a = await groundAt(page, 77, 0, 0);
  // One chunk away, where a repeating tile would line up exactly.
  const b = await groundAt(page, 77, 640, 0);
  const c = await groundAt(page, 77, 0, 640);
  assert.ok(difference(a, b) > 0.2, 'places side by side must differ');
  assert.ok(difference(a, c) > 0.2, 'places above each other must differ');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the desert clearly differs from the city at the same seed and spot', async () => {
  const { context, page, problems } = await openRun();
  const city = await groundAt(page, 42, 3000, 3000);
  await command(page, kSetMap, kWasteland);
  await page.waitForTimeout(300);
  const desert = await readBox(page, groundBox);
  assert.ok(difference(city, desert) > 0.5, 'the two maps must not look alike');
  assert.deepEqual(problems, []);
  await context.close();
});

test('walking far in every direction keeps drawing the desert', async () => {
  const { context, page, problems } = await openRun({ viewport: { width: 640, height: 360 } });
  await command(page, kSetMap, kWasteland);
  await command(page, kSetSeed, 5);
  await command(page, kShowcase, -1);
  const places = [[60000, 0], [-60000, 0], [0, 60000], [0, -60000], [-140000, -140000], [140000, 140000], [-400000, 300000]];
  for (const [x, y] of places) {
    await command(page, kMovePlayerX, x);
    await command(page, kMovePlayerY, y);
    await page.waitForTimeout(200);
    const box = await readBox(page, { x: 0, y: 200, w: 200, h: 150 });
    const colors = colorCount(box);
    assert.ok(colors > 8, `the ground at ${x},${y} must have detail, found ${colors} colors`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('real walking with the keyboard on the desert raises no errors', async () => {
  const { context, page, problems } = await openGame(browser, `${web.url}?debug`, { viewport: { width: 640, height: 360 } });
  await startGame(page);
  await command(page, kSetMap, kWasteland);
  for (const key of ['d', 'w', 'a', 's']) {
    await page.keyboard.down(key);
    await page.waitForTimeout(1000);
    await page.keyboard.up(key);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('switching maps repaints everything, and a bad id falls back to the city', async () => {
  const { context, page, problems } = await openRun();
  const city = await groundAt(page, 42, 3000, 3000);
  await command(page, kSetMap, kWasteland);
  await page.waitForTimeout(300);
  const desert = await readBox(page, groundBox);
  // Back again: not one pixel of the old map may survive, and the picture must be exact.
  await command(page, kSetMap, kCity);
  await page.waitForTimeout(300);
  assert.equal(difference(city, await readBox(page, groundBox)), 0, 'going back must paint the same city');
  await command(page, kSetMap, kWasteland);
  await page.waitForTimeout(300);
  assert.equal(difference(desert, await readBox(page, groundBox)), 0, 'going back must paint the same desert');
  for (const bad of [99, -1, 2]) {
    await command(page, kSetMap, kWasteland);
    await page.waitForTimeout(200);
    await command(page, kSetMap, bad);
    await page.waitForTimeout(300);
    assert.equal(difference(city, await readBox(page, groundBox)), 0, `map id ${bad} must fall back to the city`);
  }
  assert.deepEqual(problems, []);
  await context.close();
});

test('the city stays the default map', async () => {
  const { context, page, problems } = await openRun();
  const fresh = await groundAt(page, 42, 3000, 3000);
  await command(page, kSetMap, kCity);
  await page.waitForTimeout(300);
  assert.equal(difference(fresh, await readBox(page, groundBox)), 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('low quality still draws the desert, by day and by night', async () => {
  const { context, page, problems } = await openRun();
  await command(page, kSetMap, kWasteland);
  await command(page, kSetSeed, 21);
  await command(page, kShowcase, -1);
  await command(page, kSetQuality, 0);
  await page.waitForTimeout(400);
  assert.ok(colorCount(await readBox(page, groundBox)) > 8);
  await command(page, kSetRunTime, 420);
  await page.waitForTimeout(400);
  assert.ok(colorCount(await readBox(page, groundBox)) > 8);
  // Changing quality again must flush and repaint without trouble.
  await command(page, kSetQuality, 1);
  await page.waitForTimeout(300);
  assert.ok(colorCount(await readBox(page, groundBox)) > 8);
  assert.deepEqual(problems, []);
  await context.close();
});

test('sand stays muted: the ground is never bright, at noon or at night', async () => {
  const { context, page, problems } = await openRun();
  await command(page, kSetMap, kWasteland);
  for (const [x, y] of [[0, 0], [9000, -4000], [-20000, 15000]]) {
    const ground = await groundAt(page, 11, x, y);
    const [r, g, b] = averageColor(ground);
    assert.ok((r + g + b) / 3 < 125, `the ground at ${x},${y} is too bright for bullets and gems: ${r | 0},${g | 0},${b | 0}`);
    assert.ok(r > b, 'it must still read as warm sand');
  }
  await command(page, kSetRunTime, 420);
  await page.waitForTimeout(300);
  const [r, g, b] = averageColor(await readBox(page, groundBox));
  assert.ok((r + g + b) / 3 < 70, 'night in the desert must stay dark');
  assert.deepEqual(problems, []);
  await context.close();
});
