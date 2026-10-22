  // ---------- run state ----------
  function updateGo() {
    var b = $('go');
    b.disabled = st.busy || !!st.sw;
    text(b, st.busy ? 'Generating…' : (st.sw ? 'Switching model…' : 'Generate'));
  }

  function setBusy(busy) {
    st.busy = busy;
    updateGo();
    show('cancel', busy, 'block');
    text($('cancel'), 'Cancel');
    setPill();
  }

  function tick() {
    if (!st.busy) return;
    var elapsed = (new Date().getTime() - st.t0) / 1000;
    text($('sub'), (st.sub ? st.sub + ' · ' : '') + clock(elapsed) + ' elapsed');
    setTimeout(tick, 1000);
  }

  function begin(job) {
    st.token++;
    st.job = job || null;
    st.fails = 0;
    st.sub = '';
    st.others = false;
    setBusy(true);
    progress(0);
    tick();
  }

  function end() {
    st.token++;
    st.job = null;
    drop('job');
    setBusy(false);
    progress(0);
  }

  function fail(message) {
    status(message, true);
    end();
    text($('sub'), '');
    signal(false, message);
  }

