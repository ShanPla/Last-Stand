// Registers the service worker that makes the game work offline. It waits until
// the game is running, so the first visit downloads nothing extra while the
// loading bar is moving. Every failure is swallowed: the game never needs this.
(function () {
  if (!('serviceWorker' in navigator)) return;

  function register() {
    try {
      navigator.serviceWorker.register('sw.js', { scope: './' }).catch(function () {});
    } catch (error) {}
  }

  function afterIdle() {
    if (window.requestIdleCallback) window.requestIdleCallback(register, { timeout: 5000 });
    else setTimeout(register, 2000);
  }

  var screen = document.getElementById('loading-screen');
  if (!screen || screen.classList.contains('is-done')) {
    afterIdle();
    return;
  }
  // The loading screen gets the "is-done" class when the game has started.
  var observer = new MutationObserver(function () {
    if (!screen.classList.contains('is-done')) return;
    observer.disconnect();
    afterIdle();
  });
  observer.observe(screen, { attributes: true, attributeFilter: ['class'] });
}());
