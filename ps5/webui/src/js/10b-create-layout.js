  // ---------- the Create tab: modes, settings groups, the model in use ----------
  // st.mode is what a request is made from: 'image' (text), 'edit' (st.init, a start image),
  // 'guide' (st.cn, a ControlNet guide) or 'video' (a video model: the only mode). Each mode uses
  // only its own image. The images stay in st.init and st.cn when the mode changes.
  var MODES = ['image', 'edit', 'guide', 'video'];
  var GROUPS = ['size', 'quality', 'style', 'hires', 'seed', 'video'];
  var GROUP_WRAP = { size: 'grp-size', quality: 'grp-quality', style: 'grp-style', hires: 'hr-sect', seed: 'grp-seed', video: 'vid-sect' };
  var cmOpen = false;   // the model list under the model name

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

  // ---- the model in use and the list to change it
  function within(box, t) {
    while (t) { if (t === box) return true; t = t.parentNode; }
    return false;
  }

  // The model in use: its name, the preset under it, its family. groupsOf (13-models.js) is the
  // Models tab's list: a model once, with its presets.
  function drawModelBlock() {
    var d = st.mdl;
    var loaded = d && d.loaded ? findModel(d.loaded) : null;
    var base = loaded && loaded.base ? findModel(loaded.base) : null;
    var tags = $('cm-tags');
    clear(tags);
    var g = loaded && !d.standby && !st.sw ? groupOf(loaded.id) : null;
    if (g) {
      var multi = g.items.length > 1;
      text($('cm-name'), g.title);
      text($('model'), g.title + (multi ? ' · ' + shortName(loaded, g.title) : ''));
      text($('pr-name'), multi ? 'Preset: ' + shortName(loaded, g.title) : '');
    } else {
      text($('pr-name'), '');
    }
    show('pr-name', !!(g && g.items.length > 1), 'block');
    if (loaded && !d.standby) {
      var fam = familyOf(loaded, base);
      if (fam) tags.appendChild(el('span', 'tag fam', fam));
      if (st.video) tags.appendChild(el('span', 'tag vid', 'video'));
      if (st.cnModel) tags.appendChild(el('span', 'tag cn', 'ControlNet'));
    }
    show('resetpreset', !!loaded && !d.standby, 'inline-block');
    drawSaveBox();
    if (cmOpen) drawCmList();
  }

  // The list under the model name: each model with its presets as chips. The model's name loads
  // its default preset, a chip that preset: Use on the Models tab.
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
    var gs = groupsOf(d), shown = 0;
    for (var i = 0; i < gs.length; i++) {
      if (gs[i].main.part) continue;
      box.appendChild(cmGroup(gs[i], d));
      shown++;
    }
    if (!shown) box.appendChild(el('div', 'empty-note', 'No models installed yet. Add one under Models.'));
  }

  function cmGroup(g, d) {
    var on = null, i;
    for (i = 0; i < g.items.length; i++) if (d.loaded === g.items[i].id) on = g.items[i];
    var wrap = el('div', 'cmg' + (on ? ' on' : ''));
    var miss = g.def.missing && g.def.missing.length;
    var head = actionButton('', function () { cmPick(g.def); });
    head.className = 'cmg-head';
    head.appendChild(el('span', 'cmg-name', g.title));
    var fam = familyOf(g.main, null);
    var meta = (fam ? fam + ' · ' : '') + bytes(g.main.bytes);
    if (g.items.length === 1 && defaultsText(g.main.defaults)) meta += ' · ' + defaultsText(g.main.defaults);
    if (miss) meta += ' · files missing';
    head.appendChild(el('span', 'cmg-meta', meta));
    head.disabled = !!miss || !!st.sw || on === g.def;
    wrap.appendChild(head);
    if (g.items.length > 1) {
      var chips = el('div', 'chips');
      for (i = 0; i < g.items.length; i++) {
        (function (v) {
          var vm = v.missing && v.missing.length;
          var c = actionButton((v === g.def ? '★ ' : '') + shortName(v, g.title), function () { cmPick(v); });
          c.title = subMeta(v) || shortName(v, g.title);
          if (v === on) c.className = 'on';
          c.disabled = v !== on && (!!vm || !!st.sw);
          c.setAttribute('aria-pressed', v === on ? 'true' : 'false');
          chips.appendChild(c);
        })(g.items[i]);
      }
      wrap.appendChild(chips);
    }
    return wrap;
  }

  function cmPick(m) {
    cmToggle(false);
    if (!st.mdl || st.mdl.loaded !== m.id) useModel(m);
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
      var busy = cmOpen || !!openMenu || overlayOpen();
      if (cmOpen) cmToggle(false);
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
