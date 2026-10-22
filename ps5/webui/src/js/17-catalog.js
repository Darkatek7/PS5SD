  // ---------- catalog ----------
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

  function drawCatalog() {
    if (st.cat !== true) return;
    var sig = JSON.stringify(catList);
    if (sig !== catSig) {
      catSig = sig;
      var box = $('catlist');
      clear(box);
      catRows = {};
      if (!catList.length) box.appendChild(el('div', 'empty-note', 'The catalog is empty.'));
      // featured entries first, as bigger cards
      var feat = [], rest = [];
      for (var i = 0; i < catList.length; i++) (catList[i].featured ? feat : rest).push(catList[i]);
      if (feat.length) {
        box.appendChild(el('div', 'lbl', 'Featured'));
        var cards = el('div', 'featrow');
        for (var f = 0; f < feat.length; f++) cards.appendChild(catRow(feat[f], true));
        box.appendChild(cards);
      }
      if (feat.length && rest.length) box.appendChild(el('div', 'lbl', 'More models'));
      for (var r = 0; r < rest.length; r++) box.appendChild(catRow(rest[r], false));
    }
    updateCatalog();
  }

  function catRow(e, big) {
    var row = el('div', 'mrow' + (big ? ' feat' : ''));
    var main = el('div', 'mmain');
    var nm = el('div', 'mname', e.name || e.id);
    var badge = el('span', 'badge ok', 'Installed');
    nm.appendChild(badge);
    main.appendChild(nm);
    if (e.description) main.appendChild(el('div', 'mmeta desc', e.description));
    if (big && e.good_for) main.appendChild(el('div', 'mmeta good', 'Good for: ' + e.good_for));
    var n = e.files ? e.files.length : 0;
    main.appendChild(el('div', 'mmeta', bytes(e.bytes) + (n > 1 ? ' · ' + n + ' files' : '')));
    row.appendChild(main);
    var b = actionButton('Install', function () { installEntry(e); });
    row.appendChild(b);
    catRows[e.id] = { e: e, badge: badge, btn: b, starting: false };
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
      r.badge.style.display = r.e.installed ? 'inline-block' : 'none';
      if (pct >= 0) {
        r.btn.style.display = 'block';
        r.btn.disabled = true;
        r.btn.className = '';
        text(r.btn, 'Installing ' + pct + '%');
      } else if (r.e.installed) {
        r.btn.style.display = 'none';
      } else {
        r.btn.style.display = 'block';
        r.btn.disabled = r.starting;
        r.btn.className = r.starting ? '' : 'on';
        text(r.btn, r.starting ? 'Starting…' : 'Install');
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
      if (i >= files.length) { toast('Installing ' + (e.name || e.id) + '…'); dlSchedule(0); return; }
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

