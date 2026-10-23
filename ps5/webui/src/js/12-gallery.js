  // ---------- gallery ----------
  var galleryList = [];
  var galleryLoaded = false;
  var lbFrom = null;
  // The filter above the grid: all, images or videos (kept in this browser).
  var galleryFilter = load('gfilter') || 'all';
  // The order (kept too) and the search in the prompts (the server gives each file's prompt).
  var galleryOrder = load('gorder') === 'oldest' ? 'oldest' : 'newest';
  var gallerySearch = '';
  // While selecting: {name: true} of the selected files; null otherwise.
  var gsel = null;

  // The names and tags of the files (GET /ps5sd/v1/app/gallery/labels): {name: {title, tags}}.
  var galleryLabels = {};

  // The list and the labels, asked for together; the list is drawn when both are in.
  function loadGallery(cb) {
    var list = null, code = -1, labelsIn = false;
    function both() {
      if (code < 0 || !labelsIn) return;
      if (code === 200 && list && list.length !== undefined) {
        galleryList = list;
        galleryLoaded = true;
      } else if (code === 200) {
        galleryList = []; galleryLoaded = true;
      }
      for (var i = 0; i < galleryList.length; i++) labelItem(galleryList[i]);
      drawGallery();
      if (cb) cb(code === 200 ? galleryList : null);
    }
    req('GET', '/ps5sd/v1/gallery?limit=1000', null, function (c, data) { code = c; list = data; both(); });
    req('GET', APP + '/gallery/labels', null, function (c, data) {
      if (c === 200 && data && data.labels) galleryLabels = data.labels;
      labelsIn = true;
      both();
    });
  }

  function labelItem(it) {
    var l = galleryLabels[it.name];
    it.title = l && l.title ? l.title : '';
    it.tags = l && l.tags ? l.tags : [];
  }

  function refreshGallery() { loadGallery(null); }

  // A video in the gallery: by its type (the server's), else its name.
  function isVideo(item) {
    if (item.type) return item.type.indexOf('video/') === 0;
    return /\.(webm|mp4|avi)$/i.test(item.name || '');
  }

  function countVideos() {
    var n = 0;
    for (var i = 0; i < galleryList.length; i++) if (isVideo(galleryList[i])) n++;
    return n;
  }

  // The filter that applies. With no video in the gallery there is nothing to filter by: All.
  function galleryKind() {
    if (galleryFilter !== 'images' && galleryFilter !== 'videos') return 'all';
    return countVideos() ? galleryFilter : 'all';
  }

  // The items the grid and the viewer step through: the filter, the search, the order.
  // galleryList itself stays newest first (the Create tab finds new files in it).
  function galleryView() {
    var kind = galleryKind(), q = gallerySearch.toLowerCase(), out = [];
    for (var i = 0; i < galleryList.length; i++) {
      var it = galleryList[i];
      if (kind !== 'all' && isVideo(it) !== (kind === 'videos')) continue;
      if (q && !gallerySearchHits(it, q)) continue;
      out.push(it);
    }
    if (galleryOrder === 'oldest') out.reverse();
    return out;
  }

  function setGalleryOrder(order) {
    galleryOrder = order === 'oldest' ? 'oldest' : 'newest';
    save('gorder', galleryOrder);
    st.limit = PAGE;
    drawGallery();
  }

  // The search: "#night" is the tag night; any other text is looked for in the prompt, the name and
  // the tags. q is in lower case.
  function gallerySearchHits(it, q) {
    var tags = it.tags || [];
    if (q.charAt(0) === '#' && q.length > 1 && q.indexOf(' ') < 0) {
      for (var i = 0; i < tags.length; i++) if (tags[i].toLowerCase() === q.substring(1)) return true;
      return false;
    }
    var all = String(it.prompt || '') + '\n' + String(it.title || '') + '\n' + tags.join('\n');
    return all.toLowerCase().indexOf(q) >= 0;
  }

  function setGallerySearchNow(q) {
    if (searchTimer) { clearTimeout(searchTimer); searchTimer = null; }
    $('gsearch').value = q;
    gallerySearch = q;
    st.limit = PAGE;
    drawGallery();
  }

  var searchTimer = null;
  function setGallerySearch(q) {
    if (searchTimer) clearTimeout(searchTimer);
    searchTimer = setTimeout(function () {
      searchTimer = null;
      gallerySearch = q.replace(/^\s+|\s+$/g, '');
      st.limit = PAGE;
      drawGallery();
    }, 250);
  }

  // The item the viewer shows: st.lb is an index into galleryView().
  function lbItem() { return galleryView()[st.lb]; }

  function setGalleryFilter(kind) {
    galleryFilter = kind;
    save('gfilter', kind);
    st.limit = PAGE;
    drawGallery();
  }

  // The viewer on an item of the gallery (a strip image, an upscale result). The filter goes to All
  // when the item is not in the one on screen.
  function openLbItem(it, from) {
    var kind = galleryKind();
    if (kind !== 'all' && isVideo(it) !== (kind === 'videos')) setGalleryFilter('all');
    if (gallerySearch && galleryView().indexOf(it) < 0) { gallerySearch = ''; $('gsearch').value = ''; drawGallery(); }
    var i = galleryView().indexOf(it);
    if (i >= 0) openLb(i, from);
  }

  // The segmented control above the grid: All, Images, Videos, each with its count. Hidden without videos.
  function drawFilter(videos) {
    var kind = galleryKind();
    var ids = { all: 'gf-all', images: 'gf-images', videos: 'gf-videos' };
    show('gfilter', videos > 0, 'flex');
    text($('gf-all'), 'All ' + galleryList.length);
    text($('gf-images'), 'Images ' + (galleryList.length - videos));
    text($('gf-videos'), 'Videos ' + videos);
    for (var k in ids) {
      if (!ids.hasOwnProperty(k)) continue;
      $(ids[k]).className = k === kind ? 'on' : '';
      $(ids[k]).setAttribute('aria-pressed', k === kind ? 'true' : 'false');
    }
  }

  function drawGallery() {
    var box = $('thumbs');
    clear(box);
    var kind = galleryKind();
    var view = galleryView();
    drawFilter(countVideos());
    var shown = Math.min(view.length, st.limit);
    for (var i = 0; i < shown; i++) {
      (function (item, idx) {
        var cell = document.createElement('div');
        cell.className = gsel && gsel[item.name] ? 't sel' : 't';
        var a = document.createElement('a');
        a.href = item.url;
        a.title = item.title || item.prompt || item.name;
        // the thumbnail (0.9.0); the file itself when there is none
        if (item.thumb) {
          var img = document.createElement('img');
          img.alt = item.name;
          img.setAttribute('loading', 'lazy');
          img.onerror = function () { clear(a); fullCell(a, item); };
          img.src = item.thumb;
          a.appendChild(img);
          if (isVideo(item)) a.appendChild(el('span', 'vbadge', '▶'));
        } else {
          fullCell(a, item);
        }
        a.onclick = function () {
          if (gsel) { toggleSel(item, cell); return false; }
          openLb(idx, a);
          return false;
        };
        cell.appendChild(a);
        if (item.title) cell.appendChild(el('span', 'tname', item.title));
        box.appendChild(cell);
      })(view[i], i);
    }
    text($('gcount'), galleryList.length ? '(' + galleryList.length + ')' : '');
    text($('none'), gallerySearch ? 'Nothing matches "' + gallerySearch + '".'
      : kind === 'images' ? 'No images saved yet.' : kind === 'videos' ? 'No videos saved yet.' : 'Nothing saved yet.');
    show('none', !view.length);
    show('more', view.length > shown, 'inline-block');
    drawSelBar();
  }

  // A grid cell with the file itself: the image, or the video's first frame.
  function fullCell(a, item) {
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
  }

  // ---- several files at once: Select, then Download or Delete
  function selNames() {
    var out = [];
    if (!gsel) return out;
    for (var i = 0; i < galleryList.length; i++) if (gsel[galleryList[i].name]) out.push(galleryList[i]);
    return out;
  }

  function setSelecting(on) {
    gsel = on ? {} : null;
    text($('gsel'), on ? 'Cancel' : 'Select');
    $('gsel').setAttribute('aria-pressed', on ? 'true' : 'false');
    drawGallery();
  }

  function toggleSel(item, cell) {
    if (gsel[item.name]) delete gsel[item.name]; else gsel[item.name] = true;
    cell.className = gsel[item.name] ? 't sel' : 't';
    drawSelBar();
  }

  function selectAll() {
    var view = galleryView(), all = true;
    for (var i = 0; i < view.length; i++) if (!gsel[view[i].name]) all = false;
    for (var j = 0; j < view.length; j++) { if (all) delete gsel[view[j].name]; else gsel[view[j].name] = true; }
    st.limit = Math.max(st.limit, view.length);
    drawGallery();
  }

  function drawSelBar() {
    show('gselbar', !!gsel, 'flex');
    if (!gsel) return;
    var n = selNames().length;
    text($('gsel-n'), n ? n + ' selected' : 'Press the images to select them.');
    $('gsel-dl').disabled = !n;
    $('gsel-del').disabled = !n;
  }

  // One download after the other: a browser takes several clicks at once badly.
  function downloadSelected() {
    var list = selNames(), k = 0;
    (function next() {
      if (k >= list.length) return;
      var a = document.createElement('a');
      a.href = list[k].url;
      a.setAttribute('download', list[k].name);
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      k++;
      setTimeout(next, 600);
    })();
    toast('Downloading ' + list.length + (list.length === 1 ? ' file.' : ' files.'));
  }

  function askDeleteSelected() {
    var list = selNames();
    if (!list.length) return;
    dlgFor = { images: list };
    dlgFrom = $('gsel-del');
    text($('dlg-title'), 'Delete ' + list.length + (list.length === 1 ? ' file?' : ' files?'));
    text($('dlg-body'), 'They are deleted from the console. Download them first to keep a copy.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Delete');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function deleteImages(list) {
    var k = 0, failed = 0;
    (function next() {
      if (k >= list.length) {
        closeDlg();
        setSelecting(false);
        toast('Deleted ' + (list.length - failed) + (list.length - failed === 1 ? ' file' : ' files') +
          (failed ? '; ' + failed + ' could not be deleted.' : '.'), failed ? 6000 : 3000);
        return;
      }
      var it = list[k];
      text($('dlg-ok'), 'Deleting ' + (k + 1) + ' of ' + list.length + '…');
      req('DELETE', '/ps5sd/v1/app/gallery/' + encodeURIComponent(it.name), null, function (code) {
        if (code === 200 || code === 404) {
          for (var i = 0; i < galleryList.length; i++) {
            if (galleryList[i].name === it.name) { galleryList.splice(i, 1); break; }
          }
        } else {
          failed++;
        }
        k++;
        next();
      }, 30000);
    })();
  }

  function when(sec) {
    try { return new Date(sec * 1000).toLocaleString(); } catch (e) { return ''; }
  }

  // the URL of the item in the viewer, for the full image that arrives after the thumbnail
  var lbShown = null;

  function showLb(i) {
    var list = galleryView();
    var n = list.length;
    if (!n) { closeLb(); return; }
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    st.lb = i;
    var it = list[i];
    lbShown = it.url;
    zoomReset();
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
      img.alt = it.name;
      // the thumbnail at once and the image when it is in: a PNG of several MB takes a while on a
      // phone, and the viewer keeps its shape meanwhile
      if (it.thumb && img.getAttribute('src') !== it.url) {
        img.src = it.thumb;
        var full = new Image();
        full.onload = full.onerror = function () {
          if (lbShown === it.url) img.src = it.url;
        };
        full.src = it.url;
      } else {
        img.src = it.url;
      }
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
    drawLbInfo(it);
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
    lbShown = null;
    zoomReset();
    $('lb-img').removeAttribute('src');
    try { $('lb-vid').pause(); } catch (e) {}
    $('lb-vid').removeAttribute('src');
    $('lb-vid').load();
    if (lbFrom) { try { lbFrom.focus(); } catch (e) {} }
  }

  // Delete in the viewer: the confirm box, then the next older image (or the
  // newer one at the end, or the gallery when it was the last).
  function askDeleteImage() {
    var it = lbItem();
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
          if (galleryView().length) showLb(st.lb); else closeLb();
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
    if (labelFor) {
      if (k === 27) { closeLabels(); return false; }
      if (k === 13 && document.activeElement && document.activeElement.tagName === 'INPUT') { saveLabels(); return false; }
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
        var ring = [$('lb-prev'), $('lb-dl'), $('lb-open'), $('lb-up'), $('lb-edit'), $('lb-guide'), $('lb-label'), $('lb-del'), $('lb-close'),
          $('lb-next'), $('lb-cp'), $('lb-cn'), $('lb-ca'), $('lb-use')];
        var live = [];
        for (var i = 0; i < ring.length; i++) if (!ring[i].disabled && ring[i].style.display !== 'none' && ring[i].offsetParent !== null) live.push(ring[i]);
        var at = -1;
        for (var j = 0; j < live.length; j++) if (live[j] === document.activeElement) at = j;
        var to = e.shiftKey ? (at <= 0 ? live.length - 1 : at - 1) : (at < 0 || at >= live.length - 1 ? 0 : at + 1);
        live[to].focus();
        return false;
      }
    }
    return true;
  }

