// Shows real download progress for the game binary. The byte count compares
// decoded bytes with the true .wasm size, which the build writes into the page,
// so it stays correct when the host serves the file compressed.
// It is also the one place that shows a fatal message, so a player is never left
// looking at a frozen page with no explanation.
window.LS_LoadingScreen = (function () {
  var screen = document.getElementById('loading-screen');
  var bar = document.getElementById('loading-bar');
  var label = document.getElementById('loading-label');
  var hideTimer = 0;

  function setProgress(fraction) {
    var percent = Math.min(100, Math.round(fraction * 100));
    bar.style.width = percent + '%';
    label.textContent = 'Loading ' + percent + '%';
  }

  // Covers the page with a message. This also works after the screen has faded out,
  // which a crash in the middle of play needs. The first message stays: later ones are
  // usually a result of the first, such as a lost graphics context after an abort.
  function fail(message) {
    if (screen.classList.contains('is-error')) return;
    clearTimeout(hideTimer);
    screen.hidden = false;
    screen.classList.remove('is-done');
    screen.classList.add('is-error');
    screen.setAttribute('role', 'alert');
    label.textContent = message;
  }

  async function loadWasm(url, imports, onReady) {
    var bytes;
    try {
      var response = await fetch(url);
      if (!response.ok) throw new Error('HTTP ' + response.status);
      var reader = response.body.getReader();
      var total = window.LS_WASM_BYTES;
      var chunks = [];
      var received = 0;
      for (;;) {
        var part = await reader.read();
        if (part.done) break;
        chunks.push(part.value);
        received += part.value.length;
        setProgress(received / total);
      }
      bytes = new Uint8Array(received);
      var offset = 0;
      chunks.forEach(function (chunk) {
        bytes.set(chunk, offset);
        offset += chunk.length;
      });
    } catch (error) {
      console.error(error);
      fail('Download failed. Please check your connection and reload the page.');
      return;
    }
    // The file arrived whole. A failure from here on is the browser refusing to compile
    // or link it, which a reload may fix but a download message would misreport.
    label.textContent = 'Starting';
    var result;
    try {
      result = await WebAssembly.instantiate(bytes, imports);
    } catch (error) {
      console.error(error);
      fail('The game could not start. Please reload the page, or try a newer browser.');
      return;
    }
    onReady(result.instance, result.module);
  }

  function hide() {
    if (screen.classList.contains('is-error')) return;
    screen.classList.add('is-done');
    hideTimer = setTimeout(function () { screen.hidden = true; }, 250);
  }

  return { loadWasm: loadWasm, hide: hide, fail: fail };
}());
