  // ---------- downloads ----------
  var dlTimer = 0;
  var dlBusy = false;
  var dlFirst = true;
  var dlList = [];
  var dlSig = null;
  var dlRows = {};
  var dlPrev = {};
  var catList = [];
  var catSig = null;
  var catRows = {};
  var hfTicket = 0;

  // The Get more sections the app can serve; the Downloads sub-view itself is shown by applyMSub.
  function drawPanels() {
    show('pnl-up', st.dl === true);
    show('pnl-hf', st.dl === true && st.hf !== false);
    show('pnl-cat', st.cat === true);
    show('pnl-nodl', st.dl === false && st.cat !== true);
    drawTokens();
    applyGSub();
    applyMSub();
  }

  function enterModels() {
    applyMSub();
    loadStorage(null, 5000);
    loadModels(null);
    loadTokens();
    loadLoras(null);
    loadUpscalers();
    loadCatalog();
    dlPoll();
  }

  // A name for a download in a message: its file name, when it has one.
  function dlName(url, dest) {
    var n = baseName(dest || url);
    return /\.[a-z0-9]{2,5}$/i.test(n) ? n : 'the file';
  }

  // How soon to ask again: fast while the Models tab is open, slowly when only the tab's dot needs it.
  function dlDelay() {
    if (st.dl === false) return -1;
    if (st.view === 'models') return st.dlActive ? 1000 : 3000;
    return st.dlActive ? 4000 : -1;
  }

  function dlSchedule(ms) {
    clearTimeout(dlTimer);
    if (ms >= 0) dlTimer = setTimeout(dlPoll, ms);
  }

  function dlPoll() {
    if (dlBusy || st.dl === false) return;
    clearTimeout(dlTimer);
    dlBusy = true;
    req('GET', APP + '/downloads', null, function (code, data) {
      dlBusy = false;
      if (code === 200 && data && data.downloads) {
        st.dl = true;
        setDownloads(data.downloads);
      } else if (absent(code)) {
        st.dl = false;
      }
      drawPanels();
      dlSchedule(dlDelay());
    }, 8000);
  }

  function setDownloads(list) {
    var refresh = false, space = false;
    var active = 0;
    var seen = {};
    for (var i = 0; i < list.length; i++) {
      var d = list[i];
      var was = dlPrev[d.id];
      if (!dlFirst && was !== d.state) {
        if (d.state === 'done' || d.state === 'failed') space = true;
        if (d.state === 'done') {
          refresh = true;
          toast(baseName(d.dest || d.url) + ' is downloaded.');
        } else if (d.state === 'failed') {
          toast('Download failed: ' + baseName(d.dest || d.url) + (d.error ? ' (' + d.error + ')' : ''), 6000);
        }
      }
      seen[d.id] = d.state;
      if (d.state === 'queued' || d.state === 'running') active++;
    }
    dlPrev = seen;
    dlFirst = false;
    dlList = list;
    st.dlActive = active;
    show('mdot', active > 0, 'inline-block');
    // the count on the Downloads segment: what is queued or running
    text($('dl-badge'), String(active));
    show('dl-badge', active > 0, 'inline-block');
    drawDownloads();
    updateCatalog();
    civUpdate();
    // the free space changes as a download writes, and when one ends
    if (space) loadStorage(null);
    else if (active && st.view === 'models') loadStorage(null, 15000);
    if (refresh) { loadModels(null); loadLoras(null); loadCatalog(); loadUpscalers(); loadInfo(); }
  }

  function drawDownloads() {
    var act = [], fin = [];
    for (var i = 0; i < dlList.length; i++) {
      if (dlList[i].state === 'queued' || dlList[i].state === 'running') act.push(dlList[i]);
      else fin.unshift(dlList[i]);
    }
    var rows = act.concat(fin.slice(0, 6));
    var sig = '';
    for (var j = 0; j < rows.length; j++) sig += rows[j].id + ':' + rows[j].state + ',';
    $('dl-clear').disabled = !fin.length;
    var box = $('dlist');
    if (sig !== dlSig) {
      dlSig = sig;
      clear(box);
      dlRows = {};
      if (!rows.length) box.appendChild(el('div', 'empty-note', 'Nothing is downloading.'));
      for (var k = 0; k < rows.length; k++) {
        var r = dlRow(rows[k]);
        dlRows[rows[k].id] = r;
        box.appendChild(r.row);
      }
    }
    for (var n = 0; n < rows.length; n++) updateDlRow(dlRows[rows[n].id], rows[n]);
  }

  function catName(id) {
    for (var i = 0; i < catList.length; i++) if (catList[i].id === id) return catList[i].name;
    return '';
  }

  function dlRow(d) {
    var row = el('div', 'mrow drow');
    var main = el('div', 'mmain');
    var nm = el('div', 'mname', baseName(d.dest || d.url));
    var cn = d.catalog ? catName(d.catalog) : '';
    if (cn) nm.appendChild(el('span', 'badge', cn));
    main.appendChild(nm);
    var fillEl = document.createElement('div');
    var bar = el('div', 'bar');
    bar.appendChild(fillEl);
    main.appendChild(bar);
    var meta = el('div', 'mmeta');
    main.appendChild(meta);
    row.appendChild(main);
    if (d.state === 'queued' || d.state === 'running') {
      var cb = actionButton('Cancel', function () { cancelDl(d, cb); });
      row.appendChild(cb);
    } else if (d.state === 'failed' || d.state === 'cancelled') {
      // resumes from the .part file the download left
      var rb = actionButton('Retry', function () { retryDl(d, rb); });
      row.appendChild(rb);
    }
    return { row: row, bar: bar, fill: fillEl, meta: meta };
  }

  function updateDlRow(r, d) {
    var live = d.state === 'queued' || d.state === 'running';
    var total = Number(d.total) || 0, got = Number(d.bytes) || 0;
    var bps = Number(d.bytes_per_second) || 0;
    var pct = total > 0 ? Math.min(100, Math.round(got * 100 / total)) : 0;
    r.bar.style.display = live ? 'block' : 'none';
    r.bar.className = 'bar' + (live && total <= 0 && d.state === 'running' ? ' ind' : '');
    r.fill.style.width = pct + '%';
    var msg;
    if (d.state === 'running') {
      var parts = [bytes(got) + (total > 0 ? ' of ' + bytes(total) + ' (' + pct + '%)' : '')];
      if (bps > 0) parts.push(bytes(bps) + '/s');
      if (bps > 0 && total > got) parts.push(clock((total - got) / bps) + ' left');
      msg = parts.join(' · ');
    } else if (d.state === 'queued') {
      msg = 'Waiting for the others to finish';
    } else if (d.state === 'done') {
      msg = 'Done · ' + bytes(total || got);
    } else if (d.state === 'failed') {
      msg = 'Failed: ' + (d.error || 'unknown error');
    } else {
      msg = 'Cancelled';
    }
    text(r.meta, msg);
    r.meta.className = 'mmeta' + (d.state === 'failed' ? ' bad' : '');
  }

  function cancelDl(d, b) {
    if (b) b.disabled = true;
    req('DELETE', APP + '/downloads/' + encodeURIComponent(String(d.id)), null, function (code, data) {
      if (code === 0 || code >= 400) {
        toast(errText(code, data, 'Could not cancel'), 6000);
        if (b) b.disabled = false;
      }
      dlSchedule(0);
    }, 10000);
  }

  // A failed or cancelled download again: POST downloads/<id>/retry resumes it.
  function retryDl(d, b) {
    if (b) { b.disabled = true; text(b, 'Retrying…'); }
    req('POST', APP + '/downloads/' + encodeURIComponent(String(d.id)) + '/retry', {}, function (code, data) {
      if (code === 202 || code === 200 || code === 409) {
        toast(code === 409 ? 'Already downloading.' : 'Downloading ' + dlName(d.url, d.dest) + ' (see Downloads).');
      } else {
        if (absent(code)) { st.dl = false; drawPanels(); }
        if (b) { b.disabled = false; text(b, 'Retry'); }
        toast(errText(code, data, 'Could not retry the download'), 6000);
      }
      dlSchedule(0);
    }, 15000);
  }

  // The finished downloads leave the list (their files stay).
  function clearFinished() {
    $('dl-clear').disabled = true;
    req('POST', APP + '/downloads/clear', {}, function (code, data) {
      if (code >= 200 && code < 300) {
        if (data && data.downloads) setDownloads(data.downloads);
        dlSchedule(0);
      } else {
        toast(errText(code, data, 'Could not clear the list'), 6000);
        drawDownloads();
      }
    }, 10000);
  }

  function startDownload(url, b, sticky, done, kind) {
    if (b) { b.disabled = true; text(b, 'Starting…'); }
    var body = { url: url };
    if (kind) body.kind = kind;
    req('POST', APP + '/downloads', body, function (code, data) {
      var ok = code === 202 || code === 200 || code === 409;
      if (b) {
        b.disabled = ok && sticky;
        text(b, ok && sticky ? 'Queued' : 'Download');
      }
      if (ok) {
        toast(code === 409 ? 'Already downloading.' : 'Downloading ' + dlName(url) + ' (see Downloads).');
        dlSchedule(0);
      } else {
        if (absent(code)) { st.dl = false; drawPanels(); }
        toast(errText(code, data, 'Could not start the download'), 6000);
      }
      if (done) done(ok);
    }, 15000);
  }

  // A pasted link: a Hugging Face "blob" page is turned into the file link, and a Civitai
  // model page into its download link when it names a version.
  function normalizeLink(raw) {
    var u = trim(raw);
    if (!u) return { error: 'Paste a link first.' };
    if (!/^https?:\/\//i.test(u)) return { error: 'The link must start with https://' };
    // civitai.red is civitai.com with its mature models shown: the same files, under the other name
    u = u.replace(/^https?:\/\/(?:www\.)?civitai\.red(?=[\/?#]|$)/i, 'https://civitai.com');
    var m = /^https?:\/\/(?:www\.)?huggingface\.co\/([^\/?#]+\/[^\/?#]+)\/blob\/(.+)$/i.exec(u);
    if (m) return { url: 'https://huggingface.co/' + m[1] + '/resolve/' + m[2] };
    if (/^https?:\/\/(?:www\.)?huggingface\.co\/[^\/?#]+\/[^\/?#]+\/?$/i.test(u)) {
      return { error: 'That is a repository page. Open a file in it and copy that link, or use the search.' };
    }
    if (/^https?:\/\/(?:www\.)?civitai\.com\/models\//i.test(u)) {
      var v = /[?&]modelVersionId=(\d+)/.exec(u);
      if (v) return { url: 'https://civitai.com/api/download/models/' + v[1] };
      var id = /\/models\/(\d+)/.exec(u);
      return { error: 'On Civitai, pick the version you want so the link ends in modelVersionId=..., then paste it.',
        model: id ? id[1] : '' };
    }
    return { url: u };
  }

  function addByUrl() {
    var r = normalizeLink($('dl-url').value);
    if (r.error && r.model) { addCivitaiModel(r); return; }
    if (r.error) { toast(r.error, 6000); return; }
    startDownload(r.url, $('dl-go'), false, function (ok) {
      if (ok) $('dl-url').value = '';
    }, $('dl-kind').value);
  }

  // A Civitai model page without a version: its newest version, asked from civitai.com by this browser.
  function addCivitaiModel(r) {
    $('dl-go').disabled = true;
    civGet(CIV + '/api/v1/models/' + r.model, function (code, data) {
      $('dl-go').disabled = false;
      var v = code === 200 && data && data.modelVersions && data.modelVersions[0];
      if (!v || !v.id) { toast(r.error, 6000); return; }
      toast('Taking the newest version of ' + (data.name || 'the model') + ': ' + (v.name || v.id) + '.', 4000);
      startDownload(CIV + '/api/download/models/' + v.id, $('dl-go'), false, function (ok) {
        if (ok) $('dl-url').value = '';
      }, $('dl-kind').value);
    });
  }

