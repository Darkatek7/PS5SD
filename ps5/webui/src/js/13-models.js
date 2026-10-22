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
    drawPresets();
    drawCurrent();
  }

  function drawModels() {
    var d = st.mdl;
    var sig = String(st.app) + '|' + JSON.stringify(d) + '|' + (st.sw ? st.sw.id : '') + '|' + (st.delErr ? st.delErr.id + st.delErr.msg : '');
    if (sig === mlistSig) return;
    mlistSig = sig;
    var box = $('mlist');
    clear(box);
    if (st.app === false) {
      text($('mnote'), 'This version of the PS5SD app cannot list or switch models.');
      return;
    }
    text($('mnote'), 'Switching to another model restarts the server and takes about 10 to 20 seconds (SDXL about a minute); an image that is being made is finished first. A preset of the loaded model switches at once.');
    if (!d) {
      box.appendChild(el('div', 'empty-note', 'Loading…'));
      return;
    }
    if (!d.models.length) {
      box.appendChild(el('div', 'empty-note', 'No models installed yet. Add one below.'));
      return;
    }
    var shown = 0;
    for (var i = 0; i < d.models.length; i++) {
      if (!isModel(d.models[i], d)) continue;
      box.appendChild(modelRow(d.models[i], d));
      shown++;
    }
    if (!shown) box.appendChild(el('div', 'empty-note', 'No models installed yet. Add one below.'));
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

