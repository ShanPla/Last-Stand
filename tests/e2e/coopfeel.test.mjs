// How a co-op guest's own soldier holds up when the host's page stalls, against the REAL relay
// server in server/. A host page that stalls (a busy PC, a second game window) used to lose that
// time, so the guest's input steps piled up at the host and were dropped, and each drop pulled the
// guest's soldier back on the guest's own screen.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { useBrowser, readCoop, waitCoop, startCoopRun, near } from './coophelpers.mjs';

useBrowser();

test('co-op run: the host catches up after its page stalls, so the guest\'s steps are not dropped', async () => {
  const { relay, a, b } = await startCoopRun();
  await b.page.waitForTimeout(800);
  // The host's page is blocked for 160 ms every 1.3 s, longer than the 5 steps a frame that
  // single-player catches up.
  await a.page.evaluate(() => {
    window.__stall = setInterval(() => {
      const start = performance.now();
      while (performance.now() - start < 160) { /* A busy page. */ }
    }, 1300);
  });
  const before = await readCoop(a.page);
  // The guest walks a square, with stops, for about 8 seconds.
  for (const key of ['d', 's', 'a', 'w', 'd', 's']) {
    await b.page.keyboard.down(key);
    await b.page.waitForTimeout(1000);
    await b.page.keyboard.up(key);
    await b.page.waitForTimeout(300);
  }
  await a.page.evaluate(() => clearInterval(window.__stall));
  await b.page.waitForTimeout(800);
  const [host, guest] = await Promise.all([readCoop(a.page), readCoop(b.page)]);
  console.log(`host dropped ${host.dropped - before.dropped} steps, ${host.late - before.late} late; guest corrections ${guest.corrections}, jumps ${guest.jumps}, transport ${guest.transport}`);
  // Seven stalls happened. Without catching up, each one cost about 4 steps.
  assert.ok(host.dropped - before.dropped <= 8, `steps dropped at the host: ${host.dropped - before.dropped}`);
  assert.equal(guest.jumps, 0, 'the guest\'s soldier never jumped');
  // Both agree where the guest's soldier stands once it stops.
  assert.ok(near(host.players[1], guest.players[1], 4), `guest soldier: ${JSON.stringify([host.players[1], guest.players[1]])}`);
  assert.deepEqual(a.problems, []);
  assert.deepEqual(b.problems, []);
  await a.context.close();
  await b.context.close();
  relay.stop();
});
