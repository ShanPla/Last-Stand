// Checks for WebGL 2 before the game binary downloads, and shows a clear
// message when it is missing.
window.LS_WebGlError = (function () {
  function check() {
    var probe = document.createElement('canvas');
    var hasWebGl2 = !!(window.WebGL2RenderingContext && probe.getContext('webgl2'));
    if (!hasWebGl2) document.getElementById('webgl-error').hidden = false;
    return hasWebGl2;
  }
  return { check: check };
}());
