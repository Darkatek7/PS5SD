  // ---------- upscalers ----------
  // The ESRGAN models in /data/PS5SD/upscalers: for the hires fix and the Upscale button. Two
  // well-known ones can be fetched with one click (the Hugging Face copies are byte for byte
  // Real-ESRGAN's own releases).
  var UPSCALERS = [
    { file: 'RealESRGAN_x4plus.pth', label: 'Real-ESRGAN x4plus', note: 'photos and most images, 67 MB',
      url: 'https://huggingface.co/lllyasviel/Annotators/resolve/main/RealESRGAN_x4plus.pth' },
    { file: 'RealESRGAN_x4plus_anime_6B.pth', label: 'Real-ESRGAN x4plus anime 6B', note: 'anime and drawings, 18 MB',
      url: 'https://huggingface.co/gemasai/RealESRGAN_x4plus_anime_6B/resolve/main/RealESRGAN_x4plus_anime_6B.pth' }
  ];
  var upList = null;      // GET APP/upscalers ({name, file, bytes}), null until known
  var upModels = [];      // the server's upscalers that the Upscale button can use (names)
  var upBusy = false;

  function loadUpscalers() {
    req('GET', APP + '/upscalers', null, function (code, data) {
      if (code !== 200 || !data || !data.upscalers) return;
      upList = data.upscalers;
      show('pnl-ups', true);
      drawUpscalers();
    });
  }

  // The installed upscalers (Installed; the one the Upscale button uses is marked), and the featured
  // ones not installed yet (Get more).
  function drawUpscalers() {
    var box = $('ulist');
    clear(box);
    var list = upList || [];
    var chosen = $('opt-up').value || upModels[0] || '';
    if (!list.length) box.appendChild(el('div', 'empty-note', 'No upscalers yet. Get one under Get more.'));
    for (var i = 0; i < list.length; i++) {
      (function (u) {
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        var nm = el('div', 'mname', readable(u.name));
        if (u.name === chosen && upModels.length) nm.appendChild(el('span', 'badge ok', 'Upscale button'));
        main.appendChild(nm);
        main.appendChild(el('div', 'mfile', u.file));
        main.appendChild(el('div', 'mmeta', bytes(u.bytes)));
        row.appendChild(main);
        var acts = el('div', 'macts');
        acts.appendChild(moreMenu([{ label: 'Delete', danger: true, fn: function () { askDeleteUpscaler(u); } }], ''));
        row.appendChild(acts);
        box.appendChild(row);
      })(list[i]);
    }
    var sug = $('usugg');
    clear(sug);
    for (var k = 0; k < UPSCALERS.length; k++) {
      (function (c) {
        for (var j = 0; j < list.length; j++) if (list[j].file === c.file) return;
        if (!sug.childNodes.length) sug.appendChild(el('div', 'ghead', 'Upscalers'));
        var row = el('div', 'mrow mwrap');
        var main = el('div', 'mmain');
        main.appendChild(el('div', 'mname', c.label));
        main.appendChild(el('div', 'mmeta', c.note));
        row.appendChild(main);
        var acts = el('div', 'macts');
        var b = actionButton('Download', function () { startDownload(c.url, b, true, null, 'upscalers'); });
        b.className = 'on';
        acts.appendChild(b);
        row.appendChild(acts);
        sug.appendChild(row);
      })(UPSCALERS[k]);
    }
    showSugg();
  }

  function askDeleteUpscaler(u) {
    dlgFor = { upscaler: u };
    dlgFrom = document.activeElement;
    text($('dlg-title'), 'Delete ' + u.name + ' (' + bytes(u.bytes) + ')?');
    text($('dlg-body'), 'The upscaler file is deleted from the console.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function deleteUpscaler(u) {
    req('DELETE', APP + '/upscalers/' + encodeURIComponent(u.file), null, function (code, data) {
      closeDlg();
      if (code === 200 && data && data.upscalers) {
        upList = data.upscalers;
        drawUpscalers();
        loadInfo();
        toast('Deleted ' + u.name + '.');
      } else {
        toast(errText(code, data, 'Could not delete ' + u.name), 6000);
        loadUpscalers();
      }
    }, 30000);
  }

  // The server's upscalers (capabilities): every one for the hires fix, the ESRGAN ones for the
  // Upscale button.
  function setUpscalers(list) {
    var all = [], img = [];
    for (var i = 0; i < list.length; i++) {
      if (!list[i] || !list[i].name || list[i].name === 'None') continue;
      all.push(list[i].name);
      if (list[i].image_upscale) img.push(list[i].name);
    }
    var hr = $('hr-up'), keep = hr.value || load('hrup') || 'Latent';
    clear(hr);
    for (var j = 0; j < all.length; j++) {
      var o = document.createElement('option');
      o.value = all[j];
      text(o, all[j]);
      hr.appendChild(o);
    }
    setSelect('hr-up', keep, 'Latent');
    upModels = img;
    var sel = $('opt-up'), want = load('upscaler') || sel.value;
    clear(sel);
    for (var k = 0; k < img.length; k++) {
      var u = document.createElement('option');
      u.value = img[k];
      text(u, img[k]);
      sel.appendChild(u);
    }
    if (img.length) setSelect('opt-up', want, img[0]);
    show('opt-up-box', img.length > 0);
    show('lb-up', img.length > 0, 'inline-block');
    drawUpscalers();  // the "Upscale button" mark
    if (!st.busy && st.results) pickResult(st.pick);
  }

  // The image as base64: a data URL as it is, a gallery file fetched.
  function imageBase64(src, cb) {
    if (src.indexOf('data:') === 0) { cb(src.substring(src.indexOf(',') + 1)); return; }
    var x = new XMLHttpRequest();
    x.open('GET', src, true);
    x.responseType = 'arraybuffer';
    x.onload = function () {
      if (x.status !== 200) { cb(null); return; }
      var a = new Uint8Array(x.response), bin = '';
      for (var i = 0; i < a.length; i += 0x8000) bin += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000));
      cb(window.btoa(bin));
    };
    x.onerror = function () { cb(null); };
    x.send();
  }

  // Runs the upscaler over an image; the server saves the result to the gallery, which then
  // opens on it. b: the button pressed (shows the progress).
  function upscale(src, b) {
    if (upBusy || !upModels.length) return;
    upBusy = true;
    var label = b ? b.textContent : '';
    if (b) { b.disabled = true; text(b, 'Upscaling…'); }
    var t0 = new Date().getTime();
    var finish = function (message, ms) {
      upBusy = false;
      if (b) { b.disabled = false; text(b, label); }
      if (message) toast(message, ms || 6000);
    };
    imageBase64(src, function (b64) {
      if (!b64) { finish('Could not read the image.'); return; }
      var body = { image: b64, upscaler: $('opt-up').value || upModels[0], return_image: false };
      req('POST', '/sdcpp/v1/upscale', body, function (code, data) {
        if (code !== 200 || !data) { finish(errText(code, data, 'The upscale failed')); return; }
        var secs = (new Date().getTime() - t0) / 1000;
        var name = data.saved && data.saved[0];
        finish('Upscaled to ' + data.width + '×' + data.height + ' in ' + clock(secs) + (name ? '; it is in the gallery.' : '.'), 6000);
        if (!name) return;
        loadGallery(function (list) {
          if (!list) return;
          for (var i = 0; i < list.length; i++) if (list[i].name === name) { openLbItem(list[i], b); return; }
        });
      }, 30 * 60 * 1000);
    });
  }

