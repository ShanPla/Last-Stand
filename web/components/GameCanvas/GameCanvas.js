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

  window.Module = {
    canvas: canvas,
    // raylib logs through these. Warnings stay visible in the console.
    print: function () {},
    printErr: function (text) { console.warn(text); },
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
