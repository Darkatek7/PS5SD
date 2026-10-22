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

  function drawLoraPicker() {
    var box = $('lora-sel');
    clear(box);
    for (var i = 0; i < loraSel.length; i++) {
      (function (s) {
        var row = el('div', 'lrow');
        var gone = !loraKnown(s.path);
        row.appendChild(el('span', 'lname', loraName(s.path) + (gone ? ' (not on the console)' : '')));
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
        var rm = actionButton('×', function () {
          for (var j = 0; j < loraSel.length; j++) if (loraSel[j] === s) { loraSel.splice(j, 1); break; }
          drawLoraPicker();
          persist();
        });
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
    for (var k = 0; k < list.length; k++) {
      var picked = false;
      for (var n = 0; n < loraSel.length; n++) if (loraSel[n].path === list[k].path) picked = true;
      if (picked) continue;
      var o = document.createElement('option');
      o.value = list[k].path;
      text(o, list[k].name || loraName(list[k].path));
      add.appendChild(o);
      free++;
    }
    add.value = '';
    show('lora-add', free > 0);
    text($('lora-hint'), !list.length
      ? 'No LoRAs on the console yet. Upload or download one on the Models tab.'
      : 'Strength 1 is the full effect, 0.5 half of it. A LoRA works only with the model family it was made for.');
  }

  function addLora() {
    var path = $('lora-add').value;
    if (!path) return;
    loraSel.push({ path: path, m: 1 });
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

  function drawLoraList() {
    var box = $('llist');
    clear(box);
    var list = loraList || [];
    if (!list.length) box.appendChild(el('div', 'empty-note', 'No LoRAs yet. Get one below, upload one, or download one by link as LoRA.'));
    for (var i = 0; i < list.length; i++) {
      (function (l) {
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        main.appendChild(el('div', 'mname', l.name || loraName(l.path)));
        main.appendChild(el('div', 'mmeta', (l.bytes > 0 ? bytes(l.bytes) + ' · ' : '') + l.path));
        row.appendChild(main);
        var acts = el('div', 'macts');
        var del = actionButton('Delete', function () { askDeleteLora(l); });
        del.className = 'danger';
        acts.appendChild(del);
        row.appendChild(acts);
        box.appendChild(row);
      })(list[i]);
    }
    var sug = $('lsugg');
    clear(sug);
    for (var k = 0; k < FEATURED_LORAS.length; k++) {
      (function (c) {
        for (var j = 0; j < list.length; j++) if (list[j].path === c.file) return;
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        main.appendChild(el('div', 'mname', c.label));
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

