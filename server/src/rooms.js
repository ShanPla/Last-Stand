// Rooms and the registry that holds them. This file knows nothing about sockets: a player only
// needs a `conn` object that can send, so the rules here are easy to reason about.
import { randomInt } from 'node:crypto';

export const MAX_PLAYERS = 4;
export const HOST_SLOT = 0;

// No 0, O, 1, I or L, so a code read aloud or typed from a screen is hard to mix up.
const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXY23456789';
const CODE_LENGTH = 5;

/** @typedef {{id: number, name: string, ready: boolean, relay: boolean, conn: any}} Player */

export class Room {
  /**
   * @param {string} code
   * @param {number} now creation time in ms, used for expiry
   */
  constructor(code, now) {
    this.code = code;
    this.createdAt = now;
    this.started = false;
    /** @type {(Player | null)[]} index is the slot, which is also the player id */
    this.players = new Array(MAX_PLAYERS).fill(null);
  }

  get playerCount() {
    return this.players.reduce((n, p) => n + (p ? 1 : 0), 0);
  }

  /** The lowest free slot, or -1 when the room is full. Gives freed slots back first. */
  freeSlot() {
    return this.players.findIndex((p) => p === null);
  }

  /** Puts a player in the lowest free slot. Returns the player, or null if the room is full. */
  addPlayer(conn, name) {
    const slot = this.freeSlot();
    if (slot === -1) return null;
    const player = { id: slot, name, ready: false, relay: false, conn };
    this.players[slot] = player;
    return player;
  }

  removePlayer(slot) {
    this.players[slot] = null;
  }

  /** The connected players, in slot order. */
  members() {
    return this.players.filter(Boolean);
  }

  /** The `room` message for one member. `you` differs for each of them. */
  snapshotFor(slot) {
    return {
      t: 'room',
      code: this.code,
      you: slot,
      host: HOST_SLOT,
      started: this.started,
      players: this.members().map((p) => ({ id: p.id, name: p.name, ready: p.ready })),
    };
  }

  /** Sends every member their own copy of the lobby. */
  broadcastRoom() {
    for (const p of this.members()) p.conn.sendJson(this.snapshotFor(p.id));
  }
}

export class RoomRegistry {
  /** @param {{maxRooms: number, roomMaxAgeMs: number}} config */
  constructor(config) {
    this.config = config;
    /** @type {Map<string, Room>} */
    this.rooms = new Map();
  }

  get count() {
    return this.rooms.size;
  }

  isFull() {
    return this.rooms.size >= this.config.maxRooms;
  }

  /** Codes are typed by people, so accept any letter case and stray spaces. */
  find(code) {
    if (typeof code !== 'string') return undefined;
    return this.rooms.get(code.trim().toUpperCase());
  }

  /** Makes a room with a fresh random code. Check isFull() first. */
  create(now) {
    let code;
    do {
      code = '';
      for (let i = 0; i < CODE_LENGTH; i++) code += CODE_ALPHABET[randomInt(CODE_ALPHABET.length)];
    } while (this.rooms.has(code)); // 31^5 codes, so a clash is rare, but never reuse a live one
    const room = new Room(code, now);
    this.rooms.set(code, room);
    return room;
  }

  delete(room) {
    this.rooms.delete(room.code);
  }

  /** Rooms older than the limit. The caller closes them. */
  expired(now) {
    return [...this.rooms.values()].filter((r) => now - r.createdAt >= this.config.roomMaxAgeMs);
  }
}
