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
    drawSaveBox();
    drawCurrent();
    drawInit();  // the reference edit shows for FLUX.2 only
  }

  // The Installed sub-view: image models, then video models (each row with its saved settings under it).
  function drawModels() {
    var d = st.mdl;
    var sig = String(st.app) + '|' + JSON.stringify(d) + '|' + (st.sw ? st.sw.id : '') + '|' + (st.delErr ? st.delErr.id + st.delErr.msg : '') + '|' + (st.video ? 'v' : '');
    if (sig === mlistSig) return;
    mlistSig = sig;
    var box = $('mlist'), vbox = $('vlist');
    clear(box);
    clear(vbox);
    show('gh-vid', false);
    if (st.app === false) {
      text($('mnote'), 'This version of the PS5SD app cannot list or switch models.');
      return;
    }
    text($('mnote'), 'Switching restarts the server: 10 to 20 seconds, about a minute for SDXL. A saved setting of the loaded model switches at once.');
    if (!d) {
      box.appendChild(el('div', 'empty-note', 'Loading…'));
      return;
    }
    if (!d.models.length) {
      box.appendChild(el('div', 'empty-note', 'No models installed yet. Get one under Get more.'));
      return;
    }
    var roots = rootsOf(d), imgs = 0, vids = 0;
    for (var i = 0; i < roots.length; i++) {
      if (videoOf(roots[i], d)) { vbox.appendChild(modelItem(roots[i], d)); vids++; }
      else { box.appendChild(modelItem(roots[i], d)); imgs++; }
    }
    if (!imgs) box.appendChild(el('div', 'empty-note', 'No image models installed yet. Get one under Get more.'));
    show('gh-vid', vids > 0);
  }

  // The rows of the list: the models (a checkpoint, or a preset of its own). A saved setting (a preset
  // with a base) sits under its model; one whose model is not listed stands alone.
  function rootsOf(d) {
    var out = [], ids = {}, i, o;
    for (i = 0; i < d.models.length; i++) {
      o = d.models[i];
      if (!(o.kind === 'preset' && o.base) && isModel(o, d)) ids[o.id] = true;
    }
    for (i = 0; i < d.models.length; i++) {
      o = d.models[i];
      if (o.kind === 'preset' && o.base && ids[o.base]) continue;
      if (ids[o.id] || (o.kind === 'preset' && o.base)) out.push(o);
    }
    return out;
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

  // The size and the default size and steps, in one muted line.
  function specOf(m) {
    var parts = [bytes(m.bytes)];
    var df = m.defaults || {};
    if (df.width && df.height) parts.push(df.width + '×' + df.height);
    if (df.steps) parts.push(df.steps + (String(df.steps) === '1' ? ' step' : ' steps'));
    return parts.join(' · ');
  }

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

  // One model with its saved settings under it.
  function modelItem(m, d) {
    var vars = variantsOf(m.id);
    var loaded = d.loaded === m.id || inList(vars, d.loaded);
    var loading = !loaded && ((!!st.sw && st.sw.id === m.id) || d.active === m.id || inList(vars, d.active));
    var miss = m.missing && m.missing.length ? m.missing : null;
    var row = el('div', 'mrow mwrap' + (loaded ? ' on' : '') + (miss && !loaded ? ' dim' : ''));
    var main = el('div', 'mmain');
    var nm = el('div', 'mname', titleOf(m));
    if (loaded) nm.appendChild(el('span', 'badge ok', 'In use'));
    else if (loading) nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Incomplete'));
    if (m.part) nm.appendChild(el('span', 'badge warn', 'Part of a model'));
    main.appendChild(nm);
    if (m.kind === 'file' && m.files && m.files[0]) main.appendChild(el('div', 'mfile', baseName(m.files[0])));
    // only the diffusion model, text encoder or VAE: it runs in a preset that names the others
    if (m.part) main.appendChild(el('div', 'mmeta', 'Only the ' + m.part + ' of a model, so it cannot run on its own. ' +
      'Get more installs a model with all its files and a preset; Help, Models: Presets shows how to write one.'));
    addTags(main, tagsOf(m.name || m.id, m.files, videoOf(m, d), !!m.control_net, m.family));
    main.appendChild(el('div', 'mmeta', specOf(m)));
    if (miss) {
      var lost = [];
      for (var i = 0; i < miss.length; i++) lost.push(baseName(miss[i]));
      main.appendChild(el('div', 'mmeta bad', 'Missing: ' + lost.join(', ')));
    }
    if (st.delErr && st.delErr.id === m.id) main.appendChild(el('div', 'mmeta bad', st.delErr.msg));
    row.appendChild(main);
    var acts = el('div', 'macts');
    if (!loaded) {
      var usable = !loading && !miss && !st.sw && !m.part;
      var b = actionButton(loading ? 'Loading…' : 'Use', function () { useModel(m); });
      b.disabled = !usable;
      if (usable) b.className = 'on';
      acts.appendChild(b);
    }
    var why = deleteBlock(m, d);
    acts.appendChild(moreMenu([{ label: 'Delete', danger: true, disabled: !!why || !!st.sw, fn: function () { askDelete(m); } }], why));
    row.appendChild(acts);
    var item = el('div', 'mitem');
    item.appendChild(row);
    if (vars.length) item.appendChild(savedList(vars, d));
    return item;
  }

  function savedList(vars, d) {
    var box = el('div', 'subs');
    box.appendChild(el('div', 'subhead', vars.length + (vars.length === 1 ? ' saved setting' : ' saved settings')));
    for (var i = 0; i < vars.length; i++) box.appendChild(savedRow(vars[i], d));
    return box;
  }

  // One saved setting: its name, its defaults, and Use, Edit and Delete.
  function savedRow(v, d) {
    var loaded = d.loaded === v.id;
    var loading = !loaded && ((!!st.sw && st.sw.id === v.id) || d.active === v.id);
    var miss = v.missing && v.missing.length;
    var row = el('div', 'sub' + (loaded ? ' on' : ''));
    var main = el('div', 'smain');
    var nm = el('div', 'sname', v.name || v.id);
    if (loaded) nm.appendChild(el('span', 'badge ok', 'In use'));
    else if (loading) nm.appendChild(el('span', 'badge warn', 'Loading…'));
    if (miss) nm.appendChild(el('span', 'badge bad', 'Model missing'));
    main.appendChild(nm);
    var meta = subMeta(v);
    if (meta) main.appendChild(el('div', 'mmeta', meta));
    if (st.delErr && st.delErr.id === v.id) main.appendChild(el('div', 'mmeta bad', st.delErr.msg));
    row.appendChild(main);
    var acts = el('div', 'sacts');
    if (!loaded) {
      var usable = !loading && !miss && !st.sw;
      var b = actionButton(loading ? 'Loading…' : 'Use', function () { useModel(v); });
      b.disabled = !usable;
      if (usable) b.className = 'on';
      acts.appendChild(b);
    }
    var busy = loaded || d.active === v.id;
    acts.appendChild(moreMenu([
      { label: 'Edit', fn: function () { editPreset(v); } },
      { label: 'Delete', danger: true, disabled: busy || !!st.sw, fn: function () { askDeletePreset(v); } }
    ], busy ? 'Switch to another model or setting to delete this one.' : ''));
    row.appendChild(acts);
    return row;
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
    var names = ['installed', 'get', 'dl'];
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
    msub = saved === 'get' || saved === 'dl' ? saved : 'installed';
    $('ms-installed').onclick = function () { setMSub('installed'); };
    $('ms-get').onclick = function () { setMSub('get'); };
    $('ms-dl').onclick = function () { setMSub('dl'); };
    $('dl-clear').onclick = clearFinished;
    initMenus();
    applyMSub();
  }

  // The Models tab lists models: checkpoints and presets without a base (a model in several files,
  // or one with its own options), but not a checkpoint such a preset loads (the preset stands for it).
  // A preset with a base is a saved preset: it is on the Presets tab.
  function isModel(m, d) {
    if (m.kind === 'preset') return !m.base;
    var f = m.files && m.files[0];
    for (var i = 0; i < d.models.length; i++) {
      var o = d.models[i];
      if (o.kind !== 'preset' || o.base || !o.files) continue;
      for (var j = 0; j < o.files.length; j++) if (o.files[j] === f) return false;
    }
    return true;
  }

  function variantsOf(id) {
    var out = [], list = st.mdl && st.mdl.models ? st.mdl.models : [];
    for (var i = 0; i < list.length; i++) if (list[i].kind === 'preset' && list[i].base === id) out.push(list[i]);
    return out;
  }

