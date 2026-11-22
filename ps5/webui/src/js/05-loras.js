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
        civUpdate();  // Installed on the Civitai results
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

  // The request's "lora" field: the picked LoRAs that are on and still there, strength 0 left out.
  function loraBody(p) {
    var out = [];
    var ls = p.loras || [];
    for (var i = 0; i < ls.length; i++) {
      if (ls[i].m !== 0 && !ls[i].off && loraKnown(ls[i].path)) out.push({ path: ls[i].path, multiplier: ls[i].m });
    }
    return out;
  }

  // The Style group's summary: "2 LoRAs", "2 LoRAs, 1 off".
  function loraSummary() {
    if (!loraSel.length) return 'none';
    var off = 0;
    for (var i = 0; i < loraSel.length; i++) if (loraSel[i].off) off++;
    return loraSel.length + (loraSel.length === 1 ? ' LoRA' : ' LoRAs') + (off ? ', ' + off + ' off' : '');
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
        var row = el('div', 'lrow' + (s.off ? ' off' : ''));
        var gone = !loraKnown(s.path);
        var of = otherFamily(s.path, '', fam);
        if (of && !s.off) wrong.push(readable(loraName(s.path)) + ' is made for ' + of);
        // on or off: an off LoRA stays in the list with its strength, but is not sent
        var sw = document.createElement('input');
        sw.type = 'checkbox';
        sw.className = 'lon';
        sw.checked = !s.off;
        sw.title = 'On or off; the strength stays';
        sw.setAttribute('aria-label', 'Use ' + loraName(s.path));
        sw.onchange = function () {
          s.off = !sw.checked;
          // a fast LoRA takes its few-step settings along, as Add and × do
          var f = s.was ? fastLoraSettings(s.path) : null;
          if (f && s.off && JSON.stringify(quality()) === JSON.stringify(s.set)) {
            setQuality(s.was);
            toast('Off: the settings from before ' + f.label + ' are back: ' + settingsText(s.was) + '.');
          } else if (f && !s.off) {
            s.was = quality();
            setQuality(f);
            s.set = quality();
            toast(f.label + ' is on again: ' + settingsText(s.set) + '.');
          }
          summary();
          drawLoraPicker();
          persist();
        };
        row.appendChild(sw);
        var nm = el('div', 'lname');
        nm.appendChild(document.createTextNode(readable(loraName(s.path)) + (gone ? ' (not on the console)' : '')));
        // a LoRA with trigger words: one press puts the ones missing at the end of the prompt
        if (loraTrigger(s.path)) {
          var tb = el('button', 'linkbtn ltrig', 'Add trigger words');
          tb.type = 'button';
          tb.title = 'Put its trigger words at the cursor of the prompt: ' + loraTrigger(s.path);
          tb.onclick = function () { addTriggerWords(s.path); };
          tb.setAttribute('data-path', s.path);
          nm.appendChild(tb);
        }
        row.appendChild(nm);
        var inp = document.createElement('input');
        inp.type = 'number';
        // -100 to 100, as ComfyUI's LoRA loader: some LoRAs are made for strengths far past 1 (sliders
        // that go from -8 to 8, issue 18). sd.cpp takes any strength.
        inp.min = '-100';
        inp.max = '100';
        inp.step = '0.05';
        inp.value = s.m;
        inp.setAttribute('aria-label', 'Strength of ' + loraName(s.path));
        inp.onchange = function () {
          var v = parseFloat(inp.value);
          s.m = isNaN(v) ? 1 : Math.max(-100, Math.min(100, v));
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
    drawLoraAdd(fam);
    var list = loraList || [];
    text($('lora-hint'), !list.length
      ? 'No LoRAs on the console yet. Upload or download one on the Models tab.'
      : wrong.length
        ? wrong.join('; ') + ', not for this ' + fam + ' model: it does nothing here.'
        : 'Strength 1 is the full effect, 0.5 half of it. A LoRA works only with the model family it was made for.');
    $('lora-hint').className = wrong.length ? 'hint bad' : 'hint';
    updateTriggerButtons();
  }

  // The LoRAs not picked yet, in the order the picker shows them: the ones for the loaded model's family
  // first. [{path, label, other (its family when it is not the model's), hay}]
  function loraChoices(fam) {
    var out = [], list = loraList || [];
    for (var k = 0; k < list.length; k++) {
      var picked = false;
      for (var n = 0; n < loraSel.length; n++) if (loraSel[n].path === list[k].path) picked = true;
      if (picked) continue;
      var label = readable(list[k].name || loraName(list[k].path));
      var of2 = otherFamily(list[k].path, list[k].name, fam);
      out.push({ path: list[k].path, label: label, other: of2, mine: !of2,
        hay: (label + ' ' + list[k].path + ' ' + (list[k].trigger || '') + ' ' + (list[k].family || '')).toLowerCase() });
    }
    return out;
  }

  // Add a LoRA: a list up to 8 LoRAs; with more, a field that filters them as you type.
  function drawLoraAdd(fam) {
    var add = $('lora-add');
    clear(add);
    var first = document.createElement('option');
    first.value = '';
    text(first, 'Add a LoRA…');
    add.appendChild(first);
    var ch = loraChoices(fam);
    var many = (loraList || []).length > LORA_SEARCH_FROM;
    // the ones for this model's family first; the others in a group of their own, with their family
    var others = document.createElement('optgroup');
    others.label = 'Made for other models';
    for (var i = 0; i < ch.length; i++) {
      var o = document.createElement('option');
      o.value = ch[i].path;
      text(o, ch[i].label + (ch[i].other ? ' (' + ch[i].other + ')' : ''));
      (ch[i].other ? others : add).appendChild(o);
    }
    if (others.childNodes.length) add.appendChild(others);
    add.value = '';
    show('lora-add', ch.length > 0 && !many);
    show('lpick', ch.length > 0 && many);
    if (many) lpopDraw();
  }

  // ---- the search field of Add a LoRA (more than LORA_SEARCH_FROM LoRAs): a list under the field that
  // follows what is typed. Up and Down pick, Enter adds, Escape closes; a tap on a LoRA adds it.
  var LORA_SEARCH_FROM = 8;
  var lpop = { open: false, hits: [], at: -1 };

  function lpopDraw() {
    var box = $('lpop');
    if (!lpop.open) { show('lpop', false); $('lora-q').setAttribute('aria-expanded', 'false'); return; }
    clear(box);
    var fam = loadedFamily();
    var words = trim($('lora-q').value).toLowerCase().split(/\s+/), ch = loraChoices(fam), hits = [], i, j;
    for (i = 0; i < ch.length; i++) {
      var ok = true;
      for (j = 0; j < words.length; j++) if (words[j] && ch[i].hay.indexOf(words[j]) < 0) ok = false;
      if (ok) hits.push(ch[i]);
    }
    hits.sort(function (a, b) {
      if (a.mine !== b.mine) return a.mine ? -1 : 1;
      return a.label.toLowerCase() < b.label.toLowerCase() ? -1 : a.label.toLowerCase() > b.label.toLowerCase() ? 1 : 0;
    });
    lpop.hits = hits;
    if (lpop.at >= hits.length) lpop.at = hits.length - 1;
    if (lpop.at < 0 && hits.length) lpop.at = 0;
    var group = null;
    for (i = 0; i < hits.length; i++) {
      (function (h, idx) {
        var g = h.mine ? (fam ? 'For ' + fam + ' (the model in use)' : 'LoRAs') : 'Made for other models';
        if (g !== group) { group = g; var gh = el('div', 'lgroup', g); gh.setAttribute('role', 'presentation'); box.appendChild(gh); }
        var o = el('div', 'lopt' + (idx === lpop.at ? ' on' : ''));
        o.id = 'lopt-' + idx;
        o.setAttribute('role', 'option');
        o.setAttribute('aria-selected', idx === lpop.at ? 'true' : 'false');
        o.appendChild(el('span', 'lon2', h.label));
        if (h.other) o.appendChild(el('span', 'lof', h.other));
        // mousedown: before the field loses focus (a tap on a phone gives one as well)
        o.onmousedown = function (e) { if (e && e.preventDefault) e.preventDefault(); lpopPick(idx); };
        box.appendChild(o);
      })(hits[i], i);
    }
    if (!hits.length) box.appendChild(el('div', 'lgroup', 'No LoRA has "' + trim($('lora-q').value) + '" in its name.'));
    show('lpop', true);
    $('lora-q').setAttribute('aria-expanded', 'true');
    if (lpop.at >= 0) $('lora-q').setAttribute('aria-activedescendant', 'lopt-' + lpop.at);
    else $('lora-q').removeAttribute('aria-activedescendant');
  }

  function lpopOpen(on) {
    lpop.open = on;
    if (on) lpop.at = 0;
    lpopDraw();
  }

  function lpopPick(idx) {
    var h = lpop.hits[idx];
    if (!h) return;
    $('lora-q').value = '';
    lpop.open = false;
    addLora(h.path);
    try { $('lora-q').blur(); } catch (e) {}
  }

  function lpopKey(e) {
    e = e || window.event;
    var k = e.keyCode;
    if (k === 40 || k === 38) {
      if (!lpop.open) { lpopOpen(true); }
      else if (lpop.hits.length) {
        lpop.at = (lpop.at + (k === 40 ? 1 : lpop.hits.length - 1)) % lpop.hits.length;
        lpopDraw();
        var on = $('lpop').getElementsByClassName('on')[0];
        if (on && on.scrollIntoView) on.scrollIntoView({ block: 'nearest' });
      }
    } else if (k === 13) {
      if (lpop.open && lpop.at >= 0) lpopPick(lpop.at);
    } else if (k === 27) {
      if (!lpop.open) return true;
      lpop.open = false;
      lpopDraw();
    } else {
      return true;
    }
    if (e.preventDefault) e.preventDefault();
    if (e.stopPropagation) e.stopPropagation();
    return false;
  }

  function initLoraPicker() {
    var q = $('lora-q');
    q.onfocus = function () { lpopOpen(true); };
    q.onclick = function () { if (!lpop.open) lpopOpen(true); };
    q.oninput = function () { lpop.open = true; lpop.at = 0; lpopDraw(); };
    q.onkeydown = lpopKey;
    q.onblur = function () { setTimeout(function () { if (lpop.open && document.activeElement !== q) { lpop.open = false; lpopDraw(); } }, 150); };
    if ($('prompt').addEventListener) $('prompt').addEventListener('input', updateTriggerButtons);
  }

  // ---- trigger words: the words of the LoRA that are not in the prompt yet, at its end
  function missingWords(path) {
    var words = loraTrigger(path).split(','), p = $('prompt').value.toLowerCase(), out = [];
    for (var i = 0; i < words.length; i++) {
      var w = trim(words[i]);
      if (w && p.indexOf(w.toLowerCase()) < 0) out.push(w);
    }
    return out;
  }

  // At the caret of the prompt (09h remembers the last one), else at its end.
  function addTriggerWords(path) {
    var miss = missingWords(path);
    if (!miss.length) { toast('Its trigger words are in the prompt already.'); return; }
    putAtCaret(miss.join(', '), ', ');
    toast('Added to the prompt: ' + miss.join(', ') + '.');
  }

  // Each Add trigger words button says whether there is something to add.
  function updateTriggerButtons() {
    var bs = $('lora-sel').getElementsByClassName('ltrig');
    for (var i = 0; i < bs.length; i++) {
      var via = hasLoraPlaceholder($('prompt').value);
      var none = missingWords(bs[i].getAttribute('data-path')).length === 0;
      bs[i].disabled = via || none;
      text(bs[i], via ? 'Via __lora__' : none ? 'Words are in the prompt' : 'Add trigger words');
    }
      drawWildHint();   // __lora__ says how many words there are
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
  function addLora(path) {
    if (!path) return;
    // its preferred strength (from its info) is where it starts
    var w = loraWeight(path);
    var s = { path: path, m: w || 1 };
    var f = otherFamily(path, '', loadedFamily()) ? null : fastLoraSettings(path);
    if (f) {
      s.was = quality();
      setQuality(f);
      s.set = quality();
      toast(f.label + ' works in a few steps: set to ' + settingsText(s.set) + '.');
    } else if (w) {
      toast('Strength ' + w + ' is the one its info suggests.');
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

  // The installed LoRAs (Installed) by the family they are made for, the loaded model's first, each
  // group by name; a filter field once there are many. The featured ones not installed yet: Get more.
  function drawLoraList() {
    var box = $('llist');
    clear(box);
    var list = loraList || [];
    show('lfind', list.length > 8, 'block');
    drawLookAll();
    if (!list.length) box.appendChild(el('div', 'empty-note', 'No LoRAs yet. Get one under Get more, or upload one.'));
    var q = trim($('lfind').value || '').toLowerCase();
    var fam = loadedFamily(), groups = {}, order = [], i;
    for (i = 0; i < list.length; i++) {
      var l = list[i], raw = l.name || loraName(l.path);
      if (q && (readable(raw) + ' ' + l.path).toLowerCase().indexOf(q) < 0) continue;
      var lf = l.family || loraFamily(raw) || '';
      var key = fam && lf && sameFamily(lf, fam) ? fam : lf;
      if (!groups[key]) { groups[key] = []; order.push(key); }
      groups[key].push(l);
    }
    order.sort(function (a, b) {
      if (a === fam) return -1;
      if (b === fam) return 1;
      if (!a) return 1;
      if (!b) return -1;
      return a < b ? -1 : 1;
    });
    for (i = 0; i < order.length; i++) {
      var g = groups[order[i]];
      g.sort(function (a, b) {
        return readable(a.name || loraName(a.path)).toLowerCase() < readable(b.name || loraName(b.path)).toLowerCase() ? -1 : 1;
      });
      var head = order[i] ? 'For ' + order[i] : 'Family not known';
      if (order[i] && order[i] === fam) head += ' (the model in use)';
      box.appendChild(el('div', 'ghead', head + ' · ' + g.length));
      for (var k = 0; k < g.length; k++) box.appendChild(loraRow(g[k]));
    }
    if (q && !order.length) box.appendChild(el('div', 'empty-note', 'No LoRA has "' + q + '" in its name.'));
    var sug = $('lsugg');
    clear(sug);
    for (var c = 0; c < FEATURED_LORAS.length; c++) {
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
      })(FEATURED_LORAS[c]);
    }
    showSugg();
  }

  // One LoRA: its picture (when it has one), name, trigger words, size (and folder), and ⋯: Info, Look up
  // online, Delete.
  function loraRow(l) {
    var row = el('div', 'mrow mgrp lora');
    if (l.preview) {
      var pic = el('div', 'lthumb');
      var im = document.createElement('img');
      im.alt = '';
      im.src = loraPreviewUrl(l.path);
      im.setAttribute('loading', 'lazy');
      pic.appendChild(im);
      blurPicture(pic, l.preview_nsfw_level);
      if (loraTools()) { pic.title = 'Info'; var open = pic.onclick; pic.onclick = function (e) { if (open && open(e) === false) return false; openLoraInfo(l); return false; }; }
      row.appendChild(pic);
    }
    var main = el('div', 'mmain');
    main.appendChild(el('div', 'mname', readable(l.name || loraName(l.path))));
    if (l.trigger) main.appendChild(el('div', 'mmeta ltrigline', 'Trigger words: ' + l.trigger));
    var meta = [];
    if (l.bytes > 0) meta.push(bytes(l.bytes));
    if (l.base) meta.push(l.base);
    if (l.path.indexOf('/') > 0) meta.push(l.path.replace(/\/[^\/]*$/, '/'));
    if (loraBusy[l.path]) meta.push('Looking up…');
    if (meta.length) main.appendChild(el('div', 'mmeta', meta.join(' · ')));
    row.appendChild(main);
    var acts = el('div', 'macts');
    var items = [];
    if (loraTools()) {
      items.push({ label: 'Info', fn: function () { openLoraInfo(l); } });
      items.push({ label: 'Look up online', disabled: !!loraBusy[l.path] || !!look, fn: function () { lookupOne(l); } });
    }
    items.push({ label: 'Delete', danger: true, fn: function () { askDeleteLora(l); } });
    acts.appendChild(moreMenu(items, ''));
    row.appendChild(acts);
    return row;
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

