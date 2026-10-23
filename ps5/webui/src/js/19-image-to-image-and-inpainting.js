  // ---------- image to image and inpainting ----------
  // st.init: the start image {canvas (at most 4096 px a side), name, mask, painted}. mask is a canvas of
  // the same size, red where painted and clear elsewhere (null: no mask); painted is its painted share.
  // With a mask and "only the masked area", a box around the mask is generated at the model's size and
  // put back into the image here, in the page; otherwise the whole image (cropped to the form's aspect)
  // is the start. A masked result is always blended back over the start image with a soft edge, so the
  // rest of the image stays exactly as it was, and saved to the gallery in place of the raw result.
  var INIT_MAX = 4096;
  var me = { brush: 48, erase: false, down: false, last: null, scale: 1 };

  function canvasOf(w, h) {
    var c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    return c;
  }

  // w x h with the aspect of the source and about `area` pixels, in steps of 64.
  function fitArea(w, h, area) {
    var k = Math.sqrt(area / (w * h));
    return { w: Math.max(64, Math.round(w * k / 64) * 64), h: Math.max(64, Math.round(h * k / 64) * 64) };
  }

  function setInit(src, name) {
    if (vidStartCapable()) { setVideoStart(src, name); return; }   // a video model that starts from a picture
    var img = new Image();
    img.onload = function () {
      var w = img.naturalWidth || img.width, h = img.naturalHeight || img.height;
      var k = Math.min(1, INIT_MAX / Math.max(w, h));
      var c = canvasOf(Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)));
      c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
      st.init = { canvas: c, name: name || 'image', mask: null, painted: 0 };
      // the size follows the image: its aspect, at the model's usual number of pixels
      var f = fitArea(c.width, c.height, st.preset ? st.preset.w * st.preset.h : 512 * 512);
      $('w').value = f.w;
      $('h').value = f.h;
      markPreset();
      persist();
      setView('create');
      setMode('edit');
      drawInit();
      toast('Start image set; the size is ' + f.w + '×' + f.h + ' now. Paint a mask to change only a part of it.', 6000);
    };
    img.onerror = function () { toast('Could not read that image.', 6000); };
    img.src = src;
  }

  function dropInit() {
    st.init = null;
    drawInit();
  }

  function initFile() {
    var input = $('init-file');
    var f = input.files && input.files[0];
    input.value = '';
    if (!f) return;
    var r = new FileReader();
    r.onload = function () { setInit(r.result, f.name); };
    r.onerror = function () { toast('Could not read ' + f.name + '.', 6000); };
    r.readAsDataURL(f);
  }

  // FLUX.2 edits by the prompt: the start image goes as a reference (ref_images), with no strength and no
  // mask. sd.cpp ignores references for other families, so the choice shows only for FLUX.2.
  function refCapable() { return /^FLUX\.2/.test(loadedFamily()); }
  function refEdit() { return !!st.init && refCapable() && $('init-ref').checked; }

  function drawInit() {
    var i = st.init, ref = refEdit();
    show('init-box', !!i);
    show('init-bar', !i, 'flex');
    if (i) {
      var k = Math.min(1, 240 / Math.max(i.canvas.width, i.canvas.height));
      var t = canvasOf(Math.max(1, Math.round(i.canvas.width * k)), Math.max(1, Math.round(i.canvas.height * k)));
      var g = t.getContext('2d');
      g.drawImage(i.canvas, 0, 0, t.width, t.height);
      if (i.mask) {
        g.globalAlpha = 0.55;
        g.drawImage(i.mask, 0, 0, t.width, t.height);
        g.globalAlpha = 1;
      }
      $('init-thumb').src = t.toDataURL('image/png');
      text($('init-meta'), i.canvas.width + '×' + i.canvas.height + ' · ' + (ref
        ? 'a reference: the prompt says what changes'
        : i.mask ? Math.max(1, Math.round(i.painted * 100)) + '% masked: only that part changes'
        : 'no mask: the whole image changes'));
      text($('init-mask'), i.mask ? 'Edit the mask' : 'Paint a mask');
      show('init-ref-row', refCapable(), 'flex');
      show('init-str-row', !ref, 'flex');
      show('init-mask', !ref, 'inline-block');
      show('init-crop-row', !!i.mask && !ref, 'flex');
      text($('init-hint'), ref
        ? 'Say what to change, for example "the same house in winter, snow on the roof". The layout and the rest stay. ' +
          'It takes longer than an image of its own: about a minute at 4 steps, two at 8.'
        : i.mask && $('init-crop').checked
        ? 'A box around the mask is made with as many pixels as Width × Height, then put back into the image: small fixes in large images are quick and sharp.'
        : 'Strength 0.3 to 0.5 keeps the picture and changes details; 0.7 and up changes much more.' +
          ($('hr-on').checked ? ' The hires fix is not used with a start image.' : ''));
    }
    summary();
  }

  // ---- the guide image (ControlNet): its edges or pose steer the layout of the new image
  function drawGuide() {
    var g = st.cn;
    show('cn-box', !!g);
    show('cn-bar', !g, 'flex');
    if (g) {
      var k = Math.min(1, 240 / Math.max(g.canvas.width, g.canvas.height));
      var t = canvasOf(Math.max(1, Math.round(g.canvas.width * k)), Math.max(1, Math.round(g.canvas.height * k)));
      t.getContext('2d').drawImage(g.canvas, 0, 0, t.width, t.height);
      $('cn-thumb').src = t.toDataURL('image/png');
      text($('cn-meta'), g.canvas.width + '×' + g.canvas.height + ' · ' + st.cnModel);
    }
    text($('cn-hint'), 'The new image follows the guide\'s layout: its edges with Canny on, the image itself with it off. 0.5 is loose, 1 close.');
    summary();
  }

  // The size of the form stays as it is; the guide is scaled to it when the image is made.
  function setGuide(src, name) {
    var img = new Image();
    img.onload = function () {
      var w = img.naturalWidth || img.width, h = img.naturalHeight || img.height;
      var k = Math.min(1, INIT_MAX / Math.max(w, h));
      var c = canvasOf(Math.max(1, Math.round(w * k)), Math.max(1, Math.round(h * k)));
      c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
      st.cn = { canvas: c, name: name || 'image' };
      setView('create');
      setMode('guide');
      drawGuide();
      toast(st.cnModel ? 'Guide image set.' : 'Guide image set. It is used with a model that has a ControlNet (Models tab: SDXL + Canny ControlNet).', 6000);
    };
    img.onerror = function () { toast('Could not read that image.', 6000); };
    img.src = src;
  }

  function dropGuide() {
    st.cn = null;
    drawGuide();
  }

  function guideFile() {
    var input = $('cn-file');
    var f = input.files && input.files[0];
    input.value = '';
    if (!f) return;
    var r = new FileReader();
    r.onload = function () { setGuide(r.result, f.name); };
    r.onerror = function () { toast('Could not read ' + f.name + '.', 6000); };
    r.readAsDataURL(f);
  }

  // The guide covering w x h: aspect kept, the overflow cut off at the middle; a PNG data URL.
  function guideData(w, h) {
    var g = st.cn.canvas;
    var k = Math.max(w / g.width, h / g.height);
    var dw = g.width * k, dh = g.height * k;
    var c = canvasOf(w, h);
    c.getContext('2d').drawImage(g, (w - dw) / 2, (h - dh) / 2, dw, dh);
    return c.toDataURL('image/png');
  }

  // ---- the mask editor
  function openMask() {
    var i = st.init;
    if (!i) return;
    if (!i.mask) i.mask = canvasOf(i.canvas.width, i.canvas.height);
    me.erase = false;
    text($('me-mode'), 'Erase');
    show('me', true, 'flex');
    sizeMask();
  }

  function sizeMask() {
    var i = st.init, cv = $('me-canvas'), body = $('me-body');
    if (!i || $('me').style.display === 'none') return;
    var bw = Math.max(100, body.clientWidth - 32), bh = Math.max(100, body.clientHeight - 16);
    var k = Math.min(bw / i.canvas.width, bh / i.canvas.height);
    cv.width = Math.max(1, Math.round(i.canvas.width * k));
    cv.height = Math.max(1, Math.round(i.canvas.height * k));
    me.scale = k;
    text($('me-size'), 'Brush ' + me.brush);
    drawMask();
  }

  function drawMask() {
    var i = st.init, cv = $('me-canvas'), g = cv.getContext('2d');
    g.clearRect(0, 0, cv.width, cv.height);
    g.drawImage(i.canvas, 0, 0, cv.width, cv.height);
    g.globalAlpha = 0.55;
    g.drawImage(i.mask, 0, 0, cv.width, cv.height);
    g.globalAlpha = 1;
  }

  function maskPoint(e) {
    var r = $('me-canvas').getBoundingClientRect();
    var t = e.touches && e.touches.length ? e.touches[0] : e;
    return { x: (t.clientX - r.left) / me.scale, y: (t.clientY - r.top) / me.scale };
  }

  function strokeTo(pt) {
    var g = st.init.mask.getContext('2d'), a = me.last || pt;
    g.globalCompositeOperation = me.erase ? 'destination-out' : 'source-over';
    g.strokeStyle = 'rgb(255,48,48)';
    g.lineWidth = me.brush / me.scale;
    g.lineCap = 'round';
    g.lineJoin = 'round';
    g.beginPath();
    g.moveTo(a.x, a.y);
    g.lineTo(pt.x + 0.01, pt.y);
    g.stroke();
    g.globalCompositeOperation = 'source-over';
    me.last = pt;
    drawMask();
  }

  function maskDown(e) {
    e = e || window.event;
    me.down = true;
    me.last = null;
    strokeTo(maskPoint(e));
    if (e.preventDefault) e.preventDefault();
    return false;
  }

  function maskMove(e) {
    e = e || window.event;
    if (!me.down) return true;
    strokeTo(maskPoint(e));
    if (e.preventDefault) e.preventDefault();
    return false;
  }

  function maskUp() { me.down = false; me.last = null; }

  function invertMask() {
    var m = st.init.mask, g = m.getContext('2d'), d = g.getImageData(0, 0, m.width, m.height), a = d.data;
    for (var i = 0; i < a.length; i += 4) {
      a[i] = 255; a[i + 1] = 48; a[i + 2] = 48; a[i + 3] = 255 - a[i + 3];
    }
    g.putImageData(d, 0, 0);
    drawMask();
  }

  function clearMask() {
    var m = st.init.mask;
    m.getContext('2d').clearRect(0, 0, m.width, m.height);
    drawMask();
  }

  function maskDone() {
    var i = st.init, m = i.mask;
    var a = m.getContext('2d').getImageData(0, 0, m.width, m.height).data, n = 0, on = 0;
    for (var k = 3; k < a.length; k += 64) { n++; if (a[k] > 127) on++; }
    i.painted = n ? on / n : 0;
    if (!on) i.mask = null;
    show('me', false);
    drawInit();
  }

  // ---- the request and putting the result back
  // The box around the painted part of the mask, or null.
  function maskBounds(m) {
    var w = m.width, h = m.height, a = m.getContext('2d').getImageData(0, 0, w, h).data;
    var x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (var y = 0; y < h; y++) {
      for (var x = 0, k = y * w * 4 + 3; x < w; x++, k += 4) {
        if (a[k] > 0) {
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
      }
    }
    return x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
  }

  // b grown to the aspect `ratio` around its middle, and kept inside W x H.
  function growBox(b, ratio, W, H) {
    var w = b.w, h = b.h;
    if (w / h < ratio) w = Math.min(W, Math.round(h * ratio)); else h = Math.min(H, Math.round(w / ratio));
    var x = Math.round(b.x + b.w / 2 - w / 2), y = Math.round(b.y + b.h / 2 - h / 2);
    x = Math.max(0, Math.min(W - w, x));
    y = Math.max(0, Math.min(H - h, y));
    return { x: x, y: y, w: w, h: h };
  }

  // A two-pass box blur of a w x h array in place (radius r), for the mask's soft edge.
  function blur(a, w, h, r) {
    var tmp = new Float32Array(Math.max(w, h)), x, y, sum, n = 2 * r + 1;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) tmp[x] = a[y * w + x];
      sum = 0;
      for (x = -r; x <= r; x++) sum += tmp[Math.min(w - 1, Math.max(0, x))];
      for (x = 0; x < w; x++) {
        a[y * w + x] = sum / n;
        sum += tmp[Math.min(w - 1, x + r + 1)] - tmp[Math.max(0, x - r)];
      }
    }
    for (x = 0; x < w; x++) {
      for (y = 0; y < h; y++) tmp[y] = a[y * w + x];
      sum = 0;
      for (y = -r; y <= r; y++) sum += tmp[Math.min(h - 1, Math.max(0, y))];
      for (y = 0; y < h; y++) {
        a[y * w + x] = sum / n;
        sum += tmp[Math.min(h - 1, y + r + 1)] - tmp[Math.max(0, y - r)];
      }
    }
  }

  // The mask's box (in start-image pixels) as a gray w x h canvas with a soft edge: white changes.
  function softMask(m, box, w, h) {
    var c = canvasOf(w, h), g = c.getContext('2d');
    g.drawImage(m, box.x, box.y, box.w, box.h, 0, 0, w, h);
    var d = g.getImageData(0, 0, w, h), px = d.data, al = new Float32Array(w * h), i;
    for (i = 0; i < w * h; i++) al[i] = px[i * 4 + 3];
    blur(al, w, h, Math.max(2, Math.round(Math.min(w, h) / 96)));
    for (i = 0; i < w * h; i++) {
      var v = Math.round(al[i]);
      px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
      px[i * 4 + 3] = 255;
    }
    g.putImageData(d, 0, 0);
    return c;
  }

  // What a request sends for the start image: {init, mask (data URLs), w, h (the size made), box (the part
  // of the start image it stands for), crop, soft (the soft mask canvas), source (the start image)}.
  function initRequest(f) {
    var i = st.init, W = i.canvas.width, H = i.canvas.height, gw = f.w, gh = f.h, box;
    var ref = refEdit();
    var crop = !!(i.mask && !ref && $('init-crop').checked);
    var b = crop ? maskBounds(i.mask) : null;
    if (b) {
      var pad = Math.max(32, Math.round(Math.max(W, H) / 32));
      var x0 = Math.max(0, b.x - pad), y0 = Math.max(0, b.y - pad);
      var x1 = Math.min(W, b.x + b.w + pad), y1 = Math.min(H, b.y + b.h + pad);
      box = { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
      var g0 = fitArea(box.w, box.h, f.w * f.h);
      box = growBox(box, g0.w / g0.h, W, H);
      var g1 = fitArea(box.w, box.h, f.w * f.h);
      gw = g1.w;
      gh = g1.h;
    } else {
      crop = false;
      // the largest centred part of the start image with the form's aspect
      box = W / H > gw / gh ? { x: Math.round((W - H * gw / gh) / 2), y: 0, w: Math.round(H * gw / gh), h: H }
        : { x: 0, y: Math.round((H - W * gh / gw) / 2), w: W, h: Math.round(W * gh / gw) };
    }
    var ic = canvasOf(gw, gh);
    ic.getContext('2d').drawImage(i.canvas, box.x, box.y, box.w, box.h, 0, 0, gw, gh);
    var out = { init: ic.toDataURL('image/png'), mask: null, soft: null, w: gw, h: gh, box: box, crop: crop, source: i.canvas, ref: ref };
    if (i.mask && !ref) {
      out.soft = softMask(i.mask, box, gw, gh);
      out.mask = out.soft.toDataURL('image/png');
    }
    return out;
  }

  // A result blended over the start image where the mask is: the whole start image for "only the
  // masked area", else the part that was made. cb(base64 PNG).
  function putBackOne(rq, b64, cb) {
    var img = new Image();
    img.onload = function () {
      var tw = rq.crop ? rq.box.w : rq.w, th = rq.crop ? rq.box.h : rq.h;
      var base = canvasOf(tw, th), bg = base.getContext('2d');
      bg.drawImage(rq.source, rq.box.x, rq.box.y, rq.box.w, rq.box.h, 0, 0, tw, th);
      var res = canvasOf(tw, th), rg = res.getContext('2d');
      rg.drawImage(img, 0, 0, tw, th);
      var mk = canvasOf(tw, th), mg = mk.getContext('2d');
      mg.drawImage(rq.soft, 0, 0, tw, th);
      var bd = bg.getImageData(0, 0, tw, th), rd = rg.getImageData(0, 0, tw, th).data, md = mg.getImageData(0, 0, tw, th).data;
      var px = bd.data;
      for (var k = 0; k < px.length; k += 4) {
        var a = md[k] / 255;
        px[k] = px[k] * (1 - a) + rd[k] * a;
        px[k + 1] = px[k + 1] * (1 - a) + rd[k + 1] * a;
        px[k + 2] = px[k + 2] * (1 - a) + rd[k + 2] * a;
      }
      bg.putImageData(bd, 0, 0);
      var outc = base;
      if (rq.crop) {
        outc = canvasOf(rq.source.width, rq.source.height);
        var og = outc.getContext('2d');
        og.drawImage(rq.source, 0, 0);
        og.drawImage(base, rq.box.x, rq.box.y);
      }
      var url = outc.toDataURL('image/png');
      cb(url.substring(url.indexOf(',') + 1));
    };
    img.onerror = function () { cb(b64); };
    img.src = dataUrl(b64);
  }

  function putBack(rq, images, cb) {
    var out = [];
    (function next(k) {
      if (k >= images.length) { cb(out); return; }
      putBackOne(rq, images[k], function (b) { out.push(b); next(k + 1); });
    })(0);
  }

  // The gallery gets the put-together images in place of the raw results the server saved.
  function saveEdits(results, raw) {
    var k = 0;
    (function next() {
      if (k >= results.length) {
        if (raw.length === results.length) {
          for (var j = 0; j < raw.length; j++) req('DELETE', APP + '/gallery/' + encodeURIComponent(raw[j].name), null, function () {});
        }
        setTimeout(function () { loadGallery(null); }, 300);
        if (!st.busy && st.results === results) pickResult(st.pick);
        if (st.runMode) drawStrip();   // a page run's strip gets the names of its images
        return;
      }
      var r = results[k];
      var body = { image: r.src.substring(r.src.indexOf(',') + 1) };
      if (r.params) body.parameters = r.params;
      req('POST', APP + '/gallery', body, function (code, data) {
        if (code === 201 && data && data.saved) {
          r.name = data.saved;
          r.url = '/ps5sd/v1/gallery/' + encodeURIComponent(data.saved);
          k++;
          next();
        } else {
          // an app without the route: the raw results stay in the gallery
          if (raw.length === results.length) for (var j = 0; j < raw.length; j++) { results[j].url = raw[j].url; results[j].name = raw[j].name; }
          if (!st.busy && st.results === results) pickResult(st.pick);
        }
      }, 120000);
    })();
  }

