  // ---------- Civitai search (Get more, Civitai) ----------
  // The search runs in this browser, straight against civitai.com's public API (it allows other sites,
  // CORS), so the console needs no route and no internet of its own for it. A download goes through the
  // console's downloader as a link: https://civitai.com/api/download/models/<version>.
  var CIV = 'https://civitai.com';
  var CV_BASES = ['SD 1.5', 'SDXL 1.0', 'Pony', 'Illustrious', 'NoobAI', 'Flux.1 D', 'Flux.1 S', 'Flux.2 Klein 4B',
    'Flux.2 Klein 9B', 'Flux.3 Klein', 'ZImageTurbo', 'ZImageBase', 'Qwen', 'Chroma', 'Krea 2', 'MOSS Image',
    'Wan Video 1.3B t2v', 'Wan Video 2.2 TI2V-5B'];
  var CV_MATURE = 4;  // an image of this level (R) or above is blurred until it is clicked
  // ticket: the newest search (older answers are dropped); base: the search's address without a cursor;
  // next: the cursor of Show more; cards: what is on the page, for the buttons' state.
  var cv = { ticket: 0, base: '', next: '', cards: [], touched: false, started: false, kind: 'loras' };

  // One GET to civitai.com. No header of ours: the API allows a plain request from any site.
  function civGet(url, cb) {
    var x = new XMLHttpRequest();
    var done = false;
    function finish(code, data) {
      if (done) return;
      done = true;
      cb(code, data);
    }
    x.open('GET', url, true);
    x.onreadystatechange = function () {
      if (x.readyState !== 4) return;
      var data = null;
      try { data = JSON.parse(x.responseText); } catch (e) {}
      finish(x.status, data);
    };
    x.onerror = function () { finish(0, null); };
    x.ontimeout = function () { finish(0, null); };
    try { x.timeout = 30000; } catch (e) {}
    x.send();
  }

  // The base model of Civitai that fits the loaded model, '' (Any) when that cannot be told. The family
  // first; then words in the model's name and its own files (not the text encoder's: Klein, Z-Image and
  // MOSS-Image load a Qwen LLM as theirs).
  function civSuggestBase() {
    var d = st.mdl, m = d && d.loaded ? findModel(d.loaded) : null;
    if (!m) return '';
    var fam = familyOf(m, m.base ? findModel(m.base) : null);
    var own = [];
    var files = (m.files || []).concat(m.base && findModel(m.base) ? findModel(m.base).files || [] : []);
    for (var i = 0; i < files.length; i++) {
      if (!/text_encoder|(^|\/)vae\/|tokenizer|umt5|t5xxl|clip_[lg]|qwen\d/i.test(files[i])) own.push(files[i]);
    }
    var n = ((m.name || '') + ' ' + (m.id || '') + ' ' + own.join(' ')).toLowerCase();
    if (fam === 'MOSS-Image' || /moss/.test(n)) return 'MOSS Image';
    if (fam === 'SD 1.5') return 'SD 1.5';
    if (fam === 'SDXL') return /pony/.test(n) ? 'Pony' : /ilust|illust/.test(n) ? 'Illustrious' : /noob/.test(n) ? 'NoobAI' : 'SDXL 1.0';
    if (fam === 'FLUX.2 Klein 4B') return 'Flux.2 Klein 4B';
    if (fam === 'FLUX.2 Klein 9B') return 'Flux.2 Klein 9B';
    if (fam === 'FLUX.3 Klein') return 'Flux.3 Klein';
    if (fam === 'Z-Image') return /base/.test(n) && !/turbo/.test(n) ? 'ZImageBase' : 'ZImageTurbo';
    if (fam === 'Wan 1.3B') return 'Wan Video 1.3B t2v';
    if (fam === 'Wan 5B') return 'Wan Video 2.2 TI2V-5B';
    if (/qwen[-_ ]?image/.test(n)) return 'Qwen';
    if (/chroma/.test(n)) return 'Chroma';
    if (/krea/.test(n)) return 'Krea 2';
    if (fam === 'FLUX.1' || fam === 'FLUX') return /schnell/.test(n) ? 'Flux.1 S' : 'Flux.1 D';
    return '';
  }

  function civImg(u) {
    return /^https:\/\//i.test(u) ? String(u).replace('/original=true/', '/width=450/') : '';
  }

  // The first picture (not a video) of the version, else of another version.
  function civPicture(vs, cur) {
    var order = [vs[cur]], i, j;
    for (i = 0; i < vs.length; i++) if (i !== cur) order.push(vs[i]);
    for (i = 0; i < order.length; i++) {
      var im = order[i].images || [];
      for (j = 0; j < im.length; j++) if ((!im[j].type || im[j].type === 'image') && civImg(im[j].url)) return im[j];
    }
    return null;
  }

  // The file the download takes: the primary one, else the first model file.
  function civFile(v) {
    var fs = v.files || [], i;
    for (i = 0; i < fs.length; i++) if (fs[i].primary) return fs[i];
    for (i = 0; i < fs.length; i++) if (fs[i].type === 'Model') return fs[i];
    return fs[0] || null;
  }

  function civBytes(f) { return f && Number(f.sizeKB) > 0 ? Math.round(Number(f.sizeKB) * 1024) : 0; }

  // Is a file of this name there? LoRAs: the LoRA list; models: the library's files.
  function civInstalled(kind, name) {
    if (!name) return false;
    var n = String(name).toLowerCase(), i;
    if (kind === 'loras') {
      for (i = 0; loraList && i < loraList.length; i++) if (baseName(loraList[i].path).toLowerCase() === n) return true;
      return false;
    }
    var list = st.mdl && st.mdl.models ? st.mdl.models : [];
    for (i = 0; i < list.length; i++) {
      if (list[i].kind !== 'file') continue;
      var fs = list[i].files || [];
      for (var j = 0; j < fs.length; j++) if (baseName(fs[j]).toLowerCase() === n) return true;
    }
    return false;
  }

  // The running download of a version: the percent (0 to 99), or -1 when none runs.
  function civPct(vid) {
    var re = new RegExp('/api/download/models/' + vid + '(?:[/?#]|$)'), live = false, got = 0, total = 0;
    for (var i = 0; i < dlList.length; i++) {
      var d = dlList[i];
      if (!re.test(String(d.url)) || (d.state !== 'queued' && d.state !== 'running')) continue;
      live = true;
      got += Number(d.bytes) || 0;
      total += Number(d.total) || 0;
    }
    if (!live) return -1;
    return total > 0 ? Math.min(99, Math.floor(got * 100 / total)) : 0;
  }

  // The buttons of the cards: Download, its progress, or Installed. Called when the lists change.
  function civUpdate() {
    for (var i = 0; i < cv.cards.length; i++) {
      var c = cv.cards[i], v = c.vs[c.cur], f = civFile(v), b = c.btn;
      var pct = civPct(v.id);
      if (!f) { b.disabled = true; b.className = ''; text(b, 'No file'); }
      else if (pct >= 0) { b.disabled = true; b.className = ''; text(b, 'Downloading ' + pct + '%'); }
      else if (civInstalled(c.kind, f.name)) { b.disabled = true; b.className = ''; text(b, 'Installed'); }
      else { b.disabled = c.starting; b.className = c.starting ? '' : 'on'; text(b, c.starting ? 'Starting…' : 'Download'); }
      if (pct >= 0) c.starting = false;
    }
  }

  // One result: the picture (blurred when mature), name, maker, base model, downloads, trigger words,
  // a version list when there are several, the file's size and Download.
  function civCard(m, kind) {
    var vs = (m.modelVersions || []).slice(0, 12);
    var c = { m: m, vs: vs, cur: 0, kind: kind, starting: false, btn: null };
    var card = el('div', 'mrow cvcard');
    var pic = el('div', 'cvpic');
    card.appendChild(pic);
    var main = el('div', 'mmain');
    var nm = el('a', 'mname cvname', m.name || 'Model ' + m.id);
    nm.target = '_blank';
    nm.rel = 'noopener';
    nm.title = 'Open on civitai.com';
    main.appendChild(nm);
    var meta = el('div', 'mmeta');
    main.appendChild(meta);
    var trig = el('div', 'mmeta cvtrig');
    main.appendChild(trig);
    var act = el('div', 'cvact');
    var vsel = null;
    if (vs.length > 1) {
      vsel = document.createElement('select');
      vsel.className = 'cvver';
      vsel.setAttribute('aria-label', 'Version of ' + (m.name || 'the model'));
      for (var i = 0; i < vs.length; i++) {
        var o = document.createElement('option');
        o.value = String(i);
        text(o, (vs[i].name || 'Version') + (vs[i].baseModel ? ' · ' + vs[i].baseModel : ''));
        vsel.appendChild(o);
      }
      vsel.onchange = function () { c.cur = Number(vsel.value) || 0; paint(); };
      act.appendChild(vsel);
    }
    var size = el('span', 'cvsize');
    act.appendChild(size);
    c.btn = actionButton('Download', function () {
      var v = vs[c.cur];
      c.starting = true;
      civUpdate();
      startDownload(CIV + '/api/download/models/' + v.id, c.btn, true, function (ok) {
        if (!ok) c.starting = false;
        civUpdate();
      }, kind === 'loras' ? 'loras' : '');
    });
    act.appendChild(c.btn);
    main.appendChild(act);
    card.appendChild(main);

    function paint() {
      var v = vs[c.cur], f = civFile(v), parts = [];
      nm.href = CIV + '/models/' + m.id + '?modelVersionId=' + v.id;
      if (m.creator && m.creator.username) parts.push('by ' + m.creator.username);
      if (v.baseModel) parts.push(v.baseModel);
      if (m.stats && m.stats.downloadCount !== undefined) parts.push(count(m.stats.downloadCount) + ' downloads');
      text(meta, parts.join(' · '));
      var words = v.trainedWords || [];
      text(trig, words.length ? 'Trigger words: ' + words.join(', ') : '');
      trig.style.display = words.length ? 'block' : 'none';
      var n = civBytes(f);
      text(size, n ? bytes(n) : '');
      if (n && fits(n) === false) size.appendChild(el('span', 'lowspace', ' · More than the ' + bytes(storFree()) + ' free'));
      clear(pic);
      var im = civPicture(vs, c.cur);
      if (!im) { pic.appendChild(el('div', 'cvnone', 'No picture')); return; }
      var img = document.createElement('img');
      img.alt = '';
      img.src = civImg(im.url);
      img.setAttribute('loading', 'lazy');
      img.setAttribute('referrerpolicy', 'no-referrer');
      pic.appendChild(img);
      if ((Number(im.nsfwLevel) || 0) >= CV_MATURE) {
        pic.className = 'cvpic nsfw';
        pic.setAttribute('role', 'button');
        pic.tabIndex = 0;
        pic.setAttribute('aria-label', 'Mature picture: show it');
        pic.appendChild(el('span', 'cvmask', 'Mature. Click to show'));
        var reveal = function () { pic.className = 'cvpic nsfw shown'; };
        pic.onclick = reveal;
        pic.onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13 || e.keyCode === 32) { reveal(); return false; } return true; };
      } else {
        pic.className = 'cvpic';
        pic.onclick = pic.onkeydown = null;
        pic.removeAttribute('role');
        pic.removeAttribute('tabindex');
      }
    }
    paint();
    cv.cards.push(c);
    return card;
  }

  function civMsg(box, msg, bad) {
    box.appendChild(el('div', 'empty-note' + (bad ? ' bad' : ''), msg));
  }

  // A new search, or (more) the next page of the last one.
  function civSearch(more) {
    var box = $('cv-res');
    var url;
    if (more) {
      if (!cv.next) return;
      url = cv.base + '&cursor=' + encodeURIComponent(cv.next);
    } else {
      var q = trim($('cv-q').value), base = $('cv-base').value;
      url = CIV + '/api/v1/models?types=' + encodeURIComponent($('cv-type').value) +
        '&sort=' + encodeURIComponent($('cv-sort').value) + '&limit=20&nsfw=' + ($('cv-nsfw').checked ? 'true' : 'false') +
        (q ? '&query=' + encodeURIComponent(q) : '') + (base ? '&baseModels=' + encodeURIComponent(base) : '');
      cv.base = url;
      cv.next = '';
      cv.cards = [];
      cv.kind = $('cv-type').value === 'LORA' ? 'loras' : 'models';
      clear(box);
      civMsg(box, 'Searching…');
    }
    var ticket = ++cv.ticket;
    $('cv-go').disabled = true;
    $('cv-more').disabled = true;
    civGet(url, function (code, data) {
      if (ticket !== cv.ticket) return;
      $('cv-go').disabled = false;
      $('cv-more').disabled = false;
      if (!more) clear(box);
      if (code === 200 && data && data.items) {
        var shown = 0;
        for (var i = 0; i < data.items.length; i++) {
          var it = data.items[i];
          if (!it || !it.modelVersions || !it.modelVersions.length) continue;
          box.appendChild(civCard(it, cv.kind));
          shown++;
        }
        cv.next = data.metadata && data.metadata.nextCursor ? String(data.metadata.nextCursor) : '';
        if (!shown && !more) civMsg(box, 'Nothing found. Try other words, or "Any" as the base model.');
        civUpdate();
      } else {
        cv.next = '';
        civMsg(box, code === 0 ? 'Cannot reach civitai.com. The search runs in this browser, so it needs the internet.'
          : code === 429 ? 'Civitai asks to wait a moment. Try again soon.' : 'Civitai answered ' + code + '.', true);
      }
      show('cv-more', !!cv.next, 'inline-block');
    });
  }

  // The Civitai sub-tab was opened: the base model that fits the loaded model, and a first search.
  function civEnter() {
    if (!cv.touched) {
      var want = civSuggestBase();
      if ($('cv-base').value !== want) $('cv-base').value = want;
    }
    if (!cv.started) { cv.started = true; civSearch(false); }
  }

  function initCivitai() {
    var sel = $('cv-base');
    var any = document.createElement('option');
    any.value = '';
    text(any, 'Any');
    sel.appendChild(any);
    for (var i = 0; i < CV_BASES.length; i++) {
      var o = document.createElement('option');
      o.value = CV_BASES[i];
      text(o, CV_BASES[i]);
      sel.appendChild(o);
    }
    var again = function () { if (cv.started) civSearch(false); };
    $('cv-go').onclick = function () { cv.started = true; civSearch(false); };
    $('cv-more').onclick = function () { civSearch(true); };
    $('cv-q').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { cv.started = true; civSearch(false); return false; } return true; };
    $('cv-type').onchange = again;
    $('cv-sort').onchange = again;
    $('cv-base').onchange = function () { cv.touched = true; again(); };
    $('cv-nsfw').onchange = function () {
      $('cv-res').className = $('cv-nsfw').checked ? 'mature' : '';
      again();
    };
  }
