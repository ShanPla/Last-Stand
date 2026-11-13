// Rooms and the registry that holds them. This file knows nothing about sockets: a player only
// needs a `conn` object that can send, so the rules here are easy to reason about.
import { randomBytes, randomInt, timingSafeEqual } from 'node:crypto';

export const MAX_PLAYERS = 4;
export const HOST_SLOT = 0;

// No 0, O, 1, I or L, so a code read aloud or typed from a screen is hard to mix up.
const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXY23456789';
const CODE_LENGTH = 5;
// A rejoin token is 16 random bytes, written as 22 URL-safe characters. Nobody can guess one,
// so holding it proves the holder is the player who was given it.
const TOKEN_BYTES = 16;
const MAX_TOKEN_LENGTH = 64;

/**
 * A seat in a room. `conn` is the socket sitting in it, or null while the player is away: a
 * player of a started run whose socket closed keeps the seat, with its name and token, so the
 * same person can come back to it with `rejoin`.
 * @typedef {{id: number, name: string, ready: boolean, relay: boolean, conn: any, token: string, away: boolean}} Player
 */

/** A fresh secret for one player. */
export function newToken() {
  return randomBytes(TOKEN_BYTES).toString('base64url');
}

/**
 * Compares two tokens in a time that does not depend on where they first differ, so the
 * answer time cannot be used to guess a token one character at a time.
 */
export function sameToken(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length === 0 || a.length > MAX_TOKEN_LENGTH) return false;
  const left = Buffer.from(a);
  const right = Buffer.from(b);
  return left.length === right.length && timingSafeEqual(left, right);
}

export class Room {
  /**
   * @param {string} code
   * @param {number} now creation time in ms, used for expiry
   */
  constructor(code, now) {
    this.code = code;
    this.createdAt = now;
    this.started = false;
    /** The run settings the host last sent (map, difficulty, mode). The server never reads them:
     *  it only stores the object and hands it to everyone in the room. Null until the host sends. */
    this.settings = null;
    /** @type {(Player | null)[]} index is the slot, which is also the player id */
    this.players = new Array(MAX_PLAYERS).fill(null);
  }

  get playerCount() {
    return this.players.reduce((n, p) => n + (p ? 1 : 0), 0);
  }

  /** The lowest free slot, or -1 when the room is full. Gives freed slots back first. A seat
   *  kept for an away player is not free. */
  freeSlot() {
    return this.players.findIndex((p) => p === null);
  }

  /** Puts a player in the lowest free slot. Returns the player, or null if the room is full. */
  addPlayer(conn, name) {
    const slot = this.freeSlot();
    if (slot === -1) return null;
    const player = { id: slot, name, ready: false, relay: false, conn, token: newToken(), away: false };
    this.players[slot] = player;
    return player;
  }

  removePlayer(slot) {
    this.players[slot] = null;
  }

  /** Keeps the seat, name and token, but nobody is sitting in it until a rejoin. */
  markAway(slot) {
    const player = this.players[slot];
    if (!player) return;
    player.conn = null;
    player.away = true;
    player.relay = false;
  }

  /** Puts a socket back into a kept seat. */
  takeSeat(slot, conn) {
    const player = this.players[slot];
    player.conn = conn;
    player.away = false;
    player.relay = false;
  }

  /** The slot whose token this is, or -1. Every seat is checked, so the time is the same for any token. */
  slotOfToken(token) {
    let found = -1;
    for (const p of this.players) {
      if (p && sameToken(token, p.token) && found === -1) found = p.id;
    }
    return found;
  }

  /** The players with a socket, in slot order. Messages only go to these. */
  members() {
    return this.players.filter((p) => p && p.conn);
  }

  /** The `room` message for one member. `you` differs for each of them. */
  snapshotFor(slot) {
    const snapshot = {
      t: 'room',
      code: this.code,
      you: slot,
      host: HOST_SLOT,
      started: this.started,
      // Away players are listed too, so everyone can see who may come back. The flag is only
      // written when it is true, so a room with nobody away looks exactly as it always did.
      players: this.players.filter(Boolean).map((p) => (p.away ? { id: p.id, name: p.name, ready: p.ready, away: true } : { id: p.id, name: p.name, ready: p.ready })),
    };
    // Only written once the host has sent settings, so an older client sees the same message as before.
    if (this.settings !== null) snapshot.settings = this.settings;
    return snapshot;
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
