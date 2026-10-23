  // ---------- start up ----------
  // The app's version (an older app, or a plain sd-server, has no info route: nothing shown).
  function loadVersion() {
    req('GET', APP + '/info', null, function (code, data) {
      var sd = data && data.sd_cpp ? data.sd_cpp : {};
      if (code === 200 && data && data.version) {
        text($('ver'), 'v' + data.version);
        $('ver').title = 'stable-diffusion.cpp ' + (sd.commit || '');
        text($('about-ver'), 'v' + data.version);
        var sdText = sd.version || '';
        if (sd.commit) sdText += (sdText ? ', ' : '') + 'commit ' + sd.commit;
        text($('about-sd'), sdText || 'not reported');
      } else {
        text($('about-ver'), 'not available');
        text($('about-sd'), 'not available');
      }
    });
  }

  // A "#key=<key>" in the address (the TV's QR code, when the TV shows the key): sign in with it,
  // and take it out of the address first, so it stays out of the history and bookmarks.
  function keyFromAddress(then) {
    var m = /(^#|&)key=([a-z2-7]+)/i.exec(window.location.hash || '');
    if (!m) { then(); return; }
    try {
      if (window.history && window.history.replaceState) window.history.replaceState(null, '', window.location.pathname + window.location.search);
      else window.location.hash = '';
    } catch (e) {}
    req('POST', '/ps5sd/v1/auth/login', { key: m[2].toLowerCase() }, function () { then(); }, 15000);
  }

  // The login first (when the server wants a key), then the page.
  function init() {
    $('login-go').onclick = login;
    $('login-key').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { login(); return false; } return true; };
    keyFromAddress(function () { checkAuth(start); });
  }

  function start() {
    auth.started = true;
    loadVersion();
    loadAuth();
    buildPresets();
    initSignal();
    loadTvOption();
    $('opt-tv').onchange = setTvOption;
    $('opt-tvkey').onchange = setTvKeyOption;
    $('hr-on').onchange = function () { drawHires(); persist(); };
    $('hr-up').onchange = function () { save('hrup', this.value); persist(); };
    $('hr-scale').onchange = $('hr-steps').onchange = $('hr-den').onchange = persist;
    $('hr-scale-').onclick = function () { stepper('hr-scale', -0.25, 1, 4, 2); };
    $('hr-scale+').onclick = function () { stepper('hr-scale', 0.25, 1, 4, 2); };
    $('hr-steps-').onclick = function () { stepper('hr-steps', -1, 0, 150, 0); };
    $('hr-steps+').onclick = function () { stepper('hr-steps', 1, 0, 150, 0); };
    $('hr-den-').onclick = function () { stepper('hr-den', -0.05, 0.05, 1, 2); };
    $('hr-den+').onclick = function () { stepper('hr-den', 0.05, 0.05, 1, 2); };
    $('opt-up').onchange = function () { save('upscaler', this.value); drawUpscalers(); };
    $('init-pick').onclick = function () { $('init-file').click(); };
    $('init-file').onchange = initFile;
    $('init-drop').onclick = dropInit;
    $('vinit-pick').onclick = function () { $('vinit-file').click(); };
    $('vinit-file').onchange = vinitFile;
    $('vinit-drop').onclick = dropVideoStart;
    $('init-mask').onclick = openMask;
    $('init-crop').onchange = drawInit;
    $('init-ref').onchange = drawInit;
    $('init-str').onchange = persist;
    $('init-str-').onclick = function () { stepper('init-str', -0.05, 0.05, 1, 2); };
    $('init-str+').onclick = function () { stepper('init-str', 0.05, 0.05, 1, 2); };
    $('cn-pick').onclick = function () { $('cn-file').click(); };
    $('cn-file').onchange = guideFile;
    $('cn-drop').onclick = dropGuide;
    $('cn-str').onchange = persist;
    $('cn-str-').onclick = function () { stepper('cn-str', -0.1, 0.1, 1.5, 1); };
    $('cn-str+').onclick = function () { stepper('cn-str', 0.1, 0.1, 1.5, 1); };
    $('cn-canny').onchange = function () { summary(); };
    initCreateLayout();
    initModelsTabs();
    initHelpLinks();
    initConsole();
    $('me-size-').onclick = function () { me.brush = Math.max(8, me.brush - 8); text($('me-size'), 'Brush ' + me.brush); };
    $('me-size+').onclick = function () { me.brush = Math.min(240, me.brush + 8); text($('me-size'), 'Brush ' + me.brush); };
    $('me-mode').onclick = function () { me.erase = !me.erase; text($('me-mode'), me.erase ? 'Paint' : 'Erase'); };
    $('me-invert').onclick = invertMask;
    $('me-clear').onclick = clearMask;
    $('me-done').onclick = maskDone;
    var mc = $('me-canvas');
    mc.onmousedown = maskDown;
    mc.onmousemove = maskMove;
    if (mc.addEventListener) {
      mc.addEventListener('touchstart', maskDown, { passive: false });
      mc.addEventListener('touchmove', maskMove, { passive: false });
      mc.addEventListener('touchend', maskUp);
      document.addEventListener('mouseup', maskUp);
      window.addEventListener('resize', sizeMask);
    }
    $('lb-edit').onclick = function () { var it = lbItem(); if (it) { closeLb(); setInit(it.url, it.name); } };
    $('lb-guide').onclick = function () { var it = lbItem(); if (it) { closeLb(); setGuide(it.url, it.name); } };
    $('lb-up').onclick = function () { var it = lbItem(); if (it) upscale(it.url, $('lb-up')); };
    $('auth-on').onclick = newKey;
    $('auth-new').onclick = newKey;
    $('auth-off').onclick = askAuthOff;
    $('auth-out').onclick = signOut;
    $('auth-show').onclick = function () { auth.shown = !auth.shown; drawAuth(); };
    // the count next to Generate: typing is free, the value is clamped when the box is left
    var c = parseInt(load('count'), 10);
    $('count').value = c >= 1 ? c : 1;
    $('count').oninput = function () { updateGo(); };
    $('count').onchange = function () { drawCount(); save('count', countValue()); };
    $('count-').onclick = function () { stepCount(-1); };
    $('count+').onclick = function () { stepCount(1); };
    $('waitcancel').onclick = cancelAll;
    var v = load('prompt'); if (v !== null) $('prompt').value = v;
    v = load('neg'); if (v !== null) $('neg').value = v;
    applyModel('', null);
    st.model = null;
    st.mkey = null;
    var fields = ['prompt', 'neg', 'w', 'h', 'steps', 'cfg', 'seed', 'sampler', 'scheduler'];
    for (var i = 0; i < fields.length; i++) {
      $(fields[i]).onchange = $(fields[i]).oninput = persist;
    }
    $('w').onchange = $('h').onchange = $('w').oninput = $('h').oninput = function () { markPreset(); persist(); };
    $('steps-').onclick = function () { stepper('steps', -1, 1, 150, 0); };
    $('steps+').onclick = function () { stepper('steps', 1, 1, 150, 0); };
    $('frames-').onclick = function () { stepper('frames', -4, 5, 81, 0); snapFrames(); };
    $('frames+').onclick = function () { stepper('frames', 4, 5, 81, 0); snapFrames(); };
    $('frames').onchange = snapFrames;
    $('shift').onchange = persist;
    $('fps-').onclick = function () { stepper('fps', -1, 4, 30, 0); };
    $('fps+').onclick = function () { stepper('fps', 1, 4, 30, 0); };
    $('cfg-').onclick = function () { stepper('cfg', -0.5, 0, 30, 1); };
    $('cfg+').onclick = function () { stepper('cfg', 0.5, 0, 30, 1); };
    $('rnd').onclick = function () { $('seed').value = -1; persist(); };
    $('go').onclick = generate;
    $('cancel').onclick = cancel;
    $('surprise').onclick = function () {
      $('prompt').value = SURPRISE[Math.floor(Math.random() * SURPRISE.length)];
      persist();
    };
    $('resetpreset').onclick = function () {
      var seed = $('seed').value;
      setForm(st.preset);
      $('seed').value = seed;
      persist();
      toast('The model\'s defaults are set.');
    };
    // Save as preset opens the save box under the button, on this tab
    $('savepreset').onclick = function () {
      var open = $('ps-box').style.display === 'none';
      show('ps-box', open);
      if (open) { drawSaveBox(); try { $('ps-name').focus(); } catch (e) {} }
    };
    $('ps-save').onclick = savePreset;
    $('ps-name').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { savePreset(); return false; } return true; };
    $('pe-cancel').onclick = closeEdit;
    $('pe-save').onclick = saveEdit;
    $('tab-create').onclick = function () { setView('create'); };
    $('tab-models').onclick = function () { setView('models'); };
    $('tab-gallery').onclick = function () { setView('gallery'); };
    $('tab-settings').onclick = function () { setView('settings'); };
    $('tab-help').onclick = function () { setView('help'); };
    $('about-models').onclick = function () { showDoc('models'); setView('help'); };
    $('about-api').onclick = function () { showDoc('api'); setView('help'); };
    $('doc-models').onclick = function () { showDoc('models'); };
    $('doc-api').onclick = function () { showDoc('api'); };
    $('doc-page').onclick = function () { showDoc('page'); };
    $('doc').onclick = onDocClick;
    $('doc-notes').onclick = onDocClick;
    // The model name in the header and Change on the Create tab open the same model list.
    $('model').onclick = function () { if (st.app !== false) setView('models'); };
    $('m-refresh').onclick = enterModels;
    $('dl-go').onclick = addByUrl;
    $('lora-add').onchange = addLora;
    $('up-pick').onclick = function () { $('up-file').click(); };
    $('up-file').onchange = upStart;
    $('up-cancel').onclick = function () { if (upXhr) upXhr.abort(); };
    window.onbeforeunload = function () { return upXhr ? 'An upload is still running.' : undefined; };
    $('dl-url').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { addByUrl(); return false; } return true; };
    $('hf-go').onclick = hfSearch;
    $('hf-q').onkeydown = function (e) { e = e || window.event; if (e.keyCode === 13) { hfSearch(); return false; } return true; };
    $('more').onclick = function () { st.limit += PAGE; drawGallery(); };
    $('refresh').onclick = refreshGallery;
    $('gf-all').onclick = function () { setGalleryFilter('all'); };
    $('gf-images').onclick = function () { setGalleryFilter('images'); };
    $('gf-videos').onclick = function () { setGalleryFilter('videos'); };
    $('gorder').value = galleryOrder;
    $('gorder').onchange = function () { setGalleryOrder($('gorder').value); };
    $('gsearch').oninput = function () { setGallerySearch($('gsearch').value); };
    $('gsel').onclick = function () { setSelecting(!gsel); };
    $('gsel-all').onclick = selectAll;
    $('gsel-dl').onclick = downloadSelected;
    $('gsel-del').onclick = askDeleteSelected;
    $('lb-use').onclick = useSettings;
    $('prompt-copy').onclick = function () { copyText($('prompt').value, 'The prompt'); };
    $('neg-copy').onclick = function () { copyText($('neg').value, 'The negative prompt'); };
    show('prompt-paste', canPaste(), 'inline-block');
    show('neg-paste', canPaste(), 'inline-block');
    $('prompt-paste').onclick = function () { pasteInto('prompt'); };
    $('neg-paste').onclick = function () { pasteInto('neg'); };
    $('clipskip').onchange = persist;
    tagInit();
    $('nokeep').checked = load('nokeep') === '1';
    $('nokeep').onchange = function () { save('nokeep', $('nokeep').checked ? '1' : '0'); };
    $('lb-label').onclick = openLabels;
    $('lb-cp').onclick = function () { copyInfo('prompt'); };
    $('lb-cn').onclick = function () { copyInfo('negative'); };
    $('lb-ca').onclick = function () { copyInfo('all'); };
    $('ld-cancel').onclick = closeLabels;
    $('ld-save').onclick = saveLabels;
    zoomInit();
    $('dlg-cancel').onclick = closeDlg;
    $('dlg-ok').onclick = confirmDelete;
    $('lb-close').onclick = closeLb;
    $('lb-del').onclick = askDeleteImage;
    $('lb-prev').onclick = function () { lbStep(-1); };
    $('lb-next').onclick = function () { lbStep(1); };
    document.onkeydown = onKey;
    markPreset();
    summary();
    setPill();
    health();
    queueWatch();
    loadGallery(null);
    drawModels();
    dlPoll();
    // The tab open last time comes back; an address with #models, #help or #settings wins over it.
    var last = load('tab');
    if (last === 'gallery' || last === 'models' || last === 'settings' || last === 'help') setView(last);
    var hash = window.location.hash;
    if (hash === '#models' || hash === '#presets') setView('models');
    if (hash === '#help') setView('help');
    if (hash === '#settings') setView('settings');
  }

  init();
})();
