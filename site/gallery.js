/* Screenshot gallery (thumbnail chooser) and lightbox. Progressive enhancement:
   without this script the screenshots are simply listed one after another and
   each links to its full-size image. No network requests beyond the images. */
(function () {
  'use strict';
  var canDialog = typeof HTMLDialogElement === 'function';
  var dlg, img, cap, prev, next, list = [], pos = 0;

  function item(a) {
    var im = a.querySelector('img'), fig = a.closest('figure'), fc = fig && fig.querySelector('figcaption');
    return { src: a.getAttribute('href'), alt: im ? im.alt : '', cap: fc ? fc.textContent : '' };
  }

  function build() {
    if (dlg) return;
    dlg = document.createElement('dialog');
    dlg.className = 'lightbox';
    dlg.setAttribute('aria-label', 'Screenshot viewer');
    dlg.innerHTML =
      '<button class="lb-btn lb-close" type="button" aria-label="Close">&times;</button>' +
      '<button class="lb-btn lb-prev" type="button" aria-label="Previous screenshot">&lsaquo;</button>' +
      '<div class="lb-body"><img alt=""><p></p></div>' +
      '<button class="lb-btn lb-next" type="button" aria-label="Next screenshot">&rsaquo;</button>';
    document.body.appendChild(dlg);
    img = dlg.querySelector('img');
    cap = dlg.querySelector('p');
    cap.setAttribute('aria-live', 'polite');
    prev = dlg.querySelector('.lb-prev');
    next = dlg.querySelector('.lb-next');
    dlg.querySelector('.lb-close').addEventListener('click', function () { dlg.close(); });
    prev.addEventListener('click', function () { go(-1); });
    next.addEventListener('click', function () { go(1); });
    dlg.addEventListener('click', function (e) {
      if (e.target === dlg || e.target.classList.contains('lb-body')) dlg.close();
    });
    dlg.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowLeft') { go(-1); e.preventDefault(); }
      if (e.key === 'ArrowRight') { go(1); e.preventDefault(); }
    });
  }

  function render() {
    var it = list[pos];
    img.src = it.src;
    img.alt = it.alt;
    cap.textContent = it.cap;
    prev.hidden = next.hidden = list.length < 2;
  }

  function go(d) {
    if (list.length < 2) return;
    pos = (pos + d + list.length) % list.length;
    render();
  }

  function open(a) {
    build();
    var group = a.getAttribute('data-group');
    var links = group ? document.querySelectorAll('a.zoom[data-group="' + group + '"]') : [a];
    list = [];
    pos = 0;
    for (var i = 0; i < links.length; i++) {
      if (links[i] === a) pos = list.length;
      list.push(item(links[i]));
    }
    render();
    dlg.showModal();
  }

  document.addEventListener('click', function (e) {
    var a = e.target.closest ? e.target.closest('a.zoom') : null;
    if (!a || !canDialog || e.ctrlKey || e.metaKey || e.shiftKey || e.button) return;
    e.preventDefault();
    open(a);
  });

  /* Thumbnail chooser */
  var galleries = document.querySelectorAll('[data-gallery]');
  Array.prototype.forEach.call(galleries, function (g) {
    var shots = g.querySelectorAll('.shot');
    if (shots.length < 2) return;
    var cur = 0, thumbs = [];
    g.classList.add('is-js');

    var stage = document.createElement('div');
    stage.className = 'stage';
    shots[0].parentNode.insertBefore(stage, shots[0]);
    Array.prototype.forEach.call(shots, function (s) { stage.appendChild(s); });

    function mk(cls, label, text) {
      var b = document.createElement('button');
      b.type = 'button'; b.className = cls; b.setAttribute('aria-label', label); b.innerHTML = text;
      return b;
    }
    var bp = mk('stage-btn prev', 'Previous screenshot', '&lsaquo;');
    var bn = mk('stage-btn next', 'Next screenshot', '&rsaquo;');
    stage.appendChild(bp); stage.appendChild(bn);

    var bar = document.createElement('div');
    bar.className = 'thumbs';
    bar.setAttribute('role', 'group');
    bar.setAttribute('aria-label', 'Choose a screenshot');
    Array.prototype.forEach.call(shots, function (s, i) {
      var b = document.createElement('button');
      b.type = 'button'; b.className = 'thumb';
      b.innerHTML = '<img alt="" width="240" height="150" loading="lazy"><span></span>';
      b.querySelector('img').src = s.getAttribute('data-thumb');
      b.querySelector('span').textContent = s.getAttribute('data-label');
      b.addEventListener('click', function () { select(i); });
      b.addEventListener('keydown', function (e) {
        if (e.key === 'ArrowRight') { select((i + 1) % shots.length, true); e.preventDefault(); }
        if (e.key === 'ArrowLeft') { select((i + shots.length - 1) % shots.length, true); e.preventDefault(); }
      });
      bar.appendChild(b); thumbs.push(b);
    });
    g.appendChild(bar);

    function select(i, focus) {
      cur = i;
      Array.prototype.forEach.call(shots, function (s, k) { s.classList.toggle('active', k === i); });
      thumbs.forEach(function (t, k) { t.setAttribute('aria-pressed', k === i ? 'true' : 'false'); });
      if (focus) thumbs[i].focus();
    }
    bp.addEventListener('click', function () { select((cur + shots.length - 1) % shots.length); });
    bn.addEventListener('click', function () { select((cur + 1) % shots.length); });
    select(0);
  });
})();
