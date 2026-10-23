  // ---------- tag completion in the prompt fields ----------
  // From the tag files in /data/PS5SD/tags (Models, Upload, Tags): the tags that start with the word
  // being typed, the most used first (POST /ps5sd/v1/app/tags). Up and Down pick one, Enter or Tab
  // puts it in, Escape closes the list. Without a tag file nothing is asked.
  var tc = { on: false, field: null, hits: [], at: -1, from: 0, to: 0, timer: null, seq: 0 };

  function tagsCheck() {
    req('GET', APP + '/tags', null, function (code, data) {
      tc.on = code === 200 && data && data.tags > 0;
      if (!tc.on) tagClose();
    });
  }

  // The word before the cursor: from the last comma, bracket or line start.
  function tagWord(f) {
    var end = f.selectionEnd, v = f.value;
    if (typeof end !== 'number' || end !== f.selectionStart) return null;
    var start = end;
    while (start > 0 && ',(\n[{|'.indexOf(v.charAt(start - 1)) < 0) start--;
    while (start < end && v.charAt(start) === ' ') start++;
    var word = v.substring(start, end);
    if (word.length < 2 || word.length > 60) return null;
    return { word: word, from: start, to: end };
  }

  function tagInput(f) {
    if (!tc.on) return;
    var w = tagWord(f);
    if (tc.timer) clearTimeout(tc.timer);
    if (!w) { tagClose(); return; }
    tc.timer = setTimeout(function () {
      tc.timer = null;
      var seq = ++tc.seq;
      req('POST', APP + '/tags', { q: w.word, limit: 8 }, function (code, data) {
        if (seq !== tc.seq || document.activeElement !== f) return;
        var hits = code === 200 && data && data.tags ? data.tags : [];
        if (!hits.length) { tagClose(); return; }
        tc.field = f; tc.hits = hits; tc.at = 0; tc.from = w.from; tc.to = w.to;
        tagDraw();
      });
    }, 150);
  }

  function tagCount(n) {
    if (n >= 1e6) return (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M';
    if (n >= 1e3) return Math.round(n / 1e3) + 'k';
    return n ? String(n) : '';
  }

  function tagDraw() {
    var box = $('tagbox');
    clear(box);
    for (var i = 0; i < tc.hits.length; i++) {
      (function (idx) {
        var h = tc.hits[idx];
        var b = document.createElement('button');
        b.type = 'button';
        b.className = idx === tc.at ? 'on' : '';
        b.setAttribute('role', 'option');
        b.appendChild(el('span', 'tg', h.alias ? h.alias + ' → ' + h.tag : h.tag));
        b.appendChild(el('span', 'tn', tagCount(h.count)));
        // mousedown: before the field loses focus
        b.onmousedown = function (e) { e.preventDefault(); tc.at = idx; tagAccept(); };
        box.appendChild(b);
      })(i);
    }
    // under the field it completes
    var f = tc.field;
    box.style.left = f.offsetLeft + 'px';
    box.style.top = (f.offsetTop + f.offsetHeight + 2) + 'px';
    box.style.width = f.offsetWidth + 'px';
    show('tagbox', true);
  }

  function tagClose() {
    tc.hits = []; tc.at = -1; tc.field = null; tc.seq++;
    if ($('tagbox')) show('tagbox', false);
  }

  // The picked tag in place of the word, with ", " after it.
  function tagAccept() {
    var f = tc.field, h = tc.hits[tc.at];
    if (!f || !h) { tagClose(); return; }
    var v = f.value, rest = v.substring(tc.to);
    var sep = /^\s*,/.test(rest) ? '' : ', ';
    f.value = v.substring(0, tc.from) + h.tag + sep + rest;
    var pos = tc.from + h.tag.length + sep.length;
    try { f.setSelectionRange(pos, pos); } catch (e) {}
    tagClose();
    persist();
  }

  function tagKey(e) {
    if (!tc.hits.length || tc.field !== e.target || e.ctrlKey || e.metaKey || e.altKey) return;
    var k = e.keyCode;
    if (k === 40 || k === 38) {
      tc.at = (tc.at + (k === 40 ? 1 : tc.hits.length - 1)) % tc.hits.length;
      tagDraw();
    } else if (k === 13 || k === 9) {
      tagAccept();
    } else if (k === 27) {
      tagClose();
    } else {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
  }

  function tagInit() {
    var fields = [$('prompt'), $('neg')];
    for (var i = 0; i < fields.length; i++) {
      (function (f) {
        f.addEventListener('input', function () { tagInput(f); });
        f.addEventListener('keydown', tagKey);
        f.addEventListener('blur', function () { setTimeout(function () { if (tc.field === f) tagClose(); }, 150); });
      })(fields[i]);
    }
    tagsCheck();
  }
