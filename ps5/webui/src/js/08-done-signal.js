  // ---------- done signal ----------
  // A sound (Web Audio; the context is made on the Generate click, as browsers ask), a mark in the
  // tab's title while the page is hidden, and a notification where the browser allows one (only
  // on HTTPS or localhost; the console serves plain HTTP, so usually not).
  function canNotify() {
    return !!(window.isSecureContext && window.Notification);
  }

  function wakeAudio() {
    if (load('sound') === '0') return;
    try {
      var Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx) return;
      if (!st.audio) st.audio = new Ctx();
      if (st.audio.state === 'suspended') st.audio.resume();
    } catch (e) {}
  }

  function tone(ctx, freq, at, len) {
    var o = ctx.createOscillator(), g = ctx.createGain();
    o.type = 'sine';
    o.frequency.value = freq;
    g.gain.setValueAtTime(0.0001, at);
    g.gain.exponentialRampToValueAtTime(0.25, at + 0.02);
    g.gain.exponentialRampToValueAtTime(0.0001, at + len);
    o.connect(g);
    g.connect(ctx.destination);
    o.start(at);
    o.stop(at + len + 0.05);
  }

  function signal(ok, message) {
    if (load('sound') !== '0' && st.audio) {
      try {
        var t = st.audio.currentTime + 0.05;
        if (ok) { tone(st.audio, 660, t, 0.18); tone(st.audio, 880, t + 0.16, 0.3); }
        else { tone(st.audio, 330, t, 0.22); tone(st.audio, 247, t + 0.2, 0.35); }
      } catch (e) {}
    }
    if (document.hidden) {
      document.title = (ok ? '✓ ' : '✗ ') + st.title;
      if (load('notify') === '1' && canNotify() && Notification.permission === 'granted') {
        try { new Notification('PS5SD', { body: message, tag: 'ps5sd' }); } catch (e) {}
      }
    }
  }

  function initSignal() {
    st.title = document.title;
    $('opt-sound').checked = load('sound') !== '0';
    $('opt-sound').onchange = function () {
      save('sound', this.checked ? '1' : '0');
      if (this.checked) { wakeAudio(); signal(true, ''); }
    };
    if (canNotify()) {
      show('opt-notify-row', true, 'flex');
      $('opt-notify').checked = load('notify') === '1' && Notification.permission === 'granted';
      $('opt-notify').onchange = function () {
        var box = this;
        save('notify', box.checked ? '1' : '0');
        if (box.checked && Notification.permission !== 'granted') {
          Notification.requestPermission(function (p) {
            if (p !== 'granted') { box.checked = false; save('notify', '0'); toast('The browser does not allow notifications for this page.'); }
          });
        }
      };
    }
    var back = function () { if (!document.hidden) document.title = st.title; };
    if (document.addEventListener) {
      document.addEventListener('visibilitychange', back);
      window.addEventListener('focus', back);
    }
  }

  // The app's switch for the PS5 screen (GET/POST /ps5sd/v1/app/settings); hidden on servers without it.
  function loadTvOption() {
    req('GET', APP + '/settings', null, function (code, data) {
      if (code !== 200 || !data || typeof data.tv_images !== 'boolean') return;
      $('opt-tv').checked = data.tv_images;
      $('opt-tvkey').checked = data.tv_api_key !== false;
      show('opt-tv-box', true);
      show('pnl-oncon', true);
      if (typeof data.image_metadata === 'boolean') {
        $('nometa').checked = !data.image_metadata;
        show('nometa-box', true, 'flex');
      }
    });
  }

  // Leave the settings out of the file (settings: image_metadata), kept on the console; generate()
  // sends "embed_image_metadata": false while it is on.
  function setMetaOption() {
    var box = $('nometa');
    var want = !box.checked;
    box.disabled = true;
    req('POST', APP + '/settings', { image_metadata: want }, function (code, data) {
      box.disabled = false;
      if (code === 200 && data && typeof data.image_metadata === 'boolean') {
        box.checked = !data.image_metadata;
        toast(data.image_metadata ? 'New images carry their settings again.' : 'New images carry no settings in the file; the console keeps them for the gallery.', 6000);
      } else {
        box.checked = want;
        toast('Could not change it' + (errMessage(data) ? ': ' + errMessage(data) : '.'), 6000);
      }
    });
  }

  function setTvOption() {
    var box = $('opt-tv');
    var want = box.checked;
    box.disabled = true;
    req('POST', APP + '/settings', { tv_images: want }, function (code, data) {
      box.disabled = false;
      if (code === 200 && data && typeof data.tv_images === 'boolean') {
        box.checked = data.tv_images;
        toast(data.tv_images ? 'The TV shows images and prompts again.' : 'The TV shows no images and no prompts now.');
      } else {
        box.checked = !want;
        toast('Could not change it' + (errMessage(data) ? ': ' + errMessage(data) : '.'), 6000);
      }
    });
  }

  function setTvKeyOption() {
    var box = $('opt-tvkey');
    var want = box.checked;
    box.disabled = true;
    req('POST', APP + '/settings', { tv_api_key: want }, function (code, data) {
      box.disabled = false;
      if (code === 200 && data && typeof data.tv_api_key === 'boolean') {
        box.checked = data.tv_api_key;
        toast(data.tv_api_key ? 'The TV shows the API key.' : 'The TV does not show the API key now.');
      } else {
        box.checked = !want;
        toast('Could not change it' + (errMessage(data) ? ': ' + errMessage(data) : '.'), 6000);
      }
    });
  }

  // images: base64 PNGs, in the order of their seeds (seed, seed + 1, …). A masked run is put back
  // together over the start image first (putBack), then saved to the gallery in place of the raw results.
  function succeed(images, seed, edited) {
    var rq = st.last && st.last.req;
    if (rq && rq.soft && !edited) {
      status('Putting the image together…');
      var params = [];
      for (var p = 0; p < images.length; p++) params.push(pngParameters(images[p]));
      putBack(rq, images, function (out) { st.editParams = params; succeed(out, seed, true); });
      return;
    }
    var seconds = (new Date().getTime() - st.t0) / 1000;
    var results = [];
    var first = parseInt(seed, 10);
    for (var i = 0; i < images.length; i++) {
      var sd = isNaN(first) ? seed : first + i;
      results.push({ src: dataUrl(images[i]), seed: sd, url: null, name: 'sd_' + sd + '.png',
        params: edited && st.editParams ? st.editParams[i] || '' : '' });
    }
    // a page run (09b): this image goes to the run, and the next one is made
    if (st.run && !st.run.server) { pageRunImage(results, edited); return; }
    st.results = results;
    end();
    progress(1);
    var n = results.length;
    status((n > 1 ? n + ' images done in ' : 'Done in ') + clock(seconds));
    drawStrip();
    pickResult(0);
    signal(true, (n > 1 ? n + ' images' : 'The image') + ' done in ' + clock(seconds) + '.');
    // not kept: the tmp_ files the server saved (Keep makes them gallery files); a put-back edit is
    // only in the page until it is kept
    if (st.last && st.last.temp) {
      for (var t = 0; t < results.length; t++) results[t].temp = true;
      if (!edited) matchTemporary(results, 'image');
      return;
    }
    // the server saved them (with --output-dir): download those files instead of huge data URLs
    var known = st.known;
    loadGallery(function (list) {
      if (!known || !list || st.results !== results) return;
      var fresh = [];
      for (var i = 0; i < list.length; i++) if (!known[list[i].name]) fresh.push(list[i]);
      if (edited) { saveEdits(results, fresh); return; }
      if (fresh.length !== results.length) return;
      fresh.reverse();  // the list is newest first; the server saved them in order
      for (var j = 0; j < fresh.length; j++) { results[j].url = fresh[j].url; results[j].name = fresh[j].name; }
      if (!st.busy) pickResult(st.pick);
    });
  }

  // The video on the stage: its blob URL is freed when the stage shows something else.
  function freeVideo() {
    try { if (st.vidUrl) URL.revokeObjectURL(st.vidUrl); } catch (e) {}
    st.vidUrl = null;
  }

  function showVideo(src) {
    var stage = $('stage');
    clear(stage);
    var v = document.createElement('video');
    v.controls = true;
    v.loop = true;
    v.autoplay = true;
    v.muted = true;
    v.setAttribute('muted', '');
    v.setAttribute('playsinline', '');
    v.src = src;
    stage.appendChild(v);
  }

  // A finished video: on the stage at once; the saved file (in the gallery) for Download and Open.
  function succeedVideo(b64, mime, seed) {
    var seconds = (new Date().getTime() - st.t0) / 1000;
    var url = null;
    try {
      var bin = window.atob(b64);
      var bytes = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      url = URL.createObjectURL(new Blob([bytes], { type: mime }));
    } catch (e) { url = null; }
    freeVideo();
    st.vidUrl = url;
    st.results = null;
    end();
    progress(1);
    drawStrip();
    showVideo(url || ('data:' + mime + ';base64,' + b64));
    var secs = clock(seconds);
    status('Done in ' + secs);
    signal(true, 'The video is done in ' + secs + '.');
    var l = st.last;
    text($('sub'), (l && l.frames ? l.w + '×' + l.h + ' · ' + l.frames + ' frames · ' + l.steps + ' steps · CFG ' + l.cfg + ' · ' : '') + 'seed ' + seed);
    showActions(url || ('data:' + mime + ';base64,' + b64), 'sd_' + seed + '.webm', null, true);
    var probe = document.createElement('video');
    if (!probe.canPlayType || !probe.canPlayType(mime)) {
      toast('This browser cannot play WebM videos. Download it and play it with VLC or a PC browser.', 8000);
    }
    if (l && l.temp) {
      var vr = { src: url, url: null, name: '', temp: true, video: true };
      st.vidResult = vr;
      matchTemporary([vr], 'video');
      return;
    }
    st.vidResult = null;
    // the server saved it to the gallery: the newest new .webm is the file to link to
    var known = st.known;
    loadGallery(function (list) {
      if (!known || !list || st.known !== known) return;
      for (var j = 0; j < list.length; j++) {
        if (!known[list[j].name] && /\.webm$/i.test(list[j].name || '')) {
          showActions(list[j].url, list[j].name, list[j].url, true);
          return;
        }
      }
    });
  }

  // The tmp_ files of a run that is not kept, newest first from the server: the last n are this run's,
  // in the order of the results.
  function matchTemporary(results, type) {
    req('GET', '/ps5sd/v1/gallery?temporary=1&type=' + type + '&limit=' + results.length, null, function (code, list) {
      if (code !== 200 || !list || list.length !== results.length) return;
      list.reverse();
      for (var i = 0; i < list.length; i++) { results[i].url = list[i].url; results[i].name = list[i].name; }
      if (st.busy) return;
      if (type === 'video') { if (st.vidResult === results[0]) showActions(list[0].url, list[0].name, list[0].url, true); }
      else if (st.results === results) pickResult(st.pick);
    });
  }

  // Keep a result that was not to be kept: its tmp_ file becomes a gallery file, or (a put-back edit,
  // which only the page has) the image is saved to the gallery.
  function keepResult(r, button) {
    if (button) button.disabled = true;
    function done(code, data, name) {
      if (button) button.disabled = false;
      if (code !== 200 && code !== 201) { toast(errText(code, data, 'Could not keep it'), 6000); return; }
      r.temp = false;
      r.name = name;
      r.url = galleryUrl(name);
      toast('Kept: ' + name + ' is in the gallery.');
      loadGallery(null);
      if (r.video) showActions(r.url, r.name, r.url, true);
      else if (st.results && st.results.indexOf(r) >= 0) {
        if (st.runMode) showRunItem(st.results.indexOf(r)); else pickResult(st.results.indexOf(r));
      }
    }
    if (r.name && /^tmp_/.test(r.name)) {
      req('POST', APP + '/gallery/keep', { name: r.name }, function (code, data) { done(code, data, data && data.kept); });
    } else if (r.src && /^data:image\/png/.test(r.src)) {
      req('POST', APP + '/gallery', { image: r.src, parameters: r.params || '' }, function (code, data) { done(code, data, data && data.saved); }, 60000);
    } else if (button) {
      button.disabled = false;
      toast('The file is not on the console yet. Try again in a moment.');
    }
  }

  function drawStrip() {
    if (st.runMode) { drawRunStrip(); return; }
    var box = $('strip');
    clear(box);
    var r = st.results;
    stripBox(!!(r && r.length > 1));
    if (!r || r.length < 2) return;
    for (var i = 0; i < r.length; i++) {
      (function (idx) {
        var b = document.createElement('button');
        b.type = 'button';
        b.id = 'pick' + idx;
        b.title = 'Seed ' + r[idx].seed;
        var img = document.createElement('img');
        img.src = r[idx].src;
        img.alt = 'image ' + (idx + 1);
        b.appendChild(img);
        b.onclick = function () { if (!st.busy) pickResult(idx); };
        box.appendChild(b);
      })(i);
    }
  }

  function pickResult(i) {
    if (st.runMode) { showRunItem(i); return; }
    var r = st.results && st.results[i];
    if (!r) return;
    st.pick = i;
    for (var k = 0; k < st.results.length; k++) {
      var b = $('pick' + k);
      if (b) b.className = k === i ? 'on' : '';
    }
    showImage(r.src, 'generated image', null);
    var l = st.last;
    var bits = [];
    if (st.results.length > 1) bits.push('image ' + (i + 1) + ' of ' + st.results.length);
    if (l) {
      var size = l.w + '×' + l.h;
      if (l.req) size = (l.req.crop ? 'masked area, made at ' : '') + l.req.w + '×' + l.req.h + (l.req.ref ? ' · edited by the prompt' : l.req.mask ? ' · inpainted' : ' · from an image');
      else if (l.hr && l.hr.on) size += ' → ' + Math.round(l.w * l.hr.scale / 8) * 8 + '×' + Math.round(l.h * l.hr.scale / 8) * 8 + ' (hires)';
      bits.push(size, l.steps + (l.steps === 1 ? ' step' : ' steps'), 'CFG ' + l.cfg);
    }
    if (r.seed !== null && r.seed !== undefined && r.seed !== '') bits.push('seed ' + r.seed);
    text($('sub'), bits.join(' · '));
    showActions(r.url || r.src, r.name, r.url, true);
  }

