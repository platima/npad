/* Optional enhancement for /download/. The first tab and the visible panel are
   already right at first paint (theme.js sets data-os and data-tab on <html>
   and the stylesheet does the rest); this script only makes the tabs work:
   clicks, arrow keys, links such as #linux, ARIA state and the Copy buttons.
   Without JavaScript every operating system's section is shown, stacked.
   No network, no storage. */
(function () {
  var box = document.getElementById('downloads');
  if (!box) return;
  var root = document.documentElement;
  var tabs = Array.prototype.slice.call(box.querySelectorAll('[role="tab"]'));
  var panels = {};

  /* match the DOM order to the order the stylesheet shows (the detected
     system first), so arrow keys and screen readers agree with the eyes */
  tabs.sort(function (a, b) {
    return parseInt(getComputedStyle(a).order, 10) - parseInt(getComputedStyle(b).order, 10);
  });
  var list = box.querySelector('[role="tablist"]');
  tabs.forEach(function (tab) {
    list.appendChild(tab);
    var panel = document.getElementById(tab.getAttribute('aria-controls'));
    panels[panel.id] = panel;
    panel.setAttribute('role', 'tabpanel');
    panel.setAttribute('aria-labelledby', tab.id);
    panel.tabIndex = 0;
    tab.addEventListener('click', function () { select(tab, false); });
    tab.addEventListener('keydown', function (e) {
      var i = tabs.indexOf(tab), n = tabs.length, to = -1;
      if (e.key === 'ArrowRight' || e.key === 'ArrowDown') to = (i + 1) % n;
      else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') to = (i + n - 1) % n;
      else if (e.key === 'Home') to = 0;
      else if (e.key === 'End') to = n - 1;
      if (to < 0) return;
      e.preventDefault();
      select(tabs[to], true);
    });
  });

  function select(tab, focus) {
    root.setAttribute('data-tab', tab.getAttribute('aria-controls'));
    sync();
    if (focus) tab.focus();
  }

  function sync() {
    var current = root.getAttribute('data-tab');
    tabs.forEach(function (t) {
      var on = t.getAttribute('aria-controls') === current;
      t.setAttribute('aria-selected', on ? 'true' : 'false');
      t.tabIndex = on ? 0 : -1;
    });
  }

  /* A link such as #linux or #smartscreen opens the tab that holds its
     target. At load the stylesheet has already done that; after a link is
     followed on the page, switch tabs and scroll to the target again. */
  function fromHash() {
    var id = decodeURIComponent(location.hash.slice(1));
    var target = id && document.getElementById(id);
    var panel = target && target.closest && target.closest('.tabpanel');
    if (!panel || !panels[panel.id]) return;
    root.setAttribute('data-tab', panel.id);
    sync();
    target.scrollIntoView();
  }
  window.addEventListener('hashchange', fromHash);
  sync();

  /* Copy buttons (shown by the stylesheet only where the clipboard exists) */
  Array.prototype.forEach.call(box.querySelectorAll('.copyable'), function (block) {
    var button = block.querySelector('.copy');
    if (!button || !navigator.clipboard) return;
    button.addEventListener('click', function () {
      navigator.clipboard.writeText(block.querySelector('code').textContent).then(function () {
        button.textContent = 'Copied';
        setTimeout(function () { button.textContent = 'Copy'; }, 1500);
      });
    });
  });
})();
