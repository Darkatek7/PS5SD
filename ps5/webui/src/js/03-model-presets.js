  // ---------- model presets ----------
  function mkPreset(label, w, steps, cfg, sampler) {
    return { label: label, w: w, h: w, steps: steps, cfg: cfg, sampler: sampler || 'default', scheduler: 'default' };
  }

  function pickPreset(name) {
    var n = (name || '').toLowerCase();
    var xl = /xl|pony|juggernaut/.test(n);
    if (/sdxs/.test(n)) return mkPreset('SDXS', 512, 1, 1);
    if (/turbo/.test(n)) return mkPreset(xl ? 'SDXL Turbo' : 'SD Turbo', 512, 1, 1);
    if (/lightning|hyper/.test(n)) return mkPreset('Lightning', xl ? 1024 : 512, 4, 1);
    if (/lcm/.test(n)) return mkPreset('LCM', xl ? 1024 : 512, 4, 1.5, 'lcm');
    if (/flux/.test(n)) return mkPreset('Flux', 1024, /schnell/.test(n) ? 4 : 20, 1);
    if (/sd3|sd_3|stable-diffusion-3/.test(n)) return mkPreset('SD3', 1024, 28, 4.5);
    if (xl) {
      // dpm++2m with the ays schedule: the picture of euler_a at 25 steps in 10 (docs/CONSOLE.md, "Faster SDXL")
      var p = mkPreset('SDXL', 1024, 10, 6, 'dpm++2m');
      p.scheduler = 'ays';
      return p;
    }
    return mkPreset('SD 1.x', 512, 20, 7);
  }

  // The preset of a model: the guess from its name, then what the app says (defaults of the
  // library entry, all strings) over it.
  function presetFor(name, entry) {
    var p = pickPreset(name);
    var d = entry && entry.defaults;
    if (!d) return p;
    var any = false, n;
    n = parseInt(d.width, 10);
    if (n > 0) { p.w = n; p.h = n; any = true; }
    n = parseInt(d.height, 10);
    if (n > 0) { p.h = n; any = true; }
    n = parseInt(d.steps, 10);
    if (n > 0) { p.steps = n; any = true; }
    n = parseFloat(d.cfg);
    if (!isNaN(n) && n >= 0) { p.cfg = n; any = true; }
    if (d.sampler) { p.sampler = d.sampler; any = true; }
    if (d.scheduler) { p.scheduler = d.scheduler; any = true; }
    n = parseInt(d.clip_skip, 10);
    if (n > 0) { p.clip = n; any = true; }
    if (d.loras) { p.loras = parseLoras(d.loras); any = true; }
    n = parseFloat(d.hires_scale);
    if (n >= 1) {
      p.hr = { on: true, scale: n, up: d.hires_upscaler ? decodeURIComponent(d.hires_upscaler) : 'Latent',
        steps: parseInt(d.hires_steps, 10) || 0, den: parseFloat(d.hires_denoise) || 0.5 };
      any = true;
    }
    if (any && entry.name) p.label = entry.name;
    return p;
  }

  // "loras=styles/ink.gguf:0.8,detail.safetensors:1" in a preset's defaults.
  function parseLoras(v) {
    var out = [], parts = String(v).split(',');
    for (var i = 0; i < parts.length; i++) {
      var at = parts[i].lastIndexOf(':');
      if (at > 0) out.push({ path: parts[i].substring(0, at), m: Number(parts[i].substring(at + 1)) || 0 });
    }
    return out;
  }

  function presetNote(p) {
    return 'Defaults: ' + p.steps + (p.steps === 1 ? ' step' : ' steps') + ', CFG ' + p.cfg + ', ' + p.w + '×' + p.h +
      (p.loras && p.loras.length ? ', ' + p.loras.length + (p.loras.length === 1 ? ' LoRA' : ' LoRAs') : '') + '.';
  }

  // ---------- what a model is, from its names: family, video, ControlNet ----------
  // Shared by the Create tab (the model in use) and the Models tab (rows, tags, catalog cards).
  // The family of a model, guessed from the names (then the files) of it and its base file.
  function familyWord(s) {
    if (/flux/.test(s)) return 'FLUX';
    if (/z[-_ ]?image/.test(s)) return 'Z-Image';
    if (/wan/.test(s)) return 'Wan';
    if (/sd ?3|sd_3|stable-diffusion-3/.test(s)) return 'SD 3';
    if (/xl|pony|ilust|illust|juggernaut|animagine/.test(s)) return 'SDXL';
    if (/sd[_ ]?turbo|sd ?2\.?1|v2-1/.test(s)) return 'SD 2.1';
    if (/sd ?1\.?5|v1-5|sd_?15|sdxs/.test(s)) return 'SD 1.5';
    return '';
  }

  // The server reads it from the model's tensors ("family", API.md); the names are the fallback.
  function familyOf(entry, base) {
    if (entry && entry.family) return entry.family;
    if (base && base.family) return base.family;
    var names = ((entry && entry.name) || '') + ' ' + ((base && base.name) || '') + ' ' + ((entry && entry.id) || '');
    var files = ((entry && entry.files) || []).concat((base && base.files) || []).join(' ');
    return familyWord(names.toLowerCase()) || familyWord(files.toLowerCase());
  }

  // A video model by its name: Wan, CausVid, or "video" in the name.
  function videoName(s) { return /wan|causvid|video/.test(s); }

  // The family of a LoRA: its name; the fast ones without a family in the name (DMD2, Lightning,
  // LCM: SDXL; CausVid: Wan).
  // Whether a LoRA of family a fits a model of family b: the same, or one says less ("FLUX" and "FLUX.1").
  function sameFamily(a, b) { return a === b || a.indexOf(b) === 0 || b.indexOf(a) === 0; }

  function loraFamily(name) {
    var s = String(name || '').toLowerCase();
    return familyWord(s) || (/dmd2|lightning|lcm/.test(s) ? 'SDXL' : '') || (/causvid/.test(s) ? 'Wan' : '');
  }

  // The tags of a model or a catalog entry, as [{text, cls}] (cls: the .tag colours).
  function tagsOf(name, files, video, cn, family) {
    var s = String(name || '').toLowerCase(), f = (files || []).join(' ').toLowerCase();
    var out = [];
    var fam = family || familyWord(s) || familyWord(f);
    if (fam) out.push({ text: fam, cls: 'fam' });
    if (video || videoName(s)) out.push({ text: 'video', cls: 'vid' });
    if (cn || /control ?net|canny/.test(s)) out.push({ text: 'ControlNet', cls: 'cn' });
    if (/inpaint/.test(s)) out.push({ text: 'inpainting', cls: '' });
    return out;
  }

