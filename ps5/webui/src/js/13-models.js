  // ---------- models ----------
  var APP = '/ps5sd/v1/app';
  var SWITCH_SLOW = 90;       // seconds until the switch note admits it takes long
  var SWITCH_GIVE_UP = 600;   // seconds until the page stops waiting for a switch
  var MODEL_EXT = /\.(gguf|safetensors|ckpt|sft)$/i;
  var mlistSig = null;
  var swPost = false;

  function trim(s) { return String(s).replace(/^\s+|\s+$/g, ''); }
  function absent(code) { return code === 404 || code === 405 || code === 501; }

  function el(tag, cls, label) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (label !== undefined) text(e, label);
    return e;
  }

  function bytes(n) {
    n = Number(n) || 0;
    if (n >= 1e9) return (n / 1e9).toFixed(n >= 1e10 ? 1 : 2) + ' GB';
    if (n >= 1e6) return Math.round(n / 1e6) + ' MB';
    if (n >= 1e3) return Math.round(n / 1e3) + ' KB';
    return n + ' B';
  }

  function count(n) {
    n = Number(n) || 0;
    if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M';
    if (n >= 1e3) return (n / 1e3).toFixed(1) + 'k';
    return String(n);
  }

  function baseName(path) {
    var p = String(path || '').split('?')[0].split('/');
    return p[p.length - 1] || String(path || '');
  }

  function errText(code, data, fallback) {
    var m = data && (data.message || data.error);
    if (m) return String(m);
    if (code === 0 || code === 502 || code === 503) return 'Cannot reach the server.';
    return fallback + ' (' + code + ').';
  }

  function findModel(id) {
    var list = st.mdl && st.mdl.models ? st.mdl.models : [];
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
    return null;
  }

  function defaultsText(d) {
    if (!d) return '';
    var parts = [];
    if (d.width && d.height) parts.push(d.width + '×' + d.height);
    if (d.steps) parts.push(d.steps + (String(d.steps) === '1' ? ' step' : ' steps'));
    if (d.cfg) parts.push('CFG ' + d.cfg);
    if (d.sampler) parts.push(d.sampler);
    return parts.join(' · ');
  }

  // The library: what is installed, what is loaded, whether a switch runs.
  function loadModels(cb) {
    req('GET', APP + '/models', null, function (code, data) {
      if (code === 200 && data && data.models) {
        setModels(data);
        loadDefaults();
      } else if (absent(code)) {
        st.app = false;
        st.mdl = null;
        drawModels();
        drawCurrent();
      }
      if (cb) cb();
    }, 8000);
  }

  function setModels(data) {
    st.app = true;
    st.mdl = data;
    // a switch somebody else started (another browser, or this page before a reload)
    if (data.switching && !st.sw && data.active) beginSwitch(data.active, '', false);
    if (data.standby && !st.sw && !st.sbSeen) {
      st.sbSeen = true;
      setView('models');
      toast('No model is loaded yet. Download one here, or press Use on one you have.', 7000);
    }
    if (!data.standby) st.sbSeen = false;
    drawModels();
    if (loraList) drawLoraList();  // the loaded model's family comes first
    drawSaveBox();
    drawCurrent();
    drawInit();  // the reference edit shows for FLUX.2 only
  }

  // The Installed sub-view: image models, then video models. A model is one main model file with all
  // the presets that load it under it (groupsOf); a model with one way to load it is one row.
  function drawModels() {
    var d = st.mdl;
    var sig = String(st.app) + '|' + JSON.stringify(d) + '|' + JSON.stringify(st.defp) + '|' + (st.sw ? st.sw.id : '') + '|' +
      (st.delErr ? st.delErr.id + st.delErr.msg : '') + '|' + (st.video ? 'v' : '');
    if (sig === mlistSig) return;
    mlistSig = sig;
    drawPresetList();
    var box = $('mlist'), vbox = $('vlist');
    clear(box);
    clear(vbox);
    show('gh-vid', false);
    if (st.app === false) {
      box.appendChild(el('div', 'empty-note', 'This version of the PS5SD app cannot list or switch models.'));
      return;
    }
    if (!d) {
      box.appendChild(el('div', 'empty-note', 'Loading…'));
      return;
    }
    if (!d.models.length) {
      box.appendChild(el('div', 'empty-note', 'No models installed yet. Get one under Get more.'));
      return;
    }
    var gs = groupsOf(d), imgs = 0, vids = 0;
    for (var i = 0; i < gs.length; i++) {
      if (videoOf(gs[i].main, d)) { vbox.appendChild(groupItem(gs[i], d)); vids++; }
      else { box.appendChild(groupItem(gs[i], d)); imgs++; }
    }
    if (!imgs) box.appendChild(el('div', 'empty-note', 'No image models installed yet. Get one under Get more.'));
    show('gh-vid', vids > 0);
  }

  // The main model file of an entry (the server's model_file; older servers: its first file).
  function fileOf(m) {
    return m.model_file || baseName(m.files && m.files[0]) || m.id;
  }

  // The models: [{key, title, main, items, def}]. key: the main model file; items: the presets that
  // load it, main first, then the catalog's other presets, then the saved ones. A bare checkpoint is
  // an item only when no preset of its own stands for it (a preset saved from it does not).
  function groupsOf(d) {
    var by = {}, out = [], i, j, m, g;
    for (i = 0; i < d.models.length; i++) {
      m = d.models[i];
      var k = fileOf(m);
      g = by[k];
      if (!g) { g = by[k] = { key: k, items: [], file: null }; out.push(g); }
      if (m.kind === 'file') g.file = m; else g.items.push(m);
    }
    for (i = 0; i < out.length; i++) {
      g = out[i];
      var roots = [], saved = [], fileUsed = false;
      for (j = 0; j < g.items.length; j++) {
        m = g.items[j];
        if (m.base) saved.push(m); else roots.push(m);
        if (g.file && m.base === g.file.id) fileUsed = true;
      }
      if (g.file && !roots.length) fileUsed = true;
      var main = null;
      for (j = 0; j < roots.length; j++) {
        if (roots[j].control_net) continue;
        if (!main || titleOf(roots[j]).length < titleOf(main).length) main = roots[j];
      }
      if (!main) main = roots[0] || (fileUsed ? g.file : null) || saved[0];
      var rest = [];
      for (j = 0; j < roots.length; j++) if (roots[j] !== main) rest.push(roots[j]);
      saved.sort(function (a, b) { return titleOf(a).toLowerCase() < titleOf(b).toLowerCase() ? -1 : 1; });
      g.items = [main].concat(rest);
      if (fileUsed && g.file && g.file !== main) g.items.push(g.file);
      for (j = 0; j < saved.length; j++) if (saved[j] !== main) g.items.push(saved[j]);
      g.main = main;
      g.title = titleOf(main);
      g.def = main;
      var want = st.defp ? st.defp[g.key] : '';
      for (j = 0; want && j < g.items.length; j++) if (g.items[j].id === want) g.def = g.items[j];
    }
    out.sort(function (a, b) { return a.title.toLowerCase() < b.title.toLowerCase() ? -1 : 1; });
    return out;
  }

  // The model an entry belongs to, or null.
  function groupOf(id) {
    if (!st.mdl || !id) return null;
    var gs = groupsOf(st.mdl);
    for (var i = 0; i < gs.length; i++) for (var j = 0; j < gs[i].items.length; j++) if (gs[i].items[j].id === id) return gs[i];
    return null;
  }

  // A preset's name under its model: what it adds to the model's name ("SDXL Base 1.0 (Q8_0), fast"
  // under "SDXL Base 1.0 (Q8_0)" is "Fast"); "Standard" for the model's own settings.
  function shortName(m, title) {
    var n = titleOf(m);
    if (n === title) return 'Standard';
    var a = n.split(' '), b = title.split(' '), i = 0;
    function w(s) { return s.replace(/[,:;()+]+$/, '').replace(/^[(+]+/, '').toLowerCase(); }
    while (i < a.length && i < b.length && w(a[i]) === w(b[i])) i++;
    if (i < 2) return n;
    var rest = trim(a.slice(i).join(' ').replace(/^[\s,:;+\-·]+/, ''));
    if (!rest) return 'Standard';
    return /^[a-z]+$/.test(rest.split(' ')[0]) ? rest.charAt(0).toUpperCase() + rest.slice(1) : rest;
  }

  function inList(list, id) {
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return true;
    return false;
  }

  // A video model: the loaded one (the capabilities say so), else by its family or its name.
  function videoOf(m, d) {
    var via = d.loaded ? findModel(d.loaded) : null;
    if (st.video && d.loaded && (d.loaded === m.id || (via && via.base === m.id))) return true;
    return /^Wan/.test(m.family || '') || videoName(((m.name || '') + ' ' + (m.files || []).join(' ')).toLowerCase());
  }

  // A file name as a name: no extension, _ and - as spaces, the case kept; a quantization (Q8_0,
  // Q4_K_M) stays as it is written.
  function readable(s) {
    var q = [];
    s = String(s).replace(/\.(gguf|safetensors|ckpt|sft|pth)$/i, '')
      .replace(/(^|[_\-. ])((?:I?Q\d+(?:_[0-9A-Z]+)*)|f16|bf16|fp16|f32)(?=$|[_\-. ])/gi, function (all, sep, quant) {
        q.push(quant);
        return sep + '\u0001' + (q.length - 1) + '\u0001';
      });
    s = trim(s.replace(/[_\-]+/g, ' ').replace(/\s+/g, ' '));
    return s.replace(/\u0001(\d+)\u0001/g, function (all, i) { return q[+i]; });
  }

  function titleOf(m) {
    return m.kind === 'file' ? readable(m.name || m.id) : (m.name || m.id);
  }

  // A preset's settings in one muted line: size, steps, CFG, sampler, LoRAs, hires.
  function subMeta(v) {
    var parts = [];
    var def = defaultsText(v.defaults);
    if (def) parts.push(def);
    var ls = v.defaults && v.defaults.loras ? parseLoras(v.defaults.loras) : [];
    if (ls.length) parts.push(ls.length + (ls.length === 1 ? ' LoRA' : ' LoRAs'));
    if (v.defaults && v.defaults.hires_scale) parts.push('hires ×' + v.defaults.hires_scale);
    return parts.join(' · ');
  }

  // Tags as small pills under a row's name: [{text, cls}] (see tagsOf in 03-model-presets.js).
  function addTags(parent, tags) {
    if (!tags.length) return;
    var box = el('div', 'mtags');
    for (var i = 0; i < tags.length; i++) box.appendChild(el('span', 'tag' + (tags[i].cls ? ' ' + tags[i].cls : ''), tags[i].text));
    parent.appendChild(box);
  }

  // Loaded, or on its way.
  function stateOf(m, d) {
    if (d.loaded === m.id) return 'on';
    if ((!!st.sw && st.sw.id === m.id) || d.active === m.id) return 'loading';
    return '';
  }

  // One model: its name, size and tags, Use (its default preset), and its presets under it.
  function groupItem(g, d) {
    var m = g.main, i, on = null, loading = null;
    for (i = 0; i < g.items.length; i++) {
      var s = stateOf(g.items[i], d);
      if (s === 'on') on = g.items[i];
      else if (s === 'loading') loading = g.items[i];
    }
    var single = g.items.length === 1;
    var miss = m.missing && m.missing.length ? m.missing : null;
    var row = el('div', 'mrow mwrap mgrp' + (on ? ' on' : '') + (miss && !on ? ' dim' : ''));
    var main = el('div', 'mmain');
    var nm = el('div', 'mname', g.title);
    if (on) nm.appendChild(el('span', 'badge ok', single ? 'In use' : 'In use: ' + shortName(on, g.title)));
    else if (loading) nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Incomplete'));
    if (m.part) nm.appendChild(el('span', 'badge warn', 'Part of a model'));
    main.appendChild(nm);
    // only the diffusion model, text encoder or VAE: it runs in a preset that names the others
    if (m.part) main.appendChild(el('div', 'mmeta', 'Only the ' + m.part + ' of a model, so it cannot run on its own. ' +
      'Get more installs a model with all its files and a preset.'));
    addTags(main, tagsOf(m.name || m.id, m.files, videoOf(m, d), !!m.control_net, m.family));
    var spec = [bytes(m.bytes)];
    if (single && subMeta(m)) spec.push(subMeta(m));
    if (!single) spec.push(g.items.length + ' presets');
    main.appendChild(el('div', 'mmeta', spec.join(' · ')));
    if (miss) {
      var lost = [];
      for (i = 0; i < miss.length; i++) lost.push(baseName(miss[i]));
      main.appendChild(el('div', 'mmeta bad', 'Missing: ' + lost.join(', ')));
    }
    if (st.delErr && st.delErr.id === m.id) main.appendChild(el('div', 'mmeta bad', st.delErr.msg));
    row.appendChild(main);
    var acts = el('div', 'macts');
    if (!on) {
      var dm = g.def.missing && g.def.missing.length;
      var usable = !loading && !dm && !st.sw && !g.def.part;
      var b = actionButton(loading ? 'Loading…' : 'Use', function () { useModel(g.def); });
      b.disabled = !usable;
      if (usable) b.className = 'on';
      if (!single) b.title = 'Loads ' + shortName(g.def, g.title);
      acts.appendChild(b);
    }
    // Delete on a model is its main preset (with the presets saved from it); a catalog preset of its own
    // (a ControlNet one) stays, and so do the files it uses
    var others = 0;
    for (i = 0; i < g.items.length; i++) if (g.items[i] !== m && g.items[i].kind === 'preset' && !g.items[i].base) others++;
    acts.appendChild(single ? itemMenu(m, g, d) : moreMenu([{ label: others ? 'Delete Standard' : 'Delete the model', danger: true,
      disabled: !!deleteBlock(m, d) || !!st.sw, fn: function () { askDelete(m); } }],
      deleteBlock(m, d) || (others ? 'Its saved presets go with it; ' + (others === 1 ? 'the other preset stays' : 'the ' + others + ' other presets stay') +
        ', and so do the files they use.' : '')));
    row.appendChild(acts);
    var item = el('div', 'mitem');
    item.appendChild(row);
    if (!single) {
      var box = el('div', 'subs');
      for (i = 0; i < g.items.length; i++) box.appendChild(presetRow(g.items[i], g, d));
      item.appendChild(box);
    }
    return item;
  }

  // The star of a preset of a model with several: ★ on the default, ☆ on the others makes it the default.
  function starButton(v, g) {
    var on = v === g.def;
    var b = actionButton(on ? '★' : '☆', function () { if (!on) makeDefault(g, v); });
    b.className = 'star' + (on ? ' on' : '');
    b.title = on ? 'The default: Use on the model loads it' : 'Make it the default of ' + g.title;
    b.setAttribute('aria-pressed', on ? 'true' : 'false');
    b.setAttribute('aria-label', on ? 'The default preset' : 'Make it the default');
    b.disabled = !on && !st.defp;
    return b;
  }

  // One preset of a model: its star, its name, its settings, Use and ⋯.
  function presetRow(v, g, d) {
    var s = stateOf(v, d);
    var miss = v.missing && v.missing.length;
    var row = el('div', 'sub' + (s === 'on' ? ' on' : ''));
    row.appendChild(starButton(v, g));
    var main = el('div', 'smain');
    var nm = el('div', 'sname');
    nm.appendChild(document.createTextNode(shortName(v, g.title)));
    if (v.control_net && !/controlnet/i.test(v.name || '')) nm.appendChild(el('span', 'tag cn', 'ControlNet'));
    if (s === 'on') nm.appendChild(el('span', 'badge ok', 'In use'));
    else if (s === 'loading') nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Files missing'));
    main.appendChild(nm);
    main.appendChild(el('div', 'mmeta', subMeta(v) || 'No settings of its own'));
    if (st.delErr && st.delErr.id === v.id) main.appendChild(el('div', 'mmeta bad', st.delErr.msg));
    row.appendChild(main);
    var acts = el('div', 'sacts');
    if (s !== 'on') {
      var usable = s !== 'loading' && !miss && !st.sw && !v.part;
      var b = actionButton(s === 'loading' ? 'Loading…' : 'Use', function () { useModel(v); });
      b.disabled = !usable;
      acts.appendChild(b);
    }
    acts.appendChild(itemMenu(v, g, d));
    row.appendChild(acts);
    return row;
  }

  // The ⋯ of a preset: Edit text, Delete (the star makes it the default). inList: the Presets list,
  // where a model is not deleted (that is under Installed): its Delete is off, with the note why.
  function itemMenu(v, g, d, inList) {
    var items = [], why = '';
    if (v.kind === 'preset') items.push({ label: 'Edit text', fn: function () { editPreset(v); } });
    if (v.kind === 'preset' && v.base) {
      var busy = d.loaded === v.id || d.active === v.id;
      if (busy) why = 'Switch to another preset to delete this one.';
      items.push({ label: 'Delete', danger: true, disabled: busy || !!st.sw, fn: function () { askDeletePreset(v); } });
    } else if (inList) {
      why = 'Delete the model under Models → Installed.';
      items.push({ label: 'Delete', danger: true, disabled: true, fn: function () {} });
    } else {
      why = deleteBlock(v, d);
      items.push({ label: 'Delete', danger: true, disabled: !!why || !!st.sw, fn: function () { askDelete(v); } });
    }
    return moreMenu(items, why);
  }

  // The Presets sub-view: every preset of every model in one list by name, with its model, and the search
  // over both. Apply settings: a preset of another model of the loaded one's family (its LoRAs fit too).
  function drawPresetList() {
    var box = $('plist'), d = st.mdl, i, j;
    clear(box);
    if (st.app === false || !d) {
      box.appendChild(el('div', 'empty-note', st.app === false ? 'This version of the PS5SD app has no presets.' : 'Loading…'));
      return;
    }
    var q = trim($('pfind').value).toLowerCase();
    var gs = groupsOf(d), rows = [];
    var loaded = d.loaded && !d.standby ? findModel(d.loaded) : null, lg = null;
    var lfam = loaded ? familyOf(loaded, loaded.base ? findModel(loaded.base) : null) : '';
    for (i = 0; i < gs.length; i++) {
      for (j = 0; j < gs[i].items.length; j++) {
        var v = gs[i].items[j];
        if (loaded && v === loaded) lg = gs[i];
        if (v.part) continue;
        var fam = familyOf(v, v.base ? findModel(v.base) : null);
        if (q && (titleOf(v) + ' ' + gs[i].title + ' ' + fam).toLowerCase().indexOf(q) < 0) continue;
        rows.push({ v: v, g: gs[i], fam: fam });
      }
    }
    rows.sort(function (a, b) { return titleOf(a.v).toLowerCase() < titleOf(b.v).toLowerCase() ? -1 : 1; });
    if (!rows.length) {
      box.appendChild(el('div', 'empty-note', q ? 'No preset or model matches.' : 'No models installed yet. Get one under Get more.'));
      return;
    }
    for (i = 0; i < rows.length; i++) box.appendChild(presetListRow(rows[i], d, lfam, lg));
  }

  function presetListRow(r, d, lfam, lg) {
    var v = r.v, g = r.g, s = stateOf(v, d);
    var miss = v.missing && v.missing.length;
    var row = el('div', 'sub' + (s === 'on' ? ' on' : ''));
    row.appendChild(g.items.length > 1 ? starButton(v, g) : el('span', 'star none'));
    var main = el('div', 'smain');
    var nm = el('div', 'sname', titleOf(v));
    if (s === 'on') nm.appendChild(el('span', 'badge ok', 'In use'));
    else if (s === 'loading') nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Files missing'));
    main.appendChild(nm);
    // the model (when the name is not the model's), and the family (when neither name says it)
    var where = [];
    if (g.title !== titleOf(v)) where.push(g.title);
    if (r.fam && (g.title + ' ' + titleOf(v)).indexOf(r.fam) < 0) where.push(r.fam);
    if (where.length) main.appendChild(el('div', 'mmeta', where.join(' · ')));
    main.appendChild(el('div', 'mmeta', subMeta(v) || 'No settings of its own'));
    row.appendChild(main);
    var acts = el('div', 'sacts');
    if (lfam && r.fam === lfam && g !== lg && !st.sw) {
      var a = actionButton('Apply settings', function () { applySettings(v); });
      a.title = 'Its settings into Create, for the loaded model; no switch';
      acts.appendChild(a);
    }
    if (s !== 'on') {
      var b = actionButton(s === 'loading' ? 'Loading…' : 'Use', function () { useModel(v); });
      b.disabled = s === 'loading' || !!miss || !!st.sw;
      acts.appendChild(b);
    }
    acts.appendChild(itemMenu(v, g, d, true));
    row.appendChild(acts);
    return row;
  }

  // A preset's settings into the form, for the model that is loaded; the seed stays.
  function applySettings(v) {
    var p = presetFor(st.model, v);
    var seed = $('seed').value;
    setForm(p);
    $('seed').value = seed;
    persist();
    setView('create');
    toast('The settings of ' + titleOf(v) + ' are in the form: ' + presetNote(p).replace(/^Defaults: /, '') +
      ' Reset brings back the loaded preset\'s.', 7000);
  }

  // The preset Use on a model loads, kept on the console (settings: default_presets). The model's
  // own preset is the default without an entry.
  function makeDefault(g, v) {
    var set = {};
    set[g.key] = v === g.main ? '' : v.id;
    req('POST', APP + '/settings', { default_presets: set }, function (code, data) {
      if (code === 200 && data && data.default_presets) {
        st.defp = data.default_presets;
        drawModels();
        drawCurrent();
        toast(shortName(v, g.title) + ' is the default of ' + g.title + '.');
      } else {
        toast(errText(code, data, 'Could not set the default'), 6000);
      }
    }, 10000);
  }

  // The defaults (settings: default_presets); null on a server without them.
  function loadDefaults() {
    req('GET', APP + '/settings', null, function (code, data) {
      var defp = code === 200 && data && data.default_presets && typeof data.default_presets === 'object' ? data.default_presets : null;
      if (JSON.stringify(defp) === JSON.stringify(st.defp)) return;
      st.defp = defp;
      drawModels();
      drawCurrent();
    }, 8000);
  }

  // ---- the ⋯ button of a row: a small menu. Escape, or a click outside it, shuts it.
  var openMenu = null;

  function closeMenu() {
    if (!openMenu) return;
    openMenu.menu.style.display = 'none';
    openMenu.btn.setAttribute('aria-expanded', 'false');
    openMenu = null;
  }

  // items: [{label, fn, danger, disabled}]; note: a line under them (why an item is off).
  function moreMenu(items, note) {
    var wrap = el('div', 'rmore');
    var b = el('button', 'more-btn', '⋯');
    b.type = 'button';
    b.title = 'More';
    b.setAttribute('aria-label', 'More options');
    b.setAttribute('aria-haspopup', 'true');
    b.setAttribute('aria-expanded', 'false');
    var menu = el('div', 'menu');
    menu.setAttribute('role', 'menu');
    menu.style.display = 'none';
    for (var i = 0; i < items.length; i++) {
      (function (it) {
        var mi = actionButton(it.label, function () { closeMenu(); it.fn(); });
        mi.setAttribute('role', 'menuitem');
        mi.className = 'menu-item' + (it.danger ? ' danger' : '');
        mi.disabled = !!it.disabled;
        menu.appendChild(mi);
      })(items[i]);
    }
    if (note) menu.appendChild(el('div', 'menu-note', note));
    b.onclick = function () {
      var was = openMenu && openMenu.menu === menu;
      closeMenu();
      if (was) return;
      menu.style.display = 'block';
      b.setAttribute('aria-expanded', 'true');
      openMenu = { menu: menu, btn: b };
    };
    wrap.appendChild(b);
    wrap.appendChild(menu);
    return wrap;
  }

  function initMenus() {
    document.addEventListener('click', function (e) {
      if (!openMenu) return;
      var t = (e && (e.target || e.srcElement)) || null;
      if (!within(openMenu.btn.parentNode, t)) closeMenu();
    });
    document.addEventListener('keydown', function (e) {
      e = e || window.event;
      if (!openMenu || !(e.keyCode === 27 || e.key === 'Escape')) return;
      var b = openMenu.btn;
      closeMenu();
      try { b.focus(); } catch (x) {}
    });
  }

  // ---- the three sub-views of the Models tab: Installed, Get more, Downloads (remembered)
  var msub = 'installed';

  function setMSub(name) {
    msub = name;
    save('mtab', name);
    applyMSub();
  }

  // Downloads is shown only where the app can download; until it is known, Installed.
  function applyMSub() {
    var want = msub === 'dl' && st.dl !== true ? 'installed' : msub;
    show('ms-dl', st.dl === true, 'block');
    var names = ['installed', 'presets', 'get', 'dl'];
    for (var i = 0; i < names.length; i++) {
      var on = names[i] === want;
      show('mv-' + names[i], on, 'flex');
      var b = $('ms-' + names[i]);
      b.className = on ? 'on' : '';
      b.setAttribute('aria-selected', on ? 'true' : 'false');
    }
  }

  function initModelsTabs() {
    var saved = load('mtab');
    msub = saved === 'get' || saved === 'dl' || saved === 'presets' ? saved : 'installed';
    $('ms-installed').onclick = function () { setMSub('installed'); };
    $('ms-presets').onclick = function () { setMSub('presets'); };
    $('pfind').oninput = drawPresetList;
    $('ms-get').onclick = function () { setMSub('get'); };
    $('ms-dl').onclick = function () { setMSub('dl'); };
    $('dl-clear').onclick = clearFinished;
    initMenus();
    applyMSub();
  }

  function variantsOf(id) {
    var out = [], list = st.mdl && st.mdl.models ? st.mdl.models : [];
    for (var i = 0; i < list.length; i++) if (list[i].kind === 'preset' && list[i].base === id) out.push(list[i]);
    return out;
  }

