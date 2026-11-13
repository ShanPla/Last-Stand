// Pasting into the co-op text fields, the missing Enter prompts on co-op buttons, and the Ready check box.
// Like the other coopscreens files it uses the game's fake lobby, so no server is needed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readUi, debug, waitScreen, clickButton, tapButton } from './harness.mjs';
import {
  NET, PHASE, useBrowser, open, readNet, waitNet, frameUi, toCoop, host, shot, webUrl, phone, desktop,
} from './coophelpers.mjs';

useBrowser();

// Puts text on the real system clipboard. The context must have been given the permissions.
async function setClipboard(context, page, text) {
  await context.grantPermissions(['clipboard-read', 'clipboard-write'], { origin: new URL(webUrl()).origin });
  await page.evaluate((value) => navigator.clipboard.writeText(value), text);
}

async function toJoin(page) {
  await clickButton(page, 'Join Co-op');
  await waitScreen(page, 'coop-join');
  await page.waitForTimeout(150);
}

// The extents are in drawing buffer pixels and the buttons in CSS pixels, so this brings an extent to CSS pixels.
const cssRect = (ui, extent, page) => page.evaluate(([e, width]) => {
  const scale = innerWidth / width;
  return { x: e.x * scale, y: e.y * scale, w: e.w * scale, h: e.h * scale };
}, [extent, ui.width]);

// ---- Paste ------------------------------------------------------------------------

test('Ctrl+V pastes a room code into the join screen, in capitals, and the next Enter joins', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await toJoin(page);
  await setClipboard(context, page, ' k7qxm\n');
  await page.keyboard.press('Control+V');
  const net = await waitNet(page, (n) => n.codeText === 'K7QXM', 'the pasted code');
  assert.equal(net.editing, 0, 'a paste does not leave the field open');
  await page.waitForTimeout(150);
  assert.equal((await readUi(page)).selected, 'Join', 'a whole code moves the focus to Join');
  await shot(page, 'join-pasted');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.phase !== PHASE.offline, 'the join to start');
  assert.deepEqual(problems, []);
  await context.close();
});

test('a pasted code keeps only code characters, stops at five, and replaces what was there', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await toJoin(page);
  await setClipboard(context, page, 'k7-q x!m9zzz');  // The dash, space and ! are not code characters.
  await page.keyboard.press('Control+V');
  await waitNet(page, (n) => n.codeText === 'K7QXM', 'a cleaned code');
  // Pasting again replaces it. Letters that no code uses (I, O, 1 and 0) are dropped.
  await setClipboard(context, page, 'a1b0c2oid');
  await page.keyboard.press('Control+V');
  await waitNet(page, (n) => n.codeText === 'ABC2D', 'the second paste');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Shift+Insert pastes too, and a paste while the field is open goes in the field', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  await toJoin(page);
  await setClipboard(context, page, 'k7qxm');
  await page.keyboard.press('Shift+Insert');
  await waitNet(page, (n) => n.codeText === 'K7QXM', 'a code from Shift+Insert');
  // Open the field by click, then paste: the whole code replaces what was there.
  await clickButton(page, 'Room code');
  await waitNet(page, (n) => n.editing === 1, 'the field to open');
  await setClipboard(context, page, 'mnpqr');
  await page.keyboard.press('Control+V');
  await waitNet(page, (n) => n.codeText === 'MNPQR', 'the paste into the open field');
  assert.equal((await readNet(page)).editing, 1, 'the field stays open');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Ctrl+V pastes into the name field, adding to what is typed', async () => {
  const { context, page, problems } = await open();
  await toCoop(page);
  // Not open: the paste becomes the whole name.
  await setClipboard(context, page, '  Maria\t');
  await page.keyboard.press('Control+V');
  await waitNet(page, (n) => n.name === 'Maria', 'a pasted name');
  // Open: the paste is added, and the 12 letter limit still holds.
  await clickButton(page, 'Your name');
  await waitNet(page, (n) => n.editing === 1, 'the field to open');
  await page.keyboard.type(' Jo');
  await waitNet(page, (n) => n.name === 'Maria Jo', 'typing');
  await setClipboard(context, page, ' Santos Reyes');
  await page.keyboard.press('Control+V');
  await waitNet(page, (n) => n.name === 'Maria Jo San', 'the name with the paste added');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  assert.match(await page.evaluate(() => localStorage.getItem('laststand.save')), /coopName=Maria Jo San\n/);
  assert.deepEqual(problems, []);
  await context.close();
});

test('a paste on the title does nothing and breaks nothing, and Ctrl+V is never typed as the letter V', async () => {
  const { context, page, problems } = await open();
  await setClipboard(context, page, 'k7qxm');
  await page.keyboard.press('Control+V');
  await page.waitForTimeout(250);
  assert.equal((await readUi(page)).screen, 'title');
  await toCoop(page);
  await toJoin(page);
  await clickButton(page, 'Room code');
  await waitNet(page, (n) => n.editing === 1, 'the field to open');
  // An empty clipboard: Ctrl+V must not type a letter.
  await page.evaluate(() => navigator.clipboard.writeText(''));
  await page.keyboard.press('Control+V');
  await page.waitForTimeout(250);
  assert.equal((await readNet(page)).codeText, '');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- No Enter prompts on co-op buttons --------------------------------------------

// The key prompts ("Enter", "Esc") drawn on buttons, as rectangles in CSS pixels.
async function chips(page) {
  const ui = await frameUi(page);
  const found = [];
  for (const e of ui.extents.filter((x) => x.n === 'button chip')) found.push(await cssRect(ui, e, page));
  return { ui, found };
}

const centerIn = (box, button) => box.x + box.w / 2 > button.cx - button.cw / 2 && box.x + box.w / 2 < button.cx + button.cw / 2
  && box.y + box.h / 2 > button.cy - button.ch / 2 && box.y + box.h / 2 < button.cy + button.ch / 2;

test('co-op buttons show Esc on Back and Leave but no Enter prompt, and Enter still works', async () => {
  const { context, page, problems } = await open();
  await page.keyboard.press('ArrowDown');   // A key press makes the prompts show.
  await toCoop(page);
  // Menu: Host Co-op is the primary button and used to show Enter. Only Back shows a prompt now.
  let { ui, found } = await chips(page);
  assert.equal(found.length, 1, `the menu shows ${found.length} prompts`);
  assert.ok(centerIn(found[0], ui.buttons.find((b) => b.n === 'Back')), 'the one prompt is on Back');

  // Join: with a full code Join is enabled, and used to show Enter.
  await toJoin(page);
  await clickButton(page, 'Room code');
  await waitNet(page, (n) => n.editing === 1, 'the field to open');
  await page.keyboard.type('K7QXM');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.editing === 0, 'editing to end');
  ({ ui, found } = await chips(page));
  assert.equal(found.length, 1, `the join screen shows ${found.length} prompts`);
  assert.ok(centerIn(found[0], ui.buttons.find((b) => b.n === 'Back')), 'the one prompt is on Back');
  await page.keyboard.press('Escape');
  await waitScreen(page, 'coop-menu');

  // Lobby: Start used to show Enter once everyone was ready.
  await page.keyboard.press('Enter');   // Host Co-op is the focus, so Enter hosts.
  await waitNet(page, (n) => n.phase === PHASE.inLobby, 'the room');
  await debug(page, NET.room, 2);
  await debug(page, NET.readyMask, 0b11);
  await page.waitForTimeout(200);
  if ((await readNet(page)).players[0].ready === 0) await clickButton(page, 'Ready');
  await waitNet(page, (n) => n.canStart === 1, 'Start to be enabled');
  ({ ui, found } = await chips(page));
  assert.equal(found.length, 1, `the lobby shows ${found.length} prompts`);
  assert.ok(centerIn(found[0], ui.buttons.find((b) => b.n === 'Leave')), 'the one prompt is on Leave');
  // Enter still presses the selected Start.
  for (let i = 0; i < 4 && (await readUi(page)).selected !== 'Start'; i++) {
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(150);
  }
  assert.equal((await readUi(page)).selected, 'Start');
  await page.keyboard.press('Enter');
  await waitNet(page, (n) => n.phase === PHASE.starting, 'Enter to start the run');
  assert.deepEqual(problems, []);
  await context.close();
});

test('Rejoin shows no Enter prompt either, and Enter still rejoins', async () => {
  const { context, page, problems } = await open();
  await page.keyboard.press('ArrowDown');
  await toCoop(page);
  await debug(page, NET.rejoinSeat, 1);
  await page.waitForTimeout(250);
  const { ui, found } = await chips(page);
  assert.ok(ui.buttons.some((b) => b.n.startsWith('Rejoin')), 'there is a Rejoin button');
  assert.equal(found.length, 1, `the menu shows ${found.length} prompts`);
  assert.ok(centerIn(found[0], ui.buttons.find((b) => b.n === 'Back')), 'the one prompt is on Back');
  await page.keyboard.press('Enter');
  await waitScreen(page, 'coop-host');
  assert.deepEqual(problems, []);
  await context.close();
});

// ---- The Ready check box --------------------------------------------------------------

for (const [name, options] of [['desktop', desktop], ['phone', phone]]) {
  test(`Ready is a check box: empty, then checked, by click or tap and by Enter (${name})`, async () => {
    const { context, page, problems } = await open(options);
    await toCoop(page, options);
    await host(page, options);
    let ui = await frameUi(page);
    const ready = ui.buttons.find((b) => b.n === 'Ready');
    const off = ui.extents.find((e) => e.n === 'check box off');
    assert.ok(off, 'an empty box shows while not ready');
    assert.ok(!ui.extents.some((e) => e.n === 'check box on'));
    const box = await cssRect(ui, off, page);
    assert.ok(centerIn(box, ready), 'the box is on the Ready button');
    assert.ok(box.w >= 18 && Math.abs(box.w - box.h) < 1, `the box is ${box.w} by ${box.h} CSS pixels`);
    await shot(page, `ready-off-${name}`);
    // Click or tap toggles it on.
    if (options.hasTouch) await tapButton(page, 'Ready'); else await clickButton(page, 'Ready');
    await waitNet(page, (n) => n.players[0].ready === 1, 'ready');
    ui = await frameUi(page);
    assert.ok(ui.extents.some((e) => e.n === 'check box on') && !ui.extents.some((e) => e.n === 'check box off'), 'the box is checked');
    await shot(page, `ready-on-${name}`);
    if (!options.hasTouch) {
      // Enter toggles it back off, once the focus is on Ready.
      await page.keyboard.press('ArrowDown');
      await page.keyboard.press('ArrowUp');
      await page.waitForTimeout(150);
      assert.equal((await readUi(page)).selected, 'Ready');
      await page.keyboard.press('Enter');
      await waitNet(page, (n) => n.players[0].ready === 0, 'not ready again');
      assert.ok((await frameUi(page)).extents.some((e) => e.n === 'check box off'));
    }
    assert.deepEqual(problems, []);
    await context.close();
  });
}
