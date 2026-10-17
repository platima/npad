/* Optional light/dark override. Without this script the page simply follows
   the system colour scheme (prefers-color-scheme); this only adds the toggle
   and remembers the visitor's choice in localStorage. No network, no cookies. */
(function () {
  var d = document.documentElement, key = 'npad-theme';
  try {
    var saved = localStorage.getItem(key);
    if (saved === 'light' || saved === 'dark') d.setAttribute('data-theme', saved);
  } catch (e) {}
  d.classList.add('js');
  function current() {
    return d.getAttribute('data-theme') ||
      (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  }
  document.addEventListener('DOMContentLoaded', function () {
    var b = document.getElementById('theme-toggle');
    if (!b) return;
    function label() {
      b.setAttribute('aria-label', 'Switch to ' + (current() === 'dark' ? 'light' : 'dark') + ' theme');
    }
    b.addEventListener('click', function () {
      var next = current() === 'dark' ? 'light' : 'dark';
      var system = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
      if (next === system) {
        /* back to the system setting: forget the override */
        d.removeAttribute('data-theme');
        try { localStorage.removeItem(key); } catch (e) {}
      } else {
        d.setAttribute('data-theme', next);
        try { localStorage.setItem(key, next); } catch (e) {}
      }
      var metas = document.querySelectorAll('meta[name="theme-color"]');
      for (var i = 0; i < metas.length; i++) metas[i].setAttribute('content', next === 'dark' ? '#17181b' : '#ffffff');
      label();
    });
    label();
  });
})();
