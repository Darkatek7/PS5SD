  // ---------- what the console knows about a LoRA: trigger words, strength, notes, a picture ----------
  // From the sidecar next to the file (AUTOMATIC1111's <name>.json). GET loras says per file whether there
  // is one (info, trigger, weight, base, preview, lookup); loras/info/<path> has the rest; loras/lookup/<path>
  // asks Civitai or Hugging Face and fills it. The pictures of Civitai are blurred as in the search.
  var loraStamp = {};   // path: a number that changes with the picture (the address of the <img> changes)
  var loraBusy = {};    // path: a lookup of it is under way
  var liFor = null;     // the open Info dialog: {l, ticket}
  var look = null;      // Look up all: {list, i, stop, found, none, fail}

  // The console has the 1.8 LoRA routes (until a route says no): the Info and Look up items show.
  function loraTools() { return tokState !== false; }

  function loraEntry(path) {
    var list = loraList || [];
    for (var i = 0; i < list.length; i++) if (list[i].path === path) return list[i];
    return null;
  }

  function loraWeight(path) {
    var e = loraEntry(path), w = e ? Number(e.weight) : 0;
    return w && !isNaN(w) ? w : 0;
  }

  function loraTrigger(path) {
    var e = loraEntry(path);
    return e && typeof e.trigger === 'string' ? e.trigger : '';
  }

  function loraPreviewUrl(path) {
    return APP + '/loras/preview/' + encodePath(path) + (loraStamp[path] ? '?t=' + loraStamp[path] : '');
  }

  // The list's entry takes what the console answered (the info of one LoRA).
  function mergeLoraInfo(l, d) {
    l.info = true;
    var keys = ['trigger', 'weight', 'base', 'preview', 'lookup'];
    for (var i = 0; i < keys.length; i++) {
      if (d[keys[i]]) l[keys[i]] = d[keys[i]]; else delete l[keys[i]];
    }
    if (d.preview_nsfw_level !== undefined) l.preview_nsfw_level = d.preview_nsfw_level;
    // the list and the mode (the console of 1.8.2 and later); without them the one string stays
    if (d.triggers && d.triggers.length !== undefined) {
      l.triggers = d.triggers;
      l.trigger_mode = d.trigger_mode;
      l.trigger_count = d.trigger_count;
      delete l.oldApp;
    } else {
      delete l.triggers;
      delete l.trigger_mode;
      delete l.trigger_count;
      if (l.trigger) l.oldApp = true; else delete l.oldApp;
    }
    loraStamp[l.path] = new Date().getTime();
  }

  function errCodeAny(data) {
    return errCode(data) || (data && typeof data.code === 'string' ? data.code : '');
  }

  // A picture that is blurred until it is clicked when its level is R or above (4): img is the <img>
  // and wrap its box. level: Civitai's nsfw level.
  function blurPicture(wrap, level) {
    if ((Number(level) || 0) < CV_MATURE) return;
    wrap.className += ' nsfw';
    wrap.setAttribute('role', 'button');
    wrap.tabIndex = 0;
    wrap.setAttribute('aria-label', 'Mature picture: show it');
    wrap.appendChild(el('span', 'cvmask', 'Mature. Click to show'));
    var reveal = function (e) {
      if (wrap.className.indexOf(' shown') >= 0) return true;
      wrap.className += ' shown';
      wrap.removeAttribute('role');
      if (e && e.stopPropagation) e.stopPropagation();
      return false;
    };
    wrap.onclick = reveal;
    wrap.onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13 || e.keyCode === 32) return reveal(e); return true; };
  }

  // ---- the trigger words in the Info dialog: chips (a press turns one on or off, × removes it), a field to
  // add, the mode. liFor.trig is {items, mode, count, srv}; srv: the console keeps the list and the mode.
  function liSetTrig(e) {
    if (!liFor) return;
    liFor.trig = { items: loraItems(e), mode: loraModeOf(e), count: loraCountOf(e),
      srv: !!(e && e.triggers && e.triggers.length !== undefined) };
    $('li-trigger').value = '';
    liDrawTrig();
  }

  function liDrawTrig() {
    var t = liFor && liFor.trig, box = $('li-chips'), mode = $('li-mode');
    clear(box);
    clear(mode);
    if (!t) return;
    for (var i = 0; i < t.items.length; i++) {
      (function (idx) {
        box.appendChild(trigChip(t.items[idx], function () {
          t.items[idx].on = !t.items[idx].on;
          liDrawTrig();
        }, function () {
          t.items.splice(idx, 1);
          liDrawTrig();
          $('li-trigger').focus();
        }));
      })(i);
    }
    if (!t.items.length) box.appendChild(el('span', 'mmeta', 'No trigger words yet.'));
    if (t.srv) {
      mode.appendChild(trigModeControls(t.mode, t.count, function (m, n) { t.mode = m; t.count = n; }));
      mode.appendChild(el('div', 'mmeta', t.mode === 'all' ? '__lora__ puts in the words that are on.'
        : t.mode === 'one' ? '__lora__ puts in one of the words that are on, a new one for each image.'
        : '__lora__ puts in ' + t.count + ' of the words that are on, new ones for each image.'));
    }
  }

  // Take what is typed in the field as items: a comma or Enter ends one. all: also the rest, without a comma.
  function liAddTrig(all) {
    var t = liFor && liFor.trig, f = $('li-trigger');
    if (!t) return;
    var parts = f.value.split(','), rest = all ? '' : parts.pop();
    for (var i = 0; i < parts.length; i++) {
      var w = trim(parts[i]).replace(/\s+/g, ' ');
      if (!w || t.items.length >= 100) continue;
      var dup = false;
      for (var j = 0; j < t.items.length; j++) if (t.items[j].text.toLowerCase() === w.toLowerCase()) dup = true;
      if (!dup) t.items.push({ text: w.slice(0, 200), on: true });
    }
    f.value = rest;
    liDrawTrig();
  }

  // ---- the Info dialog
  function openLoraInfo(l) {
    liFor = { l: l, ticket: (liFor ? liFor.ticket : 0) + 1 };
    var my = liFor.ticket;
    text($('li-title'), readable(l.name || loraName(l.path)));
    text($('li-file'), l.path + (l.bytes > 0 ? ' · ' + bytes(l.bytes) : ''));
    liSetTrig(l);
    $('li-weight').value = l.weight ? l.weight : '';
    $('li-notes').value = '';
    clear($('li-imgs'));
    clear($('li-facts'));
    show('li-desc-box', false);
    text($('li-err'), '');
    $('li-save').disabled = true;
    $('li-look').disabled = false;
    text($('li-look'), 'Look up online');
    show('lidlg', true, 'flex');
    $('li-trigger').focus();
    req('GET', APP + '/loras/info/' + encodePath(l.path), null, function (code, data) {
      if (!liFor || liFor.ticket !== my) return;
      if (code === 200 && data) {
        drawLoraInfo(l, data);
        $('li-save').disabled = false;
      } else if (code === 404) {
        closeLoraInfo();
        toast(l.name + ' is not on the console any more.', 5000);
        loadLoras(null);
      } else {
        text($('li-err'), errText(code, data, 'Could not read the info'));
        $('li-save').disabled = false;
      }
    }, 15000);
  }

  function liFact(box, label, value, url) {
    var row = el('div', 'mmeta');
    row.appendChild(document.createTextNode(label + ': '));
    if (url && /^https:\/\//i.test(url)) {
      var a = el('a', '', value);
      a.href = url;
      a.target = '_blank';
      a.rel = 'noopener';
      row.appendChild(a);
    } else {
      row.appendChild(document.createTextNode(value));
    }
    box.appendChild(row);
  }

  // What the dialog shows of an answer: the fields to edit, then the facts, the description, the pictures.
  function drawLoraInfo(l, d) {
    liSetTrig(d);
    $('li-weight').value = d.weight ? d.weight : '';
    $('li-notes').value = d.notes || '';
    var box = $('li-facts');
    clear(box);
    var c = d.civitai || {}, h = d.hf || {};
    if (d.base) liFact(box, 'Base model', d.base);
    if (c.url) liFact(box, 'Civitai', (c.model || 'model page') + (c.version ? ', ' + c.version : ''), c.url);
    if (h.url) liFact(box, 'Hugging Face', h.repo || h.url, h.url);
    if (d.source && d.source !== c.url && d.source !== h.url) liFact(box, 'Downloaded from', d.source, d.source);
    if (d.lookup === 'not_found') liFact(box, 'Online', 'Civitai does not know this file');
    else if (d.looked_up) liFact(box, 'Looked up', String(d.looked_up).replace('T', ' ').replace(/:\d\dZ$/, ' UTC').replace(/Z$/, ' UTC'));
    if (!box.firstChild) box.appendChild(el('div', 'mmeta', 'Nothing is known about this file yet. Look up online asks Civitai and Hugging Face.'));
    text($('li-desc'), d.description || '');
    show('li-desc-box', !!d.description);
    var imgs = $('li-imgs');
    clear(imgs);
    var shown = 0;
    function add(src, level) {
      var w = el('div', 'liimg');
      var im = document.createElement('img');
      im.alt = '';
      im.src = src;
      im.setAttribute('loading', 'lazy');
      im.setAttribute('referrerpolicy', 'no-referrer');
      w.appendChild(im);
      blurPicture(w, level);
      imgs.appendChild(w);
      shown++;
    }
    if (d.preview) add(loraPreviewUrl(l.path), d.preview_nsfw_level);
    var im = d.images || [];
    for (var i = 0; i < im.length && i < 6; i++) {
      var u = liImgUrl(im[i].url);
      if (u) add(u, im[i].nsfw_level);
    }
  }

  // A picture of the info: an https address (Civitai, Hugging Face), or one of the console's own previews.
  function liImgUrl(u) {
    return /^\/ps5sd\/v1\/app\/loras\/preview\//.test(u) ? u : civImg(u);
  }

  function closeLoraInfo() {
    show('lidlg', false);
    liFor = null;
  }

  function saveLoraInfo() {
    var f = liFor;
    if (!f) return;
    var w = trim($('li-weight').value), wn = w === '' ? 0 : Number(w);
    if (isNaN(wn) || wn < -100 || wn > 100) { text($('li-err'), 'The strength is a number from -100 to 100, or empty.'); return; }
    text($('li-err'), '');
    $('li-save').disabled = true;
    liAddTrig(true);
    var t = f.trig, body = { weight: wn, notes: $('li-notes').value };
    if (t && t.srv) {
      body.triggers = t.items;
      body.trigger_mode = t.mode;
      body.trigger_count = t.count;
    } else {
      // an older app: one string, the words that are on
      var on = [];
      for (var k = 0; t && k < t.items.length; k++) if (t.items[k].on) on.push(t.items[k].text);
      body.trigger = on.join(', ');
    }
    req('POST', APP + '/loras/info/' + encodePath(f.l.path), body, function (code, data) {
      if (!liFor || liFor !== f) return;
      $('li-save').disabled = false;
      if (code !== 200 || !data) {
        if (code === 404) { closeLoraInfo(); toast(f.l.name + ' is not on the console any more.', 5000); loadLoras(null); return; }
        text($('li-err'), errText(code, data, 'Could not save'));
        return;
      }
      mergeLoraInfo(f.l, data);
      closeLoraInfo();
      drawLoraList();
      drawLoraPicker();
      toast('Saved the info of ' + (f.l.name || loraName(f.l.path)) + '.');
    }, 15000);
  }

  // ---- look up online
  // done(kind): found, none (Civitai does not know it), gone, unreachable, error
  function lookupLora(l, done) {
    loraBusy[l.path] = true;
    drawLoraList();
    req('POST', APP + '/loras/lookup/' + encodePath(l.path), {}, function (code, data) {
      delete loraBusy[l.path];
      var kind;
      if (code === 200 && data) { mergeLoraInfo(l, data); kind = 'found'; }
      else if (code === 404 && errCodeAny(data) === 'no_lora') kind = 'gone';
      else if (code === 404) { l.info = true; l.lookup = 'not_found'; kind = 'none'; }
      else if (code === 502 || code === 0) kind = 'unreachable';
      else kind = 'error';
      drawLoraList();
      drawLoraPicker();
      done(kind, code, data);
    }, 120000);
  }

  function lookupText(kind, l, code, data) {
    var n = l.name || loraName(l.path);
    if (kind === 'found') return 'Found ' + n + (l.trigger ? ': trigger words ' + l.trigger : '. It has no trigger words') + '.';
    if (kind === 'none') return 'Civitai does not know ' + n + '. It may be your own, or changed. Type its trigger words under Info.';
    if (kind === 'gone') return n + ' is not on the console any more.';
    if (kind === 'unreachable') return 'The console cannot reach Civitai or Hugging Face. Try again later.';
    return errText(code, data, 'The lookup failed');
  }

  function lookupOne(l) {
    if (loraBusy[l.path] || look) return;
    toast('Looking up ' + (l.name || loraName(l.path)) + '…', 4000);
    lookupLora(l, function (kind, code, data) {
      toast(lookupText(kind, l, code, data), kind === 'found' ? 5000 : 8000);
      if (kind === 'gone') loadLoras(null);
    });
  }

  // The ones Look up all asks about: no info yet, and not found online before.
  function lookupTodo() {
    var out = [], list = loraList || [];
    for (var i = 0; i < list.length; i++) if (!list[i].info && list[i].lookup !== 'not_found') out.push(list[i]);
    return out;
  }

  function drawLookAll() {
    var todo = lookupTodo();
    var on = loraTools() && !look && todo.length > 0;
    show('lookall', on, 'inline-block');
    if (on) text($('lookall'), 'Look up all (' + todo.length + ')');
  }

  function drawLookBox() {
    show('lookbox', !!look);
    if (!look) return;
    var n = look.list.length;
    text($('looktext'), look.stop ? 'Stopping after this one…'
      : 'Looking up ' + Math.min(look.i + 1, n) + ' of ' + n + ': ' + readable(look.list[Math.min(look.i, n - 1)].name || loraName(look.list[Math.min(look.i, n - 1)].path)));
    $('lookfill').style.width = Math.round(100 * look.i / n) + '%';
    $('lookstop').disabled = look.stop;
  }

  function lookupAll() {
    if (look) return;
    var todo = lookupTodo();
    if (!todo.length) return;
    look = { list: todo, i: 0, stop: false, found: 0, none: 0, fail: 0 };
    drawLookAll();
    drawLookBox();
    lookNext();
  }

  function lookNext() {
    if (!look) return;
    if (look.stop || look.i >= look.list.length) { lookEnd(''); return; }
    drawLookBox();
    var l = look.list[look.i];
    lookupLora(l, function (kind) {
      if (!look) return;
      look.i++;
      if (kind === 'found') look.found++;
      else if (kind === 'none') look.none++;
      else if (kind !== 'gone') look.fail++;
      if (kind === 'unreachable') { lookEnd('Cannot reach Civitai or Hugging Face from the console. Stopped. '); return; }
      lookNext();
    });
  }

  function lookEnd(why) {
    var r = look;
    look = null;
    show('lookbox', false);
    loadLoras(null);
    if (r) {
      var done = r.found + r.none + r.fail;
      toast(why + 'Looked up ' + done + ': ' + r.found + ' found, ' + r.none + ' not on Civitai' + (r.fail ? ', ' + r.fail + ' failed' : '') + '.', 8000);
    }
  }

  function initLoraInfo() {
    $('li-close').onclick = closeLoraInfo;
    $('li-save').onclick = saveLoraInfo;
    var tf = $('li-trigger');
    tf.onkeydown = function (e) {
      e = e || window.event;
      if (e.keyCode === 13) { liAddTrig(true); if (e.preventDefault) e.preventDefault(); return false; }
      return true;
    };
    tf.oninput = function () { if (tf.value.indexOf(',') >= 0) liAddTrig(false); };
    tf.onblur = function () { if (liFor && trim(tf.value)) liAddTrig(true); };
    $('li-look').onclick = function () {
      var f = liFor;
      if (!f || loraBusy[f.l.path]) return;
      $('li-look').disabled = true;
      text($('li-look'), 'Looking up…');
      text($('li-err'), '');
      lookupLora(f.l, function (kind, code, data) {
        if (liFor !== f) { toast(lookupText(kind, f.l, code, data), 6000); return; }
        $('li-look').disabled = false;
        text($('li-look'), 'Look up online');
        if (kind === 'found') {
          drawLoraInfo(f.l, data);
        } else {
          text($('li-err'), lookupText(kind, f.l, code, data));
        }
      });
    };
    $('lookall').onclick = lookupAll;
    $('lookstop').onclick = function () { if (look) { look.stop = true; drawLookBox(); } };
  }
