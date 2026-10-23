  // ---------- a start image for video (Wan 2.2 TI2V 5B) ----------
  // st.vinit: {canvas (at most 4096 px a side), name}. The video starts with it: it goes as init_image, cut to
  // the form's aspect at the middle and scaled to its size. Only a model that takes one shows it; Wan 2.1 T2V
  // is text only. Edit on an image of the gallery sets it while such a model is loaded (setInit).
  function vidStartCapable() {
    if (!st.video) return false;
    var d = st.mdl, m = d && d.loaded ? findModel(d.loaded) : null;
    var base = m && m.base ? findModel(m.base) : null;
    var names = ((m && m.name) || '') + ' ' + ((m && m.files) || []).concat((base && base.files) || []).join(' ');
    return /^Wan 5B/.test(loadedFamily()) || /ti2v|i2v|flf2v/.test(names.toLowerCase());
  }

  function setVideoStart(src, name) {
    var img = new Image();
    img.onload = function () {
      var w = img.naturalWidth || img.width, h = img.naturalHeight || img.height;
      var k = Math.min(1, INIT_MAX / Math.max(w, h));
      var c = canvasOf(Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)));
      c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
      st.vinit = { canvas: c, name: name || 'image' };
      // the size follows the image: its aspect, at the model's usual number of pixels
      var f = fitArea(c.width, c.height, st.preset ? st.preset.w * st.preset.h : 832 * 480);
      $('w').value = f.w;
      $('h').value = f.h;
      markPreset();
      persist();
      setView('create');
      drawVideoStart();
      toast('Start image set; the video is ' + f.w + '×' + f.h + ' now and starts with it.', 6000);
    };
    img.onerror = function () { toast('Could not read that image.', 6000); };
    img.src = src;
  }

  function dropVideoStart() {
    st.vinit = null;
    drawVideoStart();
  }

  function vinitFile() {
    var input = $('vinit-file');
    var f = input.files && input.files[0];
    input.value = '';
    if (!f) return;
    var r = new FileReader();
    r.onload = function () { setVideoStart(r.result, f.name); };
    r.onerror = function () { toast('Could not read ' + f.name + '.', 6000); };
    r.readAsDataURL(f);
  }

  function drawVideoStart() {
    var on = vidStartCapable(), v = st.vinit;
    show('vinit-sect', on);
    show('vinit-box', on && !!v);
    show('vinit-bar', on && !v, 'flex');
    if (on && v) {
      var k = Math.min(1, 240 / Math.max(v.canvas.width, v.canvas.height));
      var t = canvasOf(Math.max(1, Math.round(v.canvas.width * k)), Math.max(1, Math.round(v.canvas.height * k)));
      t.getContext('2d').drawImage(v.canvas, 0, 0, t.width, t.height);
      $('vinit-thumb').src = t.toDataURL('image/png');
      text($('vinit-meta'), v.canvas.width + '×' + v.canvas.height + ' · the first frame of the video');
    }
  }

  // The start image covering w x h: aspect kept, the overflow cut off at the middle; a PNG data URL.
  function videoStartData(w, h) {
    var g = st.vinit.canvas;
    var k = Math.max(w / g.width, h / g.height);
    var dw = g.width * k, dh = g.height * k;
    var c = canvasOf(w, h);
    c.getContext('2d').drawImage(g, (w - dw) / 2, (h - dh) / 2, dw, dh);
    return c.toDataURL('image/png');
  }
