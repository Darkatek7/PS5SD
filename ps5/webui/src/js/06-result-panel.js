  // ---------- result panel ----------
  function status(message, isError) {
    var el = $('status');
    text(el, message);
    el.className = isError ? 'error' : '';
  }

  function progress(fraction) {
    $('fill').style.width = Math.max(0, Math.min(100, Math.round(fraction * 100))) + '%';
    $('bar').className = 'bar' + (st.busy && fraction <= 0 ? ' ind' : '');
  }

  // dims: the size a live preview is drawn at (the size of the final image: the preview is a small
  // picture, and CSS scales it up). A finished image has none and keeps its own size.
  // The strip under the stage: shown or hidden; the stage makes room for it (see app.css, with-strip).
  function stripBox(on) {
    show('strip', on, 'flex');
    $('stage').className = on ? 'stage with-strip' : 'stage';
  }

  function showImage(src, label, dims) {
    var stage = $('stage');
    var img = stage.getElementsByTagName('img')[0];
    if (!img) {
      freeVideo();
      clear(stage);
      img = document.createElement('img');
      stage.appendChild(img);
    }
    img.alt = label || 'generated image';
    img.style.width = dims ? dims.w + 'px' : '';
    img.style.height = dims ? dims.h + 'px' : '';
    img.onload = label === 'preview' ? null : function () { st.shownDims = { w: img.naturalWidth, h: img.naturalHeight }; };
    img.src = src;
  }

  function link(href, label, name, blank) {
    var a = document.createElement('a');
    a.href = href;
    if (name) a.setAttribute('download', name);
    if (blank) a.target = '_blank';
    text(a, label);
    return a;
  }

  function actionButton(label, fn) {
    var b = document.createElement('button');
    b.type = 'button';
    text(b, label);
    b.onclick = fn;
    return b;
  }

  // ---- the row under the result: one line of buttons, each an icon and a short label, and a ⋯ menu
  // for the rarer ones (Use seed, Use all, Open full size, Guide, Keep on the console).
  var ICONS = {
    download: '<path d="M12 4v11"/><path d="M7.5 11l4.5 4.5 4.5-4.5"/><path d="M5 20h14"/>',
    again: '<path d="M20 11a8 8 0 1 0-2.3 5.7"/><path d="M20 4v7h-7"/>',
    upscale: '<path d="M14 4h6v6"/><path d="M20 4l-7 7"/><path d="M10 20H4v-6"/><path d="M4 20l7-7"/>',
    edit: '<path d="M4 20l1-4L16 5l3 3L8 19z"/><path d="M14 7l3 3"/>',
    trash: '<path d="M4 7h16"/><path d="M9 7V4h6v3"/><path d="M6 7l1 13h10l1-13"/><path d="M10 11v6M14 11v6"/>'
  };

  // The label of a button of the row (its .lbl span), else the button's own text. The icon stays.
  function getLabel(b) {
    var l = b.getElementsByClassName ? b.getElementsByClassName('lbl')[0] : null;
    return l ? l.textContent : b.textContent;
  }
  function setLabel(b, label) {
    var l = b.getElementsByClassName ? b.getElementsByClassName('lbl')[0] : null;
    text(l || b, label);
  }

  // A button of the row: the icon, then the label.
  function chip(label, icon, fn, title) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = 'ab';
    b.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true">' + ICONS[icon] + '</svg><span class="lbl"></span>';
    setLabel(b, label);
    if (title) b.title = title;
    b.onclick = fn;
    return b;
  }

  function chipLink(href, label, icon, name) {
    var a = document.createElement('a');
    a.className = 'ab';
    a.href = href;
    if (name) a.setAttribute('download', name);
    a.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true">' + ICONS[icon] + '</svg><span class="lbl"></span>';
    setLabel(a, label);
    return a;
  }

  // The row under the result, for an image or video of a run (withParams: Run again and Use seed need the
  // settings of the run) or of the gallery (gal: the newest file shown after a reload; Use all joins it
  // once the file's info is in, st.actAll). openHref is the file on the console: Open full size and Delete.
  function showActions(href, name, openHref, withParams, gal) {
    var box = $('actions');
    if (openMenu && within(box, openMenu.btn)) closeMenu();
    clear(box);
    if (!href) return;
    st.actFor = gal || null;
    var main = [], more = [], params = !!(withParams && st.last);
    var r = params ? st.results && st.results[st.pick] : null;
    var src = r && r.url ? r.url : href, srcName = r ? r.name : gal ? gal.name : '';
    var vid = gal ? isVideo(gal) : st.video;
    main.push(chipLink(href, 'Download', 'download', name));
    if (params) {
      main.push(chip('Run again', 'again', function () {
        setForm(st.last); $('prompt').value = st.last.template !== undefined ? st.last.template : st.last.prompt;
        $('neg').value = st.last.negTemplate !== undefined ? st.last.negTemplate : st.last.negative; negSync(); generate();
      }, 'Make it again with the same settings'));
      var seed = r ? r.seed : st.last.seed;
      // a run's image from the server has no seed the page knows
      if (seed !== null && seed !== undefined && seed !== '') more.push({ label: 'Use seed ' + seed, fn: function () {
        $('seed').value = seed; persist(); toast('Seed ' + seed + ' is set. Open Settings to see it.');
      } });
    }
    if ((params || gal) && !vid) {
      if (upModels.length) {
        var ub = chip('Upscale', 'upscale', function () { upscale(src, ub); }, 'Make it larger with the upscaler picked in Settings');
        main.push(ub);
      }
      main.push(chip('Edit', 'edit', function () { setInit(src, srcName); }, 'Use it as the start image'));
      if (st.cnModel) more.push({ label: 'Guide', fn: function () { setGuide(src, srcName); } });
    }
    if (gal && st.actAll === gal.name) more.push({ label: 'Use all', fn: function () { useSettings(gal); } });
    if (openHref) more.push({ label: 'Open full size', fn: function () { window.open(openHref, '_blank'); } });
    // not kept on the console (yet)
    var kr = st.vidResult && st.vidResult.temp && !st.results ? st.vidResult : st.results && st.results[st.pick];
    var temp = !!(kr && kr.temp);
    if (temp) more.push({ label: 'Keep on the console', fn: function () { keepResult(kr, {}); } });
    for (var i = 0; i < main.length; i++) box.appendChild(main[i]);
    if (more.length) {
      var m = moreMenu(more, temp ? 'Keep on the console puts it in the gallery; otherwise it is deleted when PS5SD starts again.' : '');
      m.lastChild.className += ' up';   // the menu opens upward: the row is at the foot of the page
      box.appendChild(m);
    }
    // a file on the console (in the gallery, or not kept yet): Delete, a second press to be sure
    if (openHref && name) {
      var db = chip('Delete', 'trash', function () { deleteResult(name, db); }, 'Delete the file from the console');
      db.className = 'ab danger';
      box.appendChild(db);
    }
  }

  // Delete under the result: the first press asks ("Delete?") for 4 s, the second deletes the file.
  // The image leaves the stage, the strip and the gallery; the next one of the run shows, if any.
  function deleteResult(name, b) {
    if (!b.armed) {
      b.armed = true;
      setLabel(b, 'Sure?');
      b.title = 'Press again to delete the file from the console';
      b.className = 'ab danger armed';
      b.armTimer = setTimeout(function () { b.armed = false; setLabel(b, 'Delete'); b.className = 'ab danger'; }, 4000);
      return;
    }
    clearTimeout(b.armTimer);
    b.disabled = true;
    req('DELETE', '/ps5sd/v1/app/gallery/' + encodeURIComponent(name), null, function (code, data) {
      if (code !== 200 && code !== 404) {
        b.disabled = false;
        b.armed = false;
        setLabel(b, 'Delete');
        b.className = 'ab danger';
        toast(errText(code, data, 'Could not delete ' + name), 5000);
        return;
      }
      for (var g = 0; g < galleryList.length; g++) {
        if (galleryList[g].name === name) { galleryList.splice(g, 1); break; }
      }
      drawGallery();
      var list = st.results, at = -1;
      for (var i = 0; list && i < list.length; i++) if (list[i].name === name) { at = i; break; }
      if (at >= 0) list.splice(at, 1);
      if (st.vidResult && st.vidResult.name === name) st.vidResult = null;
      toast('Deleted ' + name + '.');
      if (list && list.length) {
        var next = Math.min(at < 0 ? st.pick : at, list.length - 1);
        drawStrip();
        pickResult(next);
        return;
      }
      if (list) stripBox(false);
      freeVideo();
      var stage = $('stage');
      clear(stage);
      stage.appendChild(el('div', 'empty', 'Deleted. Your next image appears here.'));
      clear($('actions'));
      status('Deleted.');
      text($('sub'), '');
    }, 30000);
  }


  // The last image is shown when the gallery (bit 1) and the server's info (bit 2: Upscale and Guide depend
  // on it) are both in.
  var lastReady = 0;
  function lastItemReady(bit) {
    lastReady |= bit;
    if (lastReady === 3) { lastReady = 4; showLastItem(); }
  }

  // After a reload the stage is empty although the gallery has files: the newest one stands there, with the
  // actions of a gallery file (the viewer's: Edit, Upscale, Use all). Called once, when the gallery is in.
  // Not while a job runs or is followed, and not over a result.
  function showLastItem() {
    var stage = $('stage');
    if (st.busy || st.results || st.vidResult || st.run || load('job') || !galleryList.length) return;
    if (!stage.getElementsByClassName('empty')[0]) return;
    var it = galleryList[0];
    if (isVideo(it)) {
      freeVideo();
      clear(stage);
      var v = document.createElement('video');
      v.controls = true;
      v.loop = true;
      v.muted = true;
      v.setAttribute('muted', '');
      v.setAttribute('playsinline', '');
      v.setAttribute('preload', 'metadata');
      if (it.thumb) v.poster = it.thumb;
      v.src = it.url;
      stage.appendChild(v);
    } else {
      showImage(it.url, 'last image', null);
    }
    text($('sub'), 'Last ' + (isVideo(it) ? 'video' : 'image') + ' · ' + it.name + ' · ' + when(it.created));
    // the actions of a gallery file (Download, Upscale, Edit, Delete; Guide and Open full size in the ⋯ menu).
    // Use all needs what made it, so it joins when the file's info is in: a file without that (an upscale,
    // an old file) gets none.
    st.actAll = '';
    showActions(it.url, it.name, it.url, false, it);
    fetchInfo(it, function (info) {
      if (!info.sdcpp || st.actFor !== it || !$('actions').firstChild) return;
      st.actAll = it.name;
      showActions(it.url, it.name, it.url, false, it);
    });
  }
