  // ---------- API key: login, and the switch in Settings ----------
  // The server says whether it needs a key (GET /ps5sd/v1/auth); the login sets a cookie, which the
  // page and its images send from then on. The key itself is on the TV, under the address.
  var auth = { locked: false, key: '', enabled: false, started: false };

  function showLogin() {
    if (auth.locked) return;
    auth.locked = true;
    req('POST', '/ps5sd/v1/auth/logout', {}, function () {});  // a cookie of an old key goes
    text($('login-err'), '');
    show('login', true, 'flex');
    try { $('login-key').focus(); } catch (e) {}
  }

  function login() {
    var key = trim($('login-key').value);
    if (!key) { $('login-key').focus(); return; }
    $('login-go').disabled = true;
    req('POST', '/ps5sd/v1/auth/login', { key: key }, function (code, data) {
      $('login-go').disabled = false;
      if (code === 200) {
        auth.locked = false;
        $('login-key').value = '';
        show('login', false);
        if (!auth.started) start(); else window.location.reload();
      } else if (code === 429) {
        text($('login-err'), (data && data.error) || 'Too many tries. Wait a moment.');
      } else if (code === 401) {
        text($('login-err'), 'That is not the key. It is on the TV, under the address.');
      } else {
        text($('login-err'), errText(code, data, 'Could not sign in'));
      }
    }, 15000);
  }

  // Before anything else: does the server want a key, and does this browser have it?
  function checkAuth(then) {
    req('GET', '/ps5sd/v1/auth', null, function (code, data) {
      if (code === 200 && data && data.required && !data.authenticated) { showLogin(); return; }
      then();
    }, 8000);
  }

  function loadAuth() {
    req('GET', APP + '/auth', null, function (code, data) {
      if (code !== 200 || !data || typeof data.enabled !== 'boolean') return;
      auth.enabled = data.enabled;
      auth.key = data.key || '';
      show('opt-auth-box', true);
      drawAuth();
    });
  }

  function drawAuth() {
    text($('auth-state'), auth.enabled
      ? 'On: browsers and apps need the key. It is on the TV too.'
      : 'Off: anyone who reaches this console can use it. Turn it on before you open it to the internet.');
    show('auth-key-row', auth.enabled && auth.shown, 'block');
    text($('auth-key'), auth.enabled ? auth.key : '');  // as apps need it; the TV shows it in groups
    show('auth-on', !auth.enabled, 'inline-block');
    show('auth-show', auth.enabled, 'inline-block');
    text($('auth-show'), auth.shown ? 'Hide key' : 'Show key');
    show('auth-new', auth.enabled, 'inline-block');
    show('auth-off', auth.enabled, 'inline-block');
    show('auth-out', auth.enabled, 'inline-block');
    show('opt-tvkey-row', auth.enabled, 'flex');
  }

  // A new key (also the first): this browser signs in with it at once, so it stays in.
  function newKey() {
    var first = !auth.enabled;
    $('auth-on').disabled = $('auth-new').disabled = true;
    req('POST', APP + '/auth/enable', {}, function (code, data) {
      $('auth-on').disabled = $('auth-new').disabled = false;
      if (code !== 200 || !data || !data.key) { toast(errText(code, data, 'Could not make a key'), 6000); return; }
      auth.enabled = true;
      auth.key = data.key;
      auth.shown = true;
      req('POST', '/ps5sd/v1/auth/login', { key: data.key }, function () {
        drawAuth();
        toast(first ? 'The API key is on. Apps need it as "Authorization: Bearer <key>".'
          : 'New key. The old one does not work any more; browsers that used it must sign in again.', 8000);
      });
    });
  }

  function askAuthOff() {
    dlgFor = { authOff: true };
    dlgFrom = document.activeElement;
    text($('dlg-title'), 'Turn the API key off?');
    text($('dlg-body'), 'Anyone who can reach the console can then use it, without a key.');
    $('dlg-ok').disabled = false;
    $('dlg-cancel').disabled = false;
    text($('dlg-ok'), 'Turn off');
    show('dlg', true, 'flex');
    $('dlg-cancel').focus();
  }

  function authOff() {
    req('POST', APP + '/auth/disable', {}, function (code, data) {
      closeDlg();
      if (code !== 200) { toast(errText(code, data, 'Could not turn it off'), 6000); return; }
      auth.enabled = false;
      auth.key = '';
      auth.shown = false;
      drawAuth();
      toast('The API key is off.');
    });
  }

  function signOut() {
    req('POST', '/ps5sd/v1/auth/logout', {}, function () { window.location.reload(); });
  }

  var toastTimer = 0;
  function toast(message, ms) {
    var t = $('toast');
    text(t, message);
    t.style.display = 'block';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.style.display = 'none'; }, ms || 2500);
  }

  function setPill() {
    var cls = 'off', label = 'offline';
    if (st.sw) { cls = 'work'; label = 'switching'; }
    else if (st.online) {
      if (st.busy) { cls = 'work'; label = 'generating'; }
      else if (st.others) { cls = 'busy'; label = 'busy'; }
      else { cls = 'on'; label = 'online'; }
    }
    $('pill').className = 'pill ' + cls;
    text($('pilltext'), label);
  }

  function setOnline(on) {
    var was = st.online;
    st.online = on;
    if (on) st.offFails = 0;
    setPill();
    if (on && !was && !st.sw) loadInfo();
    if (!on && st.model === null && !st.sw) {
      text($('model'), 'server not reachable');
      text($('cm-name'), 'server not reachable');
    }
  }

  function num(id, fallback, min, max) {
    var v = parseFloat($(id).value);
    if (isNaN(v)) v = fallback;
    if (v < min) v = min;
    if (v > max) v = max;
    return v;
  }

  function size8(id, fallback) {
    var v = Math.round(num(id, fallback, 64, 2048) / 8) * 8;
    $(id).value = v;
    return v;
  }

  function clock(seconds) {
    seconds = Math.max(0, Math.round(seconds));
    var m = Math.floor(seconds / 60), s = seconds % 60;
    return m + ':' + (s < 10 ? '0' : '') + s;
  }

  function mime(b64) { return b64.indexOf('/9j/') === 0 ? 'image/jpeg' : 'image/png'; }
  function dataUrl(b64) { return 'data:' + mime(b64) + ';base64,' + b64; }

