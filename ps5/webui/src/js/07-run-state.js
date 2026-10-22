  // ---------- run state ----------
  // Generate: "Generate" or "Generate 3" (the count); while something runs it adds to the queue.
  function updateGo() {
    var b = $('go');
    var n = runCount();
    var queue = st.busy && st.useJobs;
    b.disabled = !!st.sw || (st.busy && !queue);
    text(b, st.sw ? 'Switching model…' : st.busy ? (n > 1 ? 'Add ' + n + ' to queue' : 'Add to queue')
      : (n > 1 ? 'Generate ' + n : 'Generate'));
  }

  function setBusy(busy) {
    st.busy = busy;
    updateGo();
    show('cancel', busy, 'block');
    text($('cancel'), 'Cancel');
    setPill();
    if (!busy) scheduleFit();
  }

  // one clock for the page: begin() is called again for every image of a run
  function tick() {
    if (!st.busy) { st.ticking = false; return; }
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
    if (!st.ticking) { st.ticking = true; tick(); }
    drawRunInfo();
  }

  // The run (if any) is over with this job: the page keeps no run after it.
  function end() {
    st.token++;
    st.job = null;
    st.run = null;
    st.followPrompt = '';
    drop('job');
    setBusy(false);
    progress(0);
    drawRunInfo();
  }

  function fail(message) {
    status(message, true);
    end();
    text($('sub'), '');
    signal(false, message);
  }

