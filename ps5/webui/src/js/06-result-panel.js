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

  function showActions(href, name, openHref, withParams) {
    var box = $('actions');
    clear(box);
    if (!href) return;
    box.appendChild(link(href, 'Download', name, false));
    if (openHref) box.appendChild(link(openHref, 'Open full size', null, true));
    // not kept on the console (yet)
    var kr = st.vidResult && st.vidResult.temp && !st.results ? st.vidResult : st.results && st.results[st.pick];
    if (kr && kr.temp) {
      var kb = actionButton('Keep on the console', function () { keepResult(kr, kb); });
      kb.title = 'It is deleted when PS5SD starts again; this puts it in the gallery';
      box.appendChild(kb);
    }
    if (withParams && st.last) {
      var r = st.results && st.results[st.pick];
      var seed = r ? r.seed : st.last.seed;
      // a run's image from the server has no seed the page knows
      if (seed !== null && seed !== undefined && seed !== '') box.appendChild(actionButton('Use seed ' + seed, function () {
        $('seed').value = seed; persist(); toast('Seed ' + seed + ' is set. Open Settings to see it.');
      }));
      box.appendChild(actionButton('Run again', function () {
        setForm(st.last); $('prompt').value = st.last.prompt; $('neg').value = st.last.negative; negSync(); generate();
      }));
      if (upModels.length && !st.video) {
        var ub = actionButton('Upscale', function () { upscale(r && r.url ? r.url : href, ub); });
        box.appendChild(ub);
      }
      if (!st.video) box.appendChild(actionButton('Edit', function () { setInit(r && r.url ? r.url : href, r ? r.name : ''); }));
      if (!st.video && st.cnModel) box.appendChild(actionButton('Guide', function () { setGuide(r && r.url ? r.url : href, r ? r.name : ''); }));
    }
    // a file on the console (in the gallery, or not kept yet): Delete, a second press to be sure
    if (openHref && name) {
      var db = actionButton('Delete', function () { deleteResult(name, db); });
      db.className = 'danger';
      box.appendChild(db);
    }
  }

  // Delete under the result: the first press asks ("Delete?") for 4 s, the second deletes the file.
  // The image leaves the stage, the strip and the gallery; the next one of the run shows, if any.
  function deleteResult(name, b) {
    if (!b.armed) {
      b.armed = true;
      text(b, 'Delete? Press again');
      b.className = 'danger armed';
      b.armTimer = setTimeout(function () { b.armed = false; text(b, 'Delete'); b.className = 'danger'; }, 4000);
      return;
    }
    clearTimeout(b.armTimer);
    b.disabled = true;
    req('DELETE', '/ps5sd/v1/app/gallery/' + encodeURIComponent(name), null, function (code, data) {
      if (code !== 200 && code !== 404) {
        b.disabled = false;
        b.armed = false;
        text(b, 'Delete');
        b.className = 'danger';
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
    showActions(it.url, it.name, it.url, false);
    var box = $('actions'), mine = box.firstChild, del = box.lastChild, vid = isVideo(it);
    function before(b) { box.insertBefore(b, del); }
    if (!vid) {
      if (upModels.length) {
        var ub = actionButton('Upscale', function () { upscale(it.url, ub); });
        before(ub);
      }
      before(actionButton('Edit', function () { setInit(it.url, it.name); }));
      if (st.cnModel) before(actionButton('Guide', function () { setGuide(it.url, it.name); }));
    }
    // Use all needs what made it; files without that (an upscale, an old file) get no button
    fetchInfo(it, function (info) {
      if (!info.sdcpp || box.firstChild !== mine) return;
      var ab = actionButton('Use all', function () { useSettings(it); });
      ab.title = 'The model, the preset, the prompt and the settings this image was made with';
      before(ab);
    });
  }
