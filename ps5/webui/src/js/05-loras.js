  // ---------- LoRAs ----------
  // loraList: the files in /data/PS5SD/loras ({name, path, bytes}; null until known).
  // loraSel: the ones picked for the loaded model ({path, m}), saved with its settings.
  var loraList = null;
  var loraSel = [];

  function loadLoras(cb) {
    req('GET', APP + '/loras', null, function (code, data) {
      if (code === 200 && data && data.loras) {
        loraList = data.loras;
        show('pnl-loras', true);
        drawLoraList();
        drawLoraPicker();
        if (cb) cb();
        return;
      }
      // a plain sd-server: its own list, without sizes
      req('GET', '/sdapi/v1/loras', null, function (c2, list) {
        loraList = c2 === 200 && list && list.length !== undefined ? list : [];
        drawLoraPicker();
        if (cb) cb();
      });
    });
  }

  function loraKnown(path) {
    if (!loraList) return true;  // not loaded yet: the server says if it is gone
    for (var i = 0; i < loraList.length; i++) if (loraList[i].path === path) return true;
    return false;
  }

  function loraName(path) {
    return path.replace(/^.*\//, '').replace(/\.(gguf|safetensors|ckpt|sft)$/i, '');
  }

  // The request's "lora" field: the picked LoRAs that are still there, strength 0 left out.
  function loraBody(p) {
    var out = [];
    var ls = p.loras || [];
    for (var i = 0; i < ls.length; i++) {
      if (ls[i].m !== 0 && loraKnown(ls[i].path)) out.push({ path: ls[i].path, multiplier: ls[i].m });
    }
    return out;
  }

  // The family of the loaded model ('' when it cannot be told), for the LoRA list.
  function loadedFamily() {
    var d = st.mdl;
    var m = d && d.loaded ? findModel(d.loaded) : null;
    return m ? familyOf(m, m.base ? findModel(m.base) : null) : '';
  }

  // The family of an installed LoRA: the server's (from its tensors), else its name's.
  function loraFamilyOf(path, name) {
    var list = loraList || [];
    for (var i = 0; i < list.length; i++) if (list[i].path === path && list[i].family) return list[i].family;
    return loraFamily((name || '') + ' ' + path);
  }

  // A LoRA of another family than the loaded model's: its family, else ''.
  function otherFamily(path, name, fam) {
    var lf = loraFamilyOf(path, name);
    return fam && lf && !sameFamily(lf, fam) ? lf : '';
  }

  function drawLoraPicker() {
    var box = $('lora-sel');
    clear(box);
    var fam = loadedFamily();
    var wrong = [];
    for (var i = 0; i < loraSel.length; i++) {
      (function (s) {
        var row = el('div', 'lrow');
        var gone = !loraKnown(s.path);
        var of = otherFamily(s.path, '', fam);
        if (of) wrong.push(readable(loraName(s.path)) + ' is made for ' + of);
        row.appendChild(el('span', 'lname', readable(loraName(s.path)) + (gone ? ' (not on the console)' : '')));
        var inp = document.createElement('input');
        inp.type = 'number';
        inp.min = '-2';
        inp.max = '2';
        inp.step = '0.05';
        inp.value = s.m;
        inp.setAttribute('aria-label', 'Strength of ' + loraName(s.path));
        inp.onchange = function () {
          var v = parseFloat(inp.value);
          s.m = isNaN(v) ? 1 : Math.max(-2, Math.min(2, v));
          inp.value = s.m;
          persist();
        };
        row.appendChild(inp);
        var rm = actionButton('×', function () { removeLora(s); });
        rm.setAttribute('aria-label', 'Remove ' + loraName(s.path));
        row.appendChild(rm);
        box.appendChild(row);
      })(loraSel[i]);
    }
    var add = $('lora-add');
    clear(add);
    var first = document.createElement('option');
    first.value = '';
    text(first, 'Add a LoRA…');
    add.appendChild(first);
    var free = 0;
    var list = loraList || [];
    // the ones for this model's family first; the others in a group of their own, with their family
    var others = document.createElement('optgroup');
    others.label = 'Made for other models';
    for (var k = 0; k < list.length; k++) {
      var picked = false;
      for (var n = 0; n < loraSel.length; n++) if (loraSel[n].path === list[k].path) picked = true;
      if (picked) continue;
      var o = document.createElement('option');
      o.value = list[k].path;
      var of2 = otherFamily(list[k].path, list[k].name, fam);
      text(o, readable(list[k].name || loraName(list[k].path)) + (of2 ? ' (' + of2 + ')' : ''));
      (of2 ? others : add).appendChild(o);
      free++;
    }
    if (others.childNodes.length) add.appendChild(others);
    add.value = '';
    show('lora-add', free > 0);
    text($('lora-hint'), !list.length
      ? 'No LoRAs on the console yet. Upload or download one on the Models tab.'
      : wrong.length
        ? wrong.join('; ') + ', not for this ' + fam + ' model: it does nothing here.'
        : 'Strength 1 is the full effect, 0.5 half of it. A LoRA works only with the model family it was made for.');
    $('lora-hint').className = wrong.length ? 'hint bad' : 'hint';
  }

  // The settings a fast LoRA needs (a few steps at a low CFG), by its file name; null for other LoRAs.
  function fastLoraSettings(path) {
    var n = loraName(path).toLowerCase();
    var steps = n.match(/(\d)[-_]?step/);
    if (/dmd2/.test(n)) return { label: 'DMD2', sampler: 'lcm', scheduler: 'default', steps: 4, cfg: 1 };
    if (/lightning/.test(n)) {
      return { label: 'SDXL-Lightning', sampler: 'euler', scheduler: 'sgm_uniform', steps: steps ? Number(steps[1]) : 8, cfg: 1 };
    }
    if (/causvid/.test(n)) return { label: 'CausVid', sampler: 'euler', scheduler: 'default', steps: 4, cfg: 1 };
    if (/lcm/.test(n)) return { label: 'LCM-LoRA', sampler: 'lcm', scheduler: 'default', steps: 6, cfg: 1.5 };
    return null;
  }

  function settingsText(f) {
    return (f.sampler === 'default' ? 'the default sampler' : f.sampler) +
      (f.scheduler === 'default' ? '' : ', ' + f.scheduler) + ', ' + f.steps + ' steps, CFG ' + f.cfg;
  }

  function quality() {
    return { steps: Number($('steps').value), cfg: Number($('cfg').value), sampler: $('sampler').value, scheduler: $('scheduler').value };
  }

  function setQuality(q) {
    $('steps').value = q.steps;
    $('cfg').value = q.cfg;
    setSelect('sampler', q.sampler);
    setSelect('scheduler', q.scheduler);
  }

  // A fast LoRA brings its settings and keeps the ones it replaced, which come back when it is
  // removed while its settings are still set.
  function addLora() {
    var path = $('lora-add').value;
    if (!path) return;
    var s = { path: path, m: 1 };
    var f = otherFamily(path, '', loadedFamily()) ? null : fastLoraSettings(path);
    if (f) {
      s.was = quality();
      setQuality(f);
      s.set = quality();
      toast(f.label + ' works in a few steps: set to ' + settingsText(s.set) + '.');
    }
    loraSel.push(s);
    drawLoraPicker();
    persist();
  }

  function removeLora(s) {
    for (var j = 0; j < loraSel.length; j++) if (loraSel[j] === s) { loraSel.splice(j, 1); break; }
    if (s.was && JSON.stringify(quality()) === JSON.stringify(s.set)) {
      setQuality(s.was);
      toast('The settings from before ' + fastLoraSettings(s.path).label + ' are back: ' + settingsText(s.was) + '.');
    }
    drawLoraPicker();
    persist();
  }

  // Fast LoRAs for SDXL and for Wan video (a few steps at CFG 1), one click away on the LoRAs panel.
  // Tried on the console: DMD2 on an Illustrious model, Lightning on SDXL base, CausVid on Wan 2.1 1.3B.
  var FEATURED_LORAS = [
    { file: 'dmd2_sdxl_4step_lora_fp16.safetensors', label: 'DMD2 (SDXL, anime)',
      note: 'For anime and Illustrious or Pony models. Sampler LCM, 4 to 8 steps, CFG 1. 394 MB, non-commercial licence',
      url: 'https://huggingface.co/tianweiy/DMD2/resolve/main/dmd2_sdxl_4step_lora_fp16.safetensors' },
    { file: 'sdxl_lightning_8step_lora.safetensors', label: 'SDXL-Lightning 8-step',
      note: 'For SDXL base and photo models. Sampler Euler, scheduler sgm_uniform, 8 steps, CFG 1. 394 MB',
      url: 'https://huggingface.co/ByteDance/SDXL-Lightning/resolve/main/sdxl_lightning_8step_lora.safetensors' },
    { file: 'Wan21_CausVid_bidirect2_T2V_1_3B_lora_rank32.safetensors', label: 'CausVid (Wan 2.1 1.3B video)',
      note: 'Makes Wan 2.1 1.3B videos 4 times faster: CFG 1, 4 steps, Euler. 91 MB, non-commercial licence',
      url: 'https://huggingface.co/Kijai/WanVideo_comfy/resolve/main/Wan21_CausVid_bidirect2_T2V_1_3B_lora_rank32.safetensors' }
  ];

  // The installed LoRAs (Installed), and the featured ones not installed yet (Get more).
  function drawLoraList() {
    var box = $('llist');
    clear(box);
    var list = loraList || [];
    if (!list.length) box.appendChild(el('div', 'empty-note', 'No LoRAs yet. Get one under Get more, or upload one.'));
    for (var i = 0; i < list.length; i++) {
      (function (l) {
        var raw = l.name || loraName(l.path);
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        main.appendChild(el('div', 'mname', readable(raw)));
        var lf = l.family || loraFamily(raw);
        addTags(main, lf ? [{ text: lf, cls: 'fam' }] : []);
        main.appendChild(el('div', 'mfile', l.path));
        if (l.bytes > 0) main.appendChild(el('div', 'mmeta', bytes(l.bytes)));
        row.appendChild(main);
        var acts = el('div', 'macts');
        acts.appendChild(moreMenu([{ label: 'Delete', danger: true, fn: function () { askDeleteLora(l); } }], ''));
        row.appendChild(acts);
        box.appendChild(row);
      })(list[i]);
    }
    var sug = $('lsugg');
    clear(sug);
    for (var k = 0; k < FEATURED_LORAS.length; k++) {
      (function (c) {
        for (var j = 0; j < list.length; j++) if (list[j].path === c.file) return;
        if (!sug.childNodes.length) sug.appendChild(el('div', 'ghead', 'LoRAs'));
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        main.appendChild(el('div', 'mname', c.label));
        addTags(main, loraFamily(c.label + ' ' + c.file) ? [{ text: loraFamily(c.label + ' ' + c.file), cls: 'fam' }] : []);
        main.appendChild(el('div', 'mmeta', c.note));
        row.appendChild(main);
        var acts = el('div', 'macts');
        var b = actionButton('Download', function () { startDownload(c.url, b, true, null, 'loras'); });
        b.className = 'on';
        acts.appendChild(b);
        row.appendChild(acts);
        sug.appendChild(row);
      })(FEATURED_LORAS[k]);
    }
    showSugg();
  }

  // The Get more section of featured LoRAs and upscalers: shown while one of them is not installed.
  function showSugg() {
    show('pnl-sugg', $('lsugg').childNodes.length > 0 || $('usugg').childNodes.length > 0);
  }

  function askDeleteLora(l) {
    dlgFor = { lora: l };
    dlgFrom = document.activeElement;
    text($('dlg-title'), 'Delete ' + (l.name || loraName(l.path)) + (l.bytes > 0 ? ' (' + bytes(l.bytes) + ')' : '') + '?');
    text($('dlg-body'), 'The LoRA file is deleted from the console.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function deleteLora(l) {
    req('DELETE', APP + '/loras/' + encodePath(l.path), null, function (code, data) {
      closeDlg();
      if (code === 200 || code === 404) {
        if (data && data.loras) loraList = data.loras;
        for (var i = loraSel.length - 1; i >= 0; i--) if (loraSel[i].path === l.path) loraSel.splice(i, 1);
        persist();
        drawLoraList();
        drawLoraPicker();
        if (code === 404) loadLoras(null);
        toast('Deleted ' + (l.name || loraName(l.path)) + '.');
      } else {
        toast(errText(code, data, 'Could not delete ' + (l.name || l.path)), 6000);
      }
    }, 30000);
  }

