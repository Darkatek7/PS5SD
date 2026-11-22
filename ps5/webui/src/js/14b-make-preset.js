  // ---------- Make a preset for a diffusion model that came alone, and Export of a preset (issue 25) ----------

  // What a diffusion model of each family runs with, as the catalog's presets have it (catalog.hpp): its
  // settings, then for each option [option, the files that fit (by path), the catalog's file, the catalog model
  // that brings it], then the options that follow. makeDraft takes an installed preset of the family first.
  var MAKE_RECIPES = {
    'FLUX.3 Klein': { def: 'steps=16 cfg=1 width=1024 height=1024 sampler=euler', opts: [
      ['--llm', /qwen3.?vl.?4b/i, 'text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf', 'FLUX.3 Image Klein'],
      ['--vae', /flux.?2.?vae/i, 'vae/flux2-vae.safetensors', 'FLUX.3 Image Klein']] },
    'FLUX.2 Klein 4B': { def: 'steps=4 cfg=1 width=1024 height=1024 sampler=euler', opts: [
      ['--llm', /qwen3.?4b/i, 'text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf', 'FLUX.2 Klein 4B'],
      ['--vae', /flux.?2.?vae/i, 'vae/flux2-vae.safetensors', 'FLUX.2 Klein 4B']] },
    'FLUX.2 Klein 9B': { def: 'steps=8 cfg=1 width=1024 height=1024 sampler=dpm2', opts: [
      ['--llm', /qwen3.?8b/i, 'text_encoders/Qwen3-8B-Q4_K_M.gguf', 'FLUX.2 Klein 9B'],
      ['--vae', /flux.?2.?vae/i, 'vae/flux2-vae.safetensors', 'FLUX.2 Klein 9B']], extra: ['--vae-tiling', '--vae-tile-size', '512'] },
    'Z-Image': { def: 'steps=8 cfg=1 width=1024 height=1024 sampler=euler', opts: [
      ['--llm', /qwen3.?4b/i, 'text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf', 'Z-Image Turbo'],
      ['--vae', /flux.?1.?ae|(^|\/)ae\.safetensors/i, 'vae/flux1-ae.safetensors', 'Z-Image Turbo']] },
    'MOSS-Image': { def: 'steps=20 cfg=3 width=1024 height=1024 sampler=euler', fast: 'steps=4 cfg=1 width=1024 height=1024 sampler=euler', opts: [
      ['--llm', /qwen3\.?5.?9b/i, 'text_encoders/Qwen3.5-9B-Q4_K_M.gguf', 'MOSS-Image 4-step'],
      ['--tokenizer', /qwen3\.?5/i, 'tokenizers/qwen3.5-tokenizer.json', 'MOSS-Image 4-step'],
      ['--vae', /flux.?2.?vae/i, 'vae/flux2-vae.safetensors', 'MOSS-Image 4-step']], extra: ['--vae-tiling', '--vae-tile-size', '512'] },
    'Krea 2': { def: 'steps=8 cfg=1 width=1024 height=1024 sampler=euler', opts: [
      ['--llm', /qwen3.?vl.?4b/i, 'text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf', 'FLUX.3 Image Klein'],
      ['--vae', /wan.?2\.?1.?vae/i, 'vae/wan_2.1_vae.safetensors', 'Wan 2.1 T2V 1.3B']], extra: ['--vae-tiling', '--vae-tile-size', '512'] },
    'Wan 1.3B': { def: 'steps=20 cfg=6 width=832 height=480 sampler=euler', opts: [
      ['--t5xxl', /umt5/i, 'text_encoders/umt5-xxl-encoder-Q4_K_M.gguf', 'Wan 2.1 T2V 1.3B'],
      ['--vae', /wan.?2\.?1.?vae/i, 'vae/wan_2.1_vae.safetensors', 'Wan 2.1 T2V 1.3B']] }
  };

  // The part of a model a preset option names.
  function partOfOption(o) { return o === '--vae' ? 'VAE' : o === '--tokenizer' ? 'tokenizer' : 'text encoder'; }

  // A file name without its folder and extension.
  function stemOf(path) { return baseName(path).replace(/\.(gguf|safetensors|sft|ckpt)$/i, ''); }

  // A preset of a model's family to copy the text encoder and VAE from: one of the model presets (not a
  // saved one, not a ControlNet one) with all its files, the catalog's first.
  function presetOfFamily(fam, models) {
    if (!fam) return null;
    for (var i = 0; i < models.length; i++) {
      var v = models[i];
      if (v.kind === 'preset' && !v.base && !v.control_net && !(v.missing && v.missing.length) && v.family === fam) return v;
    }
    return null;
  }

  // The draft from an installed preset's text: its lines with the model's file in place of its own, its
  // name, and the "# defaults:" line; "# base:", the prompt starters and the comments go.
  function draftFromPreset(m, text) {
    var lines = String(text).split(/\r?\n/), out = ['# name: ' + stemOf(m.id)], swap = false;
    for (var i = 0; i < lines.length; i++) {
      var l = trim(lines[i]);
      if (!l) continue;
      if (l.charAt(0) === '#') { if (/^#\s*defaults:/i.test(l)) out.push(l); continue; }
      if (swap) { out.push(m.id); swap = false; continue; }
      if (l === '--diffusion-model' || l === '-m' || l === '--model') { out.push('--diffusion-model'); swap = true; continue; }
      out.push(l);
    }
    return out.join('\n') + '\n';
  }

  // The draft from MAKE_RECIPES and the files on the console (GET models/files): {text, notes: [{text, bad}]}.
  // A file the recipe names that is not there stays in, with a note on what brings it.
  function draftFromRecipe(m, fam, files) {
    var r = MAKE_RECIPES[fam], notes = [];
    var fast = r && r.fast && /turbo|4.?step|distill|dmd|lightning|lcm/i.test(m.id);
    var out = ['# name: ' + stemOf(m.id)];
    if (r) out.push('# defaults: ' + (fast ? r.fast : r.def));
    out.push('--diffusion-model', m.id);
    if (!r) {
      out.push('--llm', 'text_encoders/<the text encoder>', '--vae', 'vae/<the VAE>');
      notes.push({ text: 'PS5SD does not know what ' + (fam || 'this model') + ' runs with. Its page on Hugging Face or Civitai ' +
        'names the text encoder and the VAE; put their paths in (the files are listed under Help, Models).', bad: true });
      return { text: out.join('\n') + '\n', notes: notes };
    }
    for (var i = 0; i < r.opts.length; i++) {
      // another family's text encoder or VAE does not fit (other widths, other latents): none is offered
      var o = r.opts[i], part = partOfOption(o[0]), pick = '';
      for (var j = 0; j < files.length && !pick; j++) {
        var f = files[j];
        if (f.part === part && !(f.format && !f.format.loads) && o[1].test(f.path)) pick = f.path;
      }
      out.push(o[0], pick || o[2]);
      if (!pick) notes.push({ text: 'The ' + part + ' ' + baseName(o[2]) + ' is not on the console: ' + o[3] + ' under Get more brings it.', bad: true });
    }
    if (r.extra) out = out.concat(r.extra);
    return { text: out.join('\n') + '\n', notes: notes };
  }

  // The Make a preset dialog: the preset editor with a draft for the diffusion model m.
  var peNew = null;
  function makePreset(m) {
    var models = (st.mdl && st.mdl.models) || [], fam = m.family || familyOf(m, null);
    req('GET', APP + '/models/files', null, function (code, data) {
      var files = code === 200 && data && data.files ? data.files : [];
      var from = presetOfFamily(fam, models);
      if (from) {
        req('GET', APP + '/presets/' + encodeURIComponent(from.id), null, function (c2, d2) {
          if (c2 !== 200 || !d2 || typeof d2.text !== 'string') { openNew(m, draftFromRecipe(m, fam, files)); return; }
          openNew(m, { text: draftFromPreset(m, d2.text),
            notes: [{ text: 'The text encoder, the VAE and the settings are those of ' + titleOf(from) + ', also ' + fam + '.' }] });
        });
      } else {
        openNew(m, draftFromRecipe(m, fam, files));
      }
    });
  }

  function openNew(m, draft) {
    peFor = null;
    peNew = m;
    dlgFrom = document.activeElement;
    text($('pe-title'), 'Make a preset for ' + baseName(m.id));
    $('pe-text').value = peOrig = draft.text;
    var note = $('pe-note');
    clear(note);
    var fam = m.family ? m.family + ' diffusion model. ' : '';
    note.appendChild(el('span', '', fam + 'Check the lines, then Save; Use on the new preset loads it.'));
    for (var i = 0; i < draft.notes.length; i++) note.appendChild(el('span', 'pe-line' + (draft.notes[i].bad ? ' bad' : ''), draft.notes[i].text));
    show('pe-note', true);
    disarmPeHelp();
    text($('pe-err'), '');
    $('pe-save').disabled = false;
    show('pe', true, 'flex');
    $('pe-text').focus();
  }

  // Save in the Make a preset dialog: a new preset, named by its "# name:" line.
  function saveNew() {
    var m = peNew, t = $('pe-text').value;
    if (!m) return;
    var nm = /^#\s*name:\s*(.+)$/im.exec(t);
    var name = nm ? trim(nm[1]) : stemOf(m.id);
    $('pe-save').disabled = true;
    req('POST', APP + '/presets', { name: name, text: t }, function (code, data) {
      $('pe-save').disabled = false;
      if (code === 201 && data && data.models) {
        closeEdit();
        setModels(data);
        toast('Saved as ' + name + '. Use loads it.', 6000);
      } else if (code === 404 || code === 405) {
        text($('pe-err'), 'This version of the PS5SD app cannot make presets; update it, or upload the text as a .txt under Upload.');
      } else {
        text($('pe-err'), errText(code, data, 'Could not save'));
      }
    }, 15000);
  }

  // A text as a file the browser saves (Export).
  function saveTextFile(name, body) {
    var blob;
    try { blob = new Blob([body], { type: 'text/plain;charset=utf-8' }); } catch (x) { toast('This browser cannot save a file.'); return false; }
    var url = (window.URL || window.webkitURL).createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { (window.URL || window.webkitURL).revokeObjectURL(url); }, 4000);
    return true;
  }

  // Export: the preset's file as <id>.txt, to share it or keep it. Upload under Models takes it back.
  function exportPreset(v) {
    req('GET', APP + '/presets/' + encodeURIComponent(v.id), null, function (code, data) {
      if (code !== 200 || !data || typeof data.text !== 'string') { toast(errText(code, data, 'Could not read the preset'), 6000); return; }
      if (saveTextFile(v.id + '.txt', data.text)) toast('Saved ' + v.id + '.txt. Upload it as a Preset (.txt) to use it on a console.', 6000);
    });
  }
