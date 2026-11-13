# Co-op relay protocol

The relay server runs no game logic. It keeps rooms and a lobby, passes WebRTC signaling between
players, and, when a player's WebRTC connection fails, passes that player's game messages
through instead. This file is the contract between `server/` and the browser code in `web/`.

Version: 1. Every connection starts with a `hello` that names this version.

## Transport

- One WebSocket per player, to the server's address. Control messages are JSON text frames.
  Game messages on the fallback path are binary frames.
- The server checks the `Origin` header against `ALLOWED_ORIGINS` (a comma-separated list).
  `http://localhost:<any port>` and `http://127.0.0.1:<any port>` are always allowed unless
  `NODE_ENV` is `production`.
  A handshake with no `Origin` header (not a browser) is allowed unless `NODE_ENV` is `production`.
  A refused handshake gets HTTP `403`. When the server already has its maximum connections, it
  answers HTTP `503`. When one client address already has its maximum connections (see Limits),
  it answers HTTP `429`.
- A socket must send a good `hello` within 10 seconds of opening, or the server closes it with
  code 1008.
- `GET /healthz` (and `HEAD`) answers `200` with the body `ok`, for Render's health check and for
  waking a sleeping free instance. When the request's `Origin` is allowed, the answer carries
  `Access-Control-Allow-Origin` so the page can read it. Any other path answers `404`.
  The server listens on `process.env.PORT` (default 8787).
- The WebSocket upgrade is accepted on any path.

## Players and rooms

- A room holds up to 4 players in slots 0 to 3. The player who creates the room is the host and
  takes slot 0. Others take the lowest free slot. A slot number is a player's id in every
  message.
- A room code is 5 characters from `ABCDEFGHJKMNPQRSTUVWXY23456789` (no 0, O, 1, I, L or Z, which the pixel font draws like 2), and
  is case-insensitive when joining.
- Names are 1 to 12 characters of letters (A-Z, a-z), digits and spaces, trimmed. The server
  rejects other names, including accented letters, with `bad-name`. Two players may share a
  name. The browser offers `Player N` when the field is empty.
- When the host leaves or disconnects, the room closes: every other player gets
  `{"t":"closed","reason":"host-left"}` and the room is deleted. There is no host migration.
- Before the start, a non-host leaving (or disconnecting) frees its slot, and the others get a
  new `room` message.
- After the start, a non-host leaving or disconnecting keeps its slot as **away**: the name and
  the rejoin token stay, nobody else can take it, and the others get a `room` message where that
  player has `"away":true`. Kept slots are freed only when the room closes. See Rejoin below.
- An empty room is deleted at once. A room older than 3 hours is closed with reason `expired`.
- Once the host sends `start`, the room is marked started and nobody new can join. Only a player
  who was in it can come back, with `rejoin`.
- After a room closes, or after a player leaves, that socket stays open and is in no room, so it
  can create or join again. A player already in a room cannot `create` or `join` (`bad-request`);
  it must `leave` first.

## Client to server (JSON)

| Message | Meaning |
|---|---|
| `{"t":"hello","v":1}` | First message. A wrong or missing version gets `error` `version` and the socket closes. Any other message before a good `hello` gets `error` `bad-request`. |
| `{"t":"create","name":"Ana"}` | Make a room and become its host. The answer is a `room` message. |
| `{"t":"join","code":"K7QXM","name":"Ben"}` | Join a room. Everyone in it gets a `room` message. A joiner gets one too. |
| `{"t":"ready","ready":true}` | Mark yourself ready or not. `ready` must be a boolean. |
| `{"t":"settings","settings":{"map":0,"difficulty":1,"mode":0}}` | Host only (`not-host` otherwise, `not-in-room` outside a room). The host's choice of map, difficulty and mode, so guests can see it in the lobby. The server does not interpret it, but checks its shape: `settings` must be a flat JSON object (not an array or null) of at most 8 keys, each value a finite number, a string or a boolean (no `null`, arrays or objects), whose `JSON.stringify` is at most 200 characters, else `bad-request` and the old settings stay. It is stored on the room and sent to everyone in the next `room` message; the sender gets no other answer. Allowed before and after `start`: it only changes what the lobby shows, and a Play Again in the same room uses the host's current choice. The browser uses small integers, and a reader must ignore missing or out-of-range values. |
| `{"t":"start"}` | Host only (`not-host` otherwise). Marks the room started. A second `start` gets `started`. A room can start with any number of players, ready or not. |
| `{"t":"leave"}` | Leave the room. The socket stays open. The leaver gets no answer; the others get a `room` message. |
| `{"t":"signal","to":2,"data":{...}}` | WebRTC signaling (offer, answer or ICE candidate) for one player. `data` is passed on unread, but must be a JSON object nested at most 4 levels deep (`data` itself is level 1, arrays count as levels too) with only strings, finite numbers, booleans and `null` at the ends, and its `JSON.stringify` must be at most 8,192 characters, else `bad-request`. The browser sends `{"kind":"offer"\|"answer","sdp":"..."}` and `{"kind":"ice","candidate":{"candidate":"...","sdpMid":"0","sdpMLineIndex":0,"usernameFragment":"..."}}`, which use 2 levels and about 1 KB. A signal to an empty slot or to yourself is silently dropped. Works before and after `start`. |
| `{"t":"relay","on":true}` | This player's WebRTC failed. The server records the flag and sends no answer. It does not gate forwarding: binary frames are passed through for every player in a started room, because the other end of a failed pair has its own flag off and must still be able to send through the server. |
| `{"t":"ping","n":17}` | Answered with `pong` with the same `n`, for measuring round trips. `n` must be a finite number, else `bad-request`. |

| `{"t":"rejoin","code":"K7QXM","token":"..."}` | Take back a kept slot in a started room. Only from a socket in no room (`bad-request` otherwise, and when `code` or `token` is not a string). The code is case-insensitive. No such room: `not-found`. A room that has not started, a token that matches no slot, or the host's token: `bad-token`. On success everyone gets a `room` message, and the rejoiner then gets `started` (with its token) like at the start. |

`ready`, `settings`, `start`, `leave`, `signal` and `relay` need the sender to be in a room, or they get
`not-in-room`. Unknown message types, text that is not JSON, and JSON that is not an
object with a string `t` all get `bad-request`. Fields the server does not use are ignored.
If handling a message fails in a way these rules did not foresee, the sender gets `bad-request`
and the server carries on; no message can stop the server.

## Server to client (JSON)

| Message | Meaning |
|---|---|
| `{"t":"welcome","v":1}` | Answer to a good `hello`. |
| `{"t":"room","code":"K7QXM","you":1,"host":0,"started":false,"players":[{"id":0,"name":"Ana","ready":true},...]}` | The whole lobby, sent to everyone in the room after any change. A player who is away has `"away":true`; the field is left out for everyone else. Once the host has sent `settings`, the message also carries `"settings":{...}` exactly as the host sent it (for example `"settings":{"map":0,"difficulty":1,"mode":0}`), and a player who joins later gets it too. Before that the field is left out. |
| `{"t":"started","token":"..."}` | The host started the run. Sent to everyone, right after a final `room` message with `started` true. Each player's copy carries that player's own rejoin token, and no other message ever contains a token. Also sent to a player after a successful `rejoin`. |
| `{"t":"signal","from":0,"data":{...}}` | Signaling from another player. |
| `{"t":"closed","reason":"host-left"}` | The room is gone. Reasons: `host-left`, `expired`, and `replaced`: this socket's slot was taken back with its token on another connection (see Rejoin), and the room itself goes on. |
| `{"t":"error","code":"full"}` | A request failed. Codes below. |
| `{"t":"pong","n":17}` | Answer to `ping`. |

Error codes: `version`, `bad-request`, `bad-name`, `not-found`, `full`, `started`, `not-host`,
`not-in-room`, `server-full`, `too-big`, `rate`, `bad-token`, `ip-limit` (on `create`: this
client address already hosts its maximum of rooms; the browser shows it like any other error).

## Rejoin

- Every player gets a rejoin token when they create or join a room: 16 random bytes from the
  system's secure random source, written as 22 base64url characters. It is sent only once the run
  starts, in that player's own `started` message, because a slot is only ever kept in a started
  room. Tokens are compared in constant time, and they never appear in `room` messages or logs.
- A non-host in a started room whose socket closes, or who sends `leave`, stays in the slot as
  away. Signals and binary frames to an away slot are dropped silently.
- `rejoin` with the room's code and the slot's token puts the new socket into that slot, with the
  same id and name. The others see a `room` message with the player no longer away, and the host,
  seeing that, opens a fresh WebRTC connection to them (only the host makes offers).
- The right token on a slot that still has a socket also works: the new socket takes the slot,
  the old one gets `closed` with reason `replaced` and is left in no room, and the others see the
  player go away and come back in two `room` messages. This covers a page reloaded, or a phone
  that changed networks, before the server noticed the old socket was dead, which can take over
  a minute. A wrong token on such a slot gets `bad-token` and changes nothing.
- The host cannot rejoin: when the host leaves, the room closes, and every kept slot goes with it.

## Fallback game messages (binary)

- Sent only by a player in a started room.
- Client to server: byte 0 is the target player id, the rest is the payload.
  Target 255 means every other player in the room.
- Server to client: byte 0 is the sender's player id, the rest is the payload.
- The server never reads the payload. It drops frames for empty slots, for the sender's own id,
  for ids 4 to 254, and for senders who are not in a started room. A drop is silent: no error is
  sent, because frames arrive many times per second.
- A frame of just one byte (the target) is allowed and arrives as just the sender id.
- If a receiver's socket is more than 1 MB behind, frames for it are dropped instead of queued.
  The same rule covers JSON messages, see Limits.

## Limits

| Limit | Value | Result when crossed |
|---|---|---|
| Message size | 16 KB, text or binary | `error` `too-big`, and the message is dropped. A frame over 64 KB (4 times the limit) closes the socket with code 1009 |
| Message rate | 150 per second per connection, as a token bucket: 150 tokens refill each second, up to 300 stored, one token per message | `error` `rate` once per second, and extra messages are dropped. After 10 seconds without a pause of at least 1 second, the socket closes with code 1008 |
| Rooms | 200 at once | `error` `server-full` on `create` |
| Connections | 1,000 at once | the connection is refused with HTTP 503 |
| Connections per client address | 20 at once | the connection is refused with HTTP 429. Only when the address source is on, see below |
| Rooms per client address | 10 hosted at once | `error` `ip-limit` on `create`. Joining is not limited. Only when the address source is on |
| Hello | a good `hello` within 10 seconds of opening | the socket closes with code 1008 |
| Pings | every socket is pinged when it has not answered a ping for 60 seconds | the server sends a WebSocket ping frame (not JSON, browsers answer it by themselves), and closes the socket if there is no pong within 10 seconds. Messages the client sends do not count: only a pong shows that it still reads what the server sends |
| Slow receiver | more than 1 MB waiting to be sent to one socket | every message for it, JSON or binary, is dropped while it is that far behind. If it stays that far behind for 10 seconds, the socket is cut (no close frame, since it would only wait in the same queue) |

A message counts against the rate limit even if it is too big or invalid. The rate check comes
first, then the size check. A server that is stopping closes every socket with code 1001.

The limits can be lowered with environment variables, for tests: `MAX_MESSAGE_BYTES`,
`RATE_PER_SEC`, `RATE_BURST`, `RATE_CLOSE_AFTER_MS`, `MAX_ROOMS`, `MAX_CONNECTIONS`, `IDLE_MS`,
`PONG_WAIT_MS`, `ROOM_MAX_AGE_MS`, `HELLO_TIMEOUT_MS`, `MAX_BUFFERED_BYTES`, `SLOW_READER_MS`,
`MAX_CONNECTIONS_PER_IP` and `MAX_ROOMS_PER_IP`.

### Client address

The per-address limits are generous because many players can share one address (a school, a
cafe, a phone network). Where the address comes from is set by `CLIENT_IP_HEADER`:

- Not set: the TCP peer address while developing. With `NODE_ENV=production` the per-address
  limits are **off**, because a hosted server usually sits behind the host's proxy, so the TCP
  peer is the proxy and every player would seem to share one address. The server says which
  source it uses when it starts.
- `socket`: the TCP peer address, also in production. For a server that faces the internet
  itself.
- A header name, for example `cf-connecting-ip` or `true-client-ip`: that header, read as one
  address (anything after a comma is ignored). A request without the header is not limited per
  address (its TCP peer would be the proxy, shared by everybody); the server-wide limits still
  apply. This is only safe when the proxy in front of the server always overwrites
  the header, because a client can send any header it likes. Never use `x-forwarded-for` behind
  a proxy that only appends to it, as Render's does: the first entry would be whatever the
  client wrote.

Addresses are never written to the log.
