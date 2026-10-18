/* Optional light/dark override. Without this script the page simply follows
   the system colour scheme (prefers-color-scheme); this only adds the toggle
   and remembers the visitor's choice in localStorage. It also notes the
   visitor's operating system for the download buttons. No network, no cookies. */
(function () {
  var d = document.documentElement, key = 'npad-theme';
  try {
    var saved = localStorage.getItem(key);
    if (saved === 'light' || saved === 'dark') d.setAttribute('data-theme', saved);
  } catch (e) {}
  d.classList.add('js');
  /* Before first paint, record the visitor's system so CSS can show the right
     download button and tab straight away (data-os), and which download tab
     opens first (data-tab, from a #linux style link or the system). Only
     ever a label choice: nothing is sent anywhere. */
  try {
    var ua = navigator.userAgent || '';
    var plat = (navigator.userAgentData && navigator.userAgentData.platform) || '';
    var os = 'other';
    if (!/android|iphone|ipad|ipod|cros/i.test(ua) && !/android|chrome os/i.test(plat)) {
      var sys = plat || ua;
      if (/windows/i.test(sys)) os = 'windows';
      else if (/mac/i.test(sys)) os = navigator.maxTouchPoints > 1 ? 'other' : 'macos'; /* an iPad asking for the desktop site */
      else if (/linux|x11|bsd/i.test(sys)) os = 'linux';
    }
    d.setAttribute('data-os', os);
    var tabs = { windows: 'windows', smartscreen: 'windows', linux: 'linux', macos: 'macos' };
    d.setAttribute('data-tab', tabs[decodeURIComponent(location.hash.slice(1))] || (os === 'linux' || os === 'macos' ? os : 'windows'));
    if (navigator.clipboard) d.classList.add('has-clip');
  } catch (e) { d.setAttribute('data-os', 'other'); d.setAttribute('data-tab', 'windows'); }
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
      for (var i = 0; i < metas.length; i++) {
        if (!metas[i].hasAttribute('data-orig')) metas[i].setAttribute('data-orig', metas[i].getAttribute('content'));
        /* an override forces one colour; following the system restores each media-specific tag */
        metas[i].setAttribute('content', next === system && !d.hasAttribute('data-theme')
          ? metas[i].getAttribute('data-orig') : (next === 'dark' ? '#17181b' : '#ffffff'));
      }
      label();
    });
    label();
  });
})();
