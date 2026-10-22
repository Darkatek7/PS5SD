  // ---------- video models (Wan: capabilities vid_gen) ----------
  // Frames are 4n+1, 5..33 for now (33 is the most measured on the console).
  function frames4k1(n) {
    return Math.max(5, Math.min(33, 4 * Math.round((n - 1) / 4) + 1));
  }

  function snapFrames() {
    $('frames').value = frames4k1(num('frames', 33, 5, 33));
  }

  // A video model: no hires fix, no batch, no start image; frames and fps show instead.
  function applyVideoMode(caps) {
    var vid = caps && caps.defaults_by_mode && caps.defaults_by_mode.vid_gen;
    if (st.video && st.init) dropInit();
    show('hr-sect', !st.video);
    show('batch-col', !st.video);
    show('vid-sect', st.video);
    show('init-bar', !st.video && !st.init, 'flex');
    drawGuide();
    if (st.video && vid) {
      if (typeof vid.video_frames === 'number') $('frames').value = frames4k1(vid.video_frames);
      if (typeof vid.fps === 'number') $('fps').value = Math.max(4, Math.min(30, Math.round(vid.fps)));
    }
    var empty = $('stage').getElementsByClassName('empty')[0];
    if (empty) text(empty, st.video ? 'Your video appears here.' : 'Your image appears here.');
  }

