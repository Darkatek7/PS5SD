  // ---------- small helpers ----------
  function save(key, value) { try { localStorage.setItem('ps5sd.' + key, String(value)); } catch (e) {} }
  function load(key) { try { return localStorage.getItem('ps5sd.' + key); } catch (e) { return null; } }
  function drop(key) { try { localStorage.removeItem('ps5sd.' + key); } catch (e) {} }

  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function text(node, value) { node.textContent = value; }
  function show(id, on, how) { $(id).style.display = on ? (how || 'block') : 'none'; }

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

