  // ---------- small helpers ----------
  function save(key, value) { try { localStorage.setItem('ps5sd.' + key, String(value)); } catch (e) {} }
  function load(key) { try { return localStorage.getItem('ps5sd.' + key); } catch (e) { return null; } }
  function drop(key) { try { localStorage.removeItem('ps5sd.' + key); } catch (e) {} }

  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function text(node, value) { node.textContent = value; }
  function show(id, on, how) { $(id).style.display = on ? (how || 'block') : 'none'; }

  // Text to the clipboard. The page comes over plain HTTP from the console, where browsers give no
  // navigator.clipboard; a selected text field and the copy command work there too.
  function copyText(value, what) {
    function done(ok) { toast(ok ? what + ' copied.' : 'Could not copy ' + what.toLowerCase() + '.', ok ? 2500 : 5000); }
    if (window.isSecureContext && navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(value).then(function () { done(true); }, function () { done(false); });
      return;
    }
    var t = document.createElement('textarea');
    t.value = value;
    t.setAttribute('readonly', '');
    t.style.position = 'fixed';
    t.style.left = '-9999px';
    document.body.appendChild(t);
    var focus = document.activeElement;
    t.select();
    try { t.setSelectionRange(0, value.length); } catch (e) {}
    var ok = false;
    try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
    document.body.removeChild(t);
    try { if (focus) focus.focus(); } catch (e) {}
    done(ok);
  }

  // Reading the clipboard needs HTTPS (or localhost): over the console's plain HTTP no browser allows it.
  function canPaste() { return !!(window.isSecureContext && navigator.clipboard && navigator.clipboard.readText); }

  // The clipboard's text in place of the field's selection, or at its cursor.
  function pasteInto(id) {
    var f = $(id);
    navigator.clipboard.readText().then(function (t) {
      var a = f.selectionStart, b = f.selectionEnd;
      if (typeof a !== 'number' || typeof b !== 'number') { a = b = f.value.length; }
      f.value = f.value.substring(0, a) + t + f.value.substring(b);
      persist();
    }, function () { toast('The browser did not give the clipboard.', 5000); });
  }

  // Every call to the server. X-PS5SD-Request lets a POST with the login cookie through (a page of
  // another site cannot add it). A 401 that asks for the API key shows the login; until it is done,
  // nothing else goes out, so the old cookie of a changed key does not count as wrong keys.
  function req(method, url, body, cb, timeout) {
    if (auth.locked && url.indexOf('/ps5sd/v1/auth') !== 0) {
      setTimeout(function () { cb(401, { error: 'API key needed', auth_required: true }); }, 0);
      return;
    }
    var x = new XMLHttpRequest();
    var done = false;
    function finish(code, data) {
      if (done) return;
      done = true;
      if (code === 401 && data && data.auth_required && url.indexOf('/ps5sd/v1/auth') !== 0) showLogin();
      cb(code, data);
    }
    x.open(method, url, true);
    x.setRequestHeader('X-PS5SD-Request', '1');
    x.onreadystatechange = function () {
      if (x.readyState !== 4) return;
      var data = null;
      try { data = JSON.parse(x.responseText); } catch (e) {}
      finish(x.status, data);
    };
    x.onerror = function () { finish(0, null); };
    x.ontimeout = function () { finish(0, null); };
    try { x.timeout = timeout || 0; } catch (e) {}
    if (body !== null && body !== undefined) {
      x.setRequestHeader('Content-Type', 'application/json');
      x.send(JSON.stringify(body));
    } else {
      x.send();
    }
  }

