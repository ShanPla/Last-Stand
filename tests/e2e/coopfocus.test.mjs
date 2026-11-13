// A co-op host whose window loses focus (a click on another window, or a second monitor) must
// not pause the run, because the host's pause stops it for everyone. F3, which shows the network
// overlay, must not open the browser's find bar, which would take focus from the game. A hidden
// tab still pauses everyone (see coopplay.test.mjs), and a single-player run still pauses on any
// loss of focus (see ui.test.mjs).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { useBrowser, readCoop, startCoopRun } from './coophelpers.mjs';

useBrowser();

test('co-op: the host losing window focus does not pause the run, and F3 only toggles the overlay', async () => {
  const { relay, a, b } = await startCoopRun();
  const before = await readCoop(a.page);
  await a.page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await a.page.waitForTimeout(700);
  const host = await readCoop(a.page);
  assert.equal(host.screen, 'playing', 'the host stays on the play screen after a blur');
  assert.ok(host.tick > before.tick + 20, `the host's run keeps going: tick ${before.tick} then ${host.tick}`);
  const guest = await readCoop(b.page);
  assert.equal(guest.hostPaused, 0, 'the guest is not told the host paused');
  assert.equal(guest.hostAway, 0, 'the guest is not told the host is away');

  // The page turns off the browser's own use of F3, so no find bar opens.
  const prevented = await a.page.evaluate(() => {
    const event = new KeyboardEvent('keydown', { key: 'F3', keyCode: 114, bubbles: true, cancelable: true });
    window.dispatchEvent(event);
    return event.defaultPrevented;
  });
  assert.equal(prevented, true, 'F3 has its default action prevented');
  // A real F3 press still reaches the game and shows the overlay, and the run goes on.
  const overlayBefore = (await readCoop(a.page)).overlay;
  await a.page.keyboard.press('F3');
  await a.page.waitForTimeout(200);
  const after = await readCoop(a.page);
  assert.equal(after.overlay, overlayBefore ? 0 : 1, 'F3 toggles the network overlay');
  assert.equal(after.screen, 'playing');

  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});
