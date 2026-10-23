  // ---------- zoom in the viewer ----------
  // The wheel or a pinch zooms where it points, a drag moves the zoomed picture, a double click or
  // double tap switches between the whole picture and 100 % (twice the size when the picture is
  // already shown whole). The picture stays over its own box: it can be moved until an edge, not
  // off the screen. A new image or Close shows it whole again.
  var zm = { s: 1, x: 0, y: 0, ptrs: {}, n: 0, pinch: null, drag: null, tap: 0, tapX: 0, tapY: 0, flip: 0 };
  var ZOOM_MAX = 8;

  // The drawn picture inside the img box (object-fit: contain leaves bars at two sides).
  function zoomBox() {
    var img = $('lb-img');
    var bw = img.offsetWidth, bh = img.offsetHeight;
    var nw = img.naturalWidth || bw, nh = img.naturalHeight || bh;
    var ar = nh ? nw / nh : 1;
    var dw = Math.min(bw, bh * ar), dh = ar ? dw / ar : bh;
    return { bw: bw, bh: bh, dw: dw, dh: dh, ox: (bw - dw) / 2, oy: (bh - dh) / 2 };
  }

  // One axis: a picture larger than its box moves at most to an edge; a smaller one stays centred.
  function zoomClamp(t, s, box, drawn, off) {
    if (s * drawn <= box) return (box - s * drawn) / 2 - s * off;
    return Math.min(-s * off, Math.max(box - s * drawn - s * off, t));
  }

  function zoomApply() {
    var img = $('lb-img');
    if (zm.s <= 1.001) {
      zm.s = 1; zm.x = 0; zm.y = 0;
      img.style.transform = '';
      img.style.cursor = '';
      img.style.touchAction = 'pan-y';
      $('lb').className = $('lb').className.replace(/ zoomed/g, '');
      return;
    }
    var b = zoomBox();
    zm.x = zoomClamp(zm.x, zm.s, b.bw, b.dw, b.ox);
    zm.y = zoomClamp(zm.y, zm.s, b.bh, b.dh, b.oy);
    img.style.transformOrigin = '0 0';
    img.style.transform = 'translate(' + zm.x + 'px,' + zm.y + 'px) scale(' + zm.s + ')';
    img.style.cursor = zm.drag ? 'grabbing' : 'grab';
    img.style.touchAction = 'none';
    if ($('lb').className.indexOf(' zoomed') < 0) $('lb').className += ' zoomed';
  }

  function zoomReset() {
    zm.s = 1; zm.ptrs = {}; zm.n = 0; zm.pinch = null; zm.drag = null;
    zoomApply();
  }

  // The scale s with the point (cx, cy) of the page staying where it is.
  function zoomTo(s, cx, cy) {
    var img = $('lb-img');
    s = Math.max(1, Math.min(ZOOM_MAX, s));
    var r = img.getBoundingClientRect();
    var px = cx - (r.left - zm.x), py = cy - (r.top - zm.y);  // the point in the unzoomed box
    zm.x = px - (px - zm.x) * (s / zm.s);
    zm.y = py - (py - zm.y) * (s / zm.s);
    zm.s = s;
    zoomApply();
  }

  // 100 %: the full image's pixels one to one, once the full image is in (not its thumbnail).
  function zoomToggle(cx, cy) {
    var now = new Date().getTime();
    if (now - zm.flip < 400) return;  // a double tap also sends dblclick
    zm.flip = now;
    if (zm.s > 1) { zm.s = 1; zoomApply(); return; }
    var img = $('lb-img');
    var full = img.getAttribute('src') === lbShown ? img.naturalWidth : 0;
    var b = zoomBox();
    var s = full && b.dw ? full / b.dw : 0;
    zoomTo(s > 1.2 ? s : 2, cx, cy);
  }

  function zoomInit() {
    var img = $('lb-img');
    img.style.touchAction = 'pan-y';
    img.ondragstart = function () { return false; };
    img.addEventListener('wheel', function (e) {
      if ($('lb-vid').style.display !== 'none') return;
      e.preventDefault();
      var k = e.deltaY < 0 ? 1.25 : 1 / 1.25;
      zoomTo(zm.s * k, e.clientX, e.clientY);
    }, { passive: false });
    img.addEventListener('dblclick', function (e) { e.preventDefault(); zoomToggle(e.clientX, e.clientY); });
    if (!window.PointerEvent) return;
    img.addEventListener('pointerdown', function (e) {
      if (e.pointerType === 'mouse' && e.button !== 0) return;
      zm.ptrs[e.pointerId] = { x: e.clientX, y: e.clientY };
      zm.n++;
      try { img.setPointerCapture(e.pointerId); } catch (x) {}
      if (zm.n === 2) {
        var p = zoomTwo();
        zm.pinch = { d: p.d, s: zm.s };
        zm.drag = null;
      } else if (zm.n === 1 && zm.s > 1) {
        zm.drag = { x: e.clientX, y: e.clientY };
        zoomApply();
      }
    });
    img.addEventListener('pointermove', function (e) {
      var p0 = zm.ptrs[e.pointerId];
      if (!p0) return;
      var dx = e.clientX - p0.x, dy = e.clientY - p0.y;
      p0.x = e.clientX; p0.y = e.clientY;
      if (zm.pinch && zm.n === 2) {
        var p = zoomTwo();
        if (zm.pinch.d > 0) zoomTo(zm.pinch.s * p.d / zm.pinch.d, p.x, p.y);
        e.preventDefault();
      } else if (zm.drag) {
        zm.x += dx; zm.y += dy;
        zoomApply();
        e.preventDefault();
      }
    });
    function up(e) {
      if (!zm.ptrs[e.pointerId]) return;
      delete zm.ptrs[e.pointerId];
      zm.n = Math.max(0, zm.n - 1);
      if (zm.n < 2) zm.pinch = null;
      if (zm.n === 0) { zm.drag = null; zoomApply(); }
      // a double tap with a finger (the browser does not always send dblclick for it)
      if (e.type === 'pointerup' && e.pointerType === 'touch' && zm.n === 0) {
        var now = new Date().getTime();
        if (now - zm.tap < 300 && Math.abs(e.clientX - zm.tapX) < 30 && Math.abs(e.clientY - zm.tapY) < 30) {
          zm.tap = 0;
          zoomToggle(e.clientX, e.clientY);
        } else {
          zm.tap = now; zm.tapX = e.clientX; zm.tapY = e.clientY;
        }
      }
    }
    img.addEventListener('pointerup', up);
    img.addEventListener('pointercancel', up);
  }

  // The two fingers of a pinch: their distance and the point between them.
  function zoomTwo() {
    var a = null, b = null;
    for (var k in zm.ptrs) {
      if (!zm.ptrs.hasOwnProperty(k)) continue;
      if (!a) a = zm.ptrs[k]; else if (!b) b = zm.ptrs[k];
    }
    if (!a || !b) return { d: 0, x: 0, y: 0 };
    var dx = a.x - b.x, dy = a.y - b.y;
    return { d: Math.sqrt(dx * dx + dy * dy), x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  }
