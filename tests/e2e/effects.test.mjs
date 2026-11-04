// Proof that game feel really follows the settings. Each test reads the effects' own
// counters through a debug query, because a setting that is saved but ignored looks
// exactly the same in the settings screen. The software renderer is slow, so tests wait
// on what happened, not on how long it should have taken.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import {
  startServer, launchChrome, openGame as openPage, waitScreen, waitSelected, clickButton, readUi, readState, CMD,
} from './harness.mjs';

const kPhone = { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true };

// The event types these tests send. They match EventType in src/core/GameState.h.
const EVENT = { playerHit: 3, levelUp: 5, bomb: 7, weaponFired: 8, bossPhase: 13, explosion: 16 };

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

const readFx = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])));
const send = (page, command, value = 0) => page.evaluate(([c, v]) => Module._ls_debug_command(c, v), [command, value]);

// A real run, with the quality fixed: this renderer would otherwise count as a weak device.
async function openRun(options = {}, seed = 4242) {
  const opened = await openPage(browser, `${web.url}?debug`, options);
  await waitScreen(opened.page, 'title');
  await send(opened.page, CMD.setQuality, 1);
  await send(opened.page, CMD.startRun, seed);
  await waitScreen(opened.page, 'playing');
  await send(opened.page, CMD.resetFxCounters);
  return opened;
}

async function waitForFx(page, check, what, timeout = 15000) {
  const start = Date.now();
  for (;;) {
    const fx = await readFx(page);
    if (check(fx)) return fx;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}: ${JSON.stringify(fx)}`);
    await page.waitForTimeout(50);
  }
}

// Sends a big event on repeat for a moment, so a slow renderer still sees its shake.
async function shakeHard(page, ms = 700) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    await send(page, CMD.pushEvent, EVENT.bossPhase);
    await page.waitForTimeout(40);
  }
}

test('screen shake follows its setting, in whole pixels, and applies at once', async () => {
  const { context, page, problems } = await openRun();
  await shakeHard(page);
  const full = await readFx(page);
  assert.ok(full.shakeEvents > 0);
  assert.ok(full.shakePeak >= 2, `a big event must really shake: ${full.shakePeak}`);
  assert.equal(full.shakePeak, Math.round(full.shakePeak), 'the camera only moves in whole art pixels');

  await send(page, CMD.setShake, 30);
  await send(page, CMD.resetFxCounters);
  await shakeHard(page);
  const low = await readFx(page);
  assert.ok(low.shakePeak <= 2, `30% of the five pixel maximum, rounded: ${low.shakePeak}`);
  assert.ok(low.shakePeak < full.shakePeak);

  await send(page, CMD.setShake, 0);
  await send(page, CMD.resetFxCounters);
  await shakeHard(page);
  const off = await readFx(page);
  assert.equal(off.shakePeak, 0);
  assert.equal(off.shakeEvents, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('damage numbers turn on and off through the real settings screen, mid-run', async () => {
  const { context, page, problems } = await openRun();
  await send(page, CMD.stressEnemies, 400);
  await send(page, CMD.stressProjectiles, 600);
  const on = await waitForFx(page, (fx) => fx.numbersPeak > 0, 'numbers to show');
  assert.ok(on.numberEvents > 0);

  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await clickButton(page, 'Damage numbers');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_ui', 'string', [], [])).damageNumbers === 0);
  assert.equal((await readFx(page)).numbersOn, 0, 'the effects hear about the change at once');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'playing');

  await send(page, CMD.resetFxCounters);
  const hitsBefore = (await readFx(page)).particlesSpawned;
  await waitForFx(page, (fx) => fx.particlesSpawned > hitsBefore + 40, 'the fight to go on');
  const off = await readFx(page);
  assert.equal(off.numberEvents, 0);
  assert.equal(off.numbers, 0);
  assert.equal(off.numbersPeak, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('damage numbers stay under their hard cap in a huge crowd', async () => {
  const { context, page, problems } = await openRun();
  await send(page, CMD.stressEnemies, 2000);
  await send(page, CMD.stressProjectiles, 3000);
  const fx = await waitForFx(page, (now) => now.numberEvents > 500, 'many hits');
  assert.ok(fx.numbersPeak <= 16, `hard cap: ${fx.numbersPeak}`);
  assert.ok(fx.numbersMerged > fx.numbersSpawned, 'most hits must add to a number already showing');
  assert.ok(fx.particles <= 1400);
  assert.deepEqual(problems, []);
  await context.close();
});

test('flash intensity scales screen flashes, hit flashes and explosion light', async () => {
  const { context, page, problems } = await openRun();
  // Screen flash: a boss phase asks for 0.26.
  await send(page, CMD.pushEvent, EVENT.bossPhase);
  const full = await waitForFx(page, (fx) => fx.flashesShown > 0, 'a screen flash');
  assert.ok(Math.abs(full.flashPeak - 0.26) < 0.001, `full strength: ${full.flashPeak}`);

  await page.waitForTimeout(1100);  // The limiter refuses a second flash sooner than this.
  await send(page, CMD.setFlash, 50);
  await send(page, CMD.resetFxCounters);
  await send(page, CMD.pushEvent, EVENT.bossPhase);
  const half = await waitForFx(page, (fx) => fx.flashesShown > 0, 'a half flash');
  assert.ok(Math.abs(half.flashPeak - 0.13) < 0.001, `half strength: ${half.flashPeak}`);

  await page.waitForTimeout(1100);
  await send(page, CMD.setFlash, 0);
  await send(page, CMD.resetFxCounters);
  await send(page, CMD.pushEvent, EVENT.bossPhase);
  await page.waitForTimeout(200);
  const none = await readFx(page);
  assert.equal(none.flashesShown, 0);
  assert.equal(none.flashPeak, 0);

  // Hit flash and explosion light: a real fight with a full build, which includes rockets.
  await send(page, CMD.giveBuild);
  await send(page, CMD.stressEnemies, 300);
  await send(page, CMD.stressProjectiles, 500);
  await send(page, CMD.setFlash, 100);
  await send(page, CMD.resetFxCounters);
  const lit = await waitForFx(page, (fx) => fx.hitFlashPeak > 0.2 && fx.explosionLightPeak > 0.2, 'hits and a blast at full flash');
  assert.ok(lit.hitFlashPeak <= 0.85);
  assert.ok(lit.zombiesFlashedPeak > 0);

  await send(page, CMD.setFlash, 50);
  await send(page, CMD.resetFxCounters);
  const halved = await waitForFx(page, (fx) => fx.hitFlashPeak > 0 && fx.explosionLightPeak > 0, 'hits and a blast at half flash');
  await page.waitForTimeout(500);
  const halvedLater = await readFx(page);
  assert.ok(halvedLater.hitFlashPeak <= 0.43, `half of 0.85 at most: ${halved.hitFlashPeak}`);
  assert.ok(halvedLater.explosionLightPeak <= 0.51, `half of 1 at most: ${halvedLater.explosionLightPeak}`);

  await send(page, CMD.setFlash, 0);
  await send(page, CMD.resetFxCounters);
  const before = (await readFx(page)).particlesSpawned;
  await waitForFx(page, (fx) => fx.particlesSpawned > before + 60, 'the fight to go on');
  const dark = await readFx(page);
  assert.equal(dark.hitFlashPeak, 0);
  assert.equal(dark.explosionLightPeak, 0);
  assert.equal(dark.zombiesFlashedPeak, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a crowd under fire never flashes more than a limited share at once', async () => {
  const { context, page, problems } = await openRun();
  await send(page, CMD.stressEnemies, 2000);
  await send(page, CMD.stressProjectiles, 3000);
  await page.waitForTimeout(3000);
  const fx = await waitForFx(page, (now) => now.flashingCount > 40, 'a big crowd under fire');
  assert.ok(fx.hitFlashPeak <= 0.85);
  // One stride can overshoot a little, plus elites, so this leaves a small margin.
  assert.ok(fx.zombiesFlashedPeak <= 60, `flashing at once: ${fx.zombiesFlashedPeak}, of ${fx.flashingCount} hit`);
  assert.deepEqual(problems, []);
  await context.close();
});

test('reduced motion stops shake and hit pause, cuts particles and softens flashes', async () => {
  const { context, page, problems } = await openRun();
  // Normal first, so the test shows what the setting takes away.
  await send(page, CMD.pushEvent, EVENT.bossPhase);
  const normal = await waitForFx(page, (fx) => fx.hitPauses > 0 && fx.shakePeak > 0, 'a hit pause and a shake');
  assert.ok(normal.hitPauseSeconds > 0.05 && normal.hitPauseSeconds < 0.13);
  assert.ok(Math.abs(normal.amount - 1) < 0.001);
  const a = (await readFx(page)).particlesSpawned;
  await send(page, CMD.pushEvent, EVENT.levelUp);
  await page.waitForFunction((was) => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])).particlesSpawned > was, a);
  const fullBurst = (await readFx(page)).particlesSpawned - a;

  await send(page, CMD.setReducedMotion, 1);
  await page.waitForTimeout(1100);
  await send(page, CMD.resetFxCounters);
  assert.equal((await readFx(page)).reduced, 1);

  await shakeHard(page);
  const reduced = await readFx(page);
  assert.equal(reduced.shakePeak, 0);
  assert.equal(reduced.shakeEvents, 0);
  assert.equal(reduced.hitPauses, 0);
  assert.ok(reduced.flashPeak <= 0.1, `a gentle flash: ${reduced.flashPeak}`);
  assert.ok(Math.abs(reduced.amount - 0.35) < 0.001);

  const b = (await readFx(page)).particlesSpawned;
  await send(page, CMD.pushEvent, EVENT.levelUp);
  await page.waitForFunction((was) => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])).particlesSpawned > was, b);
  const smallBurst = (await readFx(page)).particlesSpawned - b;
  assert.ok(smallBurst < fullBurst, `fewer particles: ${smallBurst} against ${fullBurst}`);

  // And the hit flash, in a real fight, is only ever a gentle tint.
  await send(page, CMD.stressEnemies, 300);
  await send(page, CMD.stressProjectiles, 500);
  const fight = await waitForFx(page, (fx) => fx.hitFlashPeak > 0, 'hits');
  assert.ok(fight.hitFlashPeak <= 0.3, `hit flash under reduced motion: ${fight.hitFlashPeak}`);

  // Turning it off again works at once too.
  await send(page, CMD.setReducedMotion, 0);
  await page.waitForTimeout(800);
  await send(page, CMD.resetFxCounters);
  await send(page, CMD.pushEvent, EVENT.bossPhase);
  await waitForFx(page, (fx) => fx.shakePeak > 0 && fx.hitPauses > 0, 'shake and pause to return');
  assert.deepEqual(problems, []);
  await context.close();
});

test('hit pause is short, never stacks, and only comes with big moments', async () => {
  const { context, page, problems } = await openRun();
  // Everyday events never pause: this much firing and explosion must not freeze anything.
  for (let i = 0; i < 10; i++) {
    await send(page, CMD.pushEvent, EVENT.weaponFired);
    await send(page, CMD.pushEvent, EVENT.explosion);
    await send(page, CMD.pushEvent, EVENT.levelUp);
  }
  await page.waitForTimeout(200);
  assert.equal((await readFx(page)).hitPauses, 0);

  // Big moments, one on top of the other: the first pauses and the rest are ignored.
  const tickBefore = (await readState(page)).tick;
  for (let i = 0; i < 20; i++) await send(page, CMD.pushEvent, EVENT.bossPhase);
  await waitForFx(page, (fx) => fx.hitPauses > 0, 'a hit pause');
  const fx = await readFx(page);
  assert.equal(fx.hitPauses, 1, 'a pile of big moments gives one pause');
  assert.ok(fx.hitPauseSeconds <= 0.12, `short: ${fx.hitPauseSeconds}`);

  // The game keeps going straight after.
  await page.waitForFunction((was) => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])).tick > was + 10, tickBefore);

  // Nearly dying pauses once, and doesn't pause again on the next hit.
  await page.waitForTimeout(900);
  await send(page, CMD.resetFxCounters);
  await send(page, CMD.setHp, 10);
  await send(page, CMD.pushEvent, EVENT.playerHit);
  await waitForFx(page, (now) => now.hitPauses === 1, 'the near death pause');
  await page.waitForTimeout(900);
  await send(page, CMD.pushEvent, EVENT.playerHit);
  await page.waitForTimeout(150);
  assert.equal((await readFx(page)).hitPauses, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the settings screen connects reduced motion and the sliders to the effects', async () => {
  const { context, page, problems } = await openRun();
  await page.keyboard.press('Escape');
  await waitScreen(page, 'paused');
  await clickButton(page, 'Settings');
  await waitScreen(page, 'settings');
  await clickButton(page, 'Reduced motion');
  await page.waitForFunction(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])).reduced === 1);
  // The shake and flash rows with the keyboard, each one step lower.
  for (const [row, key] of [['Screen shake', 'shakeSetting'], ['Flash intensity', 'flashSetting']]) {
    for (let tries = 0; tries < 8 && !(await readUi(page)).selected.startsWith(row); tries++) {
      await page.keyboard.press('ArrowDown');
      await page.waitForTimeout(150);
    }
    await waitSelected(page, row);
    await page.keyboard.press('ArrowLeft');
    await page.waitForFunction((name) => JSON.parse(Module.ccall('ls_query_effects', 'string', [], []))[name] < 0.95, key);
  }
  const ui = await readUi(page);
  const fx = await readFx(page);
  assert.ok(Math.abs(fx.shakeSetting - 0.9) < 0.01);
  assert.ok(Math.abs(fx.flashSetting - 0.9) < 0.01);
  assert.ok(Math.abs(fx.shakeSetting - ui.shake) < 0.01);
  assert.ok(Math.abs(fx.flashSetting - ui.flash) < 0.01);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a saved setting reaches the effects when the page loads', async () => {
  const { context, page, problems } = await openRun();
  await page.evaluate(() => localStorage.setItem('laststand.save', 'version=1\nshake=0.200\nflash=0.300\ndamageNumbers=0\nreducedMotion=1\n'));
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  const fx = await readFx(page);
  assert.ok(Math.abs(fx.shakeSetting - 0.2) < 0.001);
  assert.ok(Math.abs(fx.flashSetting - 0.3) < 0.001);
  assert.equal(fx.numbersOn, 0);
  assert.equal(fx.reduced, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('no full-screen flash rate gets near three a second, however many are asked for', async () => {
  const { context, page, problems } = await openRun();
  // Every event asks for a flash, on and on, for three seconds.
  const start = Date.now();
  while (Date.now() - start < 3000) {
    await send(page, CMD.pushEvent, EVENT.bomb);
    await page.waitForTimeout(20);
  }
  const fx = await readFx(page);
  assert.ok(fx.flashesShown >= 2, `some flashes get through: ${fx.flashesShown}`);
  assert.ok(fx.flashesShown <= 4, `at most about one a second: ${fx.flashesShown}`);
  assert.ok(fx.flashesRefused > 5);
  assert.deepEqual(problems, []);
  await context.close();
});

test('the same seed plays out identically whatever the effects are doing', async () => {
  const { context, page, problems } = await openPage(browser, `${web.url}?debug`, {});
  await waitScreen(page, 'title');
  // All in one go, with no frames in between, so only the settings differ between runs.
  const runWith = (settings) => page.evaluate(([cmd, shake, flash, numbers, reduced]) => {
    const send = (c, v = 0) => Module._ls_debug_command(c, v);
    send(cmd.setShake, shake);
    send(cmd.setFlash, flash);
    send(cmd.setDamageNumbers, numbers);
    send(cmd.setReducedMotion, reduced);
    send(cmd.startRun, 987654);
    send(cmd.giveBuild);
    send(cmd.setTime, 200);
    send(cmd.runSteps, 900);
    const state = JSON.parse(Module.ccall('ls_query_state', 'string', [], []));
    const fx = JSON.parse(Module.ccall('ls_query_effects', 'string', [], []));
    return { hash: state.hash, tick: state.tick, kills: state.kills, spawned: fx.particlesSpawned };
  }, [CMD, ...settings]);
  const a = await runWith([100, 100, 1, 0]);
  const b = await runWith([0, 0, 0, 1]);
  const c = await runWith([100, 100, 1, 0]);
  assert.equal(a.hash, b.hash, 'effects must never change the simulation');
  assert.equal(a.hash, c.hash);
  assert.equal(a.tick, b.tick);
  assert.ok(a.kills > 0, 'the fight must really have happened');
  assert.ok(c.spawned > a.spawned, 'the effects must really have run');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the glow can be switched off for weak devices', async () => {
  const { context, page, problems } = await openRun();
  await send(page, CMD.stressEnemies, 200);
  await send(page, CMD.stressProjectiles, 500);
  await waitForFx(page, (fx) => fx.glows > 0, 'shots to glow');
  await send(page, CMD.setQuality, 0);
  await send(page, CMD.resetFxCounters);
  const fx = await readFx(page);
  await page.waitForTimeout(400);
  const later = await readFx(page);
  assert.equal(fx.quality, 0);
  assert.equal(later.glows, 0);
  assert.ok(Math.abs(later.amount - 0.5) < 0.001, 'half the particles');
  assert.deepEqual(problems, []);
  await context.close();
});

// A boss that shows at the screen's edge the moment it spawns breaks the big entrance.
for (const [name, options] of [
  ['1280x720', { viewport: { width: 1280, height: 720 } }],
  ['a very wide window', { viewport: { width: 2560, height: 1080 } }],
  ['a portrait phone', kPhone],
]) {
  test(`the boss spawns out of sight and then walks in, on ${name}`, async () => {
    const { context, page, problems } = await openRun(options);
    for (const seed of [11, 22, 33, 44, 55]) {
      await send(page, CMD.setSeed, seed);
      await waitScreen(page, 'playing');
      await send(page, CMD.setQuality, 1);
      // Watches the first few frames after it spawns, while it is still far away.
      const seen = await page.evaluate((setTime) => new Promise((resolve) => {
        Module._ls_debug_command(setTime, 600);
        const start = performance.now();
        let frames = 0;
        const look = () => {
          const fx = JSON.parse(Module.ccall('ls_query_effects', 'string', [], []));
          if (fx.bossDistance >= 0) {
            frames++;
            if (fx.bossOnScreen === 1) { resolve({ visible: true, at: fx.bossDistance }); return; }
            if (frames >= 3) { resolve({ visible: false, at: fx.bossDistance }); return; }
          }
          if (performance.now() - start > 8000) { resolve({ timedOut: true }); return; }
          requestAnimationFrame(look);
        };
        requestAnimationFrame(look);
      }), CMD.setTime);
      assert.ok(!seen.timedOut, 'the boss must spawn');
      assert.equal(seen.visible, false, `seed ${seed}: the boss was on screen at distance ${seen.at}`);
    }
    // It must not stay away for long: catch-up speed brings it into view.
    await waitForFx(page, (fx) => fx.bossOnScreen === 1, 'the boss to walk in', 20000);
    assert.deepEqual(problems, []);
    await context.close();
  });
}

test('a phone shows touch prompts from the first frame, and a desktop stays on keyboard', async () => {
  const phone = await openPage(browser, `${web.url}?debug`, kPhone);
  await waitScreen(phone.page, 'title');
  // Nothing has been touched yet.
  assert.equal((await readUi(phone.page)).device, 'touch');
  assert.deepEqual(phone.problems, []);
  await phone.context.close();

  const desktop = await openPage(browser, `${web.url}?debug`, { viewport: { width: 1280, height: 720 } });
  await waitScreen(desktop.page, 'title');
  assert.equal((await readUi(desktop.page)).device, 'keyboard');
  assert.deepEqual(desktop.problems, []);
  await desktop.context.close();
});
