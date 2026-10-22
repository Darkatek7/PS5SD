  // ---------- the Create tab: modes, settings groups, the model in use ----------
  // st.mode is what a request is made from: 'image' (text), 'edit' (st.init, a start image),
  // 'guide' (st.cn, a ControlNet guide) or 'video' (a video model: the only mode). Each mode uses
  // only its own image. The images stay in st.init and st.cn when the mode changes.
  var MODES = ['image', 'edit', 'guide', 'video'];
  var GROUPS = ['size', 'quality', 'style', 'hires', 'seed', 'video'];
  var GROUP_WRAP = { size: 'grp-size', quality: 'grp-quality', style: 'grp-style', hires: 'hr-sect', seed: 'grp-seed', video: 'vid-sect' };
  var cmOpen = false;   // the model list under the model name
  var prOpen = false;   // the preset menu

  function setMode(name) {
    st.mode = name;
    drawMode();
    summary();
  }

  // The mode the page can show: a video model has only Video; a guide needs a ControlNet.
  function drawMode() {
    var m = st.mode;
    if (st.video) m = 'video';
    else if (m === 'video' || (m === 'guide' && !st.cnModel)) m = 'image';
    st.mode = m;
    show('mode-image', !st.video, 'block');
    show('mode-edit', !st.video, 'block');
    show('mode-guide', !st.video && !!st.cnModel, 'block');
    show('mode-video', !!st.video, 'block');
    for (var i = 0; i < MODES.length; i++) {
      var b = $('mode-' + MODES[i]);
      b.className = MODES[i] === m ? 'on' : '';
      b.setAttribute('aria-pressed', MODES[i] === m ? 'true' : 'false');
    }
    show('edit-sect', m === 'edit');
    show('cn-sect', m === 'guide');
  }

  // "+ Negative prompt" opens the field; it opens by itself when it has text.
  function negShow(on) {
    show('neg-box', on);
    $('neg-toggle').setAttribute('aria-expanded', on ? 'true' : 'false');
    text($('neg-toggle'), on ? '− Negative prompt' : '+ Negative prompt');
  }

  function negSync() {
    if ($('neg').value && $('neg-box').style.display === 'none') negShow(true);
  }

  // ---- the settings groups: open or shut, remembered in this browser
  function groupsOpen() {
    try { return JSON.parse(load('groups') || '{}') || {}; } catch (e) { return {}; }
  }

  function drawGroups() {
    var open = groupsOpen();
    for (var i = 0; i < GROUPS.length; i++) {
      var wrap = GROUP_WRAP[GROUPS[i]], on = !!open[GROUPS[i]];
      show(wrap + '-b', on);
      var head = $(wrap + '-h');
      head.setAttribute('aria-expanded', on ? 'true' : 'false');
      head.className = 'grphead' + (on ? ' open' : '');
    }
  }

  function groupToggle(key) {
    var open = groupsOpen();
    open[key] = !open[key];
    save('groups', JSON.stringify(open));
    drawGroups();
  }

  // ---- the model in use: its family and tags, the preset menu, the list to change it
  function within(box, t) {
    while (t) { if (t === box) return true; t = t.parentNode; }
    return false;
  }

  // The family of the model in use (familyOf: 03-model-presets.js, shared with the Models tab).
  function drawModelBlock() {
    var d = st.mdl;
    var loaded = d && d.loaded ? findModel(d.loaded) : null;
    var base = loaded && loaded.base ? findModel(loaded.base) : null;
    var tags = $('cm-tags');
    clear(tags);
    if (loaded && !d.standby) {
      var fam = familyOf(loaded, base);
      if (fam) tags.appendChild(el('span', 'tag fam', fam));
      if (st.video) tags.appendChild(el('span', 'tag vid', 'video'));
      if (st.cnModel) tags.appendChild(el('span', 'tag cn', 'ControlNet'));
    }
    text($('pr-name'), base ? (loaded.name || loaded.id) : 'model defaults');
    drawPresetList(loaded, base);
    if (cmOpen) drawCmList();
  }

  // The preset menu: the model's own defaults, then its saved presets (the same base). Picking one
  // is Use, the Models tab's switch.
  function drawPresetList(loaded, base) {
    var box = $('pr-list');
    clear(box);
    if (!loaded) return;
    var root = base || loaded;
    box.appendChild(presetItem(root, !base, 'Model defaults'));
    var vs = variantsOf(root.id);
    for (var i = 0; i < vs.length; i++) box.appendChild(presetItem(vs[i], loaded.id === vs[i].id, vs[i].name || vs[i].id));
  }

  function presetItem(m, current, label) {
    var target = !!(st.sw && st.sw.id === m.id);
    var miss = m.missing && m.missing.length;
    var b = actionButton(current ? label + ' · in use' : (target ? 'Loading…' : label), function () {
      prToggle(false);
      if (!current) useModel(m);
    });
    if (current) b.className = 'on';
    b.disabled = !current && (target || !!miss || !!st.sw);
    return b;
  }

  function prToggle(on) {
    prOpen = on === undefined ? !prOpen : !!on;
    show('pr-menu', prOpen);
    $('pr-toggle').setAttribute('aria-expanded', prOpen ? 'true' : 'false');
  }

  // The model list under the model name: installed models (not presets), each with Use.
  function cmToggle(on) {
    cmOpen = on === undefined ? !cmOpen : !!on;
    show('cm-list', cmOpen);
    $('cm-change').setAttribute('aria-expanded', cmOpen ? 'true' : 'false');
    if (cmOpen && !st.mdl) loadModels(null);
    if (cmOpen) drawCmList();
  }

  function drawCmList() {
    var box = $('cm-list');
    clear(box);
    var d = st.mdl;
    if (!d || !d.models) {
      box.appendChild(el('div', 'empty-note', 'Loading…'));
      return;
    }
    var shown = 0;
    for (var i = 0; i < d.models.length; i++) {
      if (!isModel(d.models[i], d) || d.models[i].part) continue;
      box.appendChild(cmRow(d.models[i], d));
      shown++;
    }
    if (!shown) box.appendChild(el('div', 'empty-note', 'No models installed yet. Add one under Models.'));
  }

  function cmRow(m, d) {
    var loaded = d.loaded === m.id;
    var target = !!(st.sw && st.sw.id === m.id);
    var miss = m.missing && m.missing.length;
    var row = el('div', 'mrow mwrap' + (loaded ? ' on' : ''));
    var main = el('div', 'mmain');
    var nm = el('div', 'mname', titleOf(m));
    if (loaded) nm.appendChild(el('span', 'badge ok', 'In use'));
    else if (target) nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Incomplete'));
    main.appendChild(nm);
    var fam = familyOf(m, null);
    var meta = (fam ? fam + ' · ' : '') + bytes(m.bytes);
    var def = defaultsText(m.defaults);
    if (def) meta += ' · ' + def;
    main.appendChild(el('div', 'mmeta', meta));
    row.appendChild(main);
    if (!loaded) {
      var usable = !target && !miss && !st.sw;
      var b = actionButton(target ? 'Loading…' : 'Use', function () { cmToggle(false); useModel(m); });
      b.disabled = !usable;
      if (usable) b.className = 'on';
      var acts = el('div', 'macts');
      acts.appendChild(b);
      row.appendChild(acts);
    }
    return row;
  }

  // ---- the keys and the clicks around the tab
  function overlayOpen() {
    var ids = ['lb', 'me', 'pe', 'dlg', 'login'];
    for (var i = 0; i < ids.length; i++) {
      var o = $(ids[i]);
      if (o && o.style.display !== 'none') return true;
    }
    return false;
  }

  // Ctrl+Enter (Cmd+Enter on a Mac) generates from anywhere on the Create tab.
  function createKey(e) {
    e = e || window.event;
    var enter = e.keyCode === 13 || e.key === 'Enter';
    var esc = e.keyCode === 27 || e.key === 'Escape';
    if (esc) {
      var busy = cmOpen || prOpen || !!openMenu || overlayOpen();
      if (cmOpen) cmToggle(false);
      if (prOpen) prToggle(false);
      // Escape shuts the Save as preset box too, when no menu or overlay was open
      if (!busy && $('ps-box').style.display !== 'none') {
        show('ps-box', false);
        try { $('savepreset').focus(); } catch (x) {}
      }
      return;
    }
    if (enter && (e.ctrlKey || e.metaKey) && st.view === 'create' && !overlayOpen()) {
      if (e.preventDefault) e.preventDefault();
      generate();
    }
  }

  function outsideClick(e) {
    var t = (e && (e.target || e.srcElement)) || null;
    if (cmOpen && !within($('cm-block'), t)) cmToggle(false);
    if (prOpen && !within($('pr-block'), t)) prToggle(false);
  }

  function initCreateLayout() {
    $('mode-image').onclick = function () { setMode('image'); };
    $('mode-edit').onclick = function () { setMode('edit'); };
    $('mode-guide').onclick = function () { setMode('guide'); };
    $('mode-video').onclick = function () { setMode('video'); };
    $('neg-toggle').onclick = function () {
      var open = $('neg-box').style.display === 'none';
      negShow(open);
      if (open) { try { $('neg').focus(); } catch (e) {} }
    };
    $('cm-change').onclick = function () { cmToggle(); };
    $('pr-toggle').onclick = function () { prToggle(); };
    for (var i = 0; i < GROUPS.length; i++) {
      (function (key) { $(GROUP_WRAP[key] + '-h').onclick = function () { groupToggle(key); }; })(GROUPS[i]);
    }
    $('fps').onchange = persist;
    drawGroups();
    negSync();
    if (document.addEventListener) {
      document.addEventListener('keydown', createKey);
      document.addEventListener('click', outsideClick);
    }
  }
