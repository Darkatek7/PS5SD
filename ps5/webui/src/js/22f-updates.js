  // ---------- is a newer release out ----------
  // Settings, About: GitHub's latest release of PS5SD, asked from this browser at most once a day (the
  // answer and its time are kept in this browser), compared with the running version. Without internet
  // nothing shows and nothing is said. A newer one: "PS5SD 1.4.0 is out" with a link, and a dot on the
  // Settings tab.
  var RELEASE_URL = 'https://api.github.com/repos/Darkatek7/PS5SD/releases/latest';
  var UPDATE_EVERY = 24 * 3600 * 1000;   // a good answer is kept this long
  var UPDATE_RETRY = 3600 * 1000;        // after a failed ask, wait this long

  // "v1.10.0" or "1.10.0" as [1, 10, 0]; null when it is not a version.
  function versionParts(s) {
    var m = /^v?(\d+)\.(\d+)\.(\d+)/.exec(String(s || ''));
    return m ? [parseInt(m[1], 10), parseInt(m[2], 10), parseInt(m[3], 10)] : null;
  }

  // Is version a newer than b? Number by number: 1.10.0 is newer than 1.9.0.
  function versionNewer(a, b) {
    var x = versionParts(a), y = versionParts(b);
    if (!x || !y) return false;
    for (var i = 0; i < 3; i++) {
      if (x[i] !== y[i]) return x[i] > y[i];
    }
    return false;
  }

  function showUpdateNotice(tag, url) {
    var on = versionNewer(tag, st.version);
    // only a link to GitHub is followed
    if (on && !/^https:\/\/github\.com\//.test(String(url || ''))) on = false;
    show('about-upd-row', on, 'flex');
    show('sdot', on, 'inline-block');
    if (!on) return;
    var a = $('about-upd');
    text(a, 'PS5SD ' + String(tag).replace(/^v/, '') + ' is out');
    a.href = url;
  }

  // version: the running one ("1.3.1"), known from the app's info.
  function checkUpdate() {
    if (!st.version) return;
    var now = new Date().getTime(), c = null;
    try { c = JSON.parse(load('update') || 'null'); } catch (e) { c = null; }
    if (c && typeof c.t === 'number' && c.t <= now) {
      if (c.tag && now - c.t < UPDATE_EVERY) { showUpdateNotice(c.tag, c.url); return; }
      if (!c.tag && now - c.t < UPDATE_RETRY) return;
    }
    var x = new XMLHttpRequest();
    var done = false;
    function finish(tag, url) {
      if (done) return;
      done = true;
      save('update', JSON.stringify({ t: new Date().getTime(), tag: tag || '', url: url || '' }));
      if (tag) showUpdateNotice(tag, url);
    }
    try {
      x.open('GET', RELEASE_URL, true);
      // no custom header: it would make the browser ask GitHub for permission first
      x.timeout = 8000;
      x.onreadystatechange = function () {
        if (x.readyState !== 4) return;
        var d = null;
        try { d = JSON.parse(x.responseText); } catch (e) { d = null; }
        if (x.status === 200 && d && typeof d.tag_name === 'string' && versionParts(d.tag_name)) finish(d.tag_name, d.html_url);
        else finish('', '');
      };
      x.onerror = x.ontimeout = function () { finish('', ''); };
      x.send();
    } catch (e) { finish('', ''); }
  }
