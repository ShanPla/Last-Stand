// The co-op menu, host screen and join screen, and the text entry they use, tested in a real
// browser with a physical keyboard and a mouse. Touch and gamepad are in coopscreens-input, and
// the layout at five window sizes is in coopscreens-layout. There is no real server yet, so the
// game's fake lobby is driven with debug commands (see coophelpers.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readUi, debug, waitScreen, waitSelected, clickButton, startGame } from './harness.mjs';
import {
  NET, PHASE, ERR, useBrowser, open, readNet, waitNet, frameUi, toCoop, typeText, host, shot, names, hasButton, extentOf,
  webUrl, newContext, desktop,
} from './coophelpers.mjs';

useBrowser();

// ---- Opening the screens ---------------------------------------------------------

test('Co-op sits after the selectors in the focus order, and opens by keyboard', async () => {
  const { context, page, problems } = await open();
  await waitSelected(page, 'PLAY');
  for (const name of ['Difficulty', 'Map', 'Mode', 'Co-op', 'Settings', 'How to Play']) {
    await page.keyboard.press('ArrowDown');
    await waitSelected(page, name);
  }
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  await waitSelected(page, 'Co-op');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'coop-menu');
  const ui = await frameUi(page);
  for (const name of ['Your name', 'Host Co-op', 'Join Co-op', 'Back']) assert.ok(hasButton(ui, name), `no "${name}": ${names(ui)}`);
  await waitSelected(page, 'Host Co-op');
  // Esc goes back to the title, with focus still on Co-op.
  await page.keyboard.press('Escape');
  await waitScreen(page, 'title');
  await waitSelected(page, 'Co-op');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Co-op opens by mouse click, and Back returns', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Back');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('PLAY is still one press from the title, and it is still the first focus', async () => {
  const { context, page, problems } = await open();
  await waitSelected(page, 'PLAY');
  const ui = await frameUi(page);
  assert.equal(ui.buttons[0].n, 'PLAY');
  await startGame(page);
  assert.equal((await readUi(page)).demo, 0);
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Text entry ------------------------------------------------------------------

test('the name field: typing, space, Enter, Esc, and the name is saved', async () => {
  const { context, page, problems } = await open();
  for (let i = 0; i < 4; i++) await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Co-op');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'coop-menu');
  await waitSelected(page, 'Host Co-op');
  await page.keyboard.press('ArrowUp');
  await waitSelected(page, 'Your name');
  await page.keyboard.press('Enter');
  await typeText(page, 'Ana Maria');
  let net = await readNet(page);
  assert.equal(net.name, 'Ana Maria');
  assert.equal(net.editing, 1);
  assert.equal(net.keys, 0, 'a keyboard user types, so there is no on-screen keyboard');
  // Enter ends the editing and stays on the menu. The name is saved.
  await page.keyboard.press('Enter');
  net = await waitNet(page, (n) => n.editing === 0, 'editing to end');
  assert.equal(net.sub, 'coop-menu');
  assert.match(await page.evaluate(() => localStorage.getItem('laststand.save')), /coopName=Ana Maria\n/);

  // Esc while editing ends the editing only. A second Esc leaves the screen.
  await page.keyboard.press('Enter');
  await typeText(page, 'x');
  await page.keyboard.press('Escape');
  net = await waitNet(page, (n) => n.editing === 0, 'Esc to end the editing');
  assert.equal(net.sub, 'coop-menu', 'the first Esc must not go back');
  assert.equal(net.name, 'Ana Mariax');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'title');
  assert.deepEqual(problems, []);
  await context.close();
});

test('typing letters that are menu keys, and characters that are not allowed', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Your name');
  await typeText(page, 'wasdp r');          // W A S D P R and Space are menu keys. Here they are text.
  let net = await readNet(page);
  assert.equal(net.name, 'wasdp r');
  assert.equal(net.sub, 'coop-menu');
  await page.keyboard.type('!@#-_');          // Not allowed in a name.
  await page.keyboard.type('123456789');      // The 12-character limit stops this.
  await page.waitForTimeout(150);
  net = await readNet(page);
  assert.equal(net.name, 'wasdp r12345');
  await page.keyboard.press('Backspace');
  await page.keyboard.press('Backspace');
  await page.waitForTimeout(150);
  assert.equal((await readNet(page)).name, 'wasdp r123');
  // Holding Backspace repeats.
  await page.keyboard.down('Backspace');
  await page.waitForTimeout(1500);
  await page.keyboard.up('Backspace');
  await page.waitForTimeout(150);
  assert.equal((await readNet(page)).name, '');
  // A name can't start with a space.
  await page.keyboard.type('  Bo');
  await page.waitForTimeout(150);
  assert.equal((await readNet(page)).name, 'Bo');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a saved name comes back after a reload, and an old save with no name still loads', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Your name');
  await typeText(page, 'Zed 7');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await page.reload();
  await page.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(page, 'title');
  await toCoop(page);
  assert.equal((await readNet(page)).name, 'Zed 7');
  // The saved name is what a room uses, and spaces at its ends never get through.
  await clickButton(page, 'Your name');
  await typeText(page, ' Q ');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, 30);
  await debug(page, NET.latencyMs, 30);
  const room = await host(page);
  assert.equal(room.players[0].name, 'Zed 7 Q', 'the name has no trailing space');
  assert.deepEqual(problems, []);
  await context.close();

  // A save written before co-op existed has no name key.
  const old = await newContext(desktop);
  const oldPage = await old.newPage();
  await oldPage.addInitScript(() => localStorage.setItem('laststand.save', 'version=1\nmusic=0.500\neffects=0.800\nmap=1\n'));
  await oldPage.goto(webUrl() + '?debug&fakenet');
  await oldPage.waitForFunction(() => document.getElementById('loading-screen').classList.contains('is-done'), null, { timeout: 30000 });
  await waitScreen(oldPage, 'title');
  await toCoop(oldPage);
  assert.equal((await readNet(oldPage)).name, '');
  assert.equal((await readUi(oldPage)).music, 0.5, 'the rest of the old save still loaded');
  await old.close();
});

// ---- Host ------------------------------------------------------------------------

test('Host Co-op shows a room code, you in the list as host, and Leave goes back', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Host Co-op');
  await waitScreen(page, 'coop-host');
  const net = await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
  assert.match(net.code, /^[A-HJKMNP-Z2-9]{5}$/);
  assert.equal(net.you, 0);
  assert.equal(net.host, 0);
  assert.equal(net.players[0].name, 'Player 1', 'an empty name becomes Player 1');
  const ui = await frameUi(page);
  for (const name of ['Copy', 'Ready', 'Start', 'Leave']) assert.ok(hasButton(ui, name), `no "${name}": ${names(ui)}`);
  assert.ok(extentOf(ui, 'coop code') && extentOf(ui, 'coop player 0') && extentOf(ui, 'coop player 3'));
  await clickButton(page, 'Leave');
  await waitScreen(page, 'coop-menu');
  assert.equal((await readNet(page)).phase, PHASE.offline);
  assert.deepEqual(problems, []);
  await context.close();
});

test('waking the server: a status, Retry after the timeout, Cancel, and a failed host with Retry', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, -1);
  await clickButton(page, 'Host Co-op');
  await waitScreen(page, 'coop-host');
  await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
  let ui = await frameUi(page);
  assert.ok(extentOf(ui, 'coop status'), 'there is a status line');
  assert.ok(hasButton(ui, 'Cancel'));
  assert.ok(!hasButton(ui, 'Retry'), 'no Retry before the timeout');
  await shot(page, 'waking');
  await debug(page, NET.wakeSeconds, 69);
  assert.ok(!hasButton(await frameUi(page), 'Retry'), 'still no Retry at 69 s');
  await debug(page, NET.wakeSeconds, 71);
  ui = await frameUi(page);
  assert.ok(hasButton(ui, 'Retry'), 'Retry appears after about 70 s');
  await shot(page, 'waking-retry');
  // Retry starts the wait again.
  await clickButton(page, 'Retry');
  await page.waitForTimeout(250);
  const net = await readNet(page);
  assert.equal(net.phase, PHASE.waking);
  assert.ok(net.wake < 5, `the timer restarts, it is ${net.wake}`);
  assert.ok(!hasButton(await frameUi(page), 'Retry'));
  // The server wakes at last, and the room opens.
  await debug(page, NET.wakeMs, 30);
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room after the wake');
  await clickButton(page, 'Leave');
  await waitScreen(page, 'coop-menu');

  // Cancel while waiting goes back to the menu.
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, -1);
  await clickButton(page, 'Host Co-op');
  await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
  await frameUi(page);
  await clickButton(page, 'Cancel');
  await waitScreen(page, 'coop-menu');
  assert.equal((await readNet(page)).phase, PHASE.offline);

  // A failed host shows the error and Retry tries again.
  await debug(page, NET.wakeMs, 30);
  await debug(page, NET.nextError, ERR.unreachable);
  await clickButton(page, 'Host Co-op');
  await waitNet(page, (n) => n.phase === PHASE.error, 'the error');
  ui = await frameUi(page);
  assert.ok(hasButton(ui, 'Retry') && hasButton(ui, 'Back'));
  await shot(page, 'host-error');
  await clickButton(page, 'Retry');
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room after Retry');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Start is enabled only when the host has company and everyone is ready', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  // Alone: ready, but nobody to play with.
  await clickButton(page, 'Ready');
  await waitNet(page, (n) => n.players[0].ready === 1, 'you to be ready');
  assert.equal((await readNet(page)).canStart, 0);
  await clickButton(page, 'Start');
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).phase, PHASE.inLobby, 'Start does nothing alone');

  // Three players, the other two ready, you not.
  await debug(page, NET.room, 3);
  await debug(page, NET.readyMask, 0b110);
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).canStart, 0);
  await clickButton(page, 'Start');
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).phase, PHASE.inLobby, 'Start does nothing while you are not ready');
  await shot(page, 'lobby-not-ready');

  // One of the others is not ready.
  await clickButton(page, 'Ready');
  await waitNet(page, (n) => n.players[0].ready === 1, 'you to be ready');
  await debug(page, NET.readyMask, 0b011);
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).canStart, 0);

  // Everyone is ready: Start works.
  await debug(page, NET.readyMask, 0b111);
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).canStart, 1);
  await shot(page, 'lobby-ready');
  await clickButton(page, 'Start');
  const net = await waitNet(page, (n) => n.phase === PHASE.starting, 'the run to start');
  assert.equal(net.started, 1);
  assert.deepEqual(problems, []);
  await context.close();
});

test('Start by keyboard, and the keyboard focus skips a disabled Start', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  await debug(page, NET.room, 2);
  await debug(page, NET.readyMask, 0b10);
  await page.waitForTimeout(250);
  await waitSelected(page, 'Ready');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Leave');   // Start is disabled, so it is skipped.
  await page.keyboard.press('ArrowUp');
  await waitSelected(page, 'Ready');
  await page.keyboard.press('Enter');  // Ready on.
  await waitNet(page, (n) => n.canStart === 1, 'everyone ready');
  await page.keyboard.press('ArrowDown');
  await waitSelected(page, 'Start');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.phase === PHASE.starting, 'the run to start');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a joined player sees the lobby without a Start button', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  await debug(page, NET.guestRoom, 3);
  await debug(page, NET.readyMask, 0b111);
  const ui = await frameUi(page);
  assert.ok(!hasButton(ui, 'Start'), 'only the host has Start');
  assert.ok(hasButton(ui, 'Ready') && hasButton(ui, 'Leave'));
  const net = await readNet(page);
  assert.equal(net.you, 1);
  assert.equal(net.canStart, 0);
  await shot(page, 'lobby-guest');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Copy puts the room code on the clipboard', async () => {
  const { context, page, problems } = await open();
  await context.grantPermissions(['clipboard-read', 'clipboard-write'], { origin: new URL(webUrl()).origin });
  await toCoop(page);
  const room = await host(page);
  await clickButton(page, 'Copy');
  await page.waitForFunction(() => window.LS_lastCopied !== undefined, null, { timeout: 4000 });
  assert.equal(await page.evaluate(() => window.LS_lastCopied), room.code);
  assert.equal(await page.evaluate(() => navigator.clipboard.readText()), room.code);
  assert.ok(hasButton(await frameUi(page), 'Copied'), 'the button says it worked');
  await page.waitForTimeout(2200);
  assert.ok(hasButton(await frameUi(page), 'Copy'), 'and goes back');
  assert.deepEqual(problems, []);
  await context.close();
});

test('the host leaving or the server dropping returns to the menu with a notice', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  await debug(page, NET.guestRoom, 3);
  await page.waitForTimeout(250);
  await debug(page, NET.hostLeft);
  await waitScreen(page, 'coop-menu');
  let net = await readNet(page);
  assert.equal(net.notice, 'The host left. The run has ended.');
  assert.equal(net.phase, PHASE.offline, 'the lobby is cleaned up');
  assert.ok(extentOf(await frameUi(page), 'coop notice'));
  await shot(page, 'menu-notice');

  // Hosting again clears the notice. Then the connection drops.
  await host(page);
  assert.equal((await readNet(page)).notice, '');
  await debug(page, NET.loseServer);
  await waitScreen(page, 'coop-menu');
  net = await readNet(page);
  assert.equal(net.notice, 'Lost the connection to the server.');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Starting the run ------------------------------------------------------------

// A guest whose room has started waits for the host's run. If it never comes, there must be a way out.
async function guestWaitingToStart(page) {
  await toCoop(page);
  await host(page);
  await debug(page, NET.guestRoom, 3);
  await debug(page, NET.phase, PHASE.starting);
  await waitNet(page, (n) => n.phase === PHASE.starting && n.you === 1, 'the guest to be in Starting');
  const ui = await frameUi(page);
  assert.ok(extentOf(ui, 'coop status'), 'it says the run is starting');
  assert.ok(hasButton(ui, 'Leave'), `Leave is there while starting: ${names(ui)}`);
  assert.ok(!hasButton(ui, 'Ready') && !hasButton(ui, 'Start'), 'nothing else can be pressed');
}

test('a guest who is waiting for the run to start can leave with the button', async () => {
  const { context, page, problems } = await open();
  await guestWaitingToStart(page);
  await shot(page, 'starting-leave');
  await clickButton(page, 'Leave');
  await waitScreen(page, 'coop-menu');
  const net = await readNet(page);
  assert.equal(net.phase, PHASE.offline);
  assert.equal(net.sub, 'coop-menu');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a guest who is waiting for the run to start can leave with Esc', async () => {
  const { context, page, problems } = await open();
  await guestWaitingToStart(page);
  await page.keyboard.press('Escape');
  await waitScreen(page, 'coop-menu');
  assert.equal((await readNet(page)).phase, PHASE.offline);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a guest who waits long for the run to start is told they can leave', async () => {
  const { context, page, problems } = await open();
  await guestWaitingToStart(page);
  assert.ok(!extentOf(await frameUi(page), 'coop starting hint'), 'no hint at first');
  await page.waitForTimeout(10500);
  const ui = await frameUi(page);
  assert.ok(extentOf(ui, 'coop starting hint'), 'the hint shows after ten seconds');
  assert.ok(hasButton(ui, 'Leave'));
  await clickButton(page, 'Leave');
  await waitScreen(page, 'coop-menu');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a room that starts with everyone else away does not begin a run: the host is told and the room closes', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await host(page);
  await debug(page, NET.room, 2);
  await debug(page, NET.readyMask, 0b11);
  await debug(page, NET.awayMask, 0b10);
  await debug(page, NET.phase, PHASE.starting);
  const net = await waitNet(page, (n) => n.sub === 'coop-menu' && n.notice !== '', 'the host to be sent back to the menu');
  assert.match(net.notice, /other players left/);
  assert.equal(net.phase, PHASE.offline);
  assert.equal((await readUi(page)).screen, 'coop-menu', 'no run was started');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- Join ------------------------------------------------------------------------

test('Rejoin: shown first and selected only when there is a seat, says it is rejoining, and a refusal explains and removes it', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  let ui = await frameUi(page);
  assert.ok(!names(ui).some((n) => n.startsWith('Rejoin')), 'no Rejoin without a saved seat');
  await waitSelected(page, 'Host Co-op');

  // A saved seat: Rejoin comes first and takes the selection, and Enter presses it.
  await debug(page, NET.rejoinSeat, 1);
  ui = await frameUi(page);
  assert.deepEqual(names(ui).filter((n) => /Co-op|Rejoin|Back/.test(n)), ['Rejoin K7QXM', 'Host Co-op', 'Join Co-op', 'Back']);
  await waitSelected(page, 'Rejoin K7QXM');
  await debug(page, NET.latencyMs, 600);
  await page.keyboard.press('Enter');
  const waiting = await waitNet(page, (n) => n.phase === PHASE.connecting, 'the rejoin to be sent');
  assert.equal(waiting.rejoining, 1);
  assert.equal(waiting.sub, 'coop-host');
  await shot(page, 'rejoining');
  await waitNet(page, (n) => n.phase === PHASE.starting && n.started === 1 && n.you === 1, 'back in the started run');
  assert.deepEqual(problems, []);
  await context.close();

  // A refused one: back on the menu with the reason, and no Rejoin any more.
  for (const [error, text] of [[ERR.runEnded, /That run has ended/], [ERR.roomClosed, /Room closed/]]) {
    const fresh = await open();
    await toCoop(fresh.page);
    await debug(fresh.page, NET.rejoinSeat, 1);
    await debug(fresh.page, NET.nextError, error);
    await clickButton(fresh.page, 'Rejoin K7QXM');
    const refused = await waitNet(fresh.page, (n) => n.sub === 'coop-menu' && text.test(n.notice), `the notice for error ${error}`);
    assert.equal(refused.rejoin, '');
    ui = await frameUi(fresh.page);
    assert.ok(!names(ui).some((n) => n.startsWith('Rejoin')), 'the button is gone');
    if (error === ERR.runEnded) await shot(fresh.page, 'rejoin-refused');
    assert.deepEqual(fresh.problems, []);
    await fresh.context.close();
  }
});

test('Join: a code is typed in any case, only valid characters stay, and Join waits for all five', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, 'k7q');
  assert.equal((await readNet(page)).codeText, 'K7Q');
  await page.keyboard.type('0oi1l');           // None of these is in the alphabet.
  await page.waitForTimeout(150);
  assert.equal((await readNet(page)).codeText, 'K7Q');
  // Join with three characters does nothing.
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await clickButton(page, 'Join');
  await page.waitForTimeout(300);
  let net = await readNet(page);
  assert.equal(net.phase, PHASE.offline);
  assert.equal(net.sub, 'coop-join');
  // The rest of the code, and a sixth character that is refused.
  await clickButton(page, 'Room code');
  await typeText(page, 'xmz');
  assert.equal((await readNet(page)).codeText, 'K7QXM');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  // Enter after a full code moves on to Join, and Enter again joins.
  await waitSelected(page, 'Join');
  await page.keyboard.press('Enter');
  net = await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the lobby');
  assert.equal(net.sub, 'coop-host', 'a joined player sees the lobby');
  assert.equal(net.code, 'K7QXM');
  assert.equal(net.you, 1);
  assert.equal(net.players[1].name, 'Player 2');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Join shows each error, with Retry only where trying again can help', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, 'K7QXM');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  const errors = [
    ['notFound', ERR.notFound], ['full', ERR.full], ['started', ERR.started], ['badName', ERR.badName], ['unreachable', ERR.unreachable],
  ];
  for (const [label, code] of errors) {
    await debug(page, NET.nextError, code);
    await clickButton(page, 'Join');
    // Each error is a different one, so the one from the loop before can't be mistaken for it.
    const net = await waitNet(page, (n) => n.phase === PHASE.error && n.error === code, `the ${label} error`);
    assert.equal(net.error, code);
    assert.equal(net.sub, 'coop-join');
    const ui = await frameUi(page);
    assert.ok(extentOf(ui, 'coop status'), `${label}: there is a message`);
    assert.equal(hasButton(ui, 'Retry'), code === ERR.unreachable, `${label}: Retry only where trying again can help`);
    await shot(page, `join-error-${label}`);
  }
  // The last error was a lost connection. Retry joins.
  await clickButton(page, 'Retry');
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the lobby after Retry');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Join while the server is waking shows the wait, Retry after the timeout, and Back cancels', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, 'K7QXM');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, -1);
  await clickButton(page, 'Join');
  await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
  let ui = await frameUi(page);
  assert.ok(extentOf(ui, 'coop status'));
  assert.ok(!hasButton(ui, 'Retry'));
  await shot(page, 'join-waking');
  await debug(page, NET.wakeSeconds, 72);
  ui = await frameUi(page);
  assert.ok(hasButton(ui, 'Retry'));
  // Click first and wake the server after. Waking it first made Retry vanish within a few frames,
  // and the buttons below it moved up, so a slow click landed on Back.
  await clickButton(page, 'Retry');
  await debug(page, NET.wakeMs, 30);
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the lobby');
  await clickButton(page, 'Leave');
  await waitScreen(page, 'coop-menu');

  // Back from the join screen cancels a join in progress, and the code field is empty next time.
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await clickButton(page, 'Room code');
  await typeText(page, 'K7QXM');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  await debug(page, NET.reset);
  await debug(page, NET.wakeMs, -1);
  await clickButton(page, 'Join');
  await waitNet(page, (n) => n.phase === PHASE.waking, 'Waking');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'coop-menu');
  assert.equal((await readNet(page)).phase, PHASE.offline);
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  assert.equal((await readNet(page)).codeText, '');
  assert.deepEqual(problems, []);
  await context.close();
});
