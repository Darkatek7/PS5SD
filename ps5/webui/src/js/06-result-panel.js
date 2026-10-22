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

  function showImage(src, label) {
    var stage = $('stage');
    var img = stage.getElementsByTagName('img')[0];
    if (!img) {
      freeVideo();
      clear(stage);
      img = document.createElement('img');
      stage.appendChild(img);
    }
    img.alt = label || 'generated image';
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
    if (withParams && st.last) {
      var r = st.results && st.results[st.pick];
      var seed = r ? r.seed : st.last.seed;
      box.appendChild(actionButton('Use seed ' + seed, function () {
        $('seed').value = seed; persist(); toast('Seed ' + seed + ' is set. Open Settings to see it.');
      }));
      box.appendChild(actionButton('Run again', function () {
        setForm(st.last); $('prompt').value = st.last.prompt; $('neg').value = st.last.negative; generate();
      }));
      if (upModels.length && !st.video) {
        var ub = actionButton('Upscale', function () { upscale(r && r.url ? r.url : href, ub); });
        box.appendChild(ub);
      }
      if (!st.video) box.appendChild(actionButton('Edit', function () { setInit(r && r.url ? r.url : href, r ? r.name : ''); }));
      if (!st.video && st.cnModel) box.appendChild(actionButton('Guide', function () { setGuide(r && r.url ? r.url : href, r ? r.name : ''); }));
    }
  }

