  // ---------- storage and USB ----------
  // GET /ps5sd/v1/app/storage: the console's space, what the models, LoRAs, gallery and upscalers take, and
  // the USB drives that are plugged in. The Settings card shows it, Get more and Downloads show the free
  // space, and the gallery copies files to a drive (POST /ps5sd/v1/app/gallery/copy, followed with GET).
  // Both routes are new: an older app answers 404 and every part of this stays hidden.
  var LOW_SPACE = 10e9;   // under this many bytes free the bar turns to a warning
  var stor = {
    ok: null,        // the route exists (null = not asked yet)
    data: null,      // the last answer
    t: 0,            // when it came
    busy: null,      // callbacks of a load on its way
    cp: null,        // the last answer of GET gallery/copy
    cpTimer: 0,
    mine: false,     // this page started or saw the copy that is under way
    usb: null        // the usb_copy setting: '' or a drive id; null when the server does not have it
  };

  // The free bytes on the console, or -1 when not known.
  function storFree() {
    var c = stor.ok && stor.data ? stor.data.console : null;
    return c && typeof c.free === 'number' ? c.free : -1;
  }

  // cb(data) once the answer is in. maxAge (ms): an answer that young is taken as it is.
  function loadStorage(cb, maxAge) {
    if (stor.ok && maxAge && new Date().getTime() - stor.t < maxAge) { if (cb) cb(stor.data); return; }
    if (stor.busy) { if (cb) stor.busy.push(cb); return; }
    var waiters = stor.busy = cb ? [cb] : [];
    req('GET', APP + '/storage', null, function (code, data) {
      stor.busy = null;
      if (code === 200 && data && data.console) {
        stor.ok = true;
        stor.data = data;
        stor.t = new Date().getTime();
      } else if (absent(code)) {
        stor.ok = false;
      }
      function finish() {
        drawStorage();
        for (var i = 0; i < waiters.length; i++) waiters[i](stor.ok ? stor.data : null);
      }
      // the usb_copy switch is read from the settings, which only the Settings card shows
      if (stor.ok && st.view === 'settings') {
        loadSettings(function (c, s) {
          stor.usb = c === 200 && s && typeof s.usb_copy === 'string' ? s.usb_copy : null;
          finish();
        });
      } else {
        finish();
      }
    }, 8000);
  }

  // The free space line in Get more and Downloads, and the warning on a catalog entry that does not fit.
  function drawFree() {
    var free = storFree(), c = stor.ok && stor.data ? stor.data.console : null;
    var lines = document.getElementsByClassName('freeline');
    for (var i = 0; i < lines.length; i++) {
      lines[i].style.display = free >= 0 ? 'block' : 'none';
      if (free < 0) continue;
      text(lines[i], 'Free on the console: ' + bytes(free) +
        (free < LOW_SPACE ? '. That is little: delete a model you do not use before a large download.' : '.'));
      lines[i].className = 'freeline' + (free < LOW_SPACE ? ' low' : '');
    }
    drawCatalog();
  }

  // Does n bytes fit? true, false, or null when the free space is not known. A download needs its size.
  function fits(n) {
    var free = storFree();
    if (free < 0 || !(Number(n) > 0)) return null;
    return Number(n) <= free;
  }

  // ---- the Settings card
  function usbName(u, i) { return 'USB drive ' + (i + 1) + ' (' + u.id + ')'; }

  function drawStorage() {
    show('pnl-storage', stor.ok === true);
    drawSelUsb();
    drawFree();
    drawCopyLine();
    var d = stor.ok ? stor.data : null;
    if (!d) return;
    // free only: the console tells no total (the app asks ftpsrv, which knows only what is free)
    var free = storFree();
    text($('st-line'), free >= 0 ? bytes(free) + ' free on the console' + (free < LOW_SPACE ? ': that is little.' : '')
      : 'The free space is not known: no FTP server answers on the console.');
    $('st-line').className = 'stline' + (free >= 0 && free < LOW_SPACE ? ' low' : '');
    var box = $('st-used'), u = d.used || {}, rows = [['Models', u.models], ['LoRAs', u.loras], ['Gallery', u.gallery], ['Upscalers', u.upscalers]];
    clear(box);
    for (var i = 0; i < rows.length; i++) {
      if (typeof rows[i][1] !== 'number') continue;
      var r = el('div', 'arow');
      r.appendChild(el('span', '', rows[i][0]));
      r.appendChild(el('span', '', bytes(rows[i][1])));
      box.appendChild(r);
    }
    drawUsb(d.usb || []);
  }

  function drawUsb(list) {
    var box = $('st-usb'), copying = stor.cp && stor.cp.state === 'copying', set = stor.usb, found = false, i;
    clear(box);
    if (!list.length) {
      box.appendChild(el('p', 'hint', 'No USB drive is plugged in. Plug in a USB drive (exFAT or FAT32), then press Refresh.'));
    }
    for (i = 0; i < list.length; i++) {
      (function (u, n) {
        if (set && u.id === set) found = true;
        var row = el('div', 'usbdrive');
        var head = el('div', 'usbhead');
        head.appendChild(el('span', '', usbName(u, n)));
        head.appendChild(el('span', '', typeof u.free === 'number' ? bytes(u.free) + ' free' : ''));
        row.appendChild(head);
        var b = actionButton('Copy the gallery here', function () { startCopy(u.id, null); });
        b.disabled = !!copying;
        b.title = 'Every image and video of the gallery goes to the folder PS5SD on this drive';
        row.appendChild(b);
        if (set !== null) {
          var lab = el('label', 'opt');
          var cb = document.createElement('input');
          cb.type = 'checkbox';
          cb.checked = set === u.id;
          cb.onchange = function () { setUsbCopy(cb.checked ? u.id : '', cb); };
          lab.appendChild(cb);
          lab.appendChild(document.createTextNode(' Copy each new image to this drive'));
          row.appendChild(lab);
        }
        if (stor.cp && stor.cp.to === u.id && stor.cp.state !== 'idle') {
          var line = el('div', 'cpline' + (stor.cp.state === 'failed' ? ' bad' : ''), copySummary(stor.cp));
          row.appendChild(line);
          if (stor.cp.state === 'copying') {
            var bar = el('div', 'bar');
            bar.appendChild(el('div', ''));
            bar.firstChild.style.width = copyPct(stor.cp) + '%';
            row.appendChild(bar);
          }
        }
        box.appendChild(row);
      })(list[i], i);
    }
    if (set && !found) {
      box.appendChild(el('p', 'hint bad', 'Each new image is set to be copied to ' + set + ', which is not plugged in. The copy is skipped until it is.'));
    }
  }

  // The switch: each new image or video is copied to this drive too. One drive at a time ('' for none).
  function setUsbCopy(id, box) {
    box.disabled = true;
    req('POST', APP + '/settings', { usb_copy: id }, function (code, data) {
      box.disabled = false;
      if (code === 200 && data && typeof data.usb_copy === 'string') {
        stor.usb = data.usb_copy;
        toast(data.usb_copy ? 'Each new image goes to ' + data.usb_copy + ' too, into the folder PS5SD.' : 'New images are no longer copied to a USB drive.', 5000);
      } else {
        toast(errText(code, data, 'Could not change it'), 6000);
      }
      drawStorage();
    }, 10000);
  }

  // ---- the copy
  function copyPct(cp) {
    var t = Number(cp.total) || 0;
    return t > 0 ? Math.max(0, Math.min(100, Math.round((Number(cp.done) || 0) * 100 / t))) : 0;
  }

  function copySummary(cp) {
    var n = Number(cp.done) || 0, t = Number(cp.total) || 0, sk = Number(cp.skipped) || 0;
    var s = n + ' of ' + t + (t === 1 ? ' file' : ' files') + (sk ? ', ' + sk + ' already there' : '');
    if (cp.state === 'copying') return 'Copying to ' + cp.to + ': ' + s + '…';
    if (cp.state === 'done') return 'Copied to ' + cp.to + ': ' + s + (cp.bytes ? ', ' + bytes(cp.bytes) : '') + '.';
    if (cp.state === 'failed') return 'The copy to ' + cp.to + ' stopped at ' + s + (cp.error ? ': ' + cp.error : '.');
    return '';
  }

  // The gallery's line: shown while a copy runs (and the last result of a copy this page followed).
  function drawCopyLine() {
    var cp = stor.cp, on = !!cp && (cp.state === 'copying' || (stor.mine && (cp.state === 'done' || cp.state === 'failed')));
    show('gcopy', on);
    if (!on) return;
    text($('gcopy-text'), copySummary(cp));
    $('gcopy-text').className = cp.state === 'failed' ? 'cpline bad' : 'cpline';
    $('gcopy-fill').style.width = copyPct(cp) + '%';
  }

  // A new answer of the copy: draw it, poll every 2 s while it runs, tell the end.
  function setCopy(data) {
    if (!data || typeof data.state !== 'string') return;
    var was = stor.cp ? stor.cp.state : '';
    stor.cp = data;
    clearTimeout(stor.cpTimer);
    if (data.state === 'copying') {
      stor.mine = true;
      stor.cpTimer = setTimeout(copyPoll, 2000);
    } else if (was === 'copying' && (data.state === 'done' || data.state === 'failed')) {
      toast(copySummary(data), data.state === 'failed' ? 9000 : 6000);
      loadStorage(null);
    }
    drawStorage();
  }

  function copyPoll() {
    req('GET', APP + '/gallery/copy', null, function (code, data) {
      if (code === 200 && data) { setCopy(data); return; }
      // the console did not answer: try again; a missing route stops it
      if (!absent(code)) stor.cpTimer = setTimeout(copyPoll, 4000);
    }, 8000);
  }

  // names: the files to copy, null for the whole gallery. after(ok) when the console has taken it or not.
  function startCopy(drive, names, after) {
    var body = { to: drive };
    if (names) body.names = names;
    req('POST', APP + '/gallery/copy', body, function (code, data) {
      if (code === 202 || code === 200) {
        stor.mine = true;
        toast((names ? 'Copying ' + names.length + (names.length === 1 ? ' file' : ' files') : 'Copying the gallery') +
          ' to ' + drive + ', into the folder PS5SD.', 5000);
        setCopy(data);
        if (after) after(true);
        return;
      }
      if (code === 409) { toast('A copy is already running. Wait until it is done.', 6000); copyPoll(); }
      else toast(errText(code, data, 'Could not start the copy'), 6000);
      if (after) after(false);
    }, 15000);
  }

  // Entering Settings or the gallery's selection: the space, the drives, and a copy that is already running.
  function enterStorage() {
    loadStorage(function (d) {
      if (d && stor.ok) req('GET', APP + '/gallery/copy', null, function (code, data) { if (code === 200) setCopy(data); }, 8000);
    });
  }

  // ---- Copy to USB in the gallery's selection
  function drawSelUsb() {
    var n = selNames().length;
    show('gsel-usb', !!gsel && stor.ok !== false, 'inline-block');
    if (gsel) $('gsel-usb').disabled = !n;
    if (!gsel || !n) show('gsel-drives', false);
  }

  function copySelected() {
    var list = selNames();
    if (!list.length) return;
    var names = [];
    for (var i = 0; i < list.length; i++) names.push(list[i].name);
    $('gsel-usb').disabled = true;
    loadStorage(function (d) {
      $('gsel-usb').disabled = false;
      if (!d || !stor.ok) { toast('This version of the PS5SD app cannot copy to a USB drive.', 6000); return; }
      var drives = d.usb || [];
      if (!drives.length) { toast('Plug in a USB drive (exFAT or FAT32).', 6000); return; }
      if (drives.length === 1) { copyTo(drives[0].id, names); return; }
      var box = $('gsel-drives');
      clear(box);
      box.appendChild(el('span', '', 'Copy ' + names.length + (names.length === 1 ? ' file' : ' files') + ' to the folder PS5SD on:'));
      for (var k = 0; k < drives.length; k++) {
        (function (u, n) {
          var b = actionButton(usbName(u, n) + (typeof u.free === 'number' ? ', ' + bytes(u.free) + ' free' : ''), function () { copyTo(u.id, names); });
          box.appendChild(b);
        })(drives[k], k);
      }
      box.appendChild(actionButton('Cancel', function () { show('gsel-drives', false); }));
      show('gsel-drives', true, 'flex');
    });
  }

  function copyTo(drive, names) {
    show('gsel-drives', false);
    startCopy(drive, names, function (ok) { if (ok) setSelecting(false); });
  }
