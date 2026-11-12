// The title and every co-op screen at five window sizes. Nothing may run off the safe area or
// overlap anything else, the title keeps its best run, and screenshots are left to look at.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { debug, waitScreen, clickButton, tapButton } from './harness.mjs';
import {
  NET, PHASE, ERR, sizes, useBrowser, open, waitNet, frameUi, toCoop, tapKey, typeText, host, press, isTouch, shot, hasButton, extentOf,
  overlaps, checkLayout, readNet,
} from './coophelpers.mjs';

useBrowser();

// The five sizes of the brief, and a very short window as a stress case.
const shapes = [...sizes, ['480x320', { viewport: { width: 480, height: 320 } }]];

for (const [name, options] of shapes) {
  test(`the title and every co-op screen lay out cleanly at ${name}`, async () => {
    const { context, page, problems } = await open(options);
    await page.waitForTimeout(400);

    // The title, with its best run still showing.
    let ui = await frameUi(page);
    assert.ok(hasButton(ui, 'Co-op'), 'the title has a Co-op button');
    // The very short stress window has no room beside its two columns, so only the five sizes
    // of the brief must keep the best run.
    if (name !== '480x320') assert.ok(extentOf(ui, 'best run'), 'the best run panel is shown');
    checkLayout(ui, `title ${name}`);
    await shot(page, `${name}-title`);

    // The menu, with a notice showing.
    await toCoop(page, options);
    await host(page, options);
    await debug(page, NET.guestRoom, 2);
    await page.waitForTimeout(150);
    await debug(page, NET.hostLeft);
    await waitScreen(page, 'coop-menu');
    ui = await frameUi(page);
    assert.ok(extentOf(ui, 'coop notice'));
    checkLayout(ui, `menu ${name}`);
    await shot(page, `${name}-menu`);

    // The same menu with a run to rejoin: one more button, still inside the safe area.
    await debug(page, NET.rejoinSeat, 1);
    ui = await frameUi(page);
    assert.ok(hasButton(ui, 'Rejoin K7QXM'), `buttons: ${ui.buttons.map((b) => b.n)}`);
    checkLayout(ui, `menu with rejoin ${name}`);
    await shot(page, `${name}-menu-rejoin`);
    await debug(page, NET.rejoinSeat, 0);
    await frameUi(page);  // A frame without it, so the next press finds the buttons where they are now.

    // Waiting for the server, with Retry.
    await debug(page, NET.reset);
    await debug(page, NET.wakeMs, -1);
    await press(page, options, 'Host Co-op');
    await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
    await debug(page, NET.wakeSeconds, 75);
    ui = await frameUi(page);
    assert.ok(hasButton(ui, 'Retry'));
    checkLayout(ui, `waking ${name}`);
    await shot(page, `${name}-waking`);

    // The lobby: a full room as host, then as a guest.
    await debug(page, NET.wakeMs, 30);
    await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
    await debug(page, NET.room, 4);
    await debug(page, NET.readyMask, 0b1111);
    ui = await frameUi(page);
    assert.ok(hasButton(ui, 'Start'));
    checkLayout(ui, `lobby ${name}`, [['coop code', 'Copy'], ['coop code', 'Copied']]);
    await shot(page, `${name}-lobby-host`);
    await debug(page, NET.guestRoom, 3);
    await debug(page, NET.readyMask, 0b011);
    ui = await frameUi(page);
    assert.ok(!hasButton(ui, 'Start'));
    checkLayout(ui, `guest lobby ${name}`, [['coop code', 'Copy'], ['coop code', 'Copied']]);
    await shot(page, `${name}-lobby-guest`);
    await press(page, options, 'Leave');
    await waitScreen(page, 'coop-menu');

    // Join, with the code typed, and an error with Retry showing.
    await press(page, options, 'Join Co-op');
    await waitScreen(page, 'coop-join');
    ui = await frameUi(page);
    checkLayout(ui, `join ${name}`);
    await shot(page, `${name}-join`);
    if (isTouch(options)) {
      await tapButton(page, 'Room code');
      await waitNet(page, (n) => n.keys === 1, 'the code keyboard');
      ui = await frameUi(page);
      const keys = ui.buttons.filter((b) => b.n.startsWith('key '));
      assert.ok(keys.length > 30, `the code keyboard has ${keys.length} keys`);
      for (const key of keys) {
        assert.ok(key.x >= 0 && key.y >= 0 && key.x + key.w <= ui.width && key.y + key.h <= ui.height, `key "${key.n}" leaves the screen at ${name}`);
        assert.ok(key.cw >= 30 && key.ch >= 28, `key "${key.n}" is only ${key.cw.toFixed(0)}x${key.ch.toFixed(0)} CSS pixels at ${name}`);
      }
      for (let i = 0; i < keys.length; i++) for (let j = i + 1; j < keys.length; j++) assert.ok(!overlaps(keys[i], keys[j]), `keys overlap at ${name}`);
      await shot(page, `${name}-code-keys`);
      let typed = '';
      for (const key of ['K', '7', 'Q', 'X', 'M']) {
        typed += key;
        await tapKey(page, key, 'codeText', typed);
      }
      await tapButton(page, 'key OK');
      await waitNet(page, (n) => n.editing === 0, 'the keyboard to close');
    } else {
      await clickButton(page, 'Room code');
      await typeText(page, 'K7QXM');
      await page.keyboard.press('Enter');
      await waitNet(page, (n) => n.editing === 0, 'editing to end');
    }
    await frameUi(page);
    await debug(page, NET.nextError, ERR.unreachable);
    await press(page, options, 'Join');
    await waitNet(page, (n) => n.phase === PHASE.error, 'the error');
    ui = await frameUi(page);
    assert.ok(hasButton(ui, 'Retry'));
    checkLayout(ui, `join error ${name}`);
    await shot(page, `${name}-join-error`);
    assert.equal((await readNet(page)).sub, 'coop-join');

    assert.deepEqual(problems, []);
    await context.close();
  });
}
