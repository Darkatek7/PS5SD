  // ---------- form <-> settings ----------
  function getForm() {
    return {
      w: size8('w', 512), h: size8('h', 512),
      steps: Math.round(num('steps', 20, 1, 150)), cfg: num('cfg', 7, 0, 30),
      sampler: $('sampler').value, scheduler: $('scheduler').value,
      seed: parseInt($('seed').value, 10),
      loras: loraSel.slice(),
      hr: {
        on: $('hr-on').checked, up: $('hr-up').value || 'Latent',
        scale: num('hr-scale', 1.5, 1, 4), steps: Math.round(num('hr-steps', 0, 0, 150)), den: num('hr-den', 0.5, 0.05, 1)
      }
    };
  }

  // The hires fix's fields; h is a form's hr ({on, up, scale, steps, den}) or nothing (off).
  function setHires(h) {
    h = h || {};
    $('hr-on').checked = !!h.on;
    if (h.up) setSelect('hr-up', h.up, 'Latent');
    $('hr-scale').value = h.scale || 1.5;
    $('hr-steps').value = h.steps || 0;
    $('hr-den').value = h.den || 0.5;
    drawHires();
  }

  function hiresSize() {
    var f = getForm();
    return { w: Math.round(f.w * f.hr.scale / 8) * 8, h: Math.round(f.h * f.hr.scale / 8) * 8 };
  }

  function drawHires() {
    var on = $('hr-on').checked;
    show('hr-box', on);
    if (!on) return;
    var t = hiresSize(), big = Math.max(t.w, t.h);
    // At ×1 the second pass keeps the size: it only redoes the details.
    if (num('hr-scale', 1.5, 1, 4) === 1) {
      text($('hr-hint'), 'A second pass at the same size (' + t.w + '×' + t.h + '): it refines the details without making the image larger. ' +
        'A denoising strength of 0.3 to 0.4 keeps the picture as it is.');
      return;
    }
    text($('hr-hint'), 'The image comes out at about ' + t.w + '×' + t.h + '. ' +
      (big > 4096 ? 'That is larger than the server allows (4096).'
        : big > 2048 ? 'Over 2048 px is not measured on the console.'
          : 'A latent upscaler is quickest; a model (ESRGAN) keeps more detail. Strength 0.3 to 0.6 keeps the picture and adds detail.'));
  }

  function setSelect(id, value, fallback) {
    var sel = $(id);
    for (var i = 0; i < sel.options.length; i++) {
      if (sel.options[i].value === value) { sel.value = value; return; }
    }
    sel.value = fallback || 'default';
  }

  function setForm(o) {
    if (o.w) $('w').value = o.w;
    if (o.h) $('h').value = o.h;
    if (o.steps) $('steps').value = o.steps;
    if (o.cfg !== undefined && o.cfg !== null) $('cfg').value = o.cfg;
    setSelect('sampler', o.sampler || 'default');
    setSelect('scheduler', o.scheduler || 'default');
    if (o.seed !== undefined && o.seed !== null && !isNaN(o.seed)) $('seed').value = o.seed;
    loraSel = [];
    var ls = o.loras && o.loras.length ? o.loras : [];
    for (var i = 0; i < ls.length; i++) {
      if (ls[i] && typeof ls[i].path === 'string') loraSel.push({ path: ls[i].path, m: Number(ls[i].m) || 0 });
    }
    drawLoraPicker();
    setHires(o.hr);
    markPreset();
    summary();
  }

  function persist() {
    save('prompt', $('prompt').value);
    save('neg', $('neg').value);
    save('set.' + (st.mkey || '_'), JSON.stringify(getForm()));
    summary();
  }

  // The one-line summary on each settings group's row (the group is open or shut).
  function summary() {
    var seed = parseInt($('seed').value, 10);
    var steps = Math.round(num('steps', 20, 1, 150));
    var scale = num('hr-scale', 1.5, 1, 4);
    text($('grp-size-s'), parseInt($('w').value, 10) + '×' + parseInt($('h').value, 10));
    text($('grp-quality-s'), steps + (steps === 1 ? ' step' : ' steps') + ' · CFG ' + num('cfg', 7, 0, 30) +
      ($('sampler').value !== 'default' ? ' · ' + $('sampler').value : ''));
    text($('grp-style-s'), loraSel.length ? loraSel.length + (loraSel.length === 1 ? ' LoRA' : ' LoRAs') : 'none');
    text($('hr-sect-s'), !$('hr-on').checked ? 'off' : scale === 1 ? 'refine pass' : '×' + scale);
    text($('grp-seed-s'), isNaN(seed) || seed < 0 ? 'random' : String(seed));
    text($('vid-sect-s'), parseInt($('frames').value, 10) + ' frames · ' + parseInt($('fps').value, 10) + ' fps');
    drawHires();
    if ($('ps-box').style.display !== 'none') drawSaveBox();
  }

  // The form follows the loaded model. After the user pressed Use (st.announce) it gets the
  // model's defaults, seed kept; on a passive load (reload, another client switched) the
  // settings saved for that model come back, else its defaults. The key is the library id
  // (two presets can share a file).
  function applyModel(name, entry) {
    var key = entry ? entry.id : (name || '');
    var changed = key !== st.mkey;
    st.model = name;
    st.mkey = key;
    st.mid = entry ? entry.id : '';
    st.preset = presetFor(name, entry);
    text($('presetnote'), presetNote(st.preset));
    st.cnModel = entry && entry.control_net ? entry.control_net : '';
    drawGuide();
    drawMode();
    if (!changed) return;
    var saved = null;
    try { saved = JSON.parse(load('set.' + (key || '_'))); } catch (e) {}
    var keep = parseInt($('seed').value, 10);
    if (st.announce) saved = null;
    var form = saved || st.preset;
    setForm(form);
    if (!saved && !isNaN(keep)) $('seed').value = keep;
    summary();
    if (st.announce) {
      var p = st.preset;
      toast(st.announce + ' is ready. ' + (
        'Its defaults are set: ' + p.w + '×' + p.h + ', ' + p.steps + (p.steps === 1 ? ' step' : ' steps') + ', CFG ' + p.cfg + '.'));
    }
    st.announce = '';
  }

