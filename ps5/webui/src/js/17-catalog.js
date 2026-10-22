  // ---------- catalog (Get more) ----------
  function loadCatalog() {
    req('GET', APP + '/catalog', null, function (code, data) {
      if (code === 200 && data && data.entries) {
        st.cat = true;
        catList = data.entries;
      } else if (absent(code)) {
        st.cat = false;
      }
      drawPanels();
      drawCatalog();
    }, 8000);
  }

  // The featured entries as cards, then the rest as rows.
  function drawCatalog() {
    if (st.cat !== true) return;
    var sig = JSON.stringify(catList);
    if (sig !== catSig) {
      catSig = sig;
      var box = $('catlist');
      clear(box);
      catRows = {};
      if (!catList.length) box.appendChild(el('div', 'empty-note', 'The catalog is empty.'));
      var feat = [], rest = [];
      for (var i = 0; i < catList.length; i++) (catList[i].featured ? feat : rest).push(catList[i]);
      if (feat.length) {
        box.appendChild(el('div', 'ghead', 'Featured'));
        var cards = el('div', 'featrow');
        for (var f = 0; f < feat.length; f++) cards.appendChild(catRow(feat[f], true));
        box.appendChild(cards);
      }
      if (rest.length) box.appendChild(el('div', 'ghead', feat.length ? 'More models' : 'Models'));
      for (var r = 0; r < rest.length; r++) box.appendChild(catRow(rest[r], false));
    }
    updateCatalog();
  }

  function catRow(e, big) {
    var row = el('div', big ? 'mrow feat' : 'mrow mwrap');
    var main = el('div', 'mmain');
    main.appendChild(el('div', 'mname', e.name || e.id));
    var files = e.files || [], dests = [];
    for (var i = 0; i < files.length; i++) if (files[i].dest) dests.push(files[i].dest);
    addTags(main, tagsOf(e.name, dests, false, false));
    if (e.description) main.appendChild(el('div', 'mmeta desc', e.description));
    if (e.good_for) main.appendChild(el('div', 'mmeta good', 'Good for: ' + e.good_for));
    main.appendChild(el('div', 'mmeta', bytes(e.bytes) + (files.length > 1 ? ' · ' + files.length + ' files' : '')));
    row.appendChild(main);
    var b = actionButton('Download', function () { installEntry(e); });
    row.appendChild(b);
    catRows[e.id] = { e: e, btn: b, starting: false };
    return row;
  }

  // Percent of this entry's downloads that are done, or -1 when none of them runs.
  function catPct(e) {
    var live = false, got = 0, total = 0;
    for (var i = 0; i < dlList.length; i++) {
      var d = dlList[i];
      if (d.catalog !== e.id) continue;
      if (d.state === 'queued' || d.state === 'running') live = true;
      if (d.state === 'queued' || d.state === 'running' || d.state === 'done') {
        got += Number(d.bytes) || 0;
        total += Number(d.total) || 0;
      }
    }
    if (!live) return -1;
    return total > 0 ? Math.min(99, Math.floor(got * 100 / total)) : 0;
  }

  function updateCatalog() {
    for (var id in catRows) {
      if (!catRows.hasOwnProperty(id)) continue;
      var r = catRows[id];
      var pct = catPct(r.e);
      if (pct >= 0) r.starting = false;
      if (pct >= 0) {
        r.btn.disabled = true;
        r.btn.className = '';
        text(r.btn, 'Downloading ' + pct + '%');
      } else if (r.e.installed) {
        r.btn.disabled = true;
        r.btn.className = '';
        text(r.btn, 'Installed');
      } else {
        r.btn.disabled = r.starting;
        r.btn.className = r.starting ? '' : 'on';
        text(r.btn, r.starting ? 'Starting…' : 'Download');
      }
    }
  }

  // One download per file of the entry, queued in order.
  function installEntry(e) {
    var r = catRows[e.id];
    var files = e.files || [];
    if (!files.length) { toast('This entry lists no files.'); return; }
    r.starting = true;
    updateCatalog();
    var i = 0;
    (function next() {
      if (i >= files.length) { toast('Downloading ' + (e.name || e.id) + ' (see Downloads).'); dlSchedule(0); return; }
      var f = files[i++];
      var body = { url: f.url, catalog: e.id };
      if (f.dest) body.dest = f.dest;
      req('POST', APP + '/downloads', body, function (code, data) {
        if (code === 202 || code === 200 || code === 409) { next(); return; }
        if (absent(code)) { st.dl = false; drawPanels(); }
        toast(errText(code, data, 'Could not start the download'), 6000);
        r.starting = false;
        updateCatalog();
        dlSchedule(0);
      }, 15000);
    })();
  }

