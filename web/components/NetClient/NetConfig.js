// Where the co-op relay server lives, in one place.
//
// The page is served from GitHub Pages and the relay runs on Render, so the address is
// different for local play and for the live site. Nothing else in the page shell should
// hard-code a server address: ask this file.
//
// Plain script, no modules: it adds one global, LastStandNetConfig, like the other components.
(function (global) {
  'use strict';

  // Replace this with the real address once the relay is deployed on Render.
  // While it still has this text, the page knows there is no server to reach and says so at once
  // instead of waiting a minute for a name that will never answer.
  var PLACEHOLDER = 'wss://REPLACE-WITH-RENDER-ADDRESS.onrender.com';
  var LOCAL = 'ws://localhost:8787';

  // Accepts ws://, wss://, http://, https:// or a bare host:port, and returns a ws or wss
  // address with no trailing slash. Returns '' when it makes no sense.
  function normalizeServer(text) {
    text = String(text || '').trim().replace(/\/+$/, '');
    if (!text) return '';
    if (/^https:\/\//i.test(text)) return 'wss://' + text.slice(8);
    if (/^http:\/\//i.test(text)) return 'ws://' + text.slice(7);
    if (/^wss?:\/\//i.test(text)) return text;
    if (/^[\w.-]+(:\d+)?$/.test(text)) return 'ws://' + text;
    return '';
  }

  // The health check is plain HTTP on the same host, so it comes from the same value.
  function healthUrlFor(wsUrl) {
    return wsUrl.replace(/^ws(s?):\/\//i, 'http$1://') + '/healthz';
  }

  // Works out the settings from the page address. Both arguments are only here so tests can
  // call it with made-up pages.
  //   ?server=ws://host:port   overrides the address (for tests and local servers)
  //   ?transport=relay         forces every pair to use the server instead of WebRTC (for tests)
  //   ?wakebudget=8000         how long, in milliseconds, to keep trying to wake the server (for tests)
  function resolve(hostname, search) {
    var params;
    try { params = new URLSearchParams(search || ''); } catch (error) { params = new URLSearchParams(''); }
    var override = normalizeServer(params.get('server'));
    var local = hostname === 'localhost' || hostname === '127.0.0.1';
    var wsUrl = override || (local ? LOCAL : PLACEHOLDER);
    var budget = Number(params.get('wakebudget'));
    return {
      wsUrl: wsUrl,
      healthUrl: healthUrlFor(wsUrl),
      forceRelay: params.get('transport') === 'relay',
      isPlaceholder: wsUrl === PLACEHOLDER,
      // A server on this machine is never asleep, and may not be running at all, so the game does
      // not wake it when the page loads. A named server (?server=) and the live one are woken.
      wakeOnLoad: !local || !!override,
      wakeBudgetMs: budget > 0 && budget <= 600000 ? Math.round(budget) : 0,
    };
  }

  function fromPage() {
    var loc = global.location || {};
    return resolve(loc.hostname, loc.search);
  }

  global.LastStandNetConfig = { resolve: resolve, fromPage: fromPage, normalizeServer: normalizeServer, PLACEHOLDER: PLACEHOLDER };
}(window));
