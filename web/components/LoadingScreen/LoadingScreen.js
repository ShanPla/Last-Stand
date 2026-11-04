// Shows real download progress for the game binary. The byte count compares
// decoded bytes with the true .wasm size, which the build writes into the page,
// so it stays correct when the host serves the file compressed.
window.LS_LoadingScreen = (function () {
  var screen = document.getElementById('loading-screen');
  var bar = document.getElementById('loading-bar');
  var label = document.getElementById('loading-label');

  function setProgress(fraction) {
    var percent = Math.min(100, Math.round(fraction * 100));
    bar.style.width = percent + '%';
    label.textContent = 'Loading ' + percent + '%';
  }

  async function loadWasm(url, imports, onReady) {
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
      var bytes = new Uint8Array(received);
      var offset = 0;
      chunks.forEach(function (chunk) {
        bytes.set(chunk, offset);
        offset += chunk.length;
      });
      label.textContent = 'Starting';
      var result = await WebAssembly.instantiate(bytes, imports);
      onReady(result.instance, result.module);
    } catch (error) {
      console.error(error);
      label.textContent = 'Download failed. Please reload the page.';
    }
  }

  function hide() {
    screen.classList.add('is-done');
    setTimeout(function () { screen.hidden = true; }, 250);
  }

  return { loadWasm: loadWasm, hide: hide };
}());
