  // ---------- saved settings: the list is in the Models tab (13-models.js); the save box is here ----------

  // What "Save" keeps: the loaded model and the form of the Create tab.
  function presetDefaults() {
    var f = getForm();
    var d = { width: String(f.w), height: String(f.h), steps: String(f.steps), cfg: String(f.cfg) };
    if (f.sampler && f.sampler !== 'default') d.sampler = f.sampler;
    if (f.scheduler && f.scheduler !== 'default') d.scheduler = f.scheduler;
    if (f.clip > 0) d.clip_skip = String(f.clip);
    var ls = [];
    for (var i = 0; i < f.loras.length; i++) if (f.loras[i].m !== 0) ls.push(f.loras[i].path + ':' + f.loras[i].m);
    if (ls.length) d.loras = ls.join(',');
    if (f.hr.on) {
      d.hires_scale = String(f.hr.scale);
      d.hires_steps = String(f.hr.steps);
      d.hires_denoise = String(f.hr.den);
      // "Latent (nearest-exact)": a default is one word, so the name goes percent-encoded
      d.hires_upscaler = f.hr.up.replace(/[^A-Za-z0-9._-]/g, function (c) {
        var h = c.charCodeAt(0).toString(16).toUpperCase();
        return '%' + (h.length < 2 ? '0' : '') + h;
      });
    }
    return d;
  }

  function drawSaveBox() {
    var ok = st.app === true && !!st.mid && !st.sw && !(st.mdl && st.mdl.standby);
    $('ps-save').disabled = !ok;
    $('ps-name').disabled = !ok;
    show('savepreset', st.app === true && !!st.mid, 'inline-block');
    if (!(st.app === true && !!st.mid)) show('ps-box', false);
    if (!ok) {
      text($('ps-what'), st.sw ? 'Wait for the model switch.' : 'Load a model first; a preset is a model with settings.');
      return;
    }
    var d = presetDefaults(), m = findModel(st.mid);
    var root = m && m.base ? findModel(m.base) : m;
    var parts = [d.width + '×' + d.height, d.steps + (d.steps === '1' ? ' step' : ' steps'), 'CFG ' + d.cfg];
    if (d.sampler) parts.push(d.sampler);
    if (d.scheduler) parts.push(d.scheduler);
    if (d.clip_skip) parts.push('CLIP skip ' + d.clip_skip);
    if (d.loras) parts.push(d.loras.split(',').length + (d.loras.split(',').length === 1 ? ' LoRA' : ' LoRAs'));
    if (d.hires_scale) parts.push(d.hires_scale === '1' ? 'refine pass' : 'hires ×' + d.hires_scale);
    text($('ps-what'), 'Model: ' + (root ? titleOf(root) : st.mid) + ' · ' + parts.join(' · '));
    if (!$('ps-name').value) $('ps-name').placeholder = 'Name, e.g. ' + (root ? titleOf(root) : 'My preset') + ' ' + d.width + '×' + d.height;
  }

  function savePreset() {
    var name = trim($('ps-name').value);
    if (!name) { toast('Give the preset a name.'); $('ps-name').focus(); return; }
    $('ps-save').disabled = true;
    req('POST', APP + '/presets', { name: name, base: st.mid, defaults: presetDefaults() }, function (code, data) {
      $('ps-save').disabled = false;
      if (code === 201 && data && data.models) {
        $('ps-name').value = '';
        show('ps-box', false);
        setModels(data);
        toast('Saved ' + name + '. Use switches to it.', 5000);
      } else {
        toast(errText(code, data, 'Could not save the preset'), 7000);
      }
    }, 15000);
  }

  var peFor = null;

  function editPreset(m) {
    req('GET', APP + '/presets/' + encodeURIComponent(m.id), null, function (code, data) {
      if (code !== 200 || !data || typeof data.text !== 'string') { toast(errText(code, data, 'Could not read the preset'), 6000); return; }
      peFor = m;
      dlgFrom = document.activeElement;
      text($('pe-title'), 'Edit ' + (m.name || m.id));
      $('pe-text').value = data.text;
      text($('pe-err'), '');
      $('pe-save').disabled = false;
      show('pe', true, 'flex');
      $('pe-text').focus();
    });
  }

  function closeEdit() {
    show('pe', false);
    peFor = null;
    if (dlgFrom) { try { dlgFrom.focus(); } catch (e) {} }
  }

  function saveEdit() {
    var m = peFor;
    if (!m) return;
    $('pe-save').disabled = true;
    req('POST', APP + '/presets/' + encodeURIComponent(m.id), { text: $('pe-text').value }, function (code, data) {
      $('pe-save').disabled = false;
      if (code === 200 && data && data.models) {
        closeEdit();
        setModels(data);
        toast('Saved. ' + (st.mdl && st.mdl.loaded === m.id ? 'New defaults apply when you press Use defaults; other options at the next load.' : ''), 6000);
      } else {
        text($('pe-err'), errText(code, data, 'Could not save'));
      }
    }, 15000);
  }

  function askDeletePreset(m) {
    dlgFor = { preset: m };
    dlgFrom = document.activeElement;
    text($('dlg-title'), 'Delete the preset ' + (m.name || m.id) + '?');
    text($('dlg-body'), 'Only the preset goes. The model and its files stay.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function deletePreset(m) {
    st.delErr = null;
    req('DELETE', APP + '/presets/' + encodeURIComponent(m.id), null, function (code, data) {
      closeDlg();
      if (code === 200 && data && data.models) {
        setModels(data);
        toast('Deleted the preset ' + (m.name || m.id) + '.');
      } else {
        st.delErr = { id: m.id, msg: errText(code, data, 'Could not delete') };
        mlistSig = null;
        loadModels(null);
      }
    }, 15000);
  }

  // Why this model cannot be deleted (shown in its row), or ''.
  function deleteBlock(m, d) {
    if (d.loaded === m.id || d.active === m.id) return 'Switch to another model first to delete this one.';
    var inUse = findModel(d.loaded) || findModel(d.active);
    if (inUse && inUse.base === m.id) return 'A preset of this model is in use. Switch to another model first.';
    if (m.kind === 'file' && m.files && m.files.length) {
      for (var i = 0; i < d.models.length; i++) {
        var o = d.models[i], used = o.files || [];
        if (o.kind !== 'preset' || o.base) continue;
        for (var j = 0; j < used.length; j++) {
          if (used[j] === m.files[0]) return 'Part of the preset ' + (o.name || o.id) + '. Delete that preset instead.';
        }
      }
    }
    return '';
  }

  // The confirm box: Cancel has the focus, so a stray press of X does not delete anything.
  var dlgFor = null;
  var dlgFrom = null;

  function askDelete(m) {
    dlgFor = m;
    dlgFrom = document.activeElement;
    text($('dlg-title'), 'Delete ' + (m.name || m.id) + (m.bytes > 0 ? ' (' + bytes(m.bytes) + ')' : '') + '?');
    var vs = variantsOf(m.id);
    text($('dlg-body'), 'Files another model uses are kept.' + (vs.length ? ' Its ' + (vs.length === 1 ? 'preset' : vs.length + ' presets') + ' (' +
      vs.map(function (v) { return v.name || v.id; }).join(', ') + ') ' + (vs.length === 1 ? 'goes' : 'go') + ' too.' : ''));
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function closeDlg() {
    show('dlg', false);
    dlgFor = null;
    if (dlgFrom) { try { dlgFrom.focus(); } catch (e) {} }
  }

  function sizeOfFile(path) {
    var list = st.mdl && st.mdl.models ? st.mdl.models : [];
    for (var i = 0; i < list.length; i++) {
      if (list[i].kind === 'file' && list[i].files && list[i].files[0] === path) return Number(list[i].bytes) || 0;
    }
    return -1;
  }

  function confirmDelete() {
    var m = dlgFor;
    if (!m) return;
    $('dlg-ok').disabled = true;
    $('dlg-cancel').disabled = true;
    text($('dlg-ok'), 'Deleting…');
    if (m.image) { deleteImage(m.image); return; }
    if (m.images) { deleteImages(m.images); return; }
    if (m.lora) { deleteLora(m.lora); return; }
    if (m.preset) { deletePreset(m.preset); return; }
    if (m.upscaler) { deleteUpscaler(m.upscaler); return; }
    if (m.authOff) { authOff(); return; }
    st.delErr = null;
    req('DELETE', APP + '/models/' + encodeURIComponent(m.id), null, function (code, data) {
      closeDlg();
      if (code === 200 && data && data.models) {
        // what was freed: the size of the entry, less the shared files that stayed (when we know their size)
        var kept = data.kept ? data.kept.length : 0;
        var freed = Number(m.bytes) || 0, known = true;
        for (var i = 0; i < kept; i++) {
          var sz = sizeOfFile(data.kept[i]);
          if (sz < 0) known = false; else freed -= sz;
        }
        setModels(data);
        loadCatalog();
        toast('Deleted ' + (m.name || m.id) + (freed > 0 ? ', freed ' + (known ? '' : 'up to ') + bytes(freed) : '') + '.' +
          (kept ? ' ' + kept + ' shared file' + (kept === 1 ? '' : 's') + ' kept.' : ''), 5000);
      } else {
        st.delErr = { id: m.id, msg: errText(code, data, 'Could not delete') };
        mlistSig = null;
        if (code === 404 || code === 409) loadModels(null); else drawModels();
      }
    }, 60000);
  }

  function useModel(m) {
    if (st.sw || swPost) return;
    swPost = true;
    var waits = st.busy || st.others;
    req('POST', APP + '/models/active', { id: m.id }, function (code, data) {
      swPost = false;
      if (code === 200 && st.mid && st.mid !== m.id) {
        // a preset of the model that is loaded: no restart, only its settings
        st.announce = titleOf(m);
        if (data && data.models) setModels(data);
        loadInfo();
      } else if (code === 200) {
        toast(titleOf(m) + ' is loaded already.');
        if (data && data.models) setModels(data);
      } else if (code === 202) {
        beginSwitch(m.id, titleOf(m), waits);
        if (data && data.models) setModels(data);
      } else {
        toast(errText(code, data, 'Could not switch'), code === 422 ? 12000 : 6000);
        loadModels(null);
      }
    }, 15000);
  }

  // The switch: the server restarts, so the API is gone for a while. Poll the library
  // every two seconds and treat errors as "still restarting".
  function beginSwitch(id, name, waitJob) {
    var m = findModel(id);
    var sw = { id: id, name: name || (m ? titleOf(m) : id), t0: new Date().getTime(), ticks: 0, polling: false, down: false, waitJob: !!waitJob };
    st.sw = sw;
    st.announce = '';
    updateGo();
    setPill();
    drawModels();
    drawCurrent();
    swLoop(sw);
  }

  function swLoop(sw) {
    if (st.sw !== sw) return;
    var secs = (new Date().getTime() - sw.t0) / 1000;
    if (secs > SWITCH_GIVE_UP) {
      st.sw = null;
      updateGo();
      setPill();
      drawModels();
      drawCurrent();
      toast('Stopped waiting for ' + sw.name + '. Reload this page when the console is back.', 6000);
      return;
    }
    var note = sw.waitJob ? 'Waiting for the running image, then restarting' : (sw.down ? 'The server is restarting' : 'Starting the switch');
    note += ' · ' + clock(secs);
    if (secs > SWITCH_SLOW) note += ' · This takes longer than usual. If the model does not load, the previous one comes back.';
    text($('swsub'), note);
    text($('cm-sub'), note);
    if (!sw.polling && sw.ticks % 2 === 0) swPoll(sw);
    sw.ticks++;
    setTimeout(function () { swLoop(sw); }, 1000);
  }

  function swPoll(sw) {
    sw.polling = true;
    req('GET', APP + '/models', null, function (code, data) {
      sw.polling = false;
      if (st.sw !== sw) return;
      if (code !== 200 || !data || !data.models) { sw.down = true; return; }
      sw.down = false;
      var secs = (new Date().getTime() - sw.t0) / 1000;
      if (!data.switching && data.loaded === sw.id) { endSwitch(true, data); return; }
      // the app puts the last working model back when the new one does not load
      if (!data.switching && data.loaded && data.active !== sw.id && secs > 4) { endSwitch(false, data); return; }
      // ...or in standby, when there was no model to go back to
      if (!data.switching && data.standby && secs > 4) { endSwitch(false, data); return; }
      setModels(data);
    }, 5000);
  }

  function endSwitch(ok, data) {
    var sw = st.sw;
    st.sw = null;
    st.announce = ok ? sw.name : '';
    setModels(data);
    updateGo();
    setPill();
    if (ok) {
      toast(sw.name + ' is ready.');
    } else {
      var back = data.loaded ? findModel(data.loaded) : null;
      var why = data.load_error && data.load_error.id === sw.id ? data.load_error.message : '';
      toast((why || 'Could not load ' + sw.name + '.') + ' ' + (data.loaded ? 'Back on ' + (back ? titleOf(back) : data.loaded) + '.'
        : why ? 'No model is loaded now.' : 'No model is loaded now; /data/PS5SD/app.log says why.'), why ? 14000 : 7000);
    }
    loadInfo();
  }

  // The model in use, on the Create tab and in the header.
  function drawCurrent() {
    var sw = st.sw;
    var standby = !sw && !!(st.mdl && st.mdl.standby);
    var label = sw ? 'Switching to ' + sw.name + '…' : standby ? 'No model loaded' : st.label;
    if (label) {
      text($('cm-name'), label);
      text($('model'), label);
    }
    show('cm-change', st.app !== false, 'block');
    show('cm-bar', !!sw);
    if (standby) text($('cm-sub'), 'Download a model or pick one under Models, then press Use.');
    show('cm-sub', !!sw || standby);
    show('swbox', !!sw);
    if (sw) text($('swtext'), 'Switching to ' + sw.name + '…');
    drawModelBlock();
    scheduleFit();
  }

