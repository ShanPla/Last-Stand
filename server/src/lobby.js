// The lobby: every rule about creating, joining, starting and leaving rooms, plus passing
// signaling and fallback frames between players. It works on `conn` objects (see connection.js)
// with this shape: { room, slot, sendJson(obj), sendBinary(buf), bufferedAmount }.
import { HOST_SLOT, MAX_PLAYERS } from './rooms.js';

const NAME_PATTERN = /^[A-Za-z0-9 ]{1,12}$/;
export const BROADCAST = 255;
// If a receiver is this far behind, drop frames for it instead of piling up memory.
const MAX_BUFFERED_BYTES = 1024 * 1024;

export class Lobby {
  /**
   * @param {import('./rooms.js').RoomRegistry} registry
   * @param {(line: string) => void} log
   * @param {() => number} clock milliseconds, injectable for tests
   */
  constructor(registry, log, clock = Date.now) {
    this.registry = registry;
    this.log = log;
    this.clock = clock;
  }

  /**
   * Handles one parsed JSON message from a connection that has already said hello.
   * @param {any} conn
   * @param {Record<string, any>} msg
   */
  handle(conn, msg) {
    switch (msg.t) {
      case 'create': return this.#create(conn, msg);
      case 'join': return this.#join(conn, msg);
      case 'ready': return this.#ready(conn, msg);
      case 'start': return this.#start(conn);
      case 'leave': return this.#leave(conn);
      case 'signal': return this.#signal(conn, msg);
      case 'relay': return this.#relay(conn, msg);
      default: return conn.sendError('bad-request');
    }
  }

  /** Cleans up after a socket closes, whatever the reason. */
  disconnected(conn) {
    if (conn.room) this.#removeFromRoom(conn, 'host-left');
  }

  /**
   * Closes a room and tells its members why. Used for the host leaving and for expiry.
   * @param {import('./rooms.js').Room} room
   * @param {'host-left' | 'expired'} reason
   * @param {any} [except] a connection that should not be told (the host who just left)
   */
  closeRoom(room, reason, except) {
    for (const p of room.members()) {
      p.conn.room = null;
      p.conn.slot = -1;
      if (p.conn !== except) p.conn.sendJson({ t: 'closed', reason });
    }
    this.registry.delete(room);
    this.log(`room closed (${reason}), ${this.registry.count} open`);
  }

  /** Closes every room that has outlived the limit. Called by a timer. */
  expireRooms() {
    for (const room of this.registry.expired(this.clock())) this.closeRoom(room, 'expired');
  }

  // --- message handlers ---

  #create(conn, msg) {
    if (conn.room) return conn.sendError('bad-request'); // leave first
    const name = cleanName(msg.name);
    if (name === null) return conn.sendError('bad-name');
    if (this.registry.isFull()) return conn.sendError('server-full');

    const room = this.registry.create(this.clock());
    this.#seat(conn, room, name);
    this.log(`room created, ${this.registry.count} open`);
    room.broadcastRoom();
  }

  #join(conn, msg) {
    if (conn.room) return conn.sendError('bad-request');
    if (typeof msg.code !== 'string') return conn.sendError('bad-request');
    const name = cleanName(msg.name);
    if (name === null) return conn.sendError('bad-name');

    const room = this.registry.find(msg.code);
    if (!room) return conn.sendError('not-found');
    if (room.started) return conn.sendError('started');
    if (room.freeSlot() === -1) return conn.sendError('full');

    this.#seat(conn, room, name);
    room.broadcastRoom();
  }

  #ready(conn, msg) {
    if (!conn.room) return conn.sendError('not-in-room');
    if (typeof msg.ready !== 'boolean') return conn.sendError('bad-request');
    conn.room.players[conn.slot].ready = msg.ready;
    conn.room.broadcastRoom();
  }

  #start(conn) {
    const room = conn.room;
    if (!room) return conn.sendError('not-in-room');
    if (conn.slot !== HOST_SLOT) return conn.sendError('not-host');
    if (room.started) return conn.sendError('started');
    room.started = true;
    // The lobby update goes first, so a client that sees `started` already knows the final roster.
    room.broadcastRoom();
    for (const p of room.members()) p.conn.sendJson({ t: 'started' });
  }

  #leave(conn) {
    if (!conn.room) return conn.sendError('not-in-room');
    this.#removeFromRoom(conn, 'host-left');
  }

  #signal(conn, msg) {
    const room = conn.room;
    if (!room) return conn.sendError('not-in-room');
    if (!isPlayerId(msg.to) || typeof msg.data !== 'object' || msg.data === null) {
      return conn.sendError('bad-request');
    }
    const target = room.players[msg.to];
    // Someone who just left is not an error worth reporting: the signal is simply useless now.
    if (!target || target.conn === conn) return;
    target.conn.sendJson({ t: 'signal', from: conn.slot, data: msg.data });
  }

  #relay(conn, msg) {
    if (!conn.room) return conn.sendError('not-in-room');
    if (typeof msg.on !== 'boolean') return conn.sendError('bad-request');
    // Recorded for logs and future use. Forwarding does not depend on it, because the other end
    // of a failed pair must be able to send through the server too, even though its own flag is off.
    conn.room.players[conn.slot].relay = msg.on;
  }

  /**
   * Passes a fallback game frame on. Byte 0 is the target id, the rest is the payload, which the
   * server never reads. The receiver gets the sender's id in byte 0 instead.
   * @param {any} conn
   * @param {Buffer} frame
   */
  relayBinary(conn, frame) {
    const room = conn.room;
    if (!room || !room.started || frame.length < 1) return; // silently dropped, see PROTOCOL.md
    const target = frame[0];

    let recipients;
    if (target === BROADCAST) {
      recipients = room.members().filter((p) => p.conn !== conn);
    } else {
      const p = room.players[target]; // undefined for ids 4 to 254, so those are dropped too
      recipients = p && p.conn !== conn ? [p] : [];
    }
    if (recipients.length === 0) return;

    const out = Buffer.from(frame); // one copy, then only the first byte changes per recipient
    out[0] = conn.slot;
    for (const p of recipients) {
      if (p.conn.bufferedAmount > MAX_BUFFERED_BYTES) continue;
      p.conn.sendBinary(out);
    }
  }

  // --- helpers ---

  #seat(conn, room, name) {
    const player = room.addPlayer(conn, name);
    conn.room = room;
    conn.slot = player.id;
  }

  /** Takes a connection out of its room. If it was the host, the whole room closes. */
  #removeFromRoom(conn, hostReason) {
    const room = conn.room;
    if (conn.slot === HOST_SLOT) {
      this.closeRoom(room, hostReason, conn);
      return;
    }
    room.removePlayer(conn.slot);
    conn.room = null;
    conn.slot = -1;
    room.broadcastRoom();
  }
}

/** Trims and checks a name. Returns the clean name, or null when it is not allowed. */
function cleanName(name) {
  if (typeof name !== 'string') return null;
  const trimmed = name.trim();
  return NAME_PATTERN.test(trimmed) ? trimmed : null;
}

function isPlayerId(value) {
  return Number.isInteger(value) && value >= 0 && value < MAX_PLAYERS;
}
