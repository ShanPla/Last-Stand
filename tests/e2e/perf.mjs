// Performance measurement, run by hand: node tests/e2e/perf.mjs [enemies] [shots] [width] [height]
// Uses this machine's real GPU, not the software renderer the other tests use.
import { chromium } from 'playwright-core';
import { startServer } from './harness.mjs';

const enemies = Number(process.argv[2] ?? 2000);
const shots = Number(process.argv[3] ?? 3000);
const width = Number(process.argv[4] ?? 1920);
const height = Number(process.argv[5] ?? 1080);

const web = await startServer();
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe',
  headless: true,
  args: ['--headless=new', '--use-angle=d3d11', '--ignore-gpu-blocklist', '--enable-gpu-rasterization', '--disable-frame-rate-limit=false'],
});
const page = await (await browser.newContext({ viewport: { width, height } })).newPage();
page.on('pageerror', (error) => console.error('page error:', String(error)));

const loadStart = Date.now();
await page.goto(`${web.url}?debug`);
await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
const loadMs = Date.now() - loadStart;

const gpu = await page.evaluate(() => {
  const gl = document.createElement('canvas').getContext('webgl2');
  const info = gl.getExtension('WEBGL_debug_renderer_info');
  return info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : 'unknown';
});

await page.evaluate(([e, s]) => {
  Module._ls_debug_command(5, 1);  // The game opens on the title, so start a run first.
  Module._ls_debug_command(1, e);
  Module._ls_debug_command(2, s);
}, [enemies, shots]);
await page.waitForTimeout(3000);

// Times real frames from JavaScript, separately from the game's own numbers.
const frames = await page.evaluate(() => new Promise((resolve) => {
  const samples = [];
  let last = performance.now();
  const start = last;
  function tick(now) {
    samples.push(now - last);
    last = now;
    if (now - start < 8000) requestAnimationFrame(tick);
    else resolve(samples);
  }
  requestAnimationFrame(tick);
}));

const state = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_state', 'string', [], [])));
// So the numbers can be trusted: this shows the effects really were busy while measured.
const fx = await page.evaluate(() => JSON.parse(Module.ccall('ls_query_effects', 'string', [], [])));
frames.sort((a, b) => a - b);
const average = frames.reduce((sum, value) => sum + value, 0) / frames.length;
const p99 = frames[Math.floor(frames.length * 0.99)];

console.log(JSON.stringify({
  gpu,
  viewport: `${state.width}x${state.height}`,
  enemies: state.enemies,
  shots: state.shots,
  avgFps: Number((1000 / average).toFixed(1)),
  frameMsAvg: Number(average.toFixed(2)),
  frameMsP99: Number(p99.toFixed(2)),
  gameReportedFps: state.fps,
  gameReportedFrameMs: state.frameMs,
  simMsPerStep: state.simMs,
  drawMsCpu: state.drawMs,
  effects: {
    particles: fx.particles,
    particlesSpawned: fx.particlesSpawned,
    stains: fx.stains,
    numbers: fx.numbers,
    numberEvents: fx.numberEvents,
    glowsDrawnTotal: fx.glows,
    zombiesFlashedPeak: fx.zombiesFlashedPeak,
    quality: fx.quality,
  },
  loadMs,
  transferKb: {
    raw: Math.round(web.sent.raw / 1024),
    gzip: Math.round(web.sent.compressed / 1024),
  },
}, null, 2));

await browser.close();
web.server.close();
