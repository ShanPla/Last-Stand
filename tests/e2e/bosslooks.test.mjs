// The boss's twelve looks in a real browser: three phases for each of the four
// difficulties. Each one must draw a picture of its own, by day on a laptop and at night on
// a tall phone, and switching difficulty must repaint the boss with no warnings.
// Set BOSSLOOKS_SHOTS to a folder to keep the screenshots.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { startServer, launchChrome, watchProblems, debug, CMD, waitScreen } from './harness.mjs';

// Debug command and scene numbers. They match App.cpp and WorldView.h.
const kShowcase = 17;
const kSceneBossLook = -180;  // -180 - (difficulty * 3 + phase)
const levels = ['easy', 'normal', 'hard', 'hell'];

const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true };
const laptop = { viewport: { width: 1280, height: 720 } };

// Where the boss stands in its scene, as fractions of the screen: above the soldier.
const views = [
  { name: 'laptop', options: laptop, light: 'day', seconds: 30, box: [0.32, 0.0, 0.36, 0.5] },
  { name: 'phone', options: phone, light: 'night', seconds: 420, box: [0.1, 0.08, 0.8, 0.34] },
];

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

const waitFrames = (page, count) => page.evaluate((n) => new Promise((resolve) => {
  const step = () => (n-- <= 0 ? resolve() : requestAnimationFrame(step));
  step();
}), count);

// For every pair of pictures, the share of pixels that differ clearly. The work happens in
// the page, which is much faster than sending the pixels back here.
function pairDifferences(page, pictures) {
  return page.evaluate(async (list) => {
    const read = async (data) => {
      const image = await createImageBitmap(await (await fetch(`data:image/png;base64,${data}`)).blob());
      const canvas = new OffscreenCanvas(image.width, image.height);
      const context = canvas.getContext('2d');
      context.drawImage(image, 0, 0);
      return context.getImageData(0, 0, image.width, image.height).data;
    };
    const pixels = await Promise.all(list.map(read));
    const result = [];
    for (let a = 0; a < pixels.length; a++) {
      for (let b = a + 1; b < pixels.length; b++) {
        const p = pixels[a];
        const q = pixels[b];
        let differ = 0;
        for (let i = 0; i < p.length; i += 4) {
          if (Math.abs(p[i] - q[i]) + Math.abs(p[i + 1] - q[i + 1]) + Math.abs(p[i + 2] - q[i + 2]) > 30) differ++;
        }
        result.push({ a, b, share: differ / (p.length / 4) });
      }
    }
    return result;
  }, pictures.map((picture) => picture.toString('base64')));
}

for (const view of views) {
  test(`on a ${view.name} at ${view.light}, each difficulty and phase draws its own boss picture`, async () => {
    const context = await browser.newContext(view.options);
    const page = await context.newPage();
    const problems = watchProblems(page);
    const repaints = [];
    page.on('console', (msg) => { if (msg.text().startsWith('boss looks for difficulty')) repaints.push(msg.text()); });
    await page.goto(`${web.url}?debug`);
    await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
    await waitScreen(page, 'title');
    await debug(page, CMD.startRun, 91);
    await waitScreen(page, 'playing');
    await debug(page, CMD.setTime, view.seconds);

    const { width, height } = view.options.viewport;
    const [x, y, w, h] = view.box;
    const clip = { x: Math.round(x * width), y: Math.round(y * height), width: Math.round(w * width), height: Math.round(h * height) };
    const pictures = [];
    const names = [];
    for (let level = 0; level < levels.length; level++) {
      for (let phase = 0; phase < 3; phase++) {
        await debug(page, kShowcase, kSceneBossLook - (level * 3 + phase));
        await waitFrames(page, 4);
        const state = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));
        assert.equal(state.difficulty, level, 'the scene must switch the run to its difficulty');
        const picture = await page.screenshot({ clip });
        pictures.push(picture);
        names.push(`${levels[level]}-phase${phase + 1}`);
        if (process.env.BOSSLOOKS_SHOTS) await writeFile(`${process.env.BOSSLOOKS_SHOTS}/${view.name}-${view.light}-${names.at(-1)}.png`, picture);
      }
    }

    // Any two looks must differ in a real part of the picture, not just a few pixels.
    for (const { a, b, share } of await pairDifferences(page, pictures)) {
      assert.ok(share > 0.02, `${names[a]} and ${names[b]} must look different, but only ${(share * 100).toFixed(2)}% of the pixels differ`);
    }
    // The page starts with Normal painted, so Easy, Hard and Hell each repaint once.
    assert.ok(repaints.length >= 3, `switching difficulty must repaint the boss, saw ${repaints.length} repaints`);
    assert.deepEqual(problems, []);
    await context.close();
  });
}
