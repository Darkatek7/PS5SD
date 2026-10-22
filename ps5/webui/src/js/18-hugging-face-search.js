  // ---------- Hugging Face search ----------
  function encodePath(path) {
    var parts = String(path).split('/');
    for (var i = 0; i < parts.length; i++) parts[i] = encodeURIComponent(parts[i]);
    return parts.join('/');
  }

  function hfSearch() {
    var q = trim($('hf-q').value);
    if (!q) { toast('Type what to search for.'); return; }
    var box = $('hf-res');
    clear(box);
    box.appendChild(el('div', 'empty-note', 'Searching…'));
    $('hf-go').disabled = true;
    var ticket = ++hfTicket;
    req('POST', APP + '/hf/search', { q: q, limit: 20 }, function (code, data) {
      if (ticket !== hfTicket) return;
      $('hf-go').disabled = false;
      clear(box);
      if (code === 200 && data && data.results) { drawHf(data.results); return; }
      if (absent(code)) { st.hf = false; drawPanels(); return; }
      box.appendChild(el('div', 'empty-note bad', errText(code, data, 'The search failed')));
    }, 30000);
  }

  function drawHf(results) {
    var box = $('hf-res');
    if (!results.length) { box.appendChild(el('div', 'empty-note', 'Nothing found.')); return; }
    for (var i = 0; i < results.length; i++) box.appendChild(hfCard(results[i]));
  }

  function hfCard(r) {
    var LIMIT = 5;
    var card = el('div', 'hrow');
    card.appendChild(el('div', 'mname', r.repo));
    var stats = [];
    if (r.downloads !== undefined && r.downloads !== null) stats.push(count(r.downloads) + ' downloads');
    if (r.likes !== undefined && r.likes !== null) stats.push(count(r.likes) + ' likes');
    card.appendChild(el('div', 'mmeta', stats.join(' · ')));
    var files = [];
    var all = r.files || [];
    for (var i = 0; i < all.length; i++) if (MODEL_EXT.test(all[i].path)) files.push(all[i]);
    if (!files.length) {
      card.appendChild(el('div', 'mmeta', 'No model files in this repository.'));
      return card;
    }
    for (var j = 0; j < files.length && j < LIMIT; j++) card.appendChild(hfFile(r.repo, files[j]));
    if (files.length > LIMIT) {
      var more = actionButton('Show ' + (files.length - LIMIT) + ' more files', function () {
        for (var k = LIMIT; k < files.length; k++) card.insertBefore(hfFile(r.repo, files[k]), more);
        card.removeChild(more);
      });
      more.className = 'hmore';
      card.appendChild(more);
    }
    return card;
  }

  function hfFile(repo, f) {
    var row = el('div', 'hfile');
    row.appendChild(el('span', 'fn', f.path));
    row.appendChild(el('span', 'sz', bytes(f.bytes)));
    var url = 'https://huggingface.co/' + repo + '/resolve/main/' + encodePath(f.path);
    var b = actionButton('Download', function () { startDownload(url, b, true, null); });
    row.appendChild(b);
    return row;
  }

