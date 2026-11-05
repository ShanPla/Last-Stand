// The boss's newer attacks in a real browser: the spiral, the cross and the trail. Each
// warning must really be drawn, with the boss standing outside the picture, by day and at
// night, on a tall phone and on a laptop. Then the attack is played out until it lands.
// Set BOSSMOVES_SHOTS to a folder to keep the screenshots.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { startServer, launchChrome, watchProblems, readState, debug, CMD, waitScreen } from './harness.mjs';

// Debug command numbers and test scenes. They match App.cpp and WorldView.h.
const kShowcase = 17;
const kEmptyWorld = -1;
const kSceneBossSpiral = -140;
const kSceneBossCross = -141;
const kSceneBossTrail = -142;

const kDaySeconds = 30;
const kNightSeconds = 420;

const phone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true };
const laptop = { viewport: { width: 1280, height: 720 } };

const attacks = [
  { name: 'spiral', scene: kSceneBossSpiral, chargesUp: true },
  { name: 'cross', scene: kSceneBossCross, chargesUp: false },
  { name: 'trail', scene: kSceneBossTrail, chargesUp: false },
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

async function open(options) {
  const context = await browser.newContext(options);
  const page = await context.newPage();
  const problems = watchProblems(page);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  await debug(page, CMD.startRun, 91);
  await waitScreen(page, 'playing');
  return { context, page, problems };
}

const readWorld = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));
const waitFrames = (page, count) => page.evaluate((n) => new Promise((resolve) => {
  const step = () => (n-- <= 0 ? resolve() : requestAnimationFrame(step));
  step();
}), count);

async function showScene(page, scene) {
  await debug(page, kShowcase, scene);
  await waitFrames(page, 4);
}

async function keep(name, picture) {
  if (process.env.BOSSMOVES_SHOTS) await writeFile(`${process.env.BOSSMOVES_SHOTS}/${name}.png`, picture);
}

// Counts pixels that turned the bright orange of a warning between two screenshots. The
// work happens in the page, which is much faster than sending the pixels back here.
function warmPixels(page, first, second) {
  return page.evaluate(async ([before, after]) => {
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
    const columns = innerWidth * devicePixelRatio;
    // The debug overlay's frame times sit along the bottom, and differ between any two frames.
    const lastByte = Math.max(0, a.length - columns * 4 * 70 * devicePixelRatio);
    let warm = 0;
    for (let i = 0; i < lastByte; i += 4) {
      const gap = Math.abs(b[i] - a[i]) + Math.abs(b[i + 1] - a[i + 1]) + Math.abs(b[i + 2] - a[i + 2]);
      if (gap > 40 && b[i] > 150 && b[i] > b[i + 2] + 70 && b[i + 1] < b[i]) warm++;
    }
    return warm;
  }, [first.toString('base64'), second.toString('base64')]);
}

for (const [size, options] of [['phone', phone], ['laptop', laptop]]) {
  for (const attack of attacks) {
    test(`on a ${size}, the boss's ${attack.name} warning shows with the boss outside the picture, by day and at night`, async () => {
      const { context, page, problems } = await open(options);
      for (const [light, seconds] of [['day', kDaySeconds], ['night', kNightSeconds]]) {
        await showScene(page, kEmptyWorld);
        await debug(page, CMD.setTime, seconds);
        await waitFrames(page, 4);
        const without = await page.screenshot();
        await showScene(page, attack.scene);
        const world = await readWorld(page);
        // The spiral warns around the boss itself, so it goes through the telegraph list.
        // The other two are marks on the ground at the player's feet.
        assert.equal(world.telegraphing, attack.chargesUp ? 1 : 0, 'only the spiral charges up');
        assert.equal(world.telegraphsShown, attack.chargesUp ? 1 : 0, 'a charge-up from outside the picture must be drawn');
        const withWarning = await page.screenshot();
        await keep(`${attack.name}-warning-${light}-${options.viewport.width}`, withWarning);
        const warm = await warmPixels(page, without, withWarning);
        assert.ok(warm > 300, `the ${attack.name} warning must really be on screen at ${light}, found ${warm} warm pixels`);
      }
      assert.deepEqual(problems, []);
      await context.close();
    });
  }

  test(`on a ${size}, each new attack lands after its warning: the cross and trail hurt a player who stays put, the spiral fires`, async () => {
    const { context, page, problems } = await open(options);
    for (const attack of attacks) {
      await showScene(page, attack.scene);
      await debug(page, CMD.setTime, kNightSeconds);
      await debug(page, CMD.holdSim, 1);
      await waitFrames(page, 2);
      const startHp = (await readState(page)).hp;
      let state = await readState(page);
      // A step at a time, until the attack lands. The warning is at most 0.6 seconds long.
      for (let i = 0; i < 40; i++) {
        await debug(page, CMD.runSteps, 2);
        state = await readState(page);
        if (attack.chargesUp ? state.bullets > 0 : state.hp < startHp) break;
      }
      if (attack.chargesUp) {
        assert.ok(state.bullets > 0, 'the spiral must fire once its warning ends');
        await debug(page, CMD.runSteps, 80);  // Let the pinwheel open out into the picture.
      } else {
        assert.ok(state.hp < startHp, `the ${attack.name} must hurt a player who stands in it`);
      }
      await waitFrames(page, 2);
      await keep(`${attack.name}-hit-night-${options.viewport.width}`, await page.screenshot());
      await debug(page, CMD.holdSim, 0);
    }
    assert.deepEqual(problems, []);
    await context.close();
  });
}
