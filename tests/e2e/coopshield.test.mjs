// The co-op level-up shield, against the REAL relay server with two windows. A player whose cards
// are open is safe for a few seconds, and everyone now sees it: a bubble around that soldier,
// and on that player's own cards a countdown that turns into a warning when the shield is gone.
// Single-player must show neither. Screenshots go to tests/e2e/screenshots/coop-shield-*.png.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { waitScreen, debug, readUi, openGame, startGame, CMD as SOLO_CMD } from './harness.mjs';
import { useBrowser, readCoop, waitCoop, startCoopRun, overlaps, webUrl, sizes, COOP_CMD as CMD } from './coophelpers.mjs';

const rig = useBrowser();

const readWorld = (page) => page.evaluate(() => JSON.parse(Module.ccall('ls_query_world', 'string', [], [])));

async function waitWorld(page, check, what, timeout = 8000) {
  const start = Date.now();
  for (;;) {
    const world = await readWorld(page);
    if (check(world)) return world;
    if (Date.now() - start > timeout) throw new Error(`timed out waiting for ${what}: ${JSON.stringify(world)}`);
    await page.waitForTimeout(40);
  }
}

// Opens the guest's cards and waits until the countdown line shows.
async function openGuestCards(a, b) {
  await debug(a.page, CMD.levelUpSlot, 1);
  return waitCoop(b.page, (c) => c.players[1].cards === 1 && /^Shielded \d+s$/.test(c.shieldText), "the guest's cards with the countdown");
}

// A card is picked with Enter once the cards have listened for long enough.
async function pickCard(a, b) {
  await b.page.waitForTimeout(500);
  await b.page.keyboard.press('Enter');
  await waitCoop(a.page, (c) => c.players[1].cards === 0, "the host to take the guest's card");
  await waitCoop(b.page, (c) => c.players[1].cards === 0 && c.shieldText === '', "the guest's cards to close");
}

test('co-op shield: everyone sees the bubble while a player chooses, the cards count down, and both go away', async () => {
  const { relay, a, b } = await startCoopRun();
  // Nothing before anyone levels up.
  assert.equal((await readWorld(a.page)).shields, 0);
  assert.equal((await readCoop(b.page)).shieldText, '');

  await openGuestCards(a, b);
  // The host's screen draws the guest's bubble (and the host has no cards, so no countdown).
  const hostWorld = await waitWorld(a.page, (w) => w.shields === 1, "a bubble around the guest on the host's screen");
  assert.equal((await readCoop(a.page)).shieldText, '');
  const guestSees = await waitCoop(b.page, (c) => c.players[1].shield > 0 && c.players[1].shield <= 5, 'the guest to hold a shield');
  // The guest knows its own shield time from the snapshot, to the tenth of a second or better.
  const hostSees = await readCoop(a.page);
  assert.ok(Math.abs(hostSees.players[1].shield - guestSees.players[1].shield) < 0.6, `shield on both screens: ${hostSees.players[1].shield} and ${guestSees.players[1].shield}`);
  assert.equal(hostSees.players[0].shield, 0, 'the host holds no shield');
  assert.ok(hostWorld.shields <= 1);
  await a.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-bubble-host.png' });
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-cards-guest.png' });

  // The countdown goes down.
  const before = (await readCoop(b.page)).players[1].shield;
  await b.page.waitForTimeout(1200);
  const after = (await readCoop(b.page)).players[1].shield;
  assert.ok(after < before - 0.8 || after === 0, `the shield counts down: ${before} then ${after}`);

  // The last second: the line turns amber (still counting), and then the warning shows. The bubble
  // is still drawn on the host's screen until the shield runs out.
  await waitCoop(b.page, (c) => c.shieldText === 'Shielded 1s', 'the last second', 6000);
  await a.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-bubble-ending-host.png' });
  const gone = await waitCoop(b.page, (c) => c.shieldText === 'Shield gone! Pick fast', 'the shield-gone warning', 4000);
  assert.equal(gone.players[1].cards, 1, 'the cards stay open when the shield is gone');
  assert.equal(gone.players[1].shield, 0);
  await waitWorld(a.page, (w) => w.shields === 0, 'the bubble to disappear on the host');
  await b.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-gone-guest.png' });
  await a.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-gone-host.png' });

  // After the pick the guest is safe for a moment more, so they can get away, and the bubble
  // stays on everyone's screen until that runs out.
  await pickCard(a, b);
  const afterPick = await waitCoop(a.page, (c) => c.players[1].shield > 0, 'the after-pick shield');
  assert.ok(afterPick.players[1].shield <= 2.05, `the after-pick shield is short: ${afterPick.players[1].shield}`);
  await waitWorld(a.page, (w) => w.shields === 1, 'the bubble after the pick');
  await waitWorld(a.page, (w) => w.shields === 0, 'the bubble to end after the pick', 5000);
  assert.equal((await readCoop(a.page)).players[1].shield, 0);

  // The settings that quiet the effects must not hide the shield: reduced motion and no flash.
  await debug(a.page, CMD.setReducedMotion, 1);
  await debug(a.page, CMD.setFlash, 0);
  await openGuestCards(a, b);
  await waitWorld(a.page, (w) => w.shields === 1, 'the bubble with reduced motion and no flash');
  await a.page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-bubble-reduced-host.png' });
  await pickCard(a, b);

  // Two players choosing at once: the host sees the guest's bubble and the guest sees the host's.
  await debug(a.page, CMD.setReducedMotion, 0);
  await debug(a.page, CMD.setFlash, 100);
  await debug(a.page, CMD.levelUpSlot, 0);
  await debug(a.page, CMD.levelUpSlot, 1);
  await waitCoop(a.page, (c) => c.players[0].cards === 1 && c.players[1].cards === 1, 'both players choosing');
  await waitWorld(b.page, (w) => w.shields === 2, 'both bubbles on the guest screen');
  await waitWorld(a.page, (w) => w.shields === 2, 'both bubbles on the host screen');

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('co-op shield: the countdown fits every screen size without touching the cards', async () => {
  const { relay, a, b } = await startCoopRun();
  for (const [label, options] of sizes) {
    await b.page.setViewportSize(options.viewport);
    await b.page.waitForTimeout(400);
    await openGuestCards(a, b);
    await b.page.waitForTimeout(350);
    for (const [phase, screenshotName] of [['shield', 'up'], ['gone', 'gone']]) {
      if (phase === 'gone') await waitCoop(b.page, (c) => c.shieldText === 'Shield gone! Pick fast', 'the warning', 7000);
      else await b.page.waitForTimeout(100);
      const ui = await readUi(b.page);
      const line = ui.extents.find((e) => e.n === 'Shield line');
      assert.ok(line, `${label} ${phase}: the shield line is drawn`);
      const [left, top, right, bottom] = ui.safeRect;
      assert.ok(line.x >= left - 1 && line.x + line.w <= right + 1 && line.y >= top - 1 && line.y + line.h <= bottom + 1, `${label} ${phase}: inside the safe area ${JSON.stringify({ line, safe: ui.safeRect })}`);
      const cards = ui.buttons.filter((x) => x.n.startsWith('Card '));
      assert.ok(cards.length >= 2, `${label}: cards`);
      for (const card of cards) assert.ok(!overlaps(line, card), `${label} ${phase}: the shield line overlaps ${card.n}: ${JSON.stringify({ line, card })}`);
      await b.page.screenshot({ path: `tests/e2e/screenshots/coop-shield-${screenshotName}-${label}.png` });
      // Every card and button still sits inside the safe area, and none overlap. The 4:3 window
      // is left out: its reroll button already sticks out 8 pixels below the safe area in
      // single-player, which this change doesn't touch. The shield line is checked there too.
      if (label === '1024x768') continue;
      for (const x of ui.buttons) {
        assert.ok(x.x >= left - 1 && x.y >= top - 1 && x.x + x.w <= right + 1 && x.y + x.h <= bottom + 1, `${label} ${phase}: "${x.n}" leaves the safe area`);
      }
      for (let i = 0; i < ui.buttons.length; i++) {
        for (let j = i + 1; j < ui.buttons.length; j++) assert.ok(!overlaps(ui.buttons[i], ui.buttons[j]), `${label} ${phase}: "${ui.buttons[i].n}" overlaps "${ui.buttons[j].n}"`);
      }
    }
    await pickCard(a, b);
  }
  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});

test('co-op shield: a single-player level-up shows no countdown and no bubble', async () => {
  const { page, problems, context } = await openGame(rig.browser, `${webUrl()}?debug`, { viewport: { width: 1280, height: 720 } });
  await startGame(page);
  await debug(page, SOLO_CMD.levelUp, 0);
  await waitScreen(page, 'levelup');
  await page.waitForTimeout(500);
  assert.equal((await readCoop(page)).shieldText, '');
  assert.equal((await readWorld(page)).shields, 0);
  const ui = await readUi(page);
  assert.ok(!ui.extents.some((e) => e.n === 'Shield line'));
  await page.screenshot({ path: 'tests/e2e/screenshots/coop-shield-solo-levelup.png' });
  assert.deepEqual(problems, []);
  await context.close();
});
