  // ---------- controls ----------
  // Size buttons: the built-in sizes, then the user's own (saved in this browser, for every model).
  function okSize(v) { return typeof v === 'number' && v >= 64 && v <= 2048 && v % 8 === 0; }

  function hasSize(list, w, h) {
    for (var i = 0; i < list.length; i++) if (list[i][0] === w && list[i][1] === h) return true;
    return false;
  }

  function ownSizes() {
    var out = [];
    try {
      var list = JSON.parse(load('sizes') || '[]');
      for (var i = 0; i < list.length; i++) {
        var w = list[i] && list[i][0], h = list[i] && list[i][1];
        if (okSize(w) && okSize(h) && !hasSize(PRESETS, w, h) && !hasSize(out, w, h)) out.push([w, h]);
      }
    } catch (e) {}
    return out;
  }

  function buildPresets() {
    var box = $('presets');
    clear(box);
    var mine = ownSizes();
    st.sizes = PRESETS.concat(mine);
    for (var i = 0; i < st.sizes.length; i++) {
      (function (w, h, own) {
        var b = document.createElement('button');
        b.type = 'button';
        b.id = 'p' + w + 'x' + h;
        text(b, w + '×' + h);
        b.onclick = function () { $('w').value = w; $('h').value = h; markPreset(); persist(); };
        if (own) {
          b.className = 'mine';
          b.title = 'Your size';
          var x = document.createElement('span');
          x.className = 'x';
          x.title = 'Remove this size';
          x.setAttribute('aria-label', 'Remove ' + w + '×' + h);
          text(x, '×');
          x.onclick = function (e) {
            e = e || window.event;
            if (e.stopPropagation) e.stopPropagation();
            removeSize(w, h);
            return false;
          };
          b.appendChild(x);
        }
        box.appendChild(b);
      })(st.sizes[i][0], st.sizes[i][1], i >= PRESETS.length);
    }
    var add = document.createElement('button');
    add.type = 'button';
    add.id = 'psave';
    add.className = 'save';
    add.title = 'Keep the width and height below as a size button';
    text(add, '+ Save size');
    add.onclick = saveSize;
    box.appendChild(add);
    markPreset();
  }

  function saveSize() {
    var w = size8('w', 512), h = size8('h', 512);
    var mine = ownSizes();
    if (hasSize(PRESETS, w, h) || hasSize(mine, w, h)) { toast(w + '×' + h + ' is already a size button.'); return; }
    mine.push([w, h]);
    save('sizes', JSON.stringify(mine));
    buildPresets();
    persist();
    toast(w + '×' + h + ' saved. The × on it removes it again.');
  }

  function removeSize(w, h) {
    var mine = ownSizes(), keep = [];
    for (var i = 0; i < mine.length; i++) if (mine[i][0] !== w || mine[i][1] !== h) keep.push(mine[i]);
    save('sizes', JSON.stringify(keep));
    buildPresets();
  }

  function markPreset() {
    var w = parseInt($('w').value, 10), h = parseInt($('h').value, 10);
    var sizes = st.sizes || PRESETS;
    var known = false;
    for (var i = 0; i < sizes.length; i++) {
      var b = $('p' + sizes[i][0] + 'x' + sizes[i][1]);
      var on = sizes[i][0] === w && sizes[i][1] === h;
      if (on) known = true;
      if (b) b.className = (i >= PRESETS.length ? 'mine' : '') + (on ? ' on' : '');
    }
    if ($('psave')) $('psave').disabled = known || !okSize(w) || !okSize(h);
  }

  function stepper(id, delta, min, max, digits) {
    var input = $(id);
    var v = parseFloat(input.value);
    if (isNaN(v)) v = min;
    v = Math.min(max, Math.max(min, v + delta));
    input.value = digits ? v.toFixed(digits).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '') : Math.round(v);
    persist();
  }

  function fill(select, names) {
    var current = select.value;
    clear(select);
    var list = ['default'];
    for (var i = 0; i < names.length; i++) if (names[i] !== 'default') list.push(names[i]);
    for (var j = 0; j < list.length; j++) {
      var o = document.createElement('option');
      o.value = list[j];
      text(o, list[j]);
      select.appendChild(o);
    }
    select.value = 'default';
    for (var k = 0; k < list.length; k++) if (list[k] === current) select.value = current;
  }

  function names(data) {
    var out = [];
    if (data && data.length) {
      for (var i = 0; i < data.length; i++) {
        var n = typeof data[i] === 'string' ? data[i] : (data[i] && data[i].name);
        if (n) out.push(n);
      }
    }
    return out;
  }

  function toggleSettings() {
    var open = $('settings').style.display === 'none';
    show('settings', open);
    $('set-toggle').setAttribute('aria-expanded', open ? 'true' : 'false');
    text($('set-toggle'), open ? 'Settings ▴' : 'Settings ▾');
    save('open', open ? '1' : '0');
  }

  // On a phone the tab bar scrolls sideways: bring the chosen tab into it. The page itself
  // must not move, so its position is put back if the scroll changed it.
  function showTab(name) {
    var b = $('tab-' + name), bar = b && b.parentNode;
    if (!bar || bar.scrollWidth <= bar.clientWidth) return;
    var x = window.pageXOffset, y = window.pageYOffset;
    try { b.scrollIntoView({ block: 'nearest', inline: 'nearest' }); } catch (e) {}
    if (window.pageXOffset !== x || window.pageYOffset !== y) window.scrollTo(x, y);
  }

  function setView(name) {
    st.view = name;
    show('view-create', name === 'create', 'flex');
    show('view-models', name === 'models', 'flex');
    show('view-presets', name === 'presets', 'flex');
    show('view-gallery', name === 'gallery');
    show('view-help', name === 'help');
    $('tab-create').className = name === 'create' ? 'on' : '';
    $('tab-models').className = name === 'models' ? 'on' : '';
    $('tab-presets').className = name === 'presets' ? 'on' : '';
    $('tab-gallery').className = name === 'gallery' ? 'on' : '';
    $('tab-help').className = name === 'help' ? 'on' : '';
    showTab(name);
    if (name === 'gallery') refreshGallery();
    if (name === 'help') showDoc(docName);
    if (name === 'presets') { loadModels(null); loadLoras(null); drawSaveBox(); }
    if (name === 'models') enterModels(); else dlSchedule(dlDelay());
  }

