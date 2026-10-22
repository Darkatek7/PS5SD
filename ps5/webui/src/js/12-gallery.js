  // ---------- gallery ----------
  var galleryList = [];
  var galleryLoaded = false;
  var lbFrom = null;

  function loadGallery(cb) {
    req('GET', '/ps5sd/v1/gallery?limit=200', null, function (code, data) {
      if (code === 200 && data && data.length !== undefined) {
        galleryList = data;
        galleryLoaded = true;
      } else if (code === 200) {
        galleryList = []; galleryLoaded = true;
      }
      drawGallery();
      if (cb) cb(code === 200 ? galleryList : null);
    });
  }

  function refreshGallery() { loadGallery(null); }

  // A video in the gallery: by its type (the server's), else its name.
  function isVideo(item) {
    if (item.type) return item.type.indexOf('video/') === 0;
    return /\.(webm|avi)$/i.test(item.name || '');
  }

  function drawGallery() {
    var box = $('thumbs');
    clear(box);
    var shown = Math.min(galleryList.length, st.limit);
    for (var i = 0; i < shown; i++) {
      (function (item, idx) {
        var cell = document.createElement('div');
        cell.className = 't';
        var a = document.createElement('a');
        a.href = item.url;
        a.title = item.name;
        if (isVideo(item)) {
          var vid = document.createElement('video');
          vid.muted = true;
          vid.setAttribute('muted', '');
          vid.setAttribute('preload', 'metadata');
          vid.setAttribute('playsinline', '');
          vid.src = item.url;
          a.appendChild(vid);
          a.appendChild(el('span', 'vbadge', '▶'));
        } else {
          var img = document.createElement('img');
          img.src = item.url;
          img.alt = item.name;
          a.appendChild(img);
        }
        a.onclick = function () { openLb(idx, a); return false; };
        cell.appendChild(a);
        box.appendChild(cell);
      })(galleryList[i], i);
    }
    text($('gcount'), galleryList.length ? '(' + galleryList.length + ')' : '');
    show('none', !galleryList.length);
    show('more', galleryList.length > shown, 'inline-block');
  }

  function when(sec) {
    try { return new Date(sec * 1000).toLocaleString(); } catch (e) { return ''; }
  }

  function showLb(i) {
    var n = galleryList.length;
    if (!n) { closeLb(); return; }
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    st.lb = i;
    var it = galleryList[i];
    var img = $('lb-img'), vid = $('lb-vid');
    if (isVideo(it)) {
      img.style.display = 'none';
      img.removeAttribute('src');
      vid.style.display = 'block';
      if (vid.getAttribute('src') !== it.url) vid.src = it.url;
    } else {
      vid.pause();
      vid.removeAttribute('src');
      vid.load();
      vid.style.display = 'none';
      img.style.display = '';
      img.src = it.url;
      img.alt = it.name;
    }
    // Upscale and Edit take images only
    $('lb-edit').style.display = isVideo(it) ? 'none' : '';
    $('lb-guide').style.display = isVideo(it) ? 'none' : '';
    show('lb-up', !isVideo(it) && upModels.length > 0, 'inline-block');
    var dl = $('lb-dl');
    dl.href = it.url;
    dl.setAttribute('download', it.name);
    $('lb-open').href = it.url;
    text($('lb-title'), (i + 1) + ' / ' + n + ' · ' + it.name + ' · ' + Math.round(it.size / 1024) + ' KB · ' + when(it.created));
    $('lb-prev').disabled = i <= 0;
    $('lb-next').disabled = i >= n - 1;
  }

  function openLb(i, from) {
    lbFrom = from || null;
    showLb(i);
    show('lb', true, 'flex');
    $('lb-close').focus();
  }

  function closeLb() {
    show('lb', false);
    st.lb = -1;
    $('lb-img').removeAttribute('src');
    try { $('lb-vid').pause(); } catch (e) {}
    $('lb-vid').removeAttribute('src');
    $('lb-vid').load();
    if (lbFrom) { try { lbFrom.focus(); } catch (e) {} }
  }

  // Delete in the viewer: the confirm box, then the next older image (or the
  // newer one at the end, or the gallery when it was the last).
  function askDeleteImage() {
    var it = galleryList[st.lb];
    if (!it) return;
    dlgFor = { image: it };
    dlgFrom = $('lb-del');
    text($('dlg-title'), 'Delete ' + it.name + '?');
    text($('dlg-body'), isVideo(it)
      ? 'The video is deleted from the console. Download it first to keep a copy.'
      : 'The image is deleted from the console. Download it first to keep a copy.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function deleteImage(it) {
    req('DELETE', '/ps5sd/v1/app/gallery/' + encodeURIComponent(it.name), null, function (code, data) {
      closeDlg();
      if (code === 200 || code === 404) {
        for (var i = 0; i < galleryList.length; i++) {
          if (galleryList[i].name === it.name) { galleryList.splice(i, 1); break; }
        }
        drawGallery();
        if (st.lb >= 0) {
          if (galleryList.length) showLb(st.lb); else closeLb();
        }
        toast('Deleted ' + it.name + '.');
      } else {
        toast(errText(code, data, 'Could not delete ' + it.name), 5000);
      }
    }, 30000);
  }

  function lbStep(d) {
    showLb(st.lb + d);
    var b = d < 0 ? $('lb-prev') : $('lb-next');
    if (b.disabled) $('lb-close').focus();
  }

  function onKey(e) {
    e = e || window.event;
    var k = e.keyCode;
    if (peFor) {
      if (k === 27) { closeEdit(); return false; }
      return true;
    }
    if ($('me').style.display !== 'none') {
      if (k === 27) { maskDone(); return false; }
      return true;
    }
    if (dlgFor) {
      var cb = $('dlg-cancel'), ok = $('dlg-ok');
      if (cb.disabled) return false;
      if (k === 27) { closeDlg(); return false; }
      if (k === 37) { cb.focus(); return false; }
      if (k === 39) { ok.focus(); return false; }
      if (k === 9) { (document.activeElement === cb ? ok : cb).focus(); return false; }
      return true;
    }
    if (st.lb >= 0) {
      if (k === 27) { closeLb(); return false; }
      if (k === 37) { lbStep(-1); return false; }
      if (k === 39) { lbStep(1); return false; }
      if (k === 46) { askDeleteImage(); return false; }
      if (k === 9) {
        // keep Tab inside the viewer
        var ring = [$('lb-prev'), $('lb-dl'), $('lb-open'), $('lb-up'), $('lb-edit'), $('lb-guide'), $('lb-del'), $('lb-close'), $('lb-next')];
        var live = [];
        for (var i = 0; i < ring.length; i++) if (!ring[i].disabled && ring[i].style.display !== 'none') live.push(ring[i]);
        var at = -1;
        for (var j = 0; j < live.length; j++) if (live[j] === document.activeElement) at = j;
        var to = e.shiftKey ? (at <= 0 ? live.length - 1 : at - 1) : (at < 0 || at >= live.length - 1 ? 0 : at + 1);
        live[to].focus();
        return false;
      }
    }
    return true;
  }

