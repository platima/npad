/* Optional enhancement for /download/. Without this script every operating
   system's section is shown, stacked, and the commands are plain text to
   select by hand. With it the sections become tabs (your own system first and
   selected) and the commands get a Copy button. No network, no storage. */
(function () {
  var box = document.getElementById('downloads');
  if (!box) return;

  function detect() {
    var ua = navigator.userAgent || '';
    var plat = (navigator.userAgentData && navigator.userAgentData.platform) || '';
    if (/android|iphone|ipad|ipod|cros/i.test(ua) || /android|chrome os/i.test(plat)) return '';
    var s = plat || ua;
    if (/win/i.test(s)) return 'windows';
    if (/mac/i.test(s)) return navigator.maxTouchPoints > 1 ? '' : 'macos'; /* iPad asks for the desktop site as a Mac */
    if (/linux|x11|bsd/i.test(s)) return 'linux';
    return '';
  }

  var panels = Array.prototype.slice.call(box.querySelectorAll('.tabpanel'));
  var order = ['windows', 'linux', 'macos'];
  var first = detect();
  if (first) order = [first].concat(order.filter(function (id) { return id !== first; }));
  panels.sort(function (a, b) { return order.indexOf(a.id) - order.indexOf(b.id); });

  var list = document.createElement('div');
  list.className = 'tablist';
  list.setAttribute('role', 'tablist');
  list.setAttribute('aria-label', 'Operating system');
  var tabs = panels.map(function (panel) {
    var tab = document.createElement('button');
    tab.type = 'button';
    tab.className = 'tab';
    tab.id = 'tab-' + panel.id;
    tab.setAttribute('role', 'tab');
    tab.setAttribute('aria-controls', panel.id);
    tab.textContent = panel.getAttribute('data-label');
    panel.setAttribute('role', 'tabpanel');
    panel.setAttribute('aria-labelledby', tab.id);
    panel.tabIndex = 0;
    list.appendChild(tab);
    box.appendChild(panel); /* re-append in the chosen order */
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
    return tab;
  });
  box.insertBefore(list, box.firstChild);
  box.classList.add('tabs-on');

  function select(tab, focus) {
    tabs.forEach(function (t, i) {
      var on = t === tab;
      t.setAttribute('aria-selected', on ? 'true' : 'false');
      t.tabIndex = on ? 0 : -1;
      panels[i].hidden = !on;
    });
    if (focus) tab.focus();
  }

  /* A link such as /download/#linux or /download/#smartscreen opens the tab
     that holds its target; those targets were hidden when the browser first
     tried to scroll to them, so scroll again. */
  function fromHash() {
    var id = decodeURIComponent(location.hash.slice(1));
    var target = id && document.getElementById(id);
    var panel = target && target.closest && target.closest('.tabpanel');
    if (!panel || !box.contains(panel)) return false;
    select(tabs[panels.indexOf(panel)], false);
    (target === panel ? box : target).scrollIntoView();
    return true;
  }
  if (!fromHash()) select(tabs[0], false);
  window.addEventListener('hashchange', fromHash);

  /* Copy buttons, only where the clipboard is available */
  if (navigator.clipboard) {
    Array.prototype.forEach.call(box.querySelectorAll('.copyable'), function (block) {
      var button = document.createElement('button');
      button.type = 'button';
      button.className = 'copy';
      button.textContent = 'Copy';
      button.addEventListener('click', function () {
        navigator.clipboard.writeText(block.querySelector('code').textContent).then(function () {
          button.textContent = 'Copied';
          setTimeout(function () { button.textContent = 'Copy'; }, 1500);
        });
      });
      var head = document.createElement('div');
      head.className = 'code-head';
      head.appendChild(document.createTextNode('Terminal'));
      head.appendChild(button);
      block.insertBefore(head, block.firstChild);
    });
  }
})();
