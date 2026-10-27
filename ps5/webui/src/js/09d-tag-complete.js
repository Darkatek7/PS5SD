  // ---------- tag completion in the prompt fields ----------
  // From the tag files in /data/PS5SD/tags (Models, Upload, Tags): the tags that start with the word
  // being typed, the most used first (POST /ps5sd/v1/app/tags). Up and Down pick one, Enter or Tab
  // puts it in, Escape closes the list. Without a tag file nothing is asked. The same list serves the
  // preset editor, with its own source (peSource, 14-presets.js).
  var tc = { on: false, field: null, bind: null, home: null, homeNext: null, hits: [], at: -1, from: 0, to: 0, timer: null, seq: 0 };

  function tagsCheck() {
    req('GET', APP + '/tags', null, function (code, data) {
      tc.on = code === 200 && data && data.tags > 0;
      if (!tc.on && tc.bind && tc.bind.source === tagSource) tagClose();
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

  // The source of the prompt fields: the tags that start with the word, asked 150 ms after the last key.
  function tagSource(f, done) {
    if (!tc.on) { done(null); return; }
    var w = tagWord(f);
    if (tc.timer) clearTimeout(tc.timer);
    if (!w) { done(null); return; }
    tc.timer = setTimeout(function () {
      tc.timer = null;
      var seq = ++tc.seq;
      req('POST', APP + '/tags', { q: w.word, limit: 8 }, function (code, data) {
        if (seq !== tc.seq) return;
        var tags = code === 200 && data && data.tags ? data.tags : [];
        var sep = /^\s*,/.test(f.value.substring(w.to)) ? '' : ', ';
        var hits = [];
        for (var i = 0; i < tags.length; i++) {
          var t = tags[i];
          hits.push({ label: t.alias ? t.alias + ' \u2192 ' + t.tag : t.tag, note: tagCount(t.count), insert: t.tag + sep });
        }
        done({ from: w.from, to: w.to, hits: hits });
      });
    }, 150);
  }

  // Asks the field's source; it answers {from, to, hits} or null. A hit is {label, note, insert, sel, again}:
  // insert replaces from..to, sel ([a, b] in insert) is selected after it, again asks once more.
  function tagInput(f) {
    var b = f.tagBind;
    b.source(f, function (r) {
      if (document.activeElement !== f) return;
      var hits = r ? r.hits : [];
      // only the word that is there already: nothing to pick
      if (hits.length === 1 && f.value.substring(r.from, r.to) === hits[0].insert) hits = [];
      if (!hits.length) { tagClose(); return; }
      tc.field = f; tc.bind = b; tc.hits = hits; tc.at = 0; tc.from = r.from; tc.to = r.to;
      tagDraw();
    });
  }

  function tagCount(n) {
    if (n >= 1e6) return (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M';
    if (n >= 1e3) return Math.round(n / 1e3) + 'k';
    return n ? String(n) : '';
  }

  // Where the caret's line is in the textarea f, from its top edge: {top, height}. A hidden copy of the
  // field with the text up to the caret, so a line that wraps counts too.
  function caretLine(f) {
    var cs = window.getComputedStyle(f), m = document.createElement('div');
    var props = ['boxSizing', 'paddingTop', 'paddingRight', 'paddingBottom', 'paddingLeft', 'fontFamily', 'fontSize',
      'fontWeight', 'fontStyle', 'letterSpacing', 'lineHeight', 'textTransform', 'wordSpacing', 'textIndent', 'tabSize'];
    for (var i = 0; i < props.length; i++) m.style[props[i]] = cs[props[i]];
    m.style.position = 'absolute'; m.style.visibility = 'hidden'; m.style.left = '-9999px'; m.style.top = '0';
    m.style.boxSizing = 'border-box'; m.style.border = '0'; m.style.width = f.clientWidth + 'px';
    m.style.whiteSpace = 'pre-wrap'; m.style.wordWrap = 'break-word'; m.style.overflowWrap = 'break-word';
    m.appendChild(document.createTextNode(f.value.substring(0, f.selectionEnd)));
    var mark = document.createElement('span');
    mark.appendChild(document.createTextNode('.'));
    m.appendChild(mark);
    document.body.appendChild(m);
    var r = { top: f.clientTop + mark.offsetTop - f.scrollTop, height: mark.offsetHeight };
    document.body.removeChild(m);
    return r;
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
        b.appendChild(el('span', 'tg', h.label));
        if (h.note) b.appendChild(el('span', 'tn', h.note));
        // mousedown: before the field loses focus
        b.onmousedown = function (e) { e.preventDefault(); tc.at = idx; tagAccept(); };
        box.appendChild(b);
      })(i);
    }
    var f = tc.field;
    box.setAttribute('aria-label', tc.bind.label);
    box.style.left = f.offsetLeft + 'px';
    box.style.width = f.offsetWidth + 'px';
    if (!tc.bind.atLine) {
      // under the field it completes
      if (box.parentNode !== tc.home) tc.home.insertBefore(box, tc.homeNext);
      box.style.top = (f.offsetTop + f.offsetHeight + 2) + 'px';
      show('tagbox', true);
      return;
    }
    // in a dialog: next to the field (the dialog is over the page), under the caret's line, or over it if
    // there is no room below
    if (box.parentNode !== f.parentNode) f.parentNode.insertBefore(box, f.nextSibling);
    show('tagbox', true);
    // from the screen, not offsetTop: that leaves out how far the dialog's box is scrolled
    var line = caretLine(f), op = box.offsetParent, fr = f.getBoundingClientRect();
    line.top = Math.max(0, Math.min(line.top, f.offsetHeight - line.height));
    var y = fr.top - op.getBoundingClientRect().top - op.clientTop + op.scrollTop + line.top;
    box.style.top = (y + line.height + 2) + 'px';
    if (box.getBoundingClientRect().bottom > window.innerHeight && fr.top + line.top - box.offsetHeight - 2 >= 0) {
      box.style.top = (y - box.offsetHeight - 2) + 'px';
    }
  }

  function tagClose() {
    if (tc.timer) { clearTimeout(tc.timer); tc.timer = null; }
    tc.hits = []; tc.at = -1; tc.field = null; tc.bind = null; tc.seq++;
    if ($('tagbox')) show('tagbox', false);
  }

  // The picked hit in place of the word.
  function tagAccept() {
    var f = tc.field, h = tc.hits[tc.at], b = tc.bind;
    if (!f || !h) { tagClose(); return; }
    var v = f.value;
    f.value = v.substring(0, tc.from) + h.insert + v.substring(tc.to);
    var a = tc.from + (h.sel ? h.sel[0] : h.insert.length), z = tc.from + (h.sel ? h.sel[1] : h.insert.length);
    try { f.setSelectionRange(a, z); } catch (e) {}
    tagClose();
    if (b.after) b.after(f);
    if (h.again) tagInput(f);
  }

  function tagKey(e) {
    if (!tc.hits.length || tc.field !== e.target || e.ctrlKey || e.metaKey || e.altKey) return;
    var k = e.keyCode;
    if (k === 40 || k === 38) {
      tc.at = (tc.at + (k === 40 ? 1 : tc.hits.length - 1)) % tc.hits.length;
      tagDraw();
      var on = $('tagbox').childNodes[tc.at];
      if (on && on.scrollIntoView) on.scrollIntoView({ block: 'nearest' });
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

  // b: {source, label, after, atLine}; atLine puts the list under the caret's line (a long field in a dialog)
  function tagBind(f, b) {
    f.tagBind = b;
    f.addEventListener('input', function () { tagInput(f); });
    f.addEventListener('keydown', tagKey);
    f.addEventListener('blur', function () { setTimeout(function () { if (tc.field === f) tagClose(); }, 150); });
  }

  function tagInit() {
    tc.home = $('tagbox').parentNode;
    tc.homeNext = $('tagbox').nextSibling;
    var prompt = { source: tagSource, label: 'Tags', after: function () { persist(); } };
    tagBind($('prompt'), prompt);
    tagBind($('neg'), prompt);
    tagBind($('pe-text'), { source: peSource, label: 'Suggestions', atLine: true });
    tagsCheck();
  }
