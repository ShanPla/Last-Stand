// Browser networking for online co-op: wakes the relay, runs the lobby, and carries game
// messages between players, over WebRTC when it works and through the relay server when it
// does not. It never reads game data: the game gives it bytes and gets bytes back.
//
// Plain script, no modules. Load NetConfig.js first. It adds one global, LastStandNet:
//   LastStandNet.create(options)   a new client (tests use this)
//   LastStandNet.shared()          the one client the game uses
//
// ---------------------------------------------------------------------------------------
// HOW THE GAME USES THIS
//
// The C++ side is src/components/Net/WebBackend.cpp. It calls the shared client through small
// EM_JS functions: wake(), create(name), join(code, name), setReady(on), start(), leave(), and
// every frame takeEventsJson() (to learn that something changed), then snapshotJson() and
// statsJson() (to copy the lobby and the numbers for the debug overlay). Browser events never
// run inside a C++ frame, so the game polls and needs no locking.
//
// During the run, C++ sends game messages with net.send(to, bytes, reliable), and sets
// net.onMessage once to a small queue (net.lsGameQueue). Each frame it takes the waiting
// messages out of that queue into a buffer it owns, so no message arrives mid-frame. The
// messages themselves (snapshots, inputs) are made in src/components/NetGame.
//
// How the states map to the C++ lobby interface:
//   phases  Offline | Waking | Connecting | InLobby | Starting | Error
//     Waking      the health check is running and the player has asked for nothing yet
//     Connecting  create or join was pressed: waiting for the server and for the room answer
//     InLobby     in a room that has not started
//     Starting    the host pressed Start. It stays so until leave(): C++ owns the run from here
//     Error       something failed. lastError() says what. leave() or wake() clears it
//   errors  ServerUnreachable | NotFound | Full | Started | BadName | HostLeft | Version | Other
//     server codes: not-found, full, started, bad-name, version have the same-named error.
//     host-left -> HostLeft. Server gone, socket lost, wake failed, connect timeout
//     -> ServerUnreachable (the event is also called server-lost when it happens in a room).
//     bad-request, not-host, not-in-room, server-full, too-big, rate and room expiry -> Other.
//     Inside a room only the fatal ones change the phase; rate and similar come as events.
// ---------------------------------------------------------------------------------------
(function (global) {
  'use strict';

  var Config = global.LastStandNetConfig;
  if (!Config) {
    if (global.console) console.warn('NetClient: NetConfig.js must load first, networking is off');
    return;
  }

  var PROTOCOL_VERSION = 1;
  var PHASES = ['Offline', 'Waking', 'Connecting', 'InLobby', 'Starting', 'Error'];
  var ERRORS = ['ServerUnreachable', 'NotFound', 'Full', 'Started', 'BadName', 'HostLeft', 'Version', 'Other'];
  var SERVER_ERRORS = { 'not-found': 'NotFound', 'full': 'Full', 'started': 'Started', 'bad-name': 'BadName', 'version': 'Version' };

  var BROADCAST = 255;
  // The relay refuses frames over 16 KB, and it counts the target byte. Every frame we make
  // has a target byte (relay path) or a kind byte (WebRTC path) plus a kind byte, so game
  // payloads are limited to what fits after two header bytes. Same limit on both paths,
  // so a message never works on one path and fails on the other.
  var MAX_FRAME = 16 * 1024;
  var MAX_PAYLOAD = MAX_FRAME - 2;
  var MAX_BUFFERED = 1024 * 1024;
  var MAX_CHANNEL_BUFFERED = 256 * 1024;

  // First byte after the target on every game frame, so pings can share the pipe with game data.
  var KIND_GAME = 0;
  var KIND_PING = 1;
  var KIND_PONG = 2;

  // Two public STUN servers from different companies, so one being down is not the end.
  // There is no TURN server on purpose: pairs that cannot connect use the relay instead.
  var ICE_SERVERS = [{ urls: 'stun:stun.l.google.com:19302' }, { urls: 'stun:stun.cloudflare.com:3478' }];

  var DEFAULT_OPTIONS = {
    wakeBudgetMs: 90000,   // Render's free tier can need about a minute to wake up
    wakeAttemptMs: 8000,   // one health request gives up after this
    wakeRetryMs: 1500,
    connectTimeoutMs: 10000,
    answerTimeoutMs: 10000,
    fallbackMs: 8000,      // a pair with no open WebRTC channel by then is marked as relay
    pingMs: 2000,
    disconnectGraceMs: 3000,
  };

  function now() { return (global.performance && performance.now) ? performance.now() : Date.now(); }

  function create(userOptions) {
    var opt = {};
    var key;
    for (key in DEFAULT_OPTIONS) opt[key] = DEFAULT_OPTIONS[key];
    for (key in (userOptions || {})) if (userOptions[key] !== undefined) opt[key] = userOptions[key];

    var cfg = opt.config || Config.fromPage();
    // ?wakebudget=ms on the page shortens the wake, so tests do not wait a minute and a half.
    if (!(userOptions && userOptions.wakeBudgetMs !== undefined) && cfg.wakeBudgetMs) opt.wakeBudgetMs = cfg.wakeBudgetMs;
    var canRtc = typeof global.RTCPeerConnection === 'function';
    var forceRelay = !!cfg.forceRelay || !canRtc;

    // ---- State ------------------------------------------------------------------------
    var s = {
      ws: null,
      busy: false,           // create or join is in progress
      attempt: 0,            // bumped by every create/join/leave so stale async work stops
      room: null,            // the last room message
      started: false,
      myId: -1,
      hostId: -1,
      error: null,           // { error, code } or null
      wakeState: 'idle',     // idle | waking | awake | unreachable
      wakeStart: 0,
      wakeMs: 0,
      wakeAttempts: 0,
      wakeAt: 0,
      wakePromise: null,
      phase: 'Offline',
      peers: new Map(),      // player id -> peer
      relayFlag: false,
      serverRtt: -1,
      pingN: 0,
      pingSent: new Map(),
      lastPongAt: 0,
      pingTimer: 0,
      rateTimer: 0,
      answerTimer: 0,
      dropped: 0,
    };
    var listeners = {};
    var eventLog = [];
    var client = {};
    var disposed = false;

    // ---- Events -----------------------------------------------------------------------
    function emit(name, data) {
      var entry = { e: name };
      if (data) for (var k in data) entry[k] = data[k];
      eventLog.push(entry);
      if (eventLog.length > 200) eventLog.shift();
      var list = listeners[name];
      if (!list) return;
      list.slice().forEach(function (fn) {
        try { fn(data || {}); } catch (error) { if (global.console) console.error('NetClient listener failed', error); }
      });
    }

    function on(name, fn) {
      if (typeof fn !== 'function') return client;
      (listeners[name] = listeners[name] || []).push(fn);
      return client;
    }

    function off(name, fn) {
      var list = listeners[name];
      if (list) listeners[name] = list.filter(function (f) { return f !== fn; });
      return client;
    }

    // ---- Phase and errors -------------------------------------------------------------
    function computePhase() {
      if (s.error) return 'Error';
      if (s.room && s.started) return 'Starting';
      if (s.room) return 'InLobby';
      if (s.busy) return 'Connecting';
      if (s.wakeState === 'waking') return 'Waking';
      return 'Offline';
    }

    function refresh() {
      var next = computePhase();
      if (next === s.phase) return;
      s.phase = next;
      emit('phase', { phase: next, phaseIndex: PHASES.indexOf(next) });
    }

    // Records a fatal problem: the lobby screen should show it. Only the first one of an
    // attempt counts, so a socket closing after a version error does not overwrite it.
    function fail(error, code) {
      if (s.error) return;
      s.busy = false;
      s.error = { error: error, code: code || '' };
      closeSession();
      refresh();
      emit('error', { error: error, errorIndex: ERRORS.indexOf(error), code: code || '' });
    }

    // ---- Wake -------------------------------------------------------------------------
    function sleep(ms) { return new Promise(function (resolve) { setTimeout(resolve, ms); }); }

    async function healthOnce(timeoutMs) {
      var controller = typeof AbortController === 'function' ? new AbortController() : null;
      var timer = controller ? setTimeout(function () { controller.abort(); }, timeoutMs) : 0;
      try {
        var response = await fetch(cfg.healthUrl, { cache: 'no-store', signal: controller ? controller.signal : undefined });
        return response.ok;
      } catch (error) {
        return false;
      } finally {
        clearTimeout(timer);
      }
    }

    // Asks the server to wake up, and keeps asking until it answers or the budget runs out.
    // Always resolves, with { state: 'awake' | 'unreachable', ms, attempts }.
    function wake(options) {
      try {
        if (s.wakePromise) return s.wakePromise;
        // A recent answer is still good: free instances sleep only after about 15 idle minutes.
        if (s.wakeState === 'awake' && now() - s.wakeAt < 5 * 60 * 1000) {
          return Promise.resolve({ state: 'awake', ms: s.wakeMs, attempts: s.wakeAttempts });
        }
        var budget = (options && options.budgetMs) || opt.wakeBudgetMs;
        s.wakeState = 'waking';
        s.wakeStart = now();
        s.wakeAttempts = 0;
        if (s.error && s.error.error === 'ServerUnreachable') s.error = null;
        refresh();
        emit('wake', { state: 'waking', ms: 0 });
        s.wakePromise = (async function () {
          var ok = false;
          if (!cfg.isPlaceholder) {
            while (!disposed) {
              var left = budget - (now() - s.wakeStart);
              if (left <= 0) break;
              s.wakeAttempts++;
              if (await healthOnce(Math.min(opt.wakeAttemptMs, left))) { ok = true; break; }
              if (budget - (now() - s.wakeStart) <= opt.wakeRetryMs) break;
              await sleep(opt.wakeRetryMs);
            }
          }
          s.wakeMs = Math.round(now() - s.wakeStart);
          s.wakeState = ok ? 'awake' : 'unreachable';
          s.wakeAt = now();
          s.wakePromise = null;
          var result = { state: s.wakeState, ms: s.wakeMs, attempts: s.wakeAttempts };
          emit('wake', result);
          // A failed background wake is shown as an error, but a create or join in progress
          // reports its own failure, so it is left alone here.
          if (!ok && !s.busy && !s.room) fail('ServerUnreachable', 'wake-failed');
          else refresh();
          return result;
        }());
        return s.wakePromise;
      } catch (error) {
        return Promise.resolve({ state: 'unreachable', ms: 0, attempts: 0 });
      }
    }

    // ---- Socket -----------------------------------------------------------------------
    function sendJson(obj) {
      try {
        if (s.ws && s.ws.readyState === 1) { s.ws.send(JSON.stringify(obj)); return true; }
      } catch (error) {}
      return false;
    }

    // Opens a socket and finishes the hello exchange. Rejects on any failure.
    function connectSocket() {
      return new Promise(function (resolve, reject) {
        var ws;
        try { ws = new WebSocket(cfg.wsUrl); } catch (error) { reject(new Error('bad-url')); return; }
        ws.binaryType = 'arraybuffer';
        s.ws = ws;
        var done = false;
        var timer = setTimeout(function () { finish(new Error('timeout')); }, opt.connectTimeoutMs);
        function finish(error) {
          if (done) return;
          done = true;
          clearTimeout(timer);
          if (error) reject(error); else resolve();
        }
        ws.onopen = function () {
          try { ws.send(JSON.stringify({ t: 'hello', v: PROTOCOL_VERSION })); } catch (error) { finish(error); }
        };
        ws.onmessage = function (event) {
          if (ws !== s.ws) return;
          try {
            if (typeof event.data === 'string') {
              var msg = JSON.parse(event.data);
              if (msg && msg.t === 'welcome' && !done) { finish(null); return; }
              onServerMessage(msg);
            } else {
              onBinaryFrame(new Uint8Array(event.data));
            }
          } catch (error) {
            if (global.console) console.error('NetClient message failed', error);
          }
        };
        ws.onerror = function () {};
        ws.onclose = function () {
          if (!done) finish(new Error('closed'));
          if (ws === s.ws) { s.ws = null; onSocketLost(); }
        };
      });
    }

    // Drops the socket and everything built on it. Safe to call at any time.
    function closeSession() {
      clearInterval(s.pingTimer);
      clearInterval(s.rateTimer);
      clearTimeout(s.answerTimer);
      s.pingTimer = s.rateTimer = s.answerTimer = 0;
      s.peers.forEach(closePeer);
      s.peers.clear();
      s.pingSent.clear();
      var ws = s.ws;
      s.ws = null; // cleared first, so the close handler knows this one was on purpose
      if (ws) {
        try { ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null; ws.close(); } catch (error) {}
      }
      s.room = null;
      s.started = false;
      s.myId = s.hostId = -1;
      s.relayFlag = false;
      s.serverRtt = -1;
    }

    // The socket died without us asking.
    function onSocketLost() {
      if (s.room) {
        // No automatic rejoin in this version: the room is gone for us.
        closeSession();
        s.busy = false;
        s.error = { error: 'ServerUnreachable', code: 'server-lost' };
        refresh();
        emit('server-lost', {});
        emit('error', { error: 'ServerUnreachable', errorIndex: 0, code: 'server-lost' });
      } else if (s.busy) {
        fail('ServerUnreachable', 'socket-closed');
      }
    }

    function startTimers() {
      clearInterval(s.pingTimer);
      clearInterval(s.rateTimer);
      s.lastPongAt = now();
      s.pingTimer = setInterval(pingTick, opt.pingMs);
      s.rateTimer = setInterval(rateTick, 1000);
    }

    function pingTick() {
      try {
        if (!s.ws) return;
        var n = ++s.pingN;
        s.pingSent.set(n, now());
        if (s.pingSent.size > 8) s.pingSent.delete(s.pingSent.keys().next().value);
        sendJson({ t: 'ping', n: n });
        // A socket that has stopped answering is treated as lost. A hidden tab is skipped,
        // because browsers slow its timers right down and that is not the server's fault.
        var hidden = global.document && document.hidden;
        if (s.room && !hidden && now() - s.lastPongAt > opt.pingMs * 5 + 2000) {
          onSocketLost();
          return;
        }
        s.peers.forEach(function (peer) { sendPeerPing(peer); });
      } catch (error) {}
    }

    function rateTick() {
      s.peers.forEach(function (peer) {
        peer.rate = {
          msgsIn: peer.msgsIn - peer.lastMsgsIn, msgsOut: peer.msgsOut - peer.lastMsgsOut,
          bytesIn: peer.bytesIn - peer.lastBytesIn, bytesOut: peer.bytesOut - peer.lastBytesOut,
        };
        peer.lastMsgsIn = peer.msgsIn; peer.lastMsgsOut = peer.msgsOut;
        peer.lastBytesIn = peer.bytesIn; peer.lastBytesOut = peer.bytesOut;
      });
    }

    // ---- Server messages --------------------------------------------------------------
    function onServerMessage(msg) {
      if (!msg || typeof msg.t !== 'string') return;
      switch (msg.t) {
        case 'room': onRoom(msg); break;
        case 'started': s.started = true; refresh(); emit('started', {}); break;
        case 'signal': onSignal(msg.from, msg.data); break;
        case 'pong': {
          var sent = s.pingSent.get(msg.n);
          s.lastPongAt = now();
          if (sent !== undefined) { s.pingSent.delete(msg.n); s.serverRtt = Math.round(now() - sent); }
          break;
        }
        case 'closed': onClosed(msg.reason); break;
        case 'error': onServerError(msg.code); break;
        default: break;
      }
    }

    function onServerError(code) {
      var mapped = SERVER_ERRORS[code] || 'Other';
      // Inside a room only a few things are fatal, the rest are reported and play on.
      if (s.room) {
        emit('error', { error: mapped, errorIndex: ERRORS.indexOf(mapped), code: code, fatal: false });
        return;
      }
      fail(mapped, code);
    }

    function onClosed(reason) {
      var error = reason === 'host-left' ? 'HostLeft' : 'Other';
      closeSession();
      s.busy = false;
      s.error = { error: error, code: reason || '' };
      refresh();
      emit('closed', { reason: reason || '' });
      emit('error', { error: error, errorIndex: ERRORS.indexOf(error), code: reason || '' });
    }

    function onRoom(room) {
      var previous = s.room ? s.room.players.map(function (p) { return p.id; }) : [];
      clearTimeout(s.answerTimer);
      s.room = room;
      s.myId = room.you;
      s.hostId = room.host;
      s.busy = false;
      if (room.started) s.started = true;
      var ids = room.players.map(function (p) { return p.id; });
      if (!previous.length) startTimers();

      // People who vanished: close their pair and tell the game.
      previous.forEach(function (id) {
        if (ids.indexOf(id) !== -1) return;
        var peer = s.peers.get(id);
        if (peer) { closePeer(peer); s.peers.delete(id); }
        emit('peer-dropped', { id: id });
      });
      // Pairs: the host talks to everyone, a client talks only to the host. Making them as soon
      // as the room shows them gives WebRTC the whole lobby time to connect, so the pairs are
      // usually open by the time Start is pressed.
      ids.forEach(function (id) {
        if (id === s.myId || s.peers.has(id)) return;
        if (s.myId !== s.hostId && id !== s.hostId) return;
        makePeer(id);
      });
      refresh();
      emit('room', roomSummary());
    }

    // ---- Peers ------------------------------------------------------------------------
    function makePeer(id) {
      var peer = {
        id: id, pc: null, dcU: null, dcR: null, transport: 'pending',
        fallbackTimer: 0, graceTimer: 0, iceQueue: [], pings: new Map(), pingN: 0, rtt: -1,
        msgsIn: 0, msgsOut: 0, bytesIn: 0, bytesOut: 0,
        lastMsgsIn: 0, lastMsgsOut: 0, lastBytesIn: 0, lastBytesOut: 0,
        rate: { msgsIn: 0, msgsOut: 0, bytesIn: 0, bytesOut: 0 },
      };
      s.peers.set(id, peer);
      if (forceRelay) {
        setTransport(peer, 'relay', 'forced');
        return;
      }
      peer.fallbackTimer = setTimeout(function () { fallBack(peer, 'timeout'); }, opt.fallbackMs);
      // The host always makes the offer, so two sides never offer at once.
      if (s.myId === s.hostId) {
        try { startOffer(peer); } catch (error) { fallBack(peer, 'offer-failed'); }
      }
    }

    function setTransport(peer, transport, why) {
      if (peer.transport === transport) return;
      peer.transport = transport;
      // The relay flag is only for the server's records, but keep it honest.
      var any = false;
      s.peers.forEach(function (p) { if (p.transport === 'relay') any = true; });
      if (any !== s.relayFlag && s.room) { s.relayFlag = any; sendJson({ t: 'relay', on: any }); }
      emit('transport', { id: peer.id, transport: transport, why: why || '' });
    }

    // Sends this pair through the server. The peer connection is kept, and if it opens late
    // the pair moves back to WebRTC.
    function fallBack(peer, why) {
      if (peer.transport === 'relay' || !s.peers.has(peer.id)) return;
      clearTimeout(peer.fallbackTimer);
      setTransport(peer, 'relay', why);
    }

    function closePeer(peer) {
      clearTimeout(peer.fallbackTimer);
      clearTimeout(peer.graceTimer);
      [peer.dcU, peer.dcR].forEach(function (dc) {
        if (!dc) return;
        dc.onopen = dc.onclose = dc.onmessage = dc.onerror = null;
        try { dc.close(); } catch (error) {}
      });
      if (peer.pc) {
        var pc = peer.pc;
        pc.onicecandidate = pc.onconnectionstatechange = pc.oniceconnectionstatechange = null;
        try { pc.close(); } catch (error) {}
      }
      peer.pc = peer.dcU = peer.dcR = null;
    }

    function makePc(peer) {
      var pc = new RTCPeerConnection({ iceServers: ICE_SERVERS });
      peer.pc = pc;
      peer.iceQueue = [];
      // Both channels are negotiated with fixed ids, so neither side waits for the other to
      // announce them. Channel 0 is for snapshots and inputs: a late packet is useless, so
      // it is unordered and never resent, like UDP. Channel 1 is for rare things that must
      // arrive (run settings, game over, level-up picks): ordered and reliable.
      peer.dcU = pc.createDataChannel('u', { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
      peer.dcR = pc.createDataChannel('r', { negotiated: true, id: 1, ordered: true });
      [peer.dcU, peer.dcR].forEach(function (dc) {
        dc.binaryType = 'arraybuffer';
        dc.onopen = function () { checkOpen(peer); };
        dc.onclose = function () { if (peer.transport === 'webrtc') fallBack(peer, 'channel-closed'); };
        dc.onmessage = function (event) {
          try { onFrame(peer, new Uint8Array(event.data), 'webrtc'); } catch (error) {}
        };
      });
      pc.onicecandidate = function (event) {
        if (event.candidate) signalTo(peer.id, { kind: 'ice', candidate: event.candidate.toJSON() });
      };
      pc.onconnectionstatechange = function () {
        var state = pc.connectionState;
        clearTimeout(peer.graceTimer);
        if (state === 'failed') fallBack(peer, 'failed');
        // Disconnected often heals by itself, so give it a moment before giving up.
        else if (state === 'disconnected') peer.graceTimer = setTimeout(function () { fallBack(peer, 'disconnected'); }, opt.disconnectGraceMs);
      };
      return pc;
    }

    function checkOpen(peer) {
      if (!peer.dcU || !peer.dcR) return;
      if (peer.dcU.readyState === 'open' && peer.dcR.readyState === 'open') {
        clearTimeout(peer.fallbackTimer);
        setTransport(peer, 'webrtc', 'open');
      }
    }

    function signalTo(id, data) { sendJson({ t: 'signal', to: id, data: data }); }

    async function startOffer(peer) {
      var pc = makePc(peer);
      var offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      signalTo(peer.id, { kind: 'offer', sdp: pc.localDescription.sdp });
    }

    function onSignal(from, data) {
      var peer = s.peers.get(from);
      if (!peer || forceRelay || !data || typeof data !== 'object') return;
      var guard = function () { fallBack(peer, 'signal-failed'); };
      try {
        if (data.kind === 'offer' && s.myId !== s.hostId && from === s.hostId) {
          answerOffer(peer, data.sdp).catch(guard);
        } else if (data.kind === 'answer' && s.myId === s.hostId && peer.pc) {
          peer.pc.setRemoteDescription({ type: 'answer', sdp: data.sdp }).then(function () { flushIce(peer); }).catch(guard);
        } else if (data.kind === 'ice' && peer.pc && data.candidate) {
          if (peer.pc.remoteDescription) peer.pc.addIceCandidate(data.candidate).catch(function () {});
          else peer.iceQueue.push(data.candidate);
        }
      } catch (error) { guard(); }
    }

    async function answerOffer(peer, sdp) {
      if (peer.pc) {
        // A second offer means the host started over: begin again with a clean connection.
        closePeerConnectionOnly(peer);
      }
      var pc = makePc(peer);
      await pc.setRemoteDescription({ type: 'offer', sdp: sdp });
      var answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      signalTo(peer.id, { kind: 'answer', sdp: pc.localDescription.sdp });
      flushIce(peer);
    }

    function closePeerConnectionOnly(peer) {
      var timer = peer.fallbackTimer;
      closePeer(peer);
      peer.fallbackTimer = timer; // the fallback clock keeps running across a restart
    }

    function flushIce(peer) {
      var queue = peer.iceQueue;
      peer.iceQueue = [];
      queue.forEach(function (candidate) {
        if (peer.pc) peer.pc.addIceCandidate(candidate).catch(function () {});
      });
    }

    // ---- Frames -----------------------------------------------------------------------
    // Frame on the WebRTC path:  [kind][payload]
    // Frame on the relay path:   [target or sender id][kind][payload]
    // Returns the path used, or '' when nothing was sent.
    function sendFrame(peer, kind, payload, reliable, viaRelay) {
      var n = payload.length;
      var channel = reliable ? peer.dcR : peer.dcU;
      if (!viaRelay && channel && channel.readyState === 'open' && peer.dcU && peer.dcU.readyState === 'open') {
        if (!reliable && channel.bufferedAmount > MAX_CHANNEL_BUFFERED) return '';
        // A new array each time: whether send() copies the bytes at once differs between
        // browsers, so a shared buffer could be overwritten before it is sent.
        var frame = new Uint8Array(n + 1);
        frame[0] = kind;
        frame.set(payload, 1);
        try { channel.send(frame); return 'webrtc'; } catch (error) {}
      }
      // The server only forwards frames once the room has started.
      if (!s.ws || s.ws.readyState !== 1 || !s.room || !s.room.started) return '';
      if (s.ws.bufferedAmount > MAX_BUFFERED) return '';
      var relayFrame = new Uint8Array(n + 2);
      relayFrame[0] = peer.id;
      relayFrame[1] = kind;
      relayFrame.set(payload, 2);
      try { s.ws.send(relayFrame); return 'relay'; } catch (error) { return ''; }
    }

    // A frame from the relay path: byte 0 is the sender.
    function onBinaryFrame(bytes) {
      if (bytes.length < 2) return;
      var peer = s.peers.get(bytes[0]);
      if (!peer) return;
      onFrame(peer, bytes.subarray(1), 'relay');
    }

    // Both paths end here, so a pair works when one side uses WebRTC and the other the relay.
    function onFrame(peer, bytes, via) {
      if (bytes.length < 1) return;
      var kind = bytes[0];
      var payload = bytes.subarray(1);
      if (kind === KIND_GAME) {
        peer.msgsIn++;
        peer.bytesIn += payload.length;
        if (typeof client.onMessage === 'function') {
          try { client.onMessage(peer.id, payload); } catch (error) { if (global.console) console.error('onMessage failed', error); }
        }
      } else if (kind === KIND_PING) {
        // Answer on the path the ping came in on, so the other side measures that path.
        var copy = payload.slice();
        sendFrame(peer, KIND_PONG, copy, false, via === 'relay');
      } else if (kind === KIND_PONG && payload.length >= 4) {
        var n = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3]) >>> 0;
        var sent = peer.pings.get(n);
        if (sent !== undefined) { peer.pings.delete(n); peer.rtt = Math.round(now() - sent); }
      }
    }

    function sendPeerPing(peer) {
      var n = ++peer.pingN;
      var payload = new Uint8Array([(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]);
      peer.pings.set(n, now());
      if (peer.pings.size > 8) peer.pings.delete(peer.pings.keys().next().value);
      sendFrame(peer, KIND_PING, payload, false, false);
    }

    // ---- Game API ---------------------------------------------------------------------
    // to: a player id, or 255 for everyone. The host sends to the players it names; a client
    // can only talk to the host, so anything a client sends goes there. Returns true when
    // at least one copy left. reliable (optional) picks the ordered, resent channel.
    function send(to, bytes, reliable) {
      try {
        if (bytes instanceof ArrayBuffer) bytes = new Uint8Array(bytes);
        else if (bytes && !(bytes instanceof Uint8Array) && bytes.buffer) bytes = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (!(bytes instanceof Uint8Array) || !s.room) return false;
        if (bytes.length > MAX_PAYLOAD) { s.dropped++; return false; }
        var targets;
        if (s.myId === s.hostId) {
          if (to === BROADCAST) targets = Array.from(s.peers.values());
          else targets = s.peers.has(to) ? [s.peers.get(to)] : [];
        } else {
          var hostPeer = s.peers.get(s.hostId);
          targets = hostPeer && (to === BROADCAST || to === s.hostId) ? [hostPeer] : [];
        }
        var sentAny = false;
        for (var i = 0; i < targets.length; i++) {
          var peer = targets[i];
          if (sendFrame(peer, KIND_GAME, bytes, !!reliable, false)) {
            peer.msgsOut++;
            peer.bytesOut += bytes.length;
            sentAny = true;
          } else {
            s.dropped++;
          }
        }
        return sentAny;
      } catch (error) {
        return false;
      }
    }

    // ---- Lobby actions ----------------------------------------------------------------
    // Both start the same way: wake the server if needed, open a socket, then send the request.
    // The answer arrives later as a room message (success) or an error.
    async function enter(message) {
      if (s.room || s.busy || disposed) return false;
      if (s.error) s.error = null;
      var attempt = ++s.attempt;
      s.busy = true;
      refresh();
      try {
        // After a failed wake only one quick try is made, so the button never hangs for a minute.
        var woke = await wake(s.wakeState === 'unreachable' ? { budgetMs: opt.wakeAttemptMs } : undefined);
        if (attempt !== s.attempt) return false;
        if (woke.state !== 'awake') { fail('ServerUnreachable', 'wake-failed'); return false; }
        await connectSocket();
        if (attempt !== s.attempt) return false;
        sendJson(message);
        s.answerTimer = setTimeout(function () {
          if (attempt === s.attempt && s.busy) fail('ServerUnreachable', 'no-answer');
        }, opt.answerTimeoutMs);
        return true;
      } catch (error) {
        if (attempt === s.attempt) fail('ServerUnreachable', String((error && error.message) || 'connect-failed'));
        return false;
      }
    }

    function createRoom(name) {
      try { return enter({ t: 'create', name: String(name == null ? '' : name) }); } catch (error) { return Promise.resolve(false); }
    }

    function joinRoom(code, name) {
      try { return enter({ t: 'join', code: String(code == null ? '' : code).trim().toUpperCase(), name: String(name == null ? '' : name) }); } catch (error) { return Promise.resolve(false); }
    }

    function setReady(on) { return s.room ? sendJson({ t: 'ready', ready: !!on }) : false; }

    function start() {
      if (!s.room || s.started || s.myId !== s.hostId) return false;
      return sendJson({ t: 'start' });
    }

    // Leaves the room, closes everything and clears any error. Works in every phase.
    function leave() {
      try {
        s.attempt++;
        s.busy = false;
        if (s.room) sendJson({ t: 'leave' });
        closeSession();
        s.error = null;
        refresh();
      } catch (error) {}
    }

    // ---- Reading state ----------------------------------------------------------------
    function roomSummary() {
      var room = s.room;
      return room ? { code: room.code, you: room.you, host: room.host, started: !!room.started } : {};
    }

    function players() {
      if (!s.room) return [];
      return s.room.players.map(function (p) {
        var peer = s.peers.get(p.id);
        return {
          id: p.id, name: p.name, ready: !!p.ready, isHost: p.id === s.hostId, isMe: p.id === s.myId,
          transport: p.id === s.myId ? 'local' : (peer ? peer.transport : 'none'),
          rttMs: peer ? peer.rtt : -1,
        };
      });
    }

    function stats() {
      var out = { serverRttMs: s.serverRtt, dropped: s.dropped, peers: {}, total: { msgsIn: 0, msgsOut: 0, bytesIn: 0, bytesOut: 0 } };
      s.peers.forEach(function (peer) {
        out.peers[peer.id] = {
          transport: peer.transport, rttMs: peer.rtt,
          msgsInPerSec: peer.rate.msgsIn, msgsOutPerSec: peer.rate.msgsOut,
          bytesInPerSec: peer.rate.bytesIn, bytesOutPerSec: peer.rate.bytesOut,
        };
        out.total.msgsIn += peer.rate.msgsIn; out.total.msgsOut += peer.rate.msgsOut;
        out.total.bytesIn += peer.rate.bytesIn; out.total.bytesOut += peer.rate.bytesOut;
      });
      return out;
    }

    function snapshot() {
      var e = s.error;
      return {
        phase: s.phase, phaseIndex: PHASES.indexOf(s.phase),
        wake: { state: s.wakeState, ms: s.wakeState === 'waking' ? Math.round(now() - s.wakeStart) : s.wakeMs, attempts: s.wakeAttempts },
        error: e ? e.error : '', errorIndex: e ? ERRORS.indexOf(e.error) : -1, errorCode: e ? e.code : '',
        code: s.room ? s.room.code : '', myId: s.myId, hostId: s.hostId, isHost: !!s.room && s.myId === s.hostId,
        started: s.started, players: players(), serverRttMs: s.serverRtt, forceRelay: forceRelay,
      };
    }

    // Wraps a reader so a bug here can never throw into the game.
    function safe(fn, fallback) {
      return function () { try { return fn.apply(null, arguments); } catch (error) { return fallback; } };
    }

    // Closing the tab tells the server we are gone right away, instead of after a timeout.
    function onPageHide() { if (s.room) leave(); }
    if (global.addEventListener) global.addEventListener('pagehide', onPageHide);

    function dispose() {
      disposed = true;
      leave();
      if (global.removeEventListener) global.removeEventListener('pagehide', onPageHide);
    }

    client.config = cfg;
    client.wake = wake;
    client.create = createRoom;
    client.join = joinRoom;
    client.setReady = safe(setReady, false);
    client.start = safe(start, false);
    client.leave = leave;
    client.send = send;
    client.onMessage = null; // set to function (from, bytes); bytes is a view, copy it to keep it
    client.on = on;
    client.off = off;
    client.dispose = dispose;
    client.phase = safe(function () { return s.phase; }, 'Offline');
    client.phaseIndex = safe(function () { return PHASES.indexOf(s.phase); }, 0);
    client.wakeState = safe(function () { return s.wakeState; }, 'idle');
    client.lastError = safe(function () { return s.error ? s.error.error : ''; }, '');
    client.errorIndex = safe(function () { return s.error ? ERRORS.indexOf(s.error.error) : -1; }, -1);
    client.isHost = safe(function () { return !!s.room && s.myId === s.hostId; }, false);
    client.myId = safe(function () { return s.myId; }, -1);
    client.roomCode = safe(function () { return s.room ? s.room.code : ''; }, '');
    client.players = safe(players, []);
    client.transportOf = safe(function (id) { var p = s.peers.get(id); return p ? p.transport : 'none'; }, 'none');
    client.stats = safe(stats, {});
    client.statsJson = safe(function () { return JSON.stringify(stats()); }, '{}');
    client.snapshot = safe(snapshot, {});
    client.snapshotJson = safe(function () { return JSON.stringify(snapshot()); }, '{}');
    client.takeEventsJson = safe(function () { var json = JSON.stringify(eventLog); eventLog = []; return json; }, '[]');
    client.PHASES = PHASES;
    client.ERRORS = ERRORS;
    client.MAX_PAYLOAD = MAX_PAYLOAD;
    return client;
  }

  var sharedClient = null;
  global.LastStandNet = {
    create: create,
    shared: function () { if (!sharedClient) sharedClient = create(); return sharedClient; },
    PHASES: PHASES,
    ERRORS: ERRORS,
    MAX_PAYLOAD: MAX_PAYLOAD,
  };
}(window));
