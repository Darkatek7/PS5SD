  // ---------- prompt collections ----------
  // Named lists of prompts or of a few words, kept on the console (GET, POST and DELETE /ps5sd/v1/app/promptbooks)
  // for every model and browser. __name__ in a prompt is a random entry of the collection (09f). The panel under
  // the prompt (shared with the History) lists them: Use, Use a random entry, Insert __name__, Import and Export
  // .txt, and the switch for a random prompt for each image from the ticked collections.
  // Save prompt puts the prompt and the negative prompt as typed into a collection.
  var bkOpen = '';          // the name of the open collection
  var bkShown = 100;        // how many entries of it the list draws
  var bkMode = '';          // the name box: 'new', 'rename' or 'import'
  var bkLines = null;       // the lines of an import that waits for a name
  var bkMenuId = 0;         // the entry whose Edit / Delete show
  var bkEditId = 0;         // the entry in the editor
  var promptCaret = null;   // {start, end}: the last place of the caret in the prompt
  var bkRand = false;       // Random prompt for each image: on or off (kept in this browser)
  var bkPicks = [];         // the names of the collections it takes from, ticked in the Collections tab
  var BK_PAGE = 100;
  var BK_ENTRIES_MAX = 2000;
  var BK_NAME_RE = /^[A-Za-z0-9_-]{1,40}$/;

  function bkPath(name) { return APP + '/promptbooks/' + encodeURIComponent(name); }

  function sortedBooks() {
    var out = (bookList || []).slice();
    out.sort(function (a, b) {
      var x = a.name.toLowerCase(), y = b.name.toLowerCase();
      if (x === 'saved' && y !== 'saved') return -1;
      if (y === 'saved' && x !== 'saved') return 1;
      return x < y ? -1 : x > y ? 1 : 0;
    });
    return out;
  }

  // The reason a name cannot be used, or ''. except: the collection that is being renamed.
  function bookNameError(name, except) {
    if (!BK_NAME_RE.test(name)) return 'A name has 1 to 40 letters, digits, - and _.';
    if (isLoraName(name)) return 'lora is for the trigger words of the LoRAs. Pick another name.';
    var b = findBook(name);
    if (b && b !== except) return 'There is a collection called ' + b.name + ' already.';
    return '';
  }

  // A file name made to fit the rules: "My people.txt" gives "My_people".
  function nameFromFile(file) {
    var n = String(file || '').replace(/^.*[\/\\]/, '').replace(/\.[A-Za-z0-9]{1,5}$/, '');
    n = n.replace(/[^A-Za-z0-9_-]+/g, '_').replace(/^[_-]+|[_-]+$/g, '').substring(0, 40).replace(/[_-]+$/, '');
    return n || 'imported';
  }

  function loadBooks(then) {
    req('GET', APP + '/promptbooks', null, function (code, data) {
      if (code === 200 && data && data.books && data.books.length !== undefined) {
        bookList = data.books;
        booksOk = true;
      } else if (code === 404) {
        booksOk = false;
        bookList = null;
      }
      drawBooksButtons();
      drawRandom();
      if (booksPanelOpen()) drawBooks();
      if ($('bk-savebox').style.display !== 'none') drawBookSave();
      if (then) then();
    });
  }

  function booksPanelOpen() { return $('phist').style.display !== 'none' && panelTab === 'books'; }

  function drawBooksButtons() {
    var on = booksOk && bookList !== null;
    show('prompt-books', on, 'inline-flex');
    show('prompt-save', on, 'inline-flex');
    if (!on) { show('bk-savebox', false); if (panelTab === 'books') panelSwitch('hist'); }
    show('pt-hist', histOk);
    show('pt-books', on, 'inline-block');
    drawHistButton();
  }

  // ---- the panel: two tabs
  function panelSwitch(which) {
    panelTab = which === 'books' && booksOk ? 'books' : 'hist';
    var b = panelTab === 'books';
    show('ph-hpane', !b, 'block');
    show('ph-bpane', b, 'block');
    show('ph-clear', !b && !!(promptHist && promptHist.length), 'inline-flex');
    $('pt-hist').className = b ? '' : 'on';
    $('pt-books').className = b ? 'on' : '';
    $('pt-hist').setAttribute('aria-selected', b ? 'false' : 'true');
    $('pt-books').setAttribute('aria-selected', b ? 'true' : 'false');
    if (b) { drawBooks(); loadBooks(); } else { drawHistory(); loadPromptHistory(); }
  }

  function booksShow() {
    if ($('phist').style.display !== 'none' && panelTab === 'books') { histShow(false); return; }
    panelTab = 'books';
    histShow(true);
  }

  function bkMsg(s, bad) {
    text($('bk-msg'), s || '');
    $('bk-msg').className = bad ? 'hint bad' : 'hint';
    show('bk-msg', !!s, 'block');
  }

  function bkCurrent() { return bkOpen ? findBook(bkOpen) : null; }

  // ---- Random prompt for each image: a random entry of the ticked collections instead of the typed prompt
  // (09f expandJob with p.books). Kept in this browser, as the rest of the Create tab.
  function bkPicked(name) {
    var low = String(name).toLowerCase();
    for (var i = 0; i < bkPicks.length; i++) if (String(bkPicks[i]).toLowerCase() === low) return true;
    return false;
  }

  function bkPickSet(name, on) {
    var low = String(name).toLowerCase(), keep = [];
    for (var i = 0; i < bkPicks.length; i++) if (String(bkPicks[i]).toLowerCase() !== low) keep.push(bkPicks[i]);
    if (on) keep.push(name);
    bkPicks = keep;
    save('bookpicks', JSON.stringify(bkPicks));
  }

  // The names Random prompt takes from, or null while it is off.
  function randomBooks() { return bkRand ? bkPicks.slice() : null; }

  function bkRandSet(on) {
    bkRand = !!on;
    save('bookrand', bkRand ? '1' : '0');
    // the first time: the open collection
    if (bkRand && !booksOf(bkPicks).total && bkCurrent() && bkCurrent().entries.length) bkPickSet(bkCurrent().name, true);
    if (booksPanelOpen()) drawBooks();
    drawRandom();
  }

  // "portrait, cosplay and 3 more"
  function bkNames(books) {
    var names = [], i;
    for (i = 0; i < books.length && i < 3; i++) names.push(books[i].name);
    if (books.length > 3) return names.join(', ') + ' and ' + (books.length - 3) + ' more';
    if (names.length > 1) return names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1];
    return names.join('');
  }

  // The note under the prompt, the line in the Collections tab, and the prompt box (not used while it is on).
  function drawRandom() {
    var got = booksOf(bkPicks), on = bkRand;
    $('bk-rand').checked = on;
    show('bk-rand-info', on, 'block');
    show('rand-note', on, 'block');
    $('prompt').disabled = on;
    if (on) {
      text($('bk-rand-text'), got.total ? 'Ticked: ' + plural(got.books.length, 'collection', 'collections') + ', ' +
        plural(got.total, 'entry', 'entries') + '. Each image takes one of them at random.' :
        'Tick the collections each image takes its prompt from.');
      text($('rand-text'), got.total ? 'Random prompt: each image takes one of ' + plural(got.total, 'entry', 'entries') +
        ' of ' + bkNames(got.books) + '. The prompt box is not used.' :
        'Random prompt is on, but no ticked collection has entries.');
      $('rand-text').className = got.total ? 'hint' : 'hint bad';
    }
    drawWildHint();
  }

  // ---- drawing the Collections tab
  function drawBooks() {
    var list = sortedBooks(), box = $('bk-tabs'), cur = bkCurrent(), i;
    if (!cur) { cur = list.length ? list[0] : null; bkOpen = cur ? cur.name : ''; }
    clear(box);
    for (i = 0; i < list.length; i++) {
      (function (b) {
        var on = cur && b.name === cur.name;
        var x = el('button', 'bkb' + (on ? ' on' : ''), b.name + ' (' + b.entries.length + ')');
        x.type = 'button';
        x.setAttribute('aria-pressed', on ? 'true' : 'false');
        x.onclick = function () { bkOpen = b.name; bkShown = BK_PAGE; bkMenuId = 0; bkEditId = 0; bkMsg(''); drawBooks(); };
        if (!bkRand) { box.appendChild(x); return; }
        // with Random prompt on: a tick for each collection, next to the button that opens it
        var picked = bkPicked(b.name);
        var pair = el('span', 'bkpair' + (picked ? ' picked' : ''));
        var cb = el('input', '');
        cb.type = 'checkbox';
        cb.checked = picked;
        cb.setAttribute('aria-label', 'Random prompts from ' + b.name);
        cb.title = 'Random prompts from ' + b.name;
        cb.onchange = function () { bkPickSet(b.name, cb.checked); pair.className = 'bkpair' + (cb.checked ? ' picked' : ''); drawRandom(); };
        pair.appendChild(cb);
        pair.appendChild(x);
        box.appendChild(pair);
      })(list[i]);
    }
    var nb = el('button', 'bkb bknew', '+ New');
    nb.type = 'button';
    nb.onclick = function () { bkNameBox('new'); };
    box.appendChild(nb);
    drawBookEntries(cur);
    var has = !!cur, filled = has && cur.entries.length > 0;
    show('bk-acts', has, 'flex');
    $('bk-random').disabled = !filled;
    $('bk-export').disabled = !filled;
    $('bk-insert').disabled = !has;
    if (has) text($('bk-insert'), 'Insert __' + cur.name + '__');
  }

  function drawBookEntries(cur) {
    var box = $('bk-list'), es = cur ? cur.entries : [];
    clear(box);
    show('bk-none', !!cur && !es.length, 'block');
    show('bk-nobook', !cur, 'block');
    show('bk-list', es.length > 0, 'block');
    var n = Math.min(es.length, bkShown);
    for (var i = 0; i < n; i++) box.appendChild(bkRow(cur, es[i]));
    if (es.length > n) {
      var more = el('button', 'linkbtn bkmore', 'Show more (' + (es.length - n) + ' left)');
      more.type = 'button';
      more.onclick = function () { bkShown += BK_PAGE; drawBookEntries(cur); };
      box.appendChild(more);
    }
  }

  function bkRow(cur, e) {
    var wrap = el('div', 'bkwrap');
    if (bkEditId === e.id) {
      var ed = el('div', 'bkedit');
      var p = el('textarea', '');
      p.rows = 3;
      p.value = e.prompt;
      p.setAttribute('aria-label', 'Prompt');
      var n = el('textarea', '');
      n.rows = 2;
      n.value = e.negative || '';
      n.placeholder = 'Negative prompt (optional)';
      n.setAttribute('aria-label', 'Negative prompt');
      var bar = el('div', 'bkbar');
      var ok = el('button', 'linkbtn', 'Save');
      ok.type = 'button';
      ok.onclick = function () { bkSaveEntry(cur, e, p.value, n.value); };
      var no = el('button', 'linkbtn', 'Cancel');
      no.type = 'button';
      no.onclick = function () { bkEditId = 0; drawBooks(); };
      bar.appendChild(ok);
      bar.appendChild(no);
      ed.appendChild(p);
      ed.appendChild(n);
      ed.appendChild(bar);
      wrap.appendChild(ed);
      return wrap;
    }
    var row = el('div', 'phrow');
    var body = el('div', 'bkent');
    body.appendChild(el('span', 'pht', e.prompt));
    if (e.negative) body.appendChild(el('span', 'phm', 'Negative: ' + e.negative));
    var use = el('button', 'bkuse', 'Use');
    use.type = 'button';
    use.title = 'Put this entry into the prompt';
    use.onclick = function () { useBookEntry(e); };
    var dots = el('button', 'bkdots', '⋯');
    dots.type = 'button';
    dots.title = 'Edit or delete this entry';
    dots.setAttribute('aria-label', 'More for this entry');
    dots.setAttribute('aria-expanded', bkMenuId === e.id ? 'true' : 'false');
    dots.onclick = function () { bkMenuId = bkMenuId === e.id ? 0 : e.id; drawBooks(); };
    row.appendChild(body);
    row.appendChild(use);
    row.appendChild(dots);
    wrap.appendChild(row);
    if (bkMenuId === e.id) {
      var bar2 = el('div', 'bkbar');
      var ed2 = el('button', 'linkbtn', 'Edit');
      ed2.type = 'button';
      ed2.onclick = function () { bkEditId = e.id; bkMenuId = 0; drawBooks(); };
      var del = el('button', 'linkbtn', 'Delete');
      del.type = 'button';
      del.onclick = function () { bkDeleteEntry(cur, e); };
      bar2.appendChild(ed2);
      bar2.appendChild(del);
      wrap.appendChild(bar2);
    }
    return wrap;
  }

  // ---- entries
  function useBookEntry(e) {
    $('prompt').value = e.prompt;
    if (e.negative) { $('neg').value = e.negative; negShow(true); }
    negSync();
    persist();
    histShow(false);
    // the prompt box counts again
    var randOff = bkRand;
    if (randOff) bkRandSet(false);
    toast((e.negative ? 'The prompt and its negative prompt are in the form.' : 'The prompt is in the form.') +
      (randOff ? ' Random prompt is off now.' : ''));
    try { $('prompt').focus(); } catch (x) {}
  }

  function bkRandom() {
    var b = bkCurrent();
    if (!b || !b.entries.length) return;
    useBookEntry(b.entries[Math.floor(Math.random() * b.entries.length)]);
  }

  function bkSaveEntry(b, e, p, n) {
    if (!trimmed(p)) { bkMsg('The prompt is empty.', true); return; }
    req('POST', bkPath(b.name) + '/entries/' + e.id, { prompt: trimmed(p), negative: trimmed(n) }, function (code, data) {
      if (code !== 200) { bkMsg(errText(code, data, 'Could not save the entry'), true); return; }
      bkEditId = 0;
      bkMsg('');
      loadBooks();
    });
  }

  function bkDeleteEntry(b, e) {
    req('DELETE', bkPath(b.name) + '/entries/' + e.id, null, function (code, data) {
      if (code !== 200 && code !== 404) { bkMsg(errText(code, data, 'Could not delete the entry'), true); return; }
      bkMenuId = 0;
      loadBooks();
    });
  }

  // ---- the caret of the prompt, and putting text at it
  function rememberCaret() {
    var t = $('prompt');
    try {
      if (typeof t.selectionStart === 'number') promptCaret = { start: t.selectionStart, end: t.selectionEnd };
    } catch (x) {}
  }

  // ins at the caret (the last one the prompt had, else the end), a separator on each side where there is text.
  // sep: ', ' between words, ' ' for a placeholder inside a sentence.
  function putAtCaret(ins, sep) {
    var t = $('prompt'), v = t.value;
    var s = promptCaret ? Math.min(promptCaret.start, v.length) : v.length;
    var e = promptCaret ? Math.min(promptCaret.end, v.length) : v.length;
    var before = v.substring(0, s), after = v.substring(e);
    if (sep === ', ') {
      before = before.replace(/[ \t,]+$/, '');
      after = after.replace(/^[ \t,]+/, '');
    }
    var sb = '', sa = '';
    if (before) sb = sep === ', ' ? (/\n$/.test(before) ? '' : ', ') : (/\s$/.test(before) ? '' : ' ');
    if (after) sa = sep === ', ' ? (/^\n/.test(after) ? '' : ', ') : (/^\s/.test(after) ? '' : ' ');
    var head = before + sb + ins;
    t.value = head + sa + after;
    promptCaret = { start: head.length, end: head.length };
    try { if (t.setSelectionRange) t.setSelectionRange(head.length, head.length); } catch (x) {}
    persist();
    updateTriggerButtons();
  }

  function bkInsertName() {
    var b = bkCurrent();
    if (!b) return;
    putAtCaret('__' + b.name + '__', ' ');
    toast('__' + b.name + '__ is in the prompt. Each image picks an entry of ' + b.name + '.');
  }

  // ---- the name box: New, Rename, and a name for an import
  function bkNameBox(mode, preset) {
    bkMode = mode;
    var b = bkCurrent();
    $('bk-name').value = preset !== undefined ? preset : mode === 'rename' && b ? b.name : '';
    text($('bk-name-lbl'), mode === 'rename' ? 'New name' : mode === 'import' ? 'Name for the imported lines' : 'Name of the collection');
    text($('bk-name-msg'), '');
    show('bk-namebox', true, 'block');
    try { $('bk-name').focus(); $('bk-name').select(); } catch (x) {}
  }

  function bkNameClose() { bkMode = ''; bkLines = null; show('bk-namebox', false); }

  function bkNameOk() {
    var name = trimmed($('bk-name').value), mode = bkMode;
    var b = mode === 'rename' ? bkCurrent() : null;
    var bad = bookNameError(name, b);
    if (bad) { text($('bk-name-msg'), bad); return; }
    if (mode === 'new') {
      req('POST', APP + '/promptbooks', { name: name }, function (code, data) {
        if (code !== 201) { text($('bk-name-msg'), errText(code, data, 'Could not make the collection')); return; }
        bkOpen = name;
        bkShown = BK_PAGE;
        bkNameClose();
        loadBooks();
      });
    } else if (mode === 'rename' && b) {
      if (name === b.name) { bkNameClose(); return; }
      req('POST', bkPath(b.name), { name: name }, function (code, data) {
        if (code !== 200) { text($('bk-name-msg'), errText(code, data, 'Could not rename it')); return; }
        if (bkPicked(b.name)) { bkPickSet(b.name, false); bkPickSet(name, true); }
        bkOpen = name;
        bkNameClose();
        loadBooks();
      });
    } else if (mode === 'import' && bkLines) {
      var lines = bkLines;
      bkNameClose();
      bkImportNew(name, lines);
    }
  }

  function bkDeleteBook() {
    var b = bkCurrent();
    if (!b) return;
    var n = b.entries.length;
    if (!window.confirm('Delete the collection ' + b.name + (n ? ' with its ' + plural(n, 'entry', 'entries') : '') + '?')) return;
    req('DELETE', bkPath(b.name), null, function (code, data) {
      if (code !== 200 && code !== 404) { bkMsg(errText(code, data, 'Could not delete it'), true); return; }
      bkPickSet(b.name, false);
      bkOpen = '';
      bkMsg('');
      loadBooks();
    });
  }

  // ---- Import .txt, Export .txt
  // Dynamic Prompts' wildcard file: one entry per line, empty lines and lines that start with # skipped.
  function linesOfText(s) {
    var raw = String(s).replace(/^﻿/, '').split(/\r\n|\r|\n/), out = [], skipped = 0;
    for (var i = 0; i < raw.length; i++) {
      var l = trimmed(raw[i]);
      if (!l || l.charAt(0) === '#') continue;
      if (l.length > 2000 && l.replace(/[^\x00-\x7F]/g, 'xx').length > 8000) { skipped++; continue; }
      out.push(l);
    }
    return { lines: out, skipped: skipped };
  }

  // One line of an exported file per entry: a line break inside an entry becomes a space.
  function textOfBook(b) {
    var out = [];
    for (var i = 0; i < b.entries.length; i++) out.push(b.entries[i].prompt.replace(/\s*[\r\n]+\s*/g, ' '));
    return out.join('\n') + '\n';
  }

  function bkExport() {
    var b = bkCurrent();
    if (!b || !b.entries.length) return;
    var blob;
    try { blob = new Blob([textOfBook(b)], { type: 'text/plain;charset=utf-8' }); } catch (x) { bkMsg('This browser cannot save a file.', true); return; }
    var url = (window.URL || window.webkitURL).createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = b.name + '.txt';
    a.style.display = 'none';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { (window.URL || window.webkitURL).revokeObjectURL(url); }, 4000);
    bkMsg('Saved ' + b.name + '.txt with ' + plural(b.entries.length, 'line', 'lines') + '. Negative prompts are not in the file.');
  }

  function bkImportFile(file) {
    if (!file) return;
    var r = new FileReader();
    r.onload = function () {
      var got = linesOfText(r.result);
      if (!got.lines.length) { bkMsg(file.name + ' has no lines to import.', true); return; }
      var lines = got.lines, cut = '';
      if (lines.length > BK_ENTRIES_MAX) { cut = ' Only the first ' + BK_ENTRIES_MAX + ' lines fit in a collection.'; lines = lines.slice(0, BK_ENTRIES_MAX); }
      if (got.skipped) cut += ' ' + plural(got.skipped, 'line was', 'lines were') + ' too long and skipped.';
      var name = nameFromFile(file.name);
      bkImportNote = cut;
      if (isLoraName(name)) name = 'lora-words';
      var b = findBook(name);
      if (b) {
        if (window.confirm('There is a collection called ' + b.name + ' already.\n\nOK adds the ' + plural(lines.length, 'line', 'lines') + ' to it.\nCancel asks for another name.')) {
          bkImportAdd(b, lines);
        } else {
          bkLines = lines;
          bkNameBox('import', name + '2');
        }
        return;
      }
      bkImportNew(name, lines);
    };
    r.onerror = function () { bkMsg('Could not read ' + file.name + '.', true); };
    r.readAsText(file);
  }
  var bkImportNote = '';

  function entriesOfLines(lines) {
    var out = [];
    for (var i = 0; i < lines.length; i++) out.push({ prompt: lines[i], negative: '' });
    return out;
  }

  function bkImportNew(name, lines) {
    req('POST', APP + '/promptbooks', { name: name, entries: entriesOfLines(lines) }, function (code, data) {
      if (code !== 201) { bkMsg(errText(code, data, 'Could not import the file'), true); return; }
      bkOpen = name;
      bkShown = BK_PAGE;
      loadBooks(function () { bkMsg('Imported ' + plural(lines.length, 'line', 'lines') + ' into ' + name + '.' + bkImportNote); });
    });
  }

  // Add to a collection that is there: its entries and the new lines (the ones it has already left out) in one write.
  function bkImportAdd(b, lines) {
    var have = {}, keep = [], i, fresh = 0;
    for (i = 0; i < b.entries.length; i++) {
      have[b.entries[i].prompt] = 1;
      keep.push({ prompt: b.entries[i].prompt, negative: b.entries[i].negative || '' });
    }
    for (i = 0; i < lines.length; i++) if (!have[lines[i]]) { have[lines[i]] = 1; keep.push({ prompt: lines[i], negative: '' }); fresh++; }
    if (keep.length > BK_ENTRIES_MAX) { bkMsg('A collection holds at most ' + BK_ENTRIES_MAX + ' entries.', true); return; }
    req('POST', bkPath(b.name), { entries: keep }, function (code, data) {
      if (code !== 200) { bkMsg(errText(code, data, 'Could not import the file'), true); return; }
      bkOpen = b.name;
      loadBooks(function () { bkMsg('Added ' + plural(fresh, 'new line', 'new lines') + ' to ' + b.name + '.' + bkImportNote); });
    });
  }

  // ---- Save prompt
  function bookSaveShow(on) {
    show('bk-savebox', on, 'block');
    $('prompt-save').setAttribute('aria-expanded', on ? 'true' : 'false');
    if (on) {
      drawBookSave();
      try { $('bks-sel').focus(); } catch (x) {}
    }
  }

  function drawBookSave() {
    var sel = $('bks-sel'), list = sortedBooks(), want = sel.value || load('bookpick') || 'saved', i, has = false;
    clear(sel);
    var names = [];
    for (i = 0; i < list.length; i++) names.push(list[i].name);
    var first = findBook('saved');
    if (!first) names.unshift('saved');
    for (i = 0; i < names.length; i++) {
      var o = el('option', '', names[i] + (findBook(names[i]) ? '' : ' (new)'));
      o.value = names[i];
      sel.appendChild(o);
      if (names[i].toLowerCase() === want.toLowerCase()) { sel.value = names[i]; has = true; }
    }
    var nw = el('option', '', 'New collection…');
    nw.value = '__new__';
    sel.appendChild(nw);
    if (want === '__new__') { sel.value = '__new__'; has = true; }
    if (!has) sel.value = names[0];
    show('bks-name', sel.value === '__new__', 'block');
  }

  function bkSavePrompt() {
    var p = trimmed($('prompt').value), n = trimmed($('neg').value);
    if (!p) { text($('bks-msg'), 'Type a prompt first.'); return; }
    var name = $('bks-sel').value;
    if (name === '__new__') {
      name = trimmed($('bks-name').value);
      var bad = bookNameError(name, null);
      if (bad && findBook(name)) bad = '';   // an existing name: the prompt goes into it
      if (bad) { text($('bks-msg'), bad); return; }
    }
    if (isLoraName(name)) { text($('bks-msg'), 'lora is reserved.'); return; }
    text($('bks-msg'), '');
    req('POST', bkPath(name) + '/entries', { prompt: p, negative: n }, function (code, data) {
      if (code !== 200 && code !== 201) { text($('bks-msg'), errText(code, data, 'Could not save the prompt')); return; }
      save('bookpick', name);
      bookSaveShow(false);
      bkOpen = (findBook(name) || { name: name }).name;
      toast(code === 200 ? 'It is in ' + name + ' already.' : 'Saved to ' + name + '.');
      loadBooks();
    });
  }

  function initPromptBooks() {
    var t = $('prompt'), evs = ['blur', 'keyup', 'click', 'select', 'input', 'focus'];
    for (var i = 0; i < evs.length; i++) if (t.addEventListener) t.addEventListener(evs[i], rememberCaret);
    $('pt-hist').onclick = function () { panelSwitch('hist'); };
    $('pt-books').onclick = function () { panelSwitch('books'); };
    $('prompt-books').onclick = booksShow;
    $('prompt-save').onclick = function () { bookSaveShow($('bk-savebox').style.display === 'none'); };
    $('bks-cancel').onclick = function () { bookSaveShow(false); try { $('prompt-save').focus(); } catch (x) {} };
    $('bks-ok').onclick = bkSavePrompt;
    $('bks-sel').onchange = function () { show('bks-name', $('bks-sel').value === '__new__', 'block'); };
    $('bks-name').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { bkSavePrompt(); return false; } return true; };
    $('bk-random').onclick = bkRandom;
    bkRand = load('bookrand') === '1';
    try { var picks = JSON.parse(load('bookpicks') || '[]'); bkPicks = picks && picks.length !== undefined ? picks : []; } catch (x) { bkPicks = []; }
    $('bk-rand').onchange = function () { bkRandSet($('bk-rand').checked); };
    $('bk-rand-all').onclick = function () {
      var list = sortedBooks();
      for (var k = 0; k < list.length; k++) if (list[k].entries.length && !bkPicked(list[k].name)) bkPickSet(list[k].name, true);
      drawBooks();
      drawRandom();
    };
    $('bk-rand-none').onclick = function () { bkPicks = []; save('bookpicks', '[]'); drawBooks(); drawRandom(); };
    $('rand-off').onclick = function () { bkRandSet(false); try { $('prompt').focus(); } catch (x) {} };
    $('rand-change').onclick = function () { panelTab = 'books'; histShow(true); };
    drawRandom();
    $('bk-insert').onclick = bkInsertName;
    $('bk-import').onclick = function () { $('bk-file').click(); };
    $('bk-file').onchange = function () {
      var f = $('bk-file').files && $('bk-file').files[0];
      bkImportFile(f);
      try { $('bk-file').value = ''; } catch (x) {}
    };
    $('bk-export').onclick = bkExport;
    $('bk-rename').onclick = function () { if (bkCurrent()) bkNameBox('rename'); };
    $('bk-delbook').onclick = bkDeleteBook;
    $('bk-name-ok').onclick = bkNameOk;
    $('bk-name-cancel').onclick = bkNameClose;
    $('bk-name').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { bkNameOk(); return false; } return true; };
    loadBooks();
  }
