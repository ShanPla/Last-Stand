import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';
import { Client, joinRoom, makeRoom, startedRoom, startRelay } from './helpers.js';

const CHOICE = { map: 1, difficulty: 2, mode: 1 };

describe('lobby settings', () => {
  let s;
  before(async () => {
    s = await startRelay();
  });
  after(() => s.stop());

  test('a room has no settings field until the host sends some', async () => {
    const { host, room } = await makeRoom(s.url, 'Ana');
    assert.equal('settings' in room, false);
    await host.close();
  });

  test('the host settings are stored and sent to everyone in a room message', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.nextJson(); // the room message for Ben joining
    host.sendJson({ t: 'settings', settings: CHOICE });
    const hostView = await host.nextJson();
    const benView = await ben.nextJson();
    assert.deepEqual(hostView.settings, CHOICE);
    assert.deepEqual(benView.settings, CHOICE);
    assert.equal(benView.you, 1);
    // The host gets nothing else: the room message is the only answer.
    assert.deepEqual(await host.settle(), []);
    await ben.close();
    await host.close();
  });

  test('a later change replaces the settings, and a later joiner gets them at once', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    host.sendJson({ t: 'settings', settings: CHOICE });
    await host.nextJson();
    host.sendJson({ t: 'settings', settings: { map: 0, difficulty: 0, mode: 0 } });
    assert.deepEqual((await host.nextJson()).settings, { map: 0, difficulty: 0, mode: 0 });
    const { client, room } = await joinRoom(s.url, code, 'Ben');
    assert.deepEqual(room.settings, { map: 0, difficulty: 0, mode: 0 });
    await client.close();
    await host.close();
  });

  test('the server stores the object as it is, without reading it', async () => {
    const { host } = await makeRoom(s.url, 'Ana');
    const odd = { map: 99, extra: ['x', { y: null }], future: true };
    host.sendJson({ t: 'settings', settings: odd });
    assert.deepEqual((await host.nextJson()).settings, odd);
    host.sendJson({ t: 'settings', settings: {} });
    assert.deepEqual((await host.nextJson()).settings, {});
    await host.close();
  });

  test('a guest gets not-host and nothing changes', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    const { client: ben } = await joinRoom(s.url, code, 'Ben');
    await host.nextJson();
    ben.sendJson({ t: 'settings', settings: CHOICE });
    assert.deepEqual(await ben.nextJson(), { t: 'error', code: 'not-host' });
    assert.deepEqual(await host.settle(), []);
    const { client: cy, room } = await joinRoom(s.url, code, 'Cy');
    assert.equal('settings' in room, false);
    await cy.close();
    await ben.close();
    await host.close();
  });

  test('outside a room it is not-in-room', async () => {
    const c = await Client.connect(s.url);
    c.sendJson({ t: 'settings', settings: CHOICE });
    assert.deepEqual(await c.nextJson(), { t: 'error', code: 'not-in-room' });
    await c.close();
  });

  test('bad shapes are bad-request and keep the old settings', async () => {
    const { host, code } = await makeRoom(s.url, 'Ana');
    host.sendJson({ t: 'settings', settings: CHOICE });
    await host.nextJson();
    const bad = [null, 5, 'map', true, [], [1, 2]];
    for (const settings of bad) {
      host.sendJson({ t: 'settings', settings });
      assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' }, JSON.stringify(settings));
    }
    host.sendText(JSON.stringify({ t: 'settings' }));
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    const { client, room } = await joinRoom(s.url, code, 'Ben');
    assert.deepEqual(room.settings, CHOICE);
    await client.close();
    await host.close();
  });

  test('over 200 characters of JSON is refused, exactly 200 is fine', async () => {
    const { host } = await makeRoom(s.url, 'Ana');
    // {"a":"..."} adds 8 characters around the text.
    const fits = { a: 'x'.repeat(192) };
    assert.equal(JSON.stringify(fits).length, 200);
    host.sendJson({ t: 'settings', settings: fits });
    assert.deepEqual((await host.nextJson()).settings, fits);
    host.sendJson({ t: 'settings', settings: { a: 'x'.repeat(193) } });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    host.sendJson({ t: 'settings', settings: { a: 'x'.repeat(5000) } });
    assert.deepEqual(await host.nextJson(), { t: 'error', code: 'bad-request' });
    await host.close();
  });

  test('settings stay allowed after the start', async () => {
    const { clients } = await startedRoom(s.url, 2);
    const [host, ben] = clients;
    host.sendJson({ t: 'settings', settings: CHOICE });
    assert.deepEqual((await host.nextOfType('room')).settings, CHOICE);
    assert.deepEqual((await ben.nextOfType('room')).settings, CHOICE);
    await ben.close();
    await host.close();
  });
});
