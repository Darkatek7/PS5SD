  // ---------- what made an image: the viewer's prompt and settings, and "Use all" ----------
  // GET /ps5sd/v1/gallery/info/<name>: the parameters saved with the file (A1111's text, which other tools
  // read too) and sd.cpp's JSON of the request in it (sdcpp). Files saved before 0.9.0 and upscales have none.
  var infoCache = {};

  function round2(x) { return Math.round(Number(x) * 100) / 100; }

  function baseName(path) { return String(path || '').split('/').pop(); }

  // The model file of a request: the diffusion model (FLUX, Wan, Z-Image), else the checkpoint.
  function infoModel(j) {
    var m = j.models || {};
    return m.diffusion_model || m.model || '';
  }

  // One line: model · size · steps · CFG · sampler · seed · LoRAs · hires · frames.
  function settingsLine(j) {
    var s = j.sampling || {}, g = s.guidance || {}, bits = [];
    if (j.ps5sd && j.ps5sd.preset) bits.push('preset ' + j.ps5sd.preset);
    if (infoModel(j)) bits.push(infoModel(j));
    if (j.ps5sd && j.ps5sd.seconds > 0) bits.push(took(j.ps5sd.seconds));
    bits.push(j.width + '×' + j.height);
    bits.push(s.steps + (s.steps === 1 ? ' step' : ' steps'), 'CFG ' + round2(g.txt_cfg));
    if (s.method) bits.push(s.method + (s.scheduler ? ' ' + s.scheduler : ''));
    if (j.clip_skip > 0) bits.push('CLIP skip ' + j.clip_skip);
    bits.push('seed ' + j.seed);
    var ls = j.loras || [];
    for (var i = 0; i < ls.length; i++) bits.push('LoRA ' + ls[i].name + ' ' + round2(ls[i].multiplier));
    if (j.hires && j.hires.enabled) bits.push('hires ×' + round2(j.hires.scale));
    if (j.video) bits.push(j.video.frame_count + ' frames, ' + j.video.fps + ' fps');
    return bits.join(' · ');
  }

  // How long the generation took (patches/sdcpp/0074): "took 52.3 s", "took 2 min 5 s".
  function took(secs) {
    secs = Number(secs);
    if (secs < 60) return 'took ' + (Math.round(secs * 10) / 10) + ' s';
    var s = Math.round(secs);
    return 'took ' + Math.floor(s / 60) + ' min' + (s % 60 ? ' ' + (s % 60) + ' s' : '');
  }

  // Info in the viewer's bar shows or hides the floating panel (PC, tablet), remembered in this browser.
  function lbInfoHidden() { return load('lbinfo') === 'off'; }

  function lbClass(on) {
    var hide = lbInfoHidden();
    $('lb').className = 'lb' + (on ? ' withinfo' : '') + (on && hide ? ' infohide' : '');
    show('lb-infobtn', on, 'inline-block');
    $('lb-infobtn').setAttribute('aria-pressed', hide ? 'false' : 'true');
  }

  function toggleLbInfo() {
    save('lbinfo', lbInfoHidden() ? 'on' : 'off');
    lbClass($('lb').className.indexOf('withinfo') >= 0);
  }

  function drawLbInfo(it) {
    var data = infoCache[it.name];
    if (data === undefined) {
      infoCache[it.name] = null;   // asked
      show('lb-info', false);
      lbClass(false);
      req('GET', '/ps5sd/v1/gallery/info/' + encodeURIComponent(it.name), null, function (code, d) {
        infoCache[it.name] = code === 200 && d ? d : {};
        var cur = st.lb >= 0 ? lbItem() : null;
        if (cur && cur.name === it.name) drawLbInfo(cur);
      });
      return;
    }
    var j = data && data.sdcpp;
    var has = !!(j || (data && data.prompt));
    var labelled = drawLbLabel(it);
    show('lb-info', has || labelled, 'flex');
    lbClass(has || labelled);
    show('lb-text', has);
    show('lb-copy', has, 'flex');
    show('lb-use', !!j, 'inline-block');
    if (!has) return;
    var neg = j && j.prompt ? j.prompt.negative || '' : '';
    text($('lb-prompt'), j && j.prompt ? j.prompt.positive : data.prompt);
    text($('lb-neg'), neg ? 'Negative: ' + neg : '');
    show('lb-neg', !!neg);
    show('lb-cn', !!neg, 'inline-block');
    text($('lb-set'), j ? settingsLine(j) : '');
  }

  // The name and the tags of the image in the viewer (13b), a tag a button that filters the gallery by it.
  // False when it has neither.
  function drawLbLabel(it) {
    var box = $('lb-lab');
    clear(box);
    var tags = it.tags || [];
    if (it.title) box.appendChild(el('span', 'lb-name', it.title));
    for (var i = 0; i < tags.length; i++) {
      (function (tag) {
        var b = el('button', 'ltag', '#' + tag);
        b.type = 'button';
        b.title = 'The images tagged ' + tag;
        b.onclick = function () { closeLb(); setView('gallery'); setGallerySearchNow('#' + tag); };
        box.appendChild(b);
      })(tags[i]);
    }
    var on = !!(it.title || tags.length);
    show('lb-lab', on, 'flex');
    return on;
  }

  // What made the image in the viewer as text: the prompt, the negative prompt, or all of it as A1111
  // writes it (which other tools read when it is pasted), without sd.cpp's JSON at the end.
  function copyInfo(which) {
    var it = lbItem();
    var d = it ? infoCache[it.name] : null;
    if (!d) return;
    var j = d.sdcpp;
    if (which === 'prompt') copyText(j && j.prompt ? j.prompt.positive || '' : d.prompt || '', 'The prompt');
    else if (which === 'negative') copyText(j && j.prompt ? j.prompt.negative || '' : '', 'The negative prompt');
    else copyText(String(d.parameters || d.prompt || '').replace(/,\s*SDCPP:\s*\{[\s\S]*$/, ''), 'The settings');
  }

  // The library entry an image was made with: its preset by name (0.9.5 on), else the default preset
  // of its model file; null when neither is installed.
  function infoTarget(j) {
    var list = st.mdl && st.mdl.models ? st.mdl.models : [];
    var file = baseName(infoModel(j)), name = j.ps5sd && j.ps5sd.preset ? j.ps5sd.preset : '', i;
    for (i = 0; name && i < list.length; i++) {
      if (list[i].kind === 'preset' && list[i].name === name && (!file || fileOf(list[i]) === file)) return list[i];
    }
    if (!file || !st.mdl) return null;
    var gs = groupsOf(st.mdl);
    for (i = 0; i < gs.length; i++) if (gs[i].key === file) return gs[i].def;
    return null;
  }

  // Use all: the model and the preset the image was made with, then its prompt and settings in the
  // Create tab. When a switch is needed, the settings follow once it is done (pendingInfo, from
  // applyModel), so the preset's own defaults do not replace them.
  function useSettings() {
    var it = lbItem();
    var d = it ? infoCache[it.name] : null;
    var j = d && d.sdcpp;
    if (!j) return;
    var target = infoTarget(j);
    var loaded = st.mdl ? st.mdl.loaded : '';
    closeLb();
    setView('create');
    if (target && target.id !== loaded && !st.sw && !(target.missing && target.missing.length)) {
      st.pendingInfo = { id: target.id, name: it.name, j: j };
      toast('Loading ' + titleOf(target) + '. The prompt and the settings of ' + it.name + ' follow when it is ready.', 6000);
      useModel(target);
      return;
    }
    applyInfo(j, it.name, !target);
  }

  // Called by applyModel once the model in use is known: the settings an image is waiting for.
  function pendingInfo(key) {
    var p = st.pendingInfo;
    if (!p || st.sw || swPost) return;
    st.pendingInfo = null;
    if (p.id === key) applyInfo(p.j, p.name, false);
    else toast('The model did not change, so the settings of ' + p.name + ' are not in the form.', 7000);
  }

  // The prompt and the settings of an image into the Create tab. other: its model is not installed.
  function applyInfo(j, name, other) {
    var s = j.sampling || {}, g = s.guidance || {}, hr = j.hires;
    var loras = [], missing = [];
    var ls = j.loras || [];
    for (var i = 0; i < ls.length; i++) {
      if (ls[i].is_high_noise) continue;
      var path = '';
      for (var k = 0; k < (loraList || []).length; k++) if (baseName(loraList[k].path) === ls[i].name) path = loraList[k].path;
      if (path) loras.push({ path: path, m: Number(ls[i].multiplier) || 0 }); else missing.push(ls[i].name);
    }
    $('prompt').value = j.prompt ? j.prompt.positive || '' : '';
    $('neg').value = j.prompt ? j.prompt.negative || '' : '';
    negShow(!!$('neg').value);
    setForm({
      w: j.width, h: j.height, steps: s.steps, cfg: round2(g.txt_cfg),
      sampler: s.method || 'default', scheduler: s.scheduler || 'default', clip: j.clip_skip, seed: j.seed, loras: loras,
      hr: hr && hr.enabled ? { on: true, up: hr.upscaler, scale: round2(hr.scale), steps: hr.steps, den: round2(hr.denoising_strength) } : null
    });
    if (j.video) {
      $('frames').value = j.video.frame_count;
      $('fps').value = j.video.fps;
    }
    if (!st.video && st.mode !== 'guide') setMode('image');
    persist();
    var file = infoModel(j);
    toast('The prompt and the settings of ' + name + ' are in the form.' +
      (other && file ? ' Its model, ' + file + ', is not installed; the same seed gives the same image only with it.' : '') +
      (missing.length ? ' Not installed: ' + missing.join(', ') + '.' : ''), other || missing.length ? 9000 : 4000);
  }

  // A PNG's tEXt "parameters" (what made it), from its base64 or data URL; '' when it has none.
  function pngParameters(b64) {
    var s;
    try { s = atob(String(b64).replace(/^data:[^,]*,/, '')); } catch (e) { return ''; }
    var at = 8;
    while (at + 8 <= s.length) {
      var len = s.charCodeAt(at) * 16777216 + (s.charCodeAt(at + 1) << 16) + (s.charCodeAt(at + 2) << 8) + s.charCodeAt(at + 3);
      var type = s.substr(at + 4, 4);
      if (type === 'IEND') break;
      if (type === 'tEXt' && s.substr(at + 8, 11) === 'parameters\0') {
        var t = s.substr(at + 19, len - 11);
        try { return decodeURIComponent(escape(t)); } catch (e) { return t; }
      }
      at += 12 + len;
    }
    return '';
  }
