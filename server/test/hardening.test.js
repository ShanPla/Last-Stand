// Tests for the abuse cases a public relay has to survive: values nested deep enough to break
// JSON.stringify, receivers that stop reading, sockets that never say hello, and one address
// opening too many sockets or rooms.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { EventEmitter } from 'node:events';
import path from 'node:path';
import { after, before, describe, test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { DEFAULTS, clientIp, clientIpSource, loadConfig } from '../src/config.js';
import { Connection } from '../src/connection.js';
import { MAX_SIGNAL_DEPTH, MAX_SIGNAL_JSON, isFlatSettings, isPingNumber, isSignalData } from '../src/shapes.js';
import { Client, DEV_ORIGIN, joinRoom, makeRoom, sleep, startRelay, startedRoom } from './helpers.js';

const serverDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');

/** A value nested `depth` arrays deep, as raw JSON text (JSON.stringify could not write it). */
const nestedText = (depth) => '['.repeat(depth) + ']'.repeat(depth);
/** An object nested `depth` levels deep, counting the outer object as level 1. */
function nestedObject(depth) {
  let value = 1;
  for (let i = 1; i < depth; i++) value = { a: value };
  return { a: value };
}

// What Chrome really sends through NetClient: an offer or answer with the SDP text, and ICE
// candidates from RTCIceCandidate.toJSON().
const CHROME_SDP = [
  'v=0', 'o=- 4611731400430051336 2 IN IP4 127.0.0.1', 's=-', 't=0 0', 'a=group:BUNDLE 0',
  'a=extmap-allow-mixed', 'a=msid-semantic: WMS', 'm=application 9 UDP/DTLS/SCTP webrtc-datachannel',
  'c=IN IP4 0.0.0.0', 'a=ice-ufrag:Gx2b', 'a=ice-pwd:mL3nP8yE0Jc9tKfV2sQwR7uZ', 'a=ice-options:trickle',
  'a=fingerprint:sha-256 4A:AD:B9:B1:3F:82:18:3B:26:12:B5:C0:4F:1D:02:7A:8E:63:9F:6D:2C:11:A0:94:3E:58:7B:D1:0C:E6:45:F2',
  'a=setup:actpass', 'a=mid:0', 'a=sctp-port:5000', 'a=max-message-size:262144', '',
].join('\r\n');
const CHROME_SIGNALS = [
  { kind: 'offer', sdp: CHROME_SDP },
  { kind: 'answer', sdp: CHROME_SDP.replace('actpass', 'active') },
  {
    kind: 'ice',
    candidate: {
      candidate: 'candidate:3348618123 1 udp 2122260223 192.168.1.20 54400 typ host generation 0 ufrag Gx2b network-id 1 network-cost 10',
      sdpMid: '0',
      sdpMLineIndex: 0,
      usernameFragment: 'Gx2b',
    },
  },
  { kind: 'ice', candidate: { candidate: 'candidate:1 1 udp 1 1.2.3.4 9 typ srflx', sdpMid: null, sdpMLineIndex: null, usernameFragment: null } },
];

describe('shape checks', () => {
  test('ping numbers must be finite numbers', () => {
    for (const ok of [0, 17, -3, 1.5, 1e9]) assert.equal(isPingNumber(ok), true, String(ok));
    for (const bad of [undefined, null, '1', [], {}, NaN, Infinity, true]) assert.equal(isPingNumber(bad), false, String(bad));
  });

  test('settings must be a flat object of plain values', () => {
    assert.equal(isFlatSettings({ map: 0, difficulty: 1, mode: 0 }), true);
    assert.equal(isFlatSettings({ a: 'x', b: true, c: -1.5 }), true);
    assert.equal(isFlatSettings({}), true);
    for (const bad of [null, [], 'x', 5, { a: null }, { a: [] }, { a: {} }, { a: [1] }]) {
      assert.equal(isFlatSettings(bad), false, JSON.stringify(bad));
    }
    assert.equal(isFlatSettings(JSON.parse(`{"a":${nestedText(5000)}}`)), false);
  });

  test('signal data allows the shapes Chrome sends', () => {
    for (const data of CHROME_SIGNALS) assert.equal(isSignalData(data), true, JSON.stringify(data));
    assert.equal(isSignalData({ list: [1, [2, ['x']]] }), true); // 4 levels with arrays
  });

  test('signal data refuses deep nesting, big data and non-objects', () => {
    assert.equal(isSignalData(nestedObject(MAX_SIGNAL_DEPTH)), true);
    assert.equal(isSignalData(nestedObject(MAX_SIGNAL_DEPTH + 1)), false);
    assert.equal(isSignalData(JSON.parse(`{"a":${nestedText(5000)}}`)), false);
    assert.equal(isSignalData({ s: 'x'.repeat(MAX_SIGNAL_JSON - 8) }), true); // {"s":"..."} is 8 more
    assert.equal(isSignalData({ s: 'x'.repeat(MAX_SIGNAL_JSON - 7) }), false);
    for (const bad of [null, [], 'x', 5, true]) assert.equal(isSignalData(bad), false, String(bad));
  });
});

describe('deeply nested JSON on every echoed path', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('a nested ping number is bad-request, and the server keeps answering', async () => {
    const c = await Client.connect(s.url);
    c.sendText(`{"t":"ping","n":${nestedText(5000)}}`);
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' });
    for (const n of ['7', null, { a: 1 }]) {
      c.sendJson({ t: 'ping', n });
      assert.deepEqual(await c.nextJson(), { t: 'error', code: 'bad-request' }, JSON.stringify(n));
    }
    c.sendJson({ t: 'ping', n: 7 });
    assert.deepEqual(await c.nextJson(), { t: 'pong', n: 7 });
    // A brand new connection works too, so the process is still up.
    const other = await Client.connect(s.url);
    await other.close();
    await c.close();
  });

  test('nested settings are bad-request, the old settings stay, and the room still works', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    host.sendJson({ t: 'settings', settings: { map: 1 } });
    await host.nextJson();
    host.sendText(`{"t":"settings","settings":{"a":${nestedText(5000)}}}`);
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    const { client: ben, room } = await joinRoom(s.url, code, 'Ben');
    assert.deepEqual(room.settings, { map: 1 });
    assert.equal((await host.nextJson()).t, 'room');
    await ben.close();
    await host.close();
  });

  test('nested signal data is bad-request and never reaches the target', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    host.sendText(`{"t":"signal","to":1,"data":{"a":${nestedText(5000)}}}`);
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    host.sendJson({ t: 'signal', to: 1, data: nestedObject(MAX_SIGNAL_DEPTH + 1) });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    host.sendJson({ t: 'signal', to: 1, data: { s: 'x'.repeat(MAX_SIGNAL_JSON) } });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    assert.deepEqual(await ben.settle(), []);
    // A normal signal still goes through afterwards.
    host.sendJson({ t: 'signal', to: 1, data: CHROME_SIGNALS[0] });
    assert.deepEqual(await ben.nextJson(), { t: 'signal', from: 0, data: CHROME_SIGNALS[0] });
    await ben.close();
    await host.close();
  });

  test('real Chrome offers, answers and ICE candidates pass in both directions', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    for (const data of CHROME_SIGNALS) {
      host.sendJson({ t: 'signal', to: 1, data });
      assert.deepEqual(await ben.nextJson(), { t: 'signal', from: 0, data });
      ben.sendJson({ t: 'signal', to: 0, data });
      assert.deepEqual(await host.nextJson(), { t: 'signal', from: 1, data });
    }
    await ben.close();
    await host.close();
  });

  test('deep nesting in fields the server ignores is harmless', async () => {
    const c = await Client.connect(s.url);
    c.sendText(`{"t":"create","name":"Ana","junk":${nestedText(5000)}}`);
    assert.equal((await c.nextJson()).t, 'room');
    c.sendText(`{"t":"ready","ready":true,"junk":${nestedText(5000)}}`);
    assert.equal((await c.nextJson()).t, 'room');
    await c.close();
  });
});

describe('the real server process survives the crash payloads', () => {
  test('node src/index.js stays up after nested ping, settings and signal', async () => {
    const child = spawn(process.execPath, [path.join(serverDir, 'src/index.js')], {
      env: { ...process.env, PORT: '0', NODE_ENV: 'development', ALLOWED_ORIGINS: '' },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let stderr = '';
    child.stderr.on('data', (chunk) => (stderr += chunk));
    let exited = null;
    child.once('exit', (code) => (exited = code));
    try {
      const port = await new Promise((resolve, reject) => {
        let out = '';
        child.stdout.on('data', (chunk) => {
          out += chunk;
          const m = /relay listening on port (\d+)/.exec(out);
          if (m) resolve(Number(m[1]));
        });
        child.once('exit', (code) => reject(new Error(`relay exited early with code ${code}`)));
      });
      const url = `ws://127.0.0.1:${port}`;
      const { host, code } = await makeRoom(url, 'Ana');
      const { client: ben } = await joinRoom(url, code, 'Ben');
      await host.settle();
      host.sendText(`{"t":"ping","n":${nestedText(5000)}}`);
      host.sendText(`{"t":"settings","settings":{"a":${nestedText(5000)}}}`);
      host.sendText(`{"t":"signal","to":1,"data":{"a":${nestedText(5000)}}}`);
      for (let i = 0; i < 3; i++) assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
      host.sendJson({ t: 'ping', n: 1 });
      assert.deepEqual(await host.nextJson(), { t: 'pong', n: 1 });
      assert.equal((await fetch(`http://127.0.0.1:${port}/healthz`)).status, 200);
      assert.equal(exited, null, 'the server process must still be running');
      assert.doesNotMatch(stderr, /uncaught|unhandled/i);
      await ben.close();
      await host.close();
    } finally {
      child.kill();
    }
  });
});

/** A stand-in for a ws WebSocket, enough for Connection. */
class FakeSocket extends EventEmitter {
  constructor() {
    super();
    this.readyState = 1;
    this.bufferedAmount = 0;
    this.sent = [];
    this.terminated = false;
    this.closed = null;
  }
  send(data) {
    this.sent.push(data);
  }
  ping() {}
  terminate() {
    this.terminated = true;
  }
  close(code, reason) {
    this.closed = { code, reason };
  }
}

function fakeConnection(overrides = {}, lobbyOverrides = {}) {
  const ws = new FakeSocket();
  const logs = [];
  const lobby = { log: (line) => logs.push(line), handle() {}, relayBinary() {}, disconnected() {}, ...lobbyOverrides };
  let time = 1000;
  const clock = { now: () => time, set: (t) => (time = t) };
  const conn = new Connection(ws, lobby, { ...DEFAULTS, ...overrides }, clock.now);
  return { ws, conn, logs, clock };
}

describe('Connection safety nets, with a fake socket', () => {
  test('sendJson of a value JSON cannot write returns false and never throws', () => {
    const { ws, conn, logs } = fakeConnection();
    const cyclic = { t: 'x' };
    cyclic.self = cyclic;
    assert.equal(conn.sendJson(cyclic), false);
    assert.equal(conn.sendJson({ t: 'big', n: 10n }), false);
    assert.equal(ws.sent.length, 0);
    assert.ok(logs.some((l) => l.includes('error while handling send')));
    assert.equal(conn.sendJson({ t: 'ok' }), true);
  });

  test('a lobby handler that throws becomes bad-request, not a crash', () => {
    const { ws, logs } = fakeConnection({}, {
      handle() {
        throw new RangeError('boom');
      },
    });
    ws.emit('message', Buffer.from('{"t":"hello","v":1}'), false);
    ws.emit('message', Buffer.from('{"t":"create","name":"A"}'), false);
    assert.deepEqual(JSON.parse(ws.sent.at(-1)), { t: 'error', code: 'bad-request' });
    assert.ok(logs.some((l) => l.includes('RangeError: boom')));
  });

  test('a close handler that throws is logged, not a crash', () => {
    const { ws, logs } = fakeConnection({}, {
      disconnected() {
        throw new Error('gone');
      },
    });
    ws.emit('close');
    assert.ok(logs.some((l) => l.includes('gone')));
  });

  test('messages for a receiver over the buffer limit are dropped, and it is closed if it stays behind', () => {
    const { ws, conn, clock } = fakeConnection({ maxBufferedBytes: 1000, slowReaderMs: 500 });
    assert.equal(conn.sendJson({ t: 'a' }), true);
    ws.bufferedAmount = 1001;
    assert.equal(conn.sendJson({ t: 'b' }), false);
    assert.equal(conn.sendBinary(Buffer.from([1])), false);
    assert.equal(ws.sent.length, 1, 'nothing more was queued');
    clock.set(1400);
    conn.checkIdle(1400);
    assert.equal(ws.terminated, false, 'not yet: behind for only 400 ms');
    clock.set(1500);
    conn.checkIdle(1500);
    assert.equal(ws.terminated, true);
  });

  test('a receiver that catches up in time is kept', () => {
    const { ws, conn, clock } = fakeConnection({ maxBufferedBytes: 1000, slowReaderMs: 500 });
    ws.bufferedAmount = 5000;
    assert.equal(conn.sendJson({ t: 'b' }), false);
    clock.set(1300);
    ws.bufferedAmount = 0;
    conn.checkIdle(1300);
    clock.set(2000);
    conn.checkIdle(2000);
    assert.equal(ws.terminated, false);
    assert.equal(conn.sendJson({ t: 'c' }), true);
  });

  test('a socket with no hello is closed at the deadline, one with a hello is not', () => {
    const silent = fakeConnection({ helloTimeoutMs: 300 });
    silent.conn.checkIdle(1299);
    assert.equal(silent.ws.closed, null);
    silent.conn.checkIdle(1300);
    assert.deepEqual(silent.ws.closed, { code: 1008, reason: 'hello timeout' });

    const polite = fakeConnection({ helloTimeoutMs: 300 });
    polite.ws.emit('message', Buffer.from('{"t":"hello","v":1}'), false);
    polite.conn.checkIdle(5000);
    assert.equal(polite.ws.closed, null);
  });
});

describe('backpressure on a real server', () => {
  test('a receiver that stops reading is closed instead of piling up memory', async () => {
    const s = await startRelay({ maxBufferedBytes: 64 * 1024, slowReaderMs: 300, ratePerSec: 1000, rateBurst: 1000 });
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.settle();
    ben.ws._socket.pause(); // Ben stops reading, like a stuck or hostile client
    const data = { s: 'x'.repeat(7000) };
    const timer = setInterval(() => {
      for (let i = 0; i < 10; i++) if (host.ws.readyState === 1) host.sendJson({ t: 'signal', to: 1, data });
    }, 20);
    try {
      // The server cuts Ben's socket, and the lobby sees Ben leave.
      for (;;) {
        const room = await host.nextOfType('room', 8000);
        if (room.players.length === 1) break;
      }
    } finally {
      clearInterval(timer);
    }
    assert.ok(s.logs.some((l) => l.includes('receiver too far behind')));
    assert.ok(!host.closeInfo, 'the sender is not punished');
    host.sendJson({ t: 'ping', n: 5 });
    assert.deepEqual(await host.nextOfType('pong'), { t: 'pong', n: 5 });
    await host.close();
    await s.stop();
  });

  test('fallback frames for a paused reader are dropped the same way', async () => {
    const s = await startRelay({ maxBufferedBytes: 64 * 1024, slowReaderMs: 300, ratePerSec: 1000, rateBurst: 1000 });
    const { clients } = await startedRoom(s.url, 2);
    const [host, ben] = clients;
    ben.ws._socket.pause();
    const frame = Buffer.alloc(8000, 7);
    frame[0] = 1;
    const timer = setInterval(() => {
      for (let i = 0; i < 10; i++) if (host.ws.readyState === 1) host.sendBinary(frame);
    }, 20);
    try {
      for (;;) {
        const room = await host.nextOfType('room', 8000);
        if (room.players.some((p) => p.away)) break; // a started run keeps Ben's seat as away
      }
    } finally {
      clearInterval(timer);
    }
    assert.ok(s.logs.some((l) => l.includes('receiver too far behind')));
    await host.close();
    await s.stop();
  });
});

describe('hello timeout on a real server', () => {
  test('a socket that never says hello is closed, one that does stays open', async () => {
    const s = await startRelay({ helloTimeoutMs: 200 });
    const silent = await Client.open(s.url);
    const polite = await Client.connect(s.url);
    const info = await Promise.race([silent.closedPromise, sleep(3000).then(() => null)]);
    assert.ok(info, 'the silent socket should be closed');
    assert.equal(info.code, 1008);
    assert.ok(s.logs.some((l) => l.includes('no hello')));
    await sleep(300);
    polite.sendJson({ t: 'ping', n: 2 });
    assert.deepEqual(await polite.nextJson(), { t: 'pong', n: 2 });
    await polite.close();
    await s.stop();
  });
});

describe('per-address limits', () => {
  test('the address source follows CLIENT_IP_HEADER and production', () => {
    assert.deepEqual(clientIpSource({ clientIpHeader: '', production: false }), { kind: 'socket' });
    assert.deepEqual(clientIpSource({ clientIpHeader: '', production: true }), { kind: 'off' });
    assert.deepEqual(clientIpSource({ clientIpHeader: 'socket', production: true }), { kind: 'socket' });
    assert.deepEqual(clientIpSource({ clientIpHeader: 'CF-Connecting-IP', production: true }), { kind: 'header', name: 'cf-connecting-ip' });
    const env = loadConfig({ CLIENT_IP_HEADER: ' CF-Connecting-IP ', MAX_CONNECTIONS_PER_IP: '3', MAX_ROOMS_PER_IP: '2', HELLO_TIMEOUT_MS: '500', MAX_BUFFERED_BYTES: '9', SLOW_READER_MS: '7' });
    assert.equal(env.clientIpHeader, 'cf-connecting-ip');
    assert.equal(env.maxConnectionsPerIp, 3);
    assert.equal(env.maxRoomsPerIp, 2);
    assert.equal(env.helloTimeoutMs, 500);
    assert.equal(env.maxBufferedBytes, 9);
    assert.equal(env.slowReaderMs, 7);
    const defaults = loadConfig({});
    assert.equal(defaults.maxConnectionsPerIp, 20);
    assert.equal(defaults.maxRoomsPerIp, 10);
    assert.equal(defaults.helloTimeoutMs, 10_000);
  });

  test('clientIp reads the header or the socket, and shortens mapped IPv4', () => {
    const req = (headers, remoteAddress = '::ffff:10.0.0.5') => ({ headers, socket: { remoteAddress } });
    const header = { kind: 'header', name: 'cf-connecting-ip' };
    assert.equal(clientIp(req({ 'cf-connecting-ip': '203.0.113.9' }), header), '203.0.113.9');
    assert.equal(clientIp(req({ 'cf-connecting-ip': ' 203.0.113.9 , 1.1.1.1' }), header), '203.0.113.9');
    // A missing header is not counted under the socket address, which behind a proxy is the
    // proxy's and would be shared by everybody.
    assert.equal(clientIp(req({}), header), null);
    assert.equal(clientIp(req({ 'cf-connecting-ip': '' }), header), null);
    assert.equal(clientIp(req({ 'cf-connecting-ip': '203.0.113.9' }), { kind: 'socket' }), '10.0.0.5');
    assert.equal(clientIp(req({}), { kind: 'off' }), null);
  });

  test('too many sockets from one address get 429, and a slot frees when one closes', async () => {
    const s = await startRelay({ maxConnectionsPerIp: 3 });
    const open = [];
    for (let i = 0; i < 3; i++) open.push(await Client.connect(s.url));
    await assert.rejects(Client.open(s.url), { status: 429 });
    assert.ok(s.logs.some((l) => l.includes('too many from one address')));
    assert.equal(s.relay.connectionsFrom('127.0.0.1'), 3);
    await open.pop().close();
    for (let i = 0; i < 50 && s.relay.connectionsFrom('127.0.0.1') > 2; i++) await sleep(10);
    open.push(await Client.connect(s.url));
    for (const c of open) await c.close();
    for (let i = 0; i < 50 && s.relay.connectionsFrom('127.0.0.1') > 0; i++) await sleep(10);
    assert.equal(s.relay.connectionsFrom('127.0.0.1'), 0, 'the count goes back to zero');
    await s.stop();
  });

  test('with a header configured, each address has its own count', async () => {
    const s = await startRelay({ maxConnectionsPerIp: 1, clientIpHeader: 'x-test-ip' });
    const a = await Client.connect(s.url, { headers: { 'x-test-ip': '198.51.100.1' } });
    const b = await Client.connect(s.url, { headers: { 'x-test-ip': '198.51.100.2' } });
    await assert.rejects(Client.open(s.url, { headers: { 'x-test-ip': '198.51.100.1' } }), { status: 429 });
    // Requests without the header are not limited (see clientIp).
    const bare1 = await Client.connect(s.url);
    const bare2 = await Client.connect(s.url);
    for (const c of [a, b, bare1, bare2]) await c.close();
    await s.stop();
  });

  test('one address can host only so many rooms, and others are not affected', async () => {
    const s = await startRelay({ maxRoomsPerIp: 2, clientIpHeader: 'x-test-ip' });
    const from = (ip) => ({ headers: { 'x-test-ip': ip } });
    const hosts = [];
    for (let i = 0; i < 3; i++) hosts.push(await Client.connect(s.url, from('198.51.100.7')));
    hosts[0].sendJson({ t: 'create', name: 'A' });
    const room = await hosts[0].nextJson();
    hosts[1].sendJson({ t: 'create', name: 'B' });
    assert.equal((await hosts[1].nextJson()).t, 'room');
    hosts[2].sendJson({ t: 'create', name: 'C' });
    assert.deepEqual(await hosts[2].nextJson(), { t: 'error', code: 'ip-limit' });
    // Joining is still fine from the same address, and another address can create.
    hosts[2].sendJson({ t: 'join', code: room.code, name: 'C' });
    assert.equal((await hosts[2].nextJson()).t, 'room');
    const other = await Client.connect(s.url, from('198.51.100.8'));
    other.sendJson({ t: 'create', name: 'D' });
    assert.equal((await other.nextJson()).t, 'room');
    // When one of the rooms closes, the address may create again.
    hosts[1].sendJson({ t: 'leave' });
    await hosts[1].settle();
    hosts[1].sendJson({ t: 'create', name: 'B' });
    assert.equal((await hosts[1].nextJson()).t, 'room');
    for (const c of [...hosts, other]) await c.close();
    await s.stop();
  });

  test('in production without CLIENT_IP_HEADER the per-address limits are off', async () => {
    const s = await startRelay({ production: true, allowedOrigins: [DEV_ORIGIN], maxConnectionsPerIp: 1, maxRoomsPerIp: 1 });
    assert.equal(s.relay.ipSource.kind, 'off');
    const a = await makeRoom(s.url, 'A');
    const b = await makeRoom(s.url, 'B'); // a second socket and a second room from one address
    assert.equal(s.relay.roomCount(), 2);
    for (const c of [a.host, b.host]) await c.close();
    await s.stop();
  });

  test('in production with CLIENT_IP_HEADER=socket the limits apply', async () => {
    const s = await startRelay({ production: true, allowedOrigins: [DEV_ORIGIN], maxConnectionsPerIp: 1, clientIpHeader: 'socket' });
    const a = await Client.connect(s.url);
    await assert.rejects(Client.open(s.url), { status: 429 });
    await a.close();
    await s.stop();
  });
});
