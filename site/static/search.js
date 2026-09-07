// search.js - the documentation search, as an EXTERNAL script.
//
// Copied verbatim from minicompiler/mc's site/static/search.js (MIT). Every
// path it names below is that project's own generator, which renders this site
// too: teko does not vendor it.
//
// It used to be an inline <script> written by site/gen/site.mc into every page.
// The site is served with a Content-Security-Policy of `default-src 'self';
// script-src 'self'`, which forbids inline script (and inline style, and every
// on*= attribute) with no exception short of a nonce or a hash. So the code
// moved out here and the page carries only:
//
//   <script src="/static/search.js" data-search-base="/" defer></script>
//
// The base URL is a data attribute rather than a template substitution, because
// this file is COPIED verbatim from site/static into site/public and never goes
// through site/gen/tmpl.mc. A site under a GitHub Pages project path writes
// data-search-base="/mc/" and nothing here changes.
//
// The index is fetched on the first keystroke, never on load, and the form
// starts hidden in the HTML: a reader with JavaScript off is never given a box
// that cannot search.
(function () {
  var tag = document.querySelector('script[data-search-base]');
  var base = tag ? tag.getAttribute('data-search-base') : '/';
  var form = document.querySelector('.site-search');
  var input = document.getElementById('site-search-input');
  var list = document.querySelector('.search-results');
  if (!form || !input || !list) return;
  form.hidden = false;
  form.addEventListener('submit', function (e) { e.preventDefault(); });
  var index = null;
  function load() {
    if (index) return Promise.resolve(index);
    return fetch(base + 'search.json').then(function (r) { return r.json(); })
      .then(function (data) { index = data; return index; });
  }
  function hits(data, q) {
    var out = [];
    for (var i = 0; i < data.length && out.length < 8; i++) {
      var p = data[i];
      var hay = (p.t + ' ' + p.s + ' ' + p.d + ' ' + p.h.map(function (h) { return h.t; }).join(' ')).toLowerCase();
      if (hay.indexOf(q) === -1) continue;
      var url = p.u, label = p.t;
      for (var j = 0; j < p.h.length; j++) {
        if (p.h[j].t.toLowerCase().indexOf(q) !== -1) { url = p.u + '#' + p.h[j].i; label = p.t + ' / ' + p.h[j].t; break; }
      }
      out.push({ u: url, l: label, s: p.s });
    }
    return out;
  }
  function render(items) {
    list.textContent = '';
    for (var i = 0; i < items.length; i++) {
      var li = document.createElement('li');
      var a = document.createElement('a');
      a.href = items[i].u;
      a.textContent = items[i].l;
      var span = document.createElement('span');
      span.className = 'search-section';
      span.textContent = items[i].s;
      a.appendChild(span);
      li.appendChild(a);
      list.appendChild(li);
    }
    list.hidden = items.length === 0;
  }
  input.addEventListener('input', function () {
    var q = input.value.trim().toLowerCase();
    if (q.length < 2) { render([]); return; }
    load().then(function (data) { render(hits(data, q)); });
  });
  input.addEventListener('keydown', function (e) { if (e.key === 'Escape') { input.value = ''; render([]); } });
  document.addEventListener('click', function (e) { if (!form.contains(e.target)) render([]); });
})();
