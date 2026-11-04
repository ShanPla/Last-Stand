// Shared browser test setup: serves the web build with gzip, as a real host
// would, and opens it in the installed Chrome.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { gzipSync } from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../build/web');
const chromePath = process.env.CHROME_PATH
  ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const types = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm',
};

export async function startServer(port = 0) {
  const sent = { raw: 0, compressed: 0 };
  const server = createServer(async (req, res) => {
    const urlPath = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    const file = path.join(root, urlPath === '/' ? 'index.html' : urlPath);
    try {
      const data = await readFile(file);
      const body = gzipSync(data);
      sent.raw += data.length;
      sent.compressed += body.length;
      res.writeHead(200, {
        'Content-Type': types[path.extname(file)] ?? 'application/octet-stream',
        'Content-Encoding': 'gzip',
      });
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end();
    }
  });
  await new Promise((resolve) => server.listen(port, '127.0.0.1', resolve));
  return { server, sent, url: `http://127.0.0.1:${server.address().port}/` };
}

export async function launchChrome() {
  return chromium.launch({
    executablePath: chromePath,
    headless: true,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
}

// Opens the game and records console errors and uncaught exceptions.
export async function openGame(browser, url, contextOptions = {}) {
  const context = await browser.newContext(contextOptions);
  const page = await context.newPage();
  const problems = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') problems.push(msg.text());
  });
  page.on('pageerror', (error) => problems.push(String(error)));
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  return { context, page, problems };
}

// Counts screenshot pixels close to an RGB color, inside a centered box. A real
// pixel check, because state queries can pass while nothing is visible on screen.
export async function countColorNearCenter(page, [r, g, b], boxFraction = 0.2) {
  const base64 = (await page.screenshot()).toString('base64');
  return page.evaluate(async ({ base64, r, g, b, boxFraction }) => {
    const blob = await (await fetch(`data:image/png;base64,${base64}`)).blob();
    const image = await createImageBitmap(blob);
    const canvas = new OffscreenCanvas(image.width, image.height);
    const context = canvas.getContext('2d');
    context.drawImage(image, 0, 0);
    const w = Math.floor(image.width * boxFraction);
    const h = Math.floor(image.height * boxFraction);
    const data = context.getImageData((image.width - w) >> 1, (image.height - h) >> 1, w, h).data;
    let count = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (Math.abs(data[i] - r) < 24 && Math.abs(data[i + 1] - g) < 24 && Math.abs(data[i + 2] - b) < 24) count++;
    }
    return count;
  }, { base64, r, g, b, boxFraction });
}
