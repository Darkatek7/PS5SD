  // ---------- gallery ----------
  var galleryList = [];
  var galleryLoaded = false;
  var lbFrom = null;
  // The filter above the grid: all, images, videos or favourites (all but favourites kept in this browser).
  var galleryFilter = load('gfilter') || 'all';
  // The order (kept too) and the search in the prompts (the server gives each file's prompt).
  var galleryOrder = load('gorder') === 'oldest' ? 'oldest' : 'newest';
  var gallerySearch = '';
  // While selecting: {name: true} of the selected files; null otherwise.
  var gsel = null;

  // The names, tags and favourite marks of the files (GET /ps5sd/v1/app/gallery/labels): {name: {title, tags, fav}}.
  var galleryLabels = {};
  // The app keeps favourites (the labels answer says "favourites": true): the star and the filter show only then.
  var galleryFavs = false;

  // The list and the labels, asked for together; the list is drawn when both are in. shared: join a load
  // that is already on its way (the start and a tab switch ask at the same time) instead of asking again.
  var galleryWait = null;
  var GALLERY_PAGE = 1000;     // the most the server gives in one answer
  var GALLERY_MAX = 100000;    // a stop, should a server ignore offset
  function loadGallery(cb, shared) {
    if (shared && galleryWait) { if (cb) galleryWait.push(cb); return; }
    var waiters = galleryWait = cb ? [cb] : [];
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
      if (galleryWait === waiters) galleryWait = null;
      for (var w = 0; w < waiters.length; w++) waiters[w](code === 200 ? galleryList : null);
    }
    // The server gives at most 1000 entries an answer (issue 22), so the list comes in pages, newest
    // first, until a page is short. A file made between two pages moves the rest by one: the names
    // already in are skipped.
    var all = [], seen = {};
    function page(offset) {
      req('GET', '/ps5sd/v1/gallery?limit=' + GALLERY_PAGE + '&offset=' + offset, null, function (c, data) {
        if (c !== 200 || !data || data.length === undefined) {
          // a later page that fails keeps what came in
          if (offset) { code = 200; list = all; } else { code = c; list = data; }
          both();
          return;
        }
        for (var i = 0; i < data.length; i++) {
          if (data[i] && !seen[data[i].name]) { seen[data[i].name] = true; all.push(data[i]); }
        }
        if (data.length >= GALLERY_PAGE && offset + data.length < GALLERY_MAX) { page(offset + data.length); return; }
        code = 200; list = all; both();
      });
    }
    page(0);
    req('GET', APP + '/gallery/labels', null, function (c, data) {
      if (c === 200 && data) {
        if (data.labels) galleryLabels = data.labels;
        galleryFavs = data.favourites === true;
      }
      labelsIn = true;
      both();
    });
  }

  function labelItem(it) {
    var l = galleryLabels[it.name];
    it.title = l && l.title ? l.title : '';
    it.tags = l && l.tags ? l.tags : [];
    it.fav = !!(l && l.fav);
  }

  function refreshGallery() { loadGallery(null, true); }

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

  function countFavs() {
    var n = 0;
    for (var i = 0; i < galleryList.length; i++) if (galleryList[i].fav) n++;
    return n;
  }

  // The filter that applies. With no video in the gallery there is nothing to filter by: All. Favourites
  // only where the app keeps them.
  function galleryKind() {
    if (galleryFilter === 'fav') return galleryFavs ? 'fav' : 'all';
    if (galleryFilter !== 'images' && galleryFilter !== 'videos') return 'all';
    return countVideos() ? galleryFilter : 'all';
  }

  // Does the item belong to the filter kind (galleryKind())?
  function kindHas(kind, it) {
    if (kind === 'fav') return !!it.fav;
    if (kind === 'images' || kind === 'videos') return isVideo(it) === (kind === 'videos');
    return true;
  }

  // The items the grid and the viewer step through: the filter, the search, the order.
  // galleryList itself stays newest first (the Create tab finds new files in it).
  function galleryView() {
    var kind = galleryKind(), q = gallerySearch.toLowerCase(), out = [];
    for (var i = 0; i < galleryList.length; i++) {
      var it = galleryList[i];
      if (!kindHas(kind, it)) continue;
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
    save('gfilter', kind === 'fav' ? 'all' : kind);   // Favourites is for now, not the next visit
    st.limit = PAGE;
    drawGallery();
  }

  // The viewer on an item of the gallery (a strip image, an upscale result). The filter goes to All
  // when the item is not in the one on screen.
  function openLbItem(it, from) {
    if (!kindHas(galleryKind(), it)) setGalleryFilter('all');
    if (gallerySearch && galleryView().indexOf(it) < 0) { gallerySearch = ''; $('gsearch').value = ''; drawGallery(); }
    var i = galleryView().indexOf(it);
    if (i >= 0) openLb(i, from);
  }

  // The segmented control above the grid: All, Images, Videos and Favourites, each with its count. The
  // images and videos are hidden without videos, Favourites without the app's support; the whole control
  // when there is nothing to choose between.
  function drawFilter(videos) {
    var kind = galleryKind();
    var favs = galleryFavs ? countFavs() : 0;
    var ids = { all: 'gf-all', images: 'gf-images', videos: 'gf-videos', fav: 'gf-fav' };
    show('gfilter', videos > 0 || favs > 0 || kind === 'fav', 'flex');
    show('gf-images', videos > 0 || kind === 'images', 'block');
    show('gf-videos', videos > 0 || kind === 'videos', 'block');
    show('gf-fav', galleryFavs, 'block');
    text($('gf-all'), 'All ' + galleryList.length);
    text($('gf-images'), 'Images ' + (galleryList.length - videos));
    text($('gf-videos'), 'Videos ' + videos);
    text($('gf-fav'), '★ ' + favs);
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
    show('gdlall', galleryList.length > 0, 'inline-block');
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
        if (galleryFavs) cell.appendChild(favButton(item, 'tfav'));
        box.appendChild(cell);
      })(view[i], i);
    }
    text($('gcount'), galleryList.length ? '(' + galleryList.length + ')' : '');
    text($('none'), gallerySearch ? 'Nothing matches "' + gallerySearch + '".'
      : kind === 'images' ? 'No images saved yet.' : kind === 'videos' ? 'No videos saved yet.'
      : kind === 'fav' ? 'No favourites yet. Press the star on an image.' : 'Nothing saved yet.');
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
    if (on) enterStorage();   // the drives for Copy to USB, and a copy that already runs
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
    drawSelUsb();
    if (!gsel) return;
    var n = selNames().length;
    text($('gsel-n'), n ? n + ' selected' : 'Press the images to select them.');
    $('gsel-dl').disabled = !n;
    text($('gsel-dl'), n > 1 ? 'Download ZIP' : 'Download');
    $('gsel-del').disabled = !n;
  }

  // The files as one ZIP (names), or the whole gallery (null). The console makes it while the browser
  // saves it, so nothing is held in this page: a form into a hidden frame. The page stays as it is,
  // and the login cookie goes along.
  function downloadArchive(names) {
    var f = document.createElement('form');
    f.method = names ? 'post' : 'get';
    f.action = '/ps5sd/v1/gallery/archive';
    f.target = 'gdlframe';
    f.style.display = 'none';
    if (names) {
      var inp = document.createElement('input');
      inp.type = 'hidden';
      inp.name = 'names';
      inp.value = names.join('\n');
      f.appendChild(inp);
    }
    document.body.appendChild(f);
    f.submit();
    setTimeout(function () { document.body.removeChild(f); }, 1000);
  }

  function downloadAll() {
    downloadArchive(null);
    toast('Downloading the gallery as a ZIP. It has no size until it is done.', 5000);
  }

  // One file as it is; several as one ZIP.
  function downloadSelected() {
    var list = selNames();
    if (!list.length) return;
    if (list.length === 1) {
      var a = document.createElement('a');
      a.href = list[0].url;
      a.setAttribute('download', list[0].name);
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      toast('Downloading 1 file.');
      return;
    }
    var names = [];
    for (var i = 0; i < list.length; i++) names.push(list[i].name);
    downloadArchive(names);
    toast('Downloading ' + list.length + ' files as a ZIP. It has no size until it is done.', 5000);
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
    drawLbFav(it);
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
    if (liFor) {
      if (k === 27) { closeLoraInfo(); return false; }
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
        var ring = [$('lb-prev'), $('lb-dl'), $('lb-open'), $('lb-up'), $('lb-edit'), $('lb-guide'), $('lb-label'), $('lb-fav'), $('lb-infobtn'), $('lb-del'), $('lb-close'),
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

