  // ---------- video models (Wan: capabilities vid_gen) ----------
  // Frames are 4n+1, 5..81 (81 is the most measured on the console, 5 s at 16 per second).
  function frames4k1(n) {
    return Math.max(5, Math.min(81, 4 * Math.round((n - 1) / 4) + 1));
  }

  function snapFrames() {
    $('frames').value = frames4k1(num('frames', 33, 5, 81));
    summary();
  }

  // A video model: no hires fix, no count; the Video group (frames, fps) shows instead, and the
  // mode switch shows only Video. A start image or guide stays where it is, unused.
  function applyVideoMode(caps) {
    var vid = caps && caps.defaults_by_mode && caps.defaults_by_mode.vid_gen;
    show('hr-sect', !st.video);
    drawCount();
    show('vid-sect', st.video);
    drawVideoStart();
    drawGuide();
    drawMode();
    if (st.video && vid) {
      if (typeof vid.video_frames === 'number') $('frames').value = frames4k1(vid.video_frames);
      if (typeof vid.fps === 'number') $('fps').value = Math.max(4, Math.min(30, Math.round(vid.fps)));
    }
    summary();
    var empty = $('stage').getElementsByClassName('empty')[0];
    if (empty) text(empty, st.video ? 'Your video appears here.' : 'Your image appears here.');
  }

