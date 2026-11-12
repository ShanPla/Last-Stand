// Tests for the browser networking layer (web/components/NetClient), against the real relay
// server in server/. They use a bare test page, not the game, so they do not depend on it:
// the page loads NetConfig.js and NetClient.js and nothing else.
//
// Every scenario runs on the guaranteed path (the relay) and the WebRTC path is tried too.
// If two headless Chrome contexts on this machine cannot connect over WebRTC, that test still
// passes on the fallback and says so in its output; set NET_REQUIRE_WEBRTC=1 to make it strict.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createServer as createTcpServer } from 'node:net';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright-core';
import { Client } from '../../server/test/helpers.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../..');
const componentDir = path.join(repo, 'web/components/NetClient');
const chromePath = process.env.CHROME_PATH ?? 'C:/Program Files/Google/Chrome/Application/chrome.exe';

// The test page. Its script records every event, and counts and checks every message that
// arrives, so the test can ask for a summary instead of shipping bytes back and forth.
const PAGE = `<!doctype html><meta charset="utf-8"><title>net test</title>
<script src="/NetConfig.js"></script><script src="/NetClient.js"></script>
<script>
const q = new URLSearchParams(location.search);
const options = {};
for (const [name, param] of [['wakeBudgetMs', 'wake'], ['fallbackMs', 'fallback'], ['pingMs', 'ping'], ['wakeRetryMs', 'retry']]) {
  if (q.has(param)) options[name] = Number(q.get(param));
}
// Counts binary frames sent to the server, so tests can tell which path carried the game data.
window.wsBinarySent = 0;
const realSend = WebSocket.prototype.send;
WebSocket.prototype.send = function (data) { window.lastSocket = this; if (typeof data !== 'string') window.wsBinarySent++; return realSend.call(this, data); };
window.net = LastStandNet.create(options);
window.events = [];
for (const name of ['phase', 'room', 'started', 'closed', 'error', 'server-lost', 'peer-dropped', 'peer-back', 'transport', 'wake', 'reconnecting', 'reconnected']) {
  net.on(name, (data) => events.push({ e: name, ...data }));
}
// A message is: kind(1) index(4) then a pattern that depends on the index. Lengths vary.
const rx = window.rx = { count: 0, bad: 0, reliable: 0, reliableOrdered: true, lastReliable: -1, from: {}, seen: {}, bytes: 0 };
function lengthFor(index) { return 5 + (index * 37) % 1200; }
function patternByte(index, j) { return (index * 31 + j * 7) & 255; }
window.makeMessage = function (reliable, index, length) {
  const len = length ?? lengthFor(index);
  const b = new Uint8Array(len);
  b[0] = reliable ? 1 : 0;
  new DataView(b.buffer).setUint32(1, index);
  for (let j = 5; j < len; j++) b[j] = patternByte(index, j);
  return b;
};
net.onMessage = (from, bytes) => {
  const index = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(1);
  let ok = bytes.length >= 5 && (bytes.length === lengthFor(index) || bytes.length === window.oddLength);
  for (let j = 5; ok && j < bytes.length; j++) if (bytes[j] !== patternByte(index, j)) ok = false;
  if (!ok) { rx.bad++; return; }
  rx.count++;
  rx.bytes += bytes.length;
  rx.from[from] = (rx.from[from] || 0) + 1;
  rx.seen[index] = true;
  if (bytes[0] === 1) {
    rx.reliable++;
    if (index <= rx.lastReliable) rx.reliableOrdered = false;
    rx.lastReliable = index;
  }
};
window.burst = async function (to, count, reliable, start) {
  let sent = 0;
  for (let i = 0; i < count; i++) {
    if (net.send(to, makeMessage(reliable, start + i))) sent++;
    await new Promise((r) => setTimeout(r, 4));
  }
  return sent;
};
</script>`;

const types = { '.js': 'text/javascript' };
let pageServer;
let pageOrigin;
let browser;
const relays = [];

async function freePort() {
  return new Promise((resolve) => {
    const probe = createTcpServer();
    probe.listen(0, '127.0.0.1', () => {
      const { port } = probe.address();
      probe.close(() => resolve(port));
    });
  });
}

// Runs the real server the way Render does: node server/src/index.js, with PORT set.
async function startRelay(env = {}) {
  const port = await freePort();
  const child = spawn(process.execPath, [path.join(repo, 'server/src/index.js')], {
    env: { ...process.env, PORT: String(port), ...env },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  await new Promise((resolve, reject) => {
    child.once('exit', (code) => reject(new Error(`relay exited early with code ${code}`)));
    child.stdout.on('data', (chunk) => { if (String(chunk).includes('relay listening')) resolve(); });
  });
  const relay = { port, ws: `ws://127.0.0.1:${port}`, child, stop: () => child.kill() };
  relays.push(relay);
  return relay;
}

before(async () => {
  pageServer = createServer(async (req, res) => {
    const name = new URL(req.url, 'http://x').pathname.slice(1);
    try {
      if (name === '') {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(PAGE);
      } else if (name === 'NetConfig.js' || name === 'NetClient.js') {
        res.writeHead(200, { 'Content-Type': types['.js'] });
        res.end(await readFile(path.join(componentDir, name)));
      } else {
        res.writeHead(404);
        res.end();
      }
    } catch {
      res.writeHead(500);
      res.end();
    }
  });
  await new Promise((resolve) => pageServer.listen(0, '127.0.0.1', resolve));
  pageOrigin = `http://127.0.0.1:${pageServer.address().port}`;
  browser = await chromium.launch({ executablePath: chromePath, headless: true });
});

after(async () => {
  await browser?.close();
  for (const relay of relays) relay.stop();
  pageServer?.closeAllConnections();
  pageServer?.close();
});

// ---- Helpers -----------------------------------------------------------------------

// Opens the test page in its own browser context, so each player has separate storage.
async function openPlayer(server, extra = '') {
  const context = await browser.newContext();
  const page = await context.newPage();
  const problems = [];
  page.on('pageerror', (error) => problems.push(String(error)));
  await page.goto(`${pageOrigin}/?server=${encodeURIComponent(server)}${extra}`);
  return { context, page, problems };
}

const until = (page, fn, arg, timeout = 15000) => page.waitForFunction(fn, arg, { timeout });
const phaseIs = (page, phase) => until(page, (p) => net.phase() === p, phase);
const closeAll = (...players) => Promise.all(players.map((p) => p.context.close()));

// Host creates, client joins by code, both press ready, host starts. Returns both players.
async function startedPair(relay, hostExtra = '', clientExtra = '') {
  const host = await openPlayer(relay.ws, hostExtra);
  const client = await openPlayer(relay.ws, clientExtra);
  await host.page.evaluate(() => net.create('Ana'));
  await phaseIs(host.page, 'InLobby');
  const code = await host.page.evaluate(() => net.roomCode());
  assert.match(code, /^[A-HJ-KM-NP-Z2-9]{5}$/);
  await client.page.evaluate((c) => net.join(c.toLowerCase(), 'Ben'), code); // codes are case-insensitive
  await phaseIs(client.page, 'InLobby');
  await until(host.page, () => net.players().length === 2);
  await host.page.evaluate(() => net.setReady(true));
  await client.page.evaluate(() => net.setReady(true));
  await until(host.page, () => net.players().every((p) => p.ready));
  await until(client.page, () => net.players().every((p) => p.ready));
  await host.page.evaluate(() => net.start());
  await phaseIs(host.page, 'Starting');
  await phaseIs(client.page, 'Starting');
  return { host, client, code };
}

// Waits until both sides report a transport that is not still pending, and returns them.
async function settledTransports({ host, client }) {
  await until(host.page, () => net.transportOf(1) !== 'pending', undefined, 20000);
  await until(client.page, () => net.transportOf(0) !== 'pending', undefined, 20000);
  return {
    host: await host.page.evaluate(() => net.transportOf(1)),
    client: await client.page.evaluate(() => net.transportOf(0)),
  };
}

// 200 unreliable messages each way, then 40 reliable ones. Everything that arrives must be
// byte-for-byte what was sent. The relay path must deliver all of them; WebRTC unreliable
// messages may in theory be dropped, so a few missing ones are tolerated there.
async function exchange({ host, client }, { exact }) {
  const [hostSent, clientSent] = await Promise.all([
    host.page.evaluate(() => burst(1, 200, false, 0)),
    client.page.evaluate(() => burst(0, 200, false, 1000)),
  ]);
  assert.equal(hostSent, 200);
  assert.equal(clientSent, 200);
  const [hostReliable, clientReliable] = await Promise.all([
    host.page.evaluate(() => burst(1, 40, true, 5000)),
    client.page.evaluate(() => burst(0, 40, true, 6000)),
  ]);
  assert.equal(hostReliable, 40);
  assert.equal(clientReliable, 40);
  const want = 240;
  const floor = exact ? want : Math.floor(want * 0.9);
  await until(host.page, (n) => rx.count >= n, floor, 20000);
  await until(client.page, (n) => rx.count >= n, floor, 20000);
  await host.page.waitForTimeout(500);
  for (const [who, player] of [['host', host], ['client', client]]) {
    const rx = await player.page.evaluate(() => ({ ...window.rx, seen: undefined }));
    assert.equal(rx.bad, 0, `${who} got a message whose bytes changed`);
    assert.ok(rx.count >= floor && rx.count <= want, `${who} got ${rx.count} of ${want}`);
    assert.equal(rx.reliable, 40, `${who} must get every reliable message`);
    assert.equal(rx.reliableOrdered, true, `${who} got reliable messages out of order`);
    assert.deepEqual(Object.keys(rx.from), [who === 'host' ? '1' : '0'], `${who} got a message from the wrong player`);
    if (exact) assert.equal(rx.count, want);
  }
}

// ---- Config ------------------------------------------------------------------------

test('config: localhost, placeholder, ?server and the health address', async () => {
  const relay = await startRelay();
  const { context, page } = await openPlayer(relay.ws);
  const out = await page.evaluate(() => {
    const c = LastStandNetConfig;
    return {
      local: c.resolve('localhost', ''), loopback: c.resolve('127.0.0.1', ''),
      live: c.resolve('shanpla.github.io', ''), override: c.resolve('shanpla.github.io', '?server=wss://x.example.com/'),
      bare: c.resolve('shanpla.github.io', '?server=example.com:9000'), relayMode: c.resolve('localhost', '?transport=relay'),
      junk: c.resolve('localhost', '?server=not%20a%20server'),
    };
  });
  assert.equal(out.local.wsUrl, 'ws://localhost:8787');
  assert.equal(out.local.healthUrl, 'http://localhost:8787/healthz');
  assert.equal(out.loopback.wsUrl, 'ws://localhost:8787');
  assert.match(out.live.wsUrl, /^wss:\/\/REPLACE-WITH-RENDER-ADDRESS/);
  assert.equal(out.live.isPlaceholder, true);
  assert.equal(out.live.healthUrl, 'https://REPLACE-WITH-RENDER-ADDRESS.onrender.com/healthz');
  assert.equal(out.override.wsUrl, 'wss://x.example.com');
  assert.equal(out.override.healthUrl, 'https://x.example.com/healthz');
  assert.equal(out.override.isPlaceholder, false);
  assert.equal(out.bare.wsUrl, 'ws://example.com:9000');
  assert.equal(out.relayMode.forceRelay, true);
  assert.equal(out.junk.wsUrl, 'ws://localhost:8787', 'a nonsense ?server is ignored');
  await context.close();
  relay.stop();
});

// ---- Wake --------------------------------------------------------------------------

test('wake: reports waking, then awake with a time; a dead address ends as unreachable', async () => {
  const relay = await startRelay();
  const good = await openPlayer(relay.ws);
  const result = await good.page.evaluate(async () => {
    const pending = net.wake();
    const during = net.wakeState();
    const done = await pending;
    return { during, done, after: net.wakeState(), events: events.filter((e) => e.e === 'wake').map((e) => e.state) };
  });
  assert.equal(result.during, 'waking');
  assert.equal(result.done.state, 'awake');
  assert.ok(result.done.ms >= 0 && result.done.attempts === 1);
  assert.equal(result.after, 'awake');
  assert.deepEqual(result.events, ['waking', 'awake']);
  assert.deepEqual(good.problems, []);

  const dead = await openPlayer('ws://127.0.0.1:1', '&wake=1500&retry=200');
  const failed = await dead.page.evaluate(async () => {
    const r = await net.wake();
    return { r, phase: net.phase(), error: net.lastError(), index: net.errorIndex() };
  });
  assert.equal(failed.r.state, 'unreachable');
  assert.ok(failed.r.attempts >= 2, 'it retried');
  assert.ok(failed.r.ms >= 1000 && failed.r.ms < 6000, `took ${failed.r.ms} ms`);
  assert.equal(failed.phase, 'Error');
  assert.equal(failed.error, 'ServerUnreachable');
  assert.equal(failed.index, 0);
  await closeAll(good, dead);
  relay.stop();
});

// ---- Full runs on both paths -------------------------------------------------------

test('forced relay: lobby, start, 240 messages each way arrive unchanged, transport says relay', async () => {
  const relay = await startRelay();
  // ?transport=relay in the page address is read by the config.
  const hostPage = await openPlayer(relay.ws, '&transport=relay&ping=300');
  const clientPage = await openPlayer(relay.ws, '&transport=relay&ping=300');
  await hostPage.page.evaluate(() => net.create('Ana'));
  await phaseIs(hostPage.page, 'InLobby');
  const code = await hostPage.page.evaluate(() => net.roomCode());
  await clientPage.page.evaluate((c) => net.join(c, 'Ben'), code);
  await phaseIs(clientPage.page, 'InLobby');
  await until(hostPage.page, () => net.players().length === 2);
  await hostPage.page.evaluate(() => net.start());
  await phaseIs(hostPage.page, 'Starting');
  await phaseIs(clientPage.page, 'Starting');

  assert.equal(await hostPage.page.evaluate(() => net.transportOf(1)), 'relay');
  assert.equal(await clientPage.page.evaluate(() => net.transportOf(0)), 'relay');
  assert.equal(await hostPage.page.evaluate(() => net.snapshot().forceRelay), true);
  assert.equal(await hostPage.page.evaluate(() => typeof RTCPeerConnection === 'function' && !events.some((e) => e.why === 'open')), true);

  await exchange({ host: hostPage, client: clientPage }, { exact: true });

  // Stats: both directions counted, a round trip was measured, the relay flag was sent.
  await hostPage.page.waitForTimeout(1500);
  const stats = await hostPage.page.evaluate(() => net.stats());
  assert.ok(stats.serverRttMs >= 0, 'server round trip measured');
  assert.equal(stats.peers[1].transport, 'relay');
  assert.ok(stats.peers[1].rttMs >= 0, 'peer round trip measured through the relay');
  const players = await hostPage.page.evaluate(() => net.players());
  assert.deepEqual(players.map((p) => p.transport), ['local', 'relay']);

  // A message of the largest allowed size goes through, one byte more is refused.
  const limits = await hostPage.page.evaluate(() => {
    window.oddLength = undefined;
    return { max: net.MAX_PAYLOAD, tooBig: net.send(1, new Uint8Array(net.MAX_PAYLOAD + 1)), nonBytes: net.send(1, 'text') };
  });
  assert.equal(limits.max, 16 * 1024 - 2);
  assert.equal(limits.tooBig, false);
  assert.equal(limits.nonBytes, false);
  await clientPage.page.evaluate((len) => { window.oddLength = len; }, limits.max);
  const sentBig = await hostPage.page.evaluate(() => net.send(1, makeMessage(false, 9000, net.MAX_PAYLOAD)));
  assert.equal(sentBig, true);
  await until(clientPage.page, () => rx.seen[9000] === true);

  // Broadcast from the host reaches the client too.
  await hostPage.page.evaluate(() => net.send(255, makeMessage(false, 9100)));
  await until(clientPage.page, () => rx.seen[9100] === true);

  assert.ok(await hostPage.page.evaluate(() => wsBinarySent) > 240, 'the game data went through the server');
  assert.deepEqual(hostPage.problems.concat(clientPage.problems), []);
  await closeAll(hostPage, clientPage);
  relay.stop();
});

test('WebRTC: the pair tries a direct connection, and messages arrive on whatever path results', async (t) => {
  const relay = await startRelay();
  const pair = await startedPair(relay, '&ping=300', '&ping=300');
  const seen = await settledTransports(pair);
  const webrtc = seen.host === 'webrtc' && seen.client === 'webrtc';
  t.diagnostic(`transports: host sees ${seen.host}, client sees ${seen.client}. Real WebRTC connected: ${webrtc}`);
  console.log(`# WebRTC between two headless Chrome contexts connected: ${webrtc}`);
  if (process.env.NET_REQUIRE_WEBRTC) assert.equal(webrtc, true, 'WebRTC did not connect');
  await exchange(pair, { exact: !webrtc });
  const counts = await Promise.all([pair.host, pair.client].map((p) => p.page.evaluate(() => ({ received: rx.count, viaServer: wsBinarySent }))));
  t.diagnostic(`received: host ${counts[0].received}, client ${counts[1].received}; binary frames through the server: ${counts[0].viaServer + counts[1].viaServer}`);
  if (webrtc) {
    assert.equal(counts[0].viaServer + counts[1].viaServer, 0, 'with WebRTC open, no game frame may go through the server');
    await pair.host.page.waitForTimeout(1500);
    const stats = await pair.host.page.evaluate(() => net.stats());
    assert.ok(stats.peers[1].rttMs >= 0, 'peer round trip measured over the data channel');
  }
  assert.deepEqual(pair.host.problems.concat(pair.client.problems), []);
  await closeAll(pair.host, pair.client);
  relay.stop();
});

test('mixed paths: one side forced to the relay, the other falls back by timeout, both still talk', async () => {
  const relay = await startRelay();
  const host = await openPlayer(relay.ws, '&transport=relay');
  const client = await openPlayer(relay.ws, '&fallback=1500'); // never gets an offer, so it must give up on WebRTC
  await host.page.evaluate(() => net.create('Ana'));
  await phaseIs(host.page, 'InLobby');
  const code = await host.page.evaluate(() => net.roomCode());
  await client.page.evaluate((c) => net.join(c, 'Ben'), code);
  await phaseIs(client.page, 'InLobby');
  await until(host.page, () => net.players().length === 2);
  await host.page.evaluate(() => net.start());
  await phaseIs(client.page, 'Starting');
  await until(client.page, () => net.transportOf(0) === 'relay', undefined, 8000);
  const events = await client.page.evaluate(() => events.filter((e) => e.e === 'transport'));
  assert.equal(events.at(-1).why, 'timeout');
  await exchange({ host, client }, { exact: true });
  await closeAll(host, client);
  relay.stop();
});

// ---- Errors ------------------------------------------------------------------------

test('errors: not found, full, started and bad name map to stable error names', async () => {
  const relay = await startRelay();
  const player = await openPlayer(relay.ws);
  const attempt = (name, code, who) => player.page.evaluate(async ([n, c, w]) => {
    net.leave();
    await net.join(c, w);
    await new Promise((resolve) => {
      const check = () => (net.phase() === 'Error' || net.phase() === 'InLobby' ? resolve() : setTimeout(check, 20));
      check();
    });
    return { phase: net.phase(), error: net.lastError(), index: net.errorIndex(), snapshot: net.snapshot() };
  }, [name, code, who]);

  const missing = await attempt('x', 'ZZZZZ', 'Ben');
  assert.deepEqual([missing.phase, missing.error, missing.index], ['Error', 'NotFound', 1]);
  assert.equal(missing.snapshot.errorCode, 'not-found');

  // A real room with its four seats taken by plain sockets.
  const host = await Client.connect(relay.ws);
  host.sendJson({ t: 'create', name: 'Host' });
  const code = (await host.nextJson()).code;
  const others = [];
  for (let i = 0; i < 3; i++) {
    const other = await Client.connect(relay.ws);
    other.sendJson({ t: 'join', code, name: `P${i}` });
    others.push(other);
  }
  await host.nextJson(); // let the server finish seating them
  await new Promise((resolve) => setTimeout(resolve, 200));
  const full = await attempt('x', code, 'Late');
  assert.deepEqual([full.phase, full.error, full.index], ['Error', 'Full', 2]);

  const badName = await attempt('x', code, 'Bad!!');
  assert.deepEqual([badName.phase, badName.error, badName.index], ['Error', 'BadName', 4]);

  host.sendJson({ t: 'start' });
  await new Promise((resolve) => setTimeout(resolve, 200));
  const started = await attempt('x', code, 'Late');
  assert.deepEqual([started.phase, started.error, started.index], ['Error', 'Started', 3]);

  // leave() clears the error and the phase goes back to Offline.
  const cleared = await player.page.evaluate(() => { net.leave(); return { phase: net.phase(), error: net.lastError(), index: net.errorIndex() }; });
  assert.deepEqual(cleared, { phase: 'Offline', error: '', index: -1 });
  assert.deepEqual(player.problems, []);
  for (const c of [host, ...others]) c.ws.close();
  await closeAll(player);
  relay.stop();
});

test('errors: an unreachable server gives ServerUnreachable on create and join', async () => {
  const player = await openPlayer('ws://127.0.0.1:1', '&wake=1500&retry=200');
  const result = await player.page.evaluate(async () => {
    const out = {};
    await net.create('Ana');
    await new Promise((resolve) => { const check = () => (net.phase() === 'Error' ? resolve() : setTimeout(check, 50)); check(); });
    out.create = [net.phase(), net.lastError(), net.errorIndex()];
    net.leave();
    await net.join('ABCDE', 'Ben');
    await new Promise((resolve) => { const check = () => (net.phase() === 'Error' ? resolve() : setTimeout(check, 50)); check(); });
    out.join = [net.phase(), net.lastError(), net.errorIndex()];
    out.events = events.filter((e) => e.e === 'error').map((e) => e.error);
    return out;
  });
  assert.deepEqual(result.create, ['Error', 'ServerUnreachable', 0]);
  assert.deepEqual(result.join, ['Error', 'ServerUnreachable', 0]);
  assert.deepEqual(result.events, ['ServerUnreachable', 'ServerUnreachable']);
  await closeAll(player);
});

test('errors: a server that goes away mid-lobby gives server-lost', async () => {
  const relay = await startRelay();
  const player = await openPlayer(relay.ws);
  await player.page.evaluate(() => net.create('Ana'));
  await phaseIs(player.page, 'InLobby');
  relay.stop();
  await phaseIs(player.page, 'Error');
  const out = await player.page.evaluate(() => ({
    error: net.lastError(), lost: events.filter((e) => e.e === 'server-lost').length, players: net.players().length,
  }));
  assert.deepEqual(out, { error: 'ServerUnreachable', lost: 1, players: 0 });
  await closeAll(player);
});

// ---- Leaving -----------------------------------------------------------------------

test('host leaving closes the room for the client with host-left', async () => {
  const relay = await startRelay();
  const pair = await startedPair(relay);
  await pair.host.page.evaluate(() => net.leave());
  await phaseIs(pair.client.page, 'Error');
  const out = await pair.client.page.evaluate(() => ({
    error: net.lastError(), index: net.errorIndex(),
    closed: events.filter((e) => e.e === 'closed').map((e) => e.reason),
    code: net.snapshot().errorCode,
  }));
  assert.deepEqual(out, { error: 'HostLeft', index: 5, closed: ['host-left'], code: 'host-left' });
  assert.equal(await pair.host.page.evaluate(() => net.phase()), 'Offline');
  await closeAll(pair.host, pair.client);
  relay.stop();
});

test('a client leaving a started run gives the host peer-dropped with its id, keeps its seat as away, and the host carries on', async () => {
  const relay = await startRelay();
  const pair = await startedPair(relay);
  await pair.client.page.evaluate(() => net.leave());
  await until(pair.host.page, () => events.some((e) => e.e === 'peer-dropped'));
  const out = await pair.host.page.evaluate(() => ({
    dropped: events.filter((e) => e.e === 'peer-dropped').map((e) => e.id),
    players: net.players().map((p) => `${p.id}:${p.away}`), phase: net.phase(), transport: net.transportOf(1),
    sendToGone: net.send(1, new Uint8Array([0, 0, 0, 0, 0])),
  }));
  assert.deepEqual(out.dropped, [1]);
  assert.deepEqual(out.players, ['0:false', '1:true'], 'the seat is kept, marked away, because they may come back');
  assert.equal(out.phase, 'Starting');
  assert.equal(out.transport, 'none');
  assert.equal(out.sendToGone, false, 'sending to a dropped player must not throw or pretend');
  await closeAll(pair.host, pair.client);
  relay.stop();
});

test('closing the tab tells the server at once, so the host sees peer-dropped quickly', async () => {
  const relay = await startRelay();
  const pair = await startedPair(relay);
  const before = Date.now();
  await pair.client.page.close({ runBeforeUnload: true });
  await until(pair.host.page, () => events.some((e) => e.e === 'peer-dropped'), undefined, 5000);
  assert.ok(Date.now() - before < 5000);
  await closeAll(pair.host, pair.client);
  relay.stop();
});

// ---- Rejoin ------------------------------------------------------------------------

const SEAT_KEY = 'lastStand.coopRejoin';

for (const [label, extra] of [['webrtc tried', ''], ['relay', '&transport=relay']]) {
  test(`rejoin: a guest whose socket drops gets its seat back by itself, and the host makes a fresh pair (${label})`, async () => {
    const relay = await startRelay();
    const pair = await startedPair(relay, extra, extra);
    // Only the guest keeps a seat: a host's room closes with it.
    const seat = await pair.client.page.evaluate((key) => ({ can: net.canRejoin(), info: net.rejoinInfo(), stored: JSON.parse(localStorage.getItem(key)) }), SEAT_KEY);
    assert.equal(seat.can, true);
    assert.deepEqual(seat.info, { code: pair.code, slot: 1, name: 'Ben' });
    assert.match(seat.stored.token, /^[A-Za-z0-9_-]{22}$/);
    assert.equal(await pair.host.page.evaluate(() => net.canRejoin()), false);
    await settledTransports(pair);

    // A network cut: the socket closes without the page asking.
    await pair.client.page.evaluate(() => window.lastSocket.close());
    await until(pair.client.page, () => events.some((e) => e.e === 'reconnected'), undefined, 15000);
    await phaseIs(pair.client.page, 'Starting');
    await until(pair.host.page, () => events.some((e) => e.e === 'peer-back'));
    const back = await Promise.all([
      pair.client.page.evaluate(() => ({ id: net.myId(), phases: events.filter((e) => e.e === 'phase').map((e) => e.phase), error: net.lastError() })),
      pair.host.page.evaluate(() => ({ dropped: events.filter((e) => e.e === 'peer-dropped').map((e) => e.id), back: events.filter((e) => e.e === 'peer-back').map((e) => e.id), players: net.players().map((p) => `${p.id}:${p.away}:${p.returns}`) })),
    ]);
    assert.equal(back[0].id, 1, 'the same slot');
    assert.equal(back[0].error, '');
    assert.ok(back[0].phases.includes('Reconnecting'), `phases: ${back[0].phases}`);
    assert.deepEqual(back[1].dropped, [1]);
    assert.deepEqual(back[1].back, [1]);
    assert.deepEqual(back[1].players, ['0:false:0', '1:false:1']);

    // The new pair carries game messages both ways, like the first one did.
    await settledTransports(pair);
    await exchange(pair, { exact: !!extra });
    assert.deepEqual(pair.client.problems, []);
    assert.deepEqual(pair.host.problems, []);
    await closeAll(pair.host, pair.client);
    relay.stop();
  });
}

test('rejoin: after a reload the saved seat is taken back with rejoin(), a refused rejoin forgets it, and a closed room says RoomClosed', async () => {
  const relay = await startRelay();
  const pair = await startedPair(relay, '&transport=relay', '&transport=relay');
  const stored = await pair.client.page.evaluate((key) => localStorage.getItem(key), SEAT_KEY);
  // Reloading closes the tab's socket (it leaves), and the server keeps the seat.
  await pair.client.page.reload();
  await until(pair.host.page, () => net.players().some((p) => p.id === 1 && p.away));
  assert.equal(await pair.client.page.evaluate(() => net.rejoinInfo().code), pair.code);

  // A wrong token is refused: RunEnded, and the seat is forgotten.
  await pair.client.page.evaluate(([key, text]) => {
    const info = JSON.parse(text);
    info.token = 'AAAAAAAAAAAAAAAAAAAAAA';
    localStorage.setItem(key, JSON.stringify(info));
  }, [SEAT_KEY, stored]);
  await pair.client.page.evaluate(() => net.rejoin());
  await phaseIs(pair.client.page, 'Error');
  assert.deepEqual(await pair.client.page.evaluate((key) => [net.lastError(), net.canRejoin(), localStorage.getItem(key)], SEAT_KEY), ['RunEnded', false, null]);

  // The right one works: same slot, and the host sees the player back.
  await pair.client.page.evaluate(([key, text]) => { net.leave(); localStorage.setItem(key, text); }, [SEAT_KEY, stored]);
  await pair.client.page.evaluate(() => net.rejoin());
  await phaseIs(pair.client.page, 'Starting');
  assert.equal(await pair.client.page.evaluate(() => net.myId()), 1);
  await until(pair.host.page, () => net.players().every((p) => !p.away));

  // The host leaves: the room closes and the seat is forgotten.
  await pair.host.page.evaluate(() => net.leave());
  await phaseIs(pair.client.page, 'Error');
  assert.deepEqual(await pair.client.page.evaluate((key) => [net.lastError(), net.canRejoin(), localStorage.getItem(key)], SEAT_KEY), ['HostLeft', false, null]);

  // A stale seat for that closed room: RoomClosed, and forgotten.
  await pair.client.page.evaluate(([key, text]) => { net.leave(); localStorage.setItem(key, text); }, [SEAT_KEY, stored]);
  await pair.client.page.reload();
  assert.equal(await pair.client.page.evaluate(() => net.canRejoin()), true);
  await pair.client.page.evaluate(() => net.rejoin());
  await phaseIs(pair.client.page, 'Error');
  assert.deepEqual(await pair.client.page.evaluate((key) => [net.lastError(), net.errorIndex(), net.canRejoin(), localStorage.getItem(key)], SEAT_KEY), ['RoomClosed', 9, false, null]);
  assert.deepEqual(pair.client.problems, []);
  await closeAll(pair.host, pair.client);
  relay.stop();
});

test('rejoin: storage that throws still lets a dropped guest reconnect from memory', async () => {
  const relay = await startRelay();
  const pair = await startedPair(relay, '&transport=relay', '&transport=relay');
  await pair.client.page.evaluate(() => {
    const boom = () => { throw new Error('blocked'); };
    Object.defineProperty(window, 'localStorage', { get: boom, configurable: true });
  });
  await pair.client.page.evaluate(() => window.lastSocket.close());
  await until(pair.client.page, () => events.some((e) => e.e === 'reconnected'), undefined, 15000);
  assert.equal(await pair.client.page.evaluate(() => net.myId()), 1);
  assert.deepEqual(pair.client.problems, []);
  await closeAll(pair.host, pair.client);
  relay.stop();
});

// ---- Game-facing helpers -----------------------------------------------------------

test('bridge helpers: phase and error numbers, snapshot and event list are plain data', async () => {
  const relay = await startRelay();
  const player = await openPlayer(relay.ws);
  const out = await player.page.evaluate(async () => {
    const first = JSON.parse(net.snapshotJson());
    await net.create('Ana');
    await new Promise((resolve) => { const check = () => (net.phase() === 'InLobby' ? resolve() : setTimeout(check, 20)); check(); });
    const second = JSON.parse(net.snapshotJson());
    const taken = JSON.parse(net.takeEventsJson());
    const again = JSON.parse(net.takeEventsJson());
    return { first, second, kinds: taken.map((e) => e.e), again, phases: net.PHASES, errors: net.ERRORS, host: net.isHost(), id: net.myId() };
  });
  assert.deepEqual(out.phases, ['Offline', 'Waking', 'Connecting', 'InLobby', 'Starting', 'Error', 'Reconnecting']);
  assert.deepEqual(out.errors, ['ServerUnreachable', 'NotFound', 'Full', 'Started', 'BadName', 'HostLeft', 'Version', 'Other', 'RunEnded', 'RoomClosed', 'Replaced']);
  assert.equal(out.first.phaseIndex, 0);
  assert.equal(out.first.errorIndex, -1);
  assert.equal(out.second.phaseIndex, 3);
  assert.equal(out.second.isHost, true);
  assert.equal(out.second.players[0].name, 'Ana');
  assert.ok(out.kinds.includes('room') && out.kinds.includes('phase') && out.kinds.includes('wake'));
  assert.deepEqual(out.again, [], 'events are taken once');
  assert.equal(out.host, true);
  assert.equal(out.id, 0);
  assert.deepEqual(player.problems, []);
  await closeAll(player);
  relay.stop();
});
