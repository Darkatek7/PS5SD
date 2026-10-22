  // ---------- will it fit: the hint under Generate ----------
  // POST /ps5sd/v1/app/fit with the size of the run; the server knows the model and what has run since it
  // loaded. A warning when the run may run out of memory, nothing when it fits. It never stops a run.
  var fitTimer = null;

  function fitBody() {
    var f = getForm();
    var body = { width: f.w, height: f.h };
    if (st.video) body.video_frames = frames4k1(num('frames', 33, 5, 81));
    else if (f.hr.on && st.mode !== 'edit') body.hires = { enabled: true, scale: f.hr.scale };
    return body;
  }

  function drawFit(data) {
    var warn = !!(data && data.verdict && data.verdict !== 'fits' && data.message);
    text($('fit-hint'), warn ? data.message : '');
    show('fit-hint', warn);
  }

  function checkFit() {
    fitTimer = null;
    var d = st.mdl;
    if (!d || !d.loaded || d.standby || st.sw) { drawFit(null); return; }
    req('POST', '/ps5sd/v1/app/fit', fitBody(), function (code, data) { drawFit(code === 200 ? data : null); });
  }

  // after a change of the settings, the model, or a run that ended (what ran counts)
  function scheduleFit() {
    if (fitTimer) clearTimeout(fitTimer);
    fitTimer = setTimeout(checkFit, 400);
  }

  $('view-create').addEventListener('input', scheduleFit);
  $('view-create').addEventListener('change', scheduleFit);
