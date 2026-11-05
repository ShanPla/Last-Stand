// Performance measurement for phone sizes and slow frames, run by hand:
//   node tests/e2e/perfscenes.mjs '{"width":390,"height":844,"dpr":2,"throttle":4,"enemies":1000,"walk":true}'
// One JSON object per argument. Any field left out takes the default below.
//
// It separates real work from the browser's pacing: "work" is how long the game's own
// animation frame callback ran, and "gap" is the time between two of them. A long gap with
// short work is the browser's timing, not the game's. Chrome's CPU throttle slows only the
// page's main thread, so it approximates a phone and is no substitute for a real one.
import { chromium } from 'playwright-core';
import { startServer, CMD } from './harness.mjs';

const DEFAULTS = {
  width: 1920, height: 1080, dpr: 1, throttle: 1, enemies: 2000, shots: null, map: 0, quality: 1, walk: false, seconds: 12, settle: 3, seed: 12345, time: null,
};

const web = await startServer();
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe',
  headless: true,
  args: ['--headless=new', '--use-angle=d3d11', '--ignore-gpu-blocklist', '--enable-gpu-rasterization', ...(process.env.CHROME_ARGS ?? '').split(' ').filter(Boolean)],
});

// Goes in before the game script, so every frame the game asks for gets timed. The numbers go
// in fixed arrays, so the measuring itself makes no garbage.
function installFrameTimer() {
  const raf = window.requestAnimationFrame.bind(window);
  const size = 40000;
  const log = { gap: new Float32Array(size), work: new Float32Array(size), paints: new Uint8Array(size), count: 0, on: false, last: 0, lastPaints: 0 };
  window.__frames = log;
  window.requestAnimationFrame = (callback) => raf((time) => {
    const start = performance.now();
    callback(time);
    if (!log.on) { log.last = start; return; }
    const end = performance.now();
    if (log.count < size) {
      const total = window.Module && Module._ls_ground_paints ? Module._ls_ground_paints() : 0;
      log.gap[log.count] = start - log.last;
      log.work[log.count] = end - start;
      log.paints[log.count] = Math.min(255, total - log.lastPaints);
      log.lastPaints = total;
      log.count++;
    }
    log.last = start;
  });
}

function percentile(sorted, fraction) {
  return sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * fraction))] : 0;
}

function summarize(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const sum = sorted.reduce((total, value) => total + value, 0);
  const round = (value) => Number(value.toFixed(2));
  return {
    avg: round(sum / Math.max(1, sorted.length)),
    p50: round(percentile(sorted, 0.5)),
    p95: round(percentile(sorted, 0.95)),
    p99: round(percentile(sorted, 0.99)),
    worst: round(sorted[sorted.length - 1] ?? 0),
  };
}

// Walks a square so it crosses chunk borders. Solid cars slide him along the way.
async function walkRoute(page, seconds) {
  const legs = ['KeyD', 'KeyS', 'KeyA', 'KeyW'];
  const legMs = seconds * 1000 / legs.length;
  for (const key of legs) {
    await page.keyboard.down(key);
    await page.waitForTimeout(legMs);
    await page.keyboard.up(key);
  }
}

async function measure(options) {
  const o = { ...DEFAULTS, ...options };
  const shots = o.shots ?? Math.round(o.enemies * 1.5);
  const context = await browser.newContext({ viewport: { width: o.width, height: o.height }, deviceScaleFactor: o.dpr });
  const page = await context.newPage();
  page.on('pageerror', (error) => console.error('page error:', String(error)));
  await page.addInitScript(installFrameTimer);
  await page.goto(`${web.url}?debug`);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 60000 });

  const cdp = await context.newCDPSession(page);
  await cdp.send('Emulation.setFocusEmulationEnabled', { enabled: true });
  await page.evaluate(([cmd, map, seed, quality, enemies, count, time]) => {
    Module._ls_debug_command(cmd.setMap, map);
    Module._ls_debug_command(cmd.startRun, seed);
    if (quality !== null) Module._ls_debug_command(cmd.setQuality, quality);
    Module._ls_debug_command(cmd.stressEnemies, enemies);
    Module._ls_debug_command(cmd.stressProjectiles, count);
    if (time !== null) Module._ls_debug_command(cmd.setTime, time);
  }, [CMD, o.map, o.seed, o.quality, o.enemies, shots, o.time]);
  if (o.throttle > 1) await cdp.send('Emulation.setCPUThrottlingRate', { rate: o.throttle });
  await page.waitForTimeout(o.settle * 1000);

  const before = await page.evaluate(() => ({ ...JSON.parse(Module.ccall("ls_query_state", "string", [], [])), whole: Module._ls_ground_whole_paints ? Module._ls_ground_whole_paints() : 0 }));
  await page.evaluate(() => { const log = window.__frames; log.count = 0; log.lastPaints = Module._ls_ground_paints(); log.last = performance.now(); log.on = true; });
  if (o.walk) await walkRoute(page, o.seconds);
  else await page.waitForTimeout(o.seconds * 1000);
  const data = await page.evaluate(() => {
    const log = window.__frames;
    log.on = false;
    return { gap: Array.from(log.gap.subarray(0, log.count)), work: Array.from(log.work.subarray(0, log.count)), paints: Array.from(log.paints.subarray(0, log.count)) };
  });

  const state = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));
  const fx = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])));
  const world = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));
  const heap = await page.evaluate(() => (typeof HEAP8 === "undefined" ? 0 : HEAP8.length));
  const paintFrames = data.work.filter((_, i) => data.paints[i] > 0);
  const plainFrames = data.work.filter((_, i) => data.paints[i] === 0);
  const result = {
    size: `${o.width}x${o.height}@${o.dpr}`,
    buffer: `${state.width}x${state.height}`,
    throttle: o.throttle,
    map: o.map,
    quality: fx.quality,
    walk: o.walk,
    enemies: state.enemies,
    shots: state.shots,
    frames: data.gap.length,
    fps: Number((data.gap.length / Math.max(0.001, data.gap.reduce((a, b) => a + b, 0) / 1000)).toFixed(1)),
    gap: summarize(data.gap),
    work: summarize(data.work),
    simMs: state.simMs,
    drawMs: state.drawMs,
    paintFrames: paintFrames.length,
    paintedWhole: await page.evaluate(() => (Module._ls_ground_whole_paints ? Module._ls_ground_whole_paints() : 0)) - before.whole,
    paintsMostInFrame: Math.max(0, ...data.paints),
    workOnPaintFrames: summarize(paintFrames),
    workOnOtherFrames: summarize(plainFrames),
    moved: Number(Math.hypot(state.x - before.x, state.y - before.y).toFixed(0)),
    carsInView: world.cars,
    pixelScale: world.pixelScale,
    wasmHeapMb: Number((heap / 1048576).toFixed(1)),
  };
  console.log(JSON.stringify(result));
  await context.close();
  return result;
}

const scenes = process.argv.slice(2).map((text) => JSON.parse(text));
for (const scene of scenes.length ? scenes : [{}]) await measure(scene);

await browser.close();
web.server.close();
