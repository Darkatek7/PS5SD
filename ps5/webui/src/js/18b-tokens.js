  // ---------- tokens: the Civitai API key and the Hugging Face token (Get more) ----------
  // GET /ps5sd/v1/app/tokens says whether each is set, never what it is. POST sets one, "" removes it.
  // tokState: null until asked, false on a console without the route, else {civitai, huggingface}.
  var tokState = null;
  var tokBusy = false;
  var TOKEN_KINDS = [
    { kind: 'civitai', panel: 'pnl-civtok', name: 'Civitai API key', done: 'The Civitai API key' },
    { kind: 'huggingface', panel: 'pnl-hftok', name: 'Hugging Face token', done: 'The Hugging Face token' }
  ];

  function loadTokens() {
    req('GET', APP + '/tokens', null, function (code, data) {
      if (code === 200 && data && typeof data === 'object') tokState = { civitai: !!data.civitai, huggingface: !!data.huggingface };
      else if (absent(code)) tokState = false;
      drawTokens();
    }, 8000);
  }

  // The panels show where the console keeps tokens; each says whether one is set.
  function drawTokens() {
    for (var i = 0; i < TOKEN_KINDS.length; i++) {
      var k = TOKEN_KINDS[i], on = !!tokState && st.dl === true;
      show(k.panel, on);
      if (!on) continue;
      var set = !!tokState[k.kind], b = $('tok-' + k.kind + '-b');
      text(b, set ? 'Set' : 'Not set');
      b.className = 'badge' + (set ? ' ok' : '');
      $('tok-' + k.kind + '-rm').disabled = tokBusy || !set;
      $('tok-' + k.kind + '-save').disabled = tokBusy;
    }
  }

  function saveToken(k, value) {
    var body = {};
    body[k.kind] = value;
    tokBusy = true;
    drawTokens();
    req('POST', APP + '/tokens', body, function (code, data) {
      tokBusy = false;
      if (code === 200 && data && typeof data === 'object') {
        tokState = { civitai: !!data.civitai, huggingface: !!data.huggingface };
        $('tok-' + k.kind).value = '';
        toast(value ? k.done + ' is saved on the console.' : k.done + ' is removed from the console.', 4000);
      } else {
        if (absent(code)) tokState = false;
        toast(errText(code, data, 'Could not save the token'), 6000);
      }
      drawTokens();
    }, 10000);
  }

  function initTokens() {
    for (var i = 0; i < TOKEN_KINDS.length; i++) {
      (function (k) {
        var save1 = function () {
          var v = trim($('tok-' + k.kind).value);
          if (!v) { toast('Paste the ' + k.name + ' first.'); return; }
          if (!/^[A-Za-z0-9_.\-]{1,200}$/.test(v)) { toast('A token has only letters, digits and - _ . in it, at most 200.', 6000); return; }
          saveToken(k, v);
        };
        $('tok-' + k.kind + '-save').onclick = save1;
        $('tok-' + k.kind + '-rm').onclick = function () { saveToken(k, ''); };
        $('tok-' + k.kind).onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { save1(); return false; } return true; };
      })(TOKEN_KINDS[i]);
    }
  }
