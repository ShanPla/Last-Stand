// Service worker: lets the game start with no network after one visit.
//
// The build fills in the two placeholders below. The build id is a hash of every
// file listed, so a new build always gets a new cache name. Every file of one version
// comes from one cache, which is what keeps the page, script and wasm from ever
// disagreeing. The install also checks each downloaded file against the hash the build
// recorded for it: a host or CDN that is half way through an update can serve the new
// worker with some old files, and without the check that mix would be cached for good.
// This file is plain JavaScript and the game never depends on it.
const CACHE = 'laststand-__LS_BUILD_ID__';
// Pairs of [file, SHA-256 of that file as lowercase hex], written by the build.
const FILES = __LS_PRECACHE__;

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('');
}

self.addEventListener('install', (event) => {
  event.waitUntil((async () => {
    try {
      const cache = await caches.open(CACHE);
      // 'no-cache' asks the server whether our copy is current. A fresh copy the
      // page just downloaded is reused, so this costs no second download on the
      // first visit, and a stale browser-cached copy can never end up pinned here.
      await Promise.all(FILES.map(async ([file, hash]) => {
        const response = await fetch(new Request(file, { cache: 'no-cache' }));
        if (!response.ok) throw new Error(file + ': HTTP ' + response.status);
        // A copy is hashed, and the original goes into the cache untouched.
        if (await sha256Hex(await response.clone().arrayBuffer()) !== hash) {
          throw new Error(file + ': is not the file this build was made with');
        }
        await cache.put(file, response);
      }));
    } catch (error) {
      // Throwing fails the install, so the previous version keeps working.
      // The half-filled cache is removed first so it cannot be mistaken for a good one.
      await caches.delete(CACHE);
      throw error;
    }
    await self.skipWaiting();
  })());
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    // Delete every cache of ours except this version's, so old builds never pile up.
    for (const key of await caches.keys()) {
      if (key.startsWith('laststand-') && key !== CACHE) await caches.delete(key);
    }
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') return;
  if (new URL(request.url).origin !== location.origin) return;
  // Any failure falls through to the network, as if this worker did not exist.
  event.respondWith(answer(request).catch(() => fetch(request)));
});

async function answer(request) {
  const url = new URL(request.url);
  let key = request;
  if (request.mode === 'navigate') {
    // Only the game page itself, not other paths that happen to be in scope.
    if (!url.pathname.endsWith('/') && !url.pathname.endsWith('/index.html')) return fetch(request);
    key = new URL('index.html', self.registration.scope).href;
  }
  // ignoreSearch so that "?debug" still finds the page.
  const cached = await caches.match(key, { cacheName: CACHE, ignoreSearch: true });
  return cached || fetch(request);
}
