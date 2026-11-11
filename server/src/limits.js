// Per-connection message rate limit: a token bucket.
// The bucket holds up to `burst` tokens and refills `perSec` tokens each second. Every message
// costs one token. A burst of messages is fine, but a steady flood runs the bucket dry.

export class RateLimiter {
  /**
   * @param {{perSec: number, burst: number, closeAfterMs: number}} options
   */
  constructor({ perSec, burst, closeAfterMs }) {
    this.perSec = perSec;
    this.burst = burst;
    this.closeAfterMs = closeAfterMs;
    this.tokens = burst;
    this.lastRefill = null;
    this.overSince = 0; // when the current run of refused messages began
    this.lastRefusedAt = 0;
    this.lastWarnAt = -Infinity;
  }

  /**
   * Call once per incoming message.
   * @param {number} now milliseconds from any steady clock
   * @returns {'ok' | 'drop' | 'warn' | 'close'} what to do with the message:
   *   ok = handle it, drop = ignore it, warn = ignore it and send an `error rate`,
   *   close = ignore it, send `error rate` and close the socket.
   */
  take(now) {
    if (this.lastRefill === null) this.lastRefill = now;
    this.tokens = Math.min(this.burst, this.tokens + ((now - this.lastRefill) * this.perSec) / 1000);
    this.lastRefill = now;

    if (this.tokens >= 1) {
      this.tokens -= 1;
      return 'ok';
    }

    // A pause of one second or more means the client calmed down, so start counting again.
    if (now - this.lastRefusedAt > 1000) this.overSince = now;
    this.lastRefusedAt = now;
    if (now - this.overSince >= this.closeAfterMs) return 'close';

    // Tell the client about the problem at most once per second, so we do not add to the flood.
    if (now - this.lastWarnAt >= 1000) {
      this.lastWarnAt = now;
      return 'warn';
    }
    return 'drop';
  }
}
