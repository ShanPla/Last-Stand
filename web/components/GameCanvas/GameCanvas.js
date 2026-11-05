// Page-level setup for the game canvas: the Emscripten Module object, and
// blocking the browser gestures that would interrupt play.
(function () {
  var canvas = document.getElementById('canvas');

  // Stops scrolling, pinch zoom, double-tap zoom and the long-press menu.
  function block(event) { event.preventDefault(); }
  document.addEventListener('contextmenu', block);
  document.addEventListener('gesturestart', block);
  document.addEventListener('dblclick', block);
  document.addEventListener('touchmove', block, { passive: false });
  window.addEventListener('wheel', function (event) {
    if (event.ctrlKey) event.preventDefault();
  }, { passive: false });
  window.addEventListener('keydown', function (event) {
    var keys = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Tab'];
    if (keys.indexOf(event.key) !== -1) event.preventDefault();
  });

  var webGlOk = window.LS_WebGlError.check();
  if (!webGlOk) window.LS_LoadingScreen.hide();

  var crashMessage = 'The game stopped unexpectedly. Please reload the page.';

  // The game script is added to the page by the build, so it has no onerror of its own.
  // A failed load of a script does not bubble, but it can be caught on the way down.
  window.addEventListener('error', function (event) {
    var target = event.target;
    if (target && target.tagName === 'SCRIPT' && /(^|\/)index\.js(\?|$)/.test(target.src)) {
      window.LS_LoadingScreen.fail('The game could not be loaded. Please check your connection and reload the page.');
    } else if (event.error instanceof WebAssembly.RuntimeError) {
      // A crash inside the game, such as a trap. Emscripten does not call onAbort for
      // these, and the frame loop just stops.
      window.LS_LoadingScreen.fail(crashMessage);
    }
  }, true);

  // A lost WebGL context cannot be brought back: raylib would have to upload every
  // texture and shader again. So the player is told, rather than left with a blank canvas.
  canvas.addEventListener('webglcontextlost', function () {
    window.LS_LoadingScreen.fail('The graphics stopped working, which can happen when a device runs low on memory. Please reload the page.');
  });

  window.Module = {
    canvas: canvas,
    // raylib's own messages go to stdout, so only its warnings and errors are passed
    // on. Anything written to stderr is already a warning. Both stay in the console.
    print: function (text) { if (/^(WARNING|ERROR|FATAL)\b/.test(text)) console.warn(text); },
    printErr: function (text) { console.warn(text); },
    // Called by Emscripten when the program crashes, for example when memory cannot grow.
    onAbort: function () { window.LS_LoadingScreen.fail(crashMessage); },
    instantiateWasm: function (imports, onReady) {
      if (webGlOk) window.LS_LoadingScreen.loadWasm('index.wasm', imports, onReady);
      return {};
    },
    onRuntimeInitialized: function () {
      window.LS_LoadingScreen.hide();
      canvas.focus();
    }
  };
}());
