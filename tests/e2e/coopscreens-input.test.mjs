// The co-op screens with touch and with a gamepad: the on-screen character keyboard, taps, and
// the D-pad. See coopscreens.test.mjs for the keyboard and mouse.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { debug, waitScreen, waitSelected, tapButton, installFakePad, padPress } from './harness.mjs';
import {
  NET, PHASE, useBrowser, open, readNet, waitNet, frameUi, toCoop, tapKey, shot, hasButton, names, overlaps, phone, smallPhone,
} from './coophelpers.mjs';

useBrowser();

test('touch: the on-screen keyboard types a name and a code, and the keys are finger sized', async () => {
  const { context, page, problems } = await open(phone);
  await toCoop(page, phone);
  await tapButton(page, 'Your name');
  await waitNet(page, (n) => n.keys === 1, 'the on-screen keyboard');
  let ui = await frameUi(page);
  const keys = ui.buttons.filter((b) => b.n.startsWith('key '));
  assert.ok(keys.length >= 40, `a name keyboard has its keys, got ${keys.length}`);
  for (const key of keys) {
    assert.ok(key.cw >= 38 && key.ch >= 36, `key "${key.n}" is only ${key.cw.toFixed(0)}x${key.ch.toFixed(0)} CSS pixels`);
    assert.ok(key.x >= 0 && key.y >= 0 && key.x + key.w <= ui.width && key.y + key.h <= ui.height, `key "${key.n}" leaves the screen`);
  }
  for (let i = 0; i < keys.length; i++) {
    for (let j = i + 1; j < keys.length; j++) assert.ok(!overlaps(keys[i], keys[j]), `keys "${keys[i].n}" and "${keys[j].n}" overlap`);
  }
  await shot(page, 'phone-name-keys');
  // The first letter is a capital, the next ones are lower case.
  await tapKey(page, 'B', 'name', 'B');
  await tapKey(page, 'e', 'name', 'Be');
  await tapKey(page, '7', 'name', 'Be7');
  await tapKey(page, 'SPACE', 'name', 'Be7 ');
  await tapKey(page, 'x', 'name', 'Be7 x');
  await tapKey(page, 'DEL', 'name', 'Be7 ');
  await tapButton(page, 'key OK');
  const net = await waitNet(page, (n) => n.editing === 0, 'the keyboard to close');
  assert.equal(net.keys, 0);
  assert.equal(net.name, 'Be7', 'a trailing space is trimmed');
  assert.match(await page.evaluate(() => localStorage.getItem('laststand.save')), /coopName=Be7\n/);

  // The code keyboard has only the code alphabet.
  await tapButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await tapButton(page, 'Room code');
  await waitNet(page, (n) => n.keys === 1, 'the code keyboard');
  ui = await frameUi(page);
  const codeKeys = ui.buttons.filter((b) => b.n.startsWith('key '));
  assert.ok(!codeKeys.some((k) => ['key 0', 'key 1', 'key O', 'key I', 'key L'].includes(k.n)), 'no look-alike characters on the code keyboard');
  assert.ok(codeKeys.some((k) => k.n === 'key K') && codeKeys.some((k) => k.n === 'key 7'));
  await shot(page, 'phone-code-keys');
  let typed = '';
  for (const key of ['K', '7', 'Q', 'X', 'M']) {
    typed += key;
    await tapKey(page, key, 'codeText', typed);
  }
  await tapButton(page, 'key OK');
  await waitNet(page, (n) => n.editing === 0, 'the keyboard to close');
  await frameUi(page);
  await tapButton(page, 'Join');
  const room = await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the lobby');
  assert.equal(room.players[1].name, 'Be7');
  assert.deepEqual(problems, []);
  await context.close();
});

test('touch: a tap outside the keyboard closes it, and the tap that opened it does not type', async () => {
  const { context, page, problems } = await open(smallPhone);
  await toCoop(page, smallPhone);
  await tapButton(page, 'Your name');
  await waitNet(page, (n) => n.keys === 1, 'the on-screen keyboard');
  await page.waitForTimeout(300);
  assert.equal((await readNet(page)).name, '', 'opening the keyboard types nothing');
  // Near the top of the screen, outside the keyboard and the field.
  await page.touchscreen.tap(160, 20);
  await waitNet(page, (n) => n.editing === 0, 'the keyboard to close');
  assert.equal((await readNet(page)).sub, 'coop-menu');
  assert.deepEqual(problems, []);
  await context.close();
});

test('touch: Ready, Start and Leave work by tap', async () => {
  const { context, page, problems } = await open(phone);
  await toCoop(page, phone);
  await tapButton(page, 'Host Co-op');
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
  await debug(page, NET.room, 2);
  await debug(page, NET.readyMask, 0b10);
  await frameUi(page);
  await tapButton(page, 'Ready');
  await waitNet(page, (n) => n.canStart === 1, 'everyone ready');
  await tapButton(page, 'Start');
  await waitNet(page, (n) => n.phase === PHASE.starting, 'the run to start');
  assert.deepEqual(problems, []);
  await context.close();
});

test('gamepad: open Co-op, pick a name with the character keyboard, host and ready up', async () => {
  const { context, page, problems } = await open();
  await installFakePad(page);
  await page.waitForTimeout(200);
  for (const name of ['Difficulty', 'Map', 'Mode', 'Co-op']) {
    await padPress(page, 13);                             // D-pad down.
    await waitSelected(page, name);
  }
  await padPress(page, 0);                                // A.
  await waitScreen(page, 'coop-menu');
  await waitSelected(page, 'Host Co-op');
  await padPress(page, 12);                               // Up to the name.
  await waitSelected(page, 'Your name');
  await padPress(page, 0);
  await waitNet(page, (n) => n.keys === 1, 'the character keyboard');
  const ui = await frameUi(page);
  assert.ok(hasButton(ui, 'key A'), `the first key is A: ${names(ui).filter((n) => n.startsWith('key')).slice(0, 4)}`);
  await padPress(page, 0);                                // Types A.
  await waitNet(page, (n) => n.name === 'A', 'A');
  await padPress(page, 15);                               // Right, twice.
  await padPress(page, 15);
  await padPress(page, 0);                                // The key two along: a lower case c.
  await waitNet(page, (n) => n.name === 'Ac', 'Ac');
  await padPress(page, 3);                                // Y deletes.
  await waitNet(page, (n) => n.name === 'A', 'A again');
  await padPress(page, 13);                               // Down a row.
  await padPress(page, 0);
  await waitNet(page, (n) => n.name.length === 2, 'a key in the next row');
  await padPress(page, 1);                                // B closes the keyboard, and stays on the screen.
  const net = await waitNet(page, (n) => n.editing === 0, 'the keyboard to close');
  assert.equal(net.sub, 'coop-menu');
  // Host with A, then Ready with A.
  await padPress(page, 13);
  await waitSelected(page, 'Host Co-op');
  await padPress(page, 0);
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
  await waitSelected(page, 'Ready');
  await padPress(page, 0);
  await waitNet(page, (n) => n.players[0].ready === 1, 'ready');
  await padPress(page, 1);                                // B leaves the room.
  await waitScreen(page, 'coop-menu');
  assert.deepEqual(problems, []);
  await context.close();
});
