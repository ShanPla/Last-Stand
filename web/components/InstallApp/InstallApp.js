// A small "Install app" button for browsers that offer to install the game.
// It shows on the title screen only, never during play, and never takes
// keyboard focus away from the game, so Enter still starts a run.
(function () {
  var box = document.getElementById('install-app');
  var installButton = document.getElementById('install-app-button');
  var dismissButton = document.getElementById('install-app-dismiss');
  var canvas = document.getElementById('canvas');
  if (!box || !installButton || !dismissButton) return;

  var DISMISS_KEY = 'laststand.installDismissed';
  var deferredPrompt = null;
  var installed = isInstalled();
  var dismissed = readDismissed();
  var timer = 0;

  function isInstalled() {
    try {
      if (navigator.standalone) return true; // iPhone home screen app
      return ['standalone', 'fullscreen', 'minimal-ui', 'window-controls-overlay'].some(function (mode) {
        return window.matchMedia('(display-mode: ' + mode + ')').matches;
      });
    } catch (error) {
      return false;
    }
  }

  // Storage can be blocked (private mode), so the choice is also kept in memory.
  function readDismissed() {
    try { return window.localStorage.getItem(DISMISS_KEY) === '1'; } catch (error) { return false; }
  }
  function saveDismissed() {
    try { window.localStorage.setItem(DISMISS_KEY, '1'); } catch (error) {}
  }

  // The page does not know which screen the game is showing, so it asks.
  // This only runs while an install offer exists, twice a second.
  function onTitleScreen() {
    try {
      return JSON.parse(window.Module.ccall('ls_query_ui', 'string', [], [])).screen === 'title';
    } catch (error) {
      return false;
    }
  }

  function refresh() {
    var show =!!deferredPrompt && !installed && !dismissed && !document.hidden && onTitleScreen();
    box.hidden = !show;
    if (!deferredPrompt || installed || dismissed) {
      clearInterval(timer);
      timer = 0;
    }
  }

  function giveFocusBack() {
    if (canvas) canvas.focus();
  }

  // Clicks and taps on the button must not reach the game, which listens on the
  // whole window and would press a title button under the finger. Pressing the
  // mouse is also cancelled so the button never becomes the focused element.
  function shield(event) {
    if (!box.contains(event.target)) return;
    event.stopImmediatePropagation();
    if (event.type === 'mousedown') event.preventDefault();
  }
  ['mousedown', 'mouseup', 'pointerdown', 'pointerup', 'touchstart', 'touchend'].forEach(function (type) {
    window.addEventListener(type, shield, { capture: true, passive: false });
  });

  window.addEventListener('beforeinstallprompt', function (event) {
    // Stops the browser's own banner, so the offer stays in our hands.
    event.preventDefault();
    deferredPrompt = event;
    if (!timer) timer = setInterval(refresh, 500);
    refresh();
  });

  window.addEventListener('appinstalled', function () {
    installed = true;
    deferredPrompt = null;
    refresh();
  });

  installButton.addEventListener('click', function () {
    var offer = deferredPrompt;
    deferredPrompt = null; // an offer can be used only once
    refresh();
    giveFocusBack();
    if (!offer) return;
    try {
      offer.prompt();
      Promise.resolve(offer.userChoice).catch(function () {});
    } catch (error) {}
  });

  dismissButton.addEventListener('click', function () {
    dismissed = true;
    saveDismissed();
    refresh();
    giveFocusBack();
  });
}());
