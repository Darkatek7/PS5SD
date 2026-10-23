  // ---------- runs and the queue ----------
  // A run is the count next to Generate: several images from one Generate. The server makes it as one
  // job ("repeat"; st.run.server): the job reports its count and the names of the images it has saved.
  // The page makes it itself, one job after the other (startPageRun), when it has to put each image back
  // into the picture (an Edit with a mask: the server does not blend it) or the server cannot repeat.
  // The page also follows the server's queue (refreshQueue): a job it did not start, after a reload,
  // from another device, or the next one in line.

  // ---- the count next to Generate (kept in this browser)
  function countValue() { return Math.round(num('count', 1, 1, st.maxRepeat)); }

  // The count as it is, clamped; its Generate label follows. A video has no count.
  function drawCount() {
    $('count').value = countValue();
    show('count-box', !st.video && st.useJobs, 'flex');
    updateGo();
  }

  function stepCount(d) {
    $('count').value = Math.min(st.maxRepeat, Math.max(1, countValue() + d));
    drawCount();
    save('count', countValue());
  }

  // The images one Generate makes in one go: one for a video, and without the job API.
  function runCount() { return st.video || !st.useJobs ? 1 : countValue(); }

  // The page makes the run itself: each image has to be put back here, or the server cannot repeat.
  function pageRunNeeded(p, n) { return n > 1 && (!st.serverRepeat || !!(p.req && p.req.mask)); }

  function galleryUrl(name) { return '/ps5sd/v1/gallery/' + encodeURIComponent(name); }

  function galleryIndex(name) {
    for (var i = 0; i < galleryList.length; i++) if (galleryList[i].name === name) return i;
    return -1;
  }

  // The names in the gallery now: a job's new images are the ones not in this list.
  function snapshotGallery() {
    if (!galleryLoaded) return null;
    var known = {};
    for (var i = 0; i < galleryList.length; i++) known[galleryList[i].name] = true;
    return known;
  }

  // "about 2 h 10 min left", "about 4 min left", "about 30 s left"
  function leftText(sec) {
    sec = Math.max(0, sec);
    if (sec < 60) return 'about ' + Math.max(1, Math.round(sec)) + ' s left';
    if (sec < 3600) return 'about ' + Math.max(1, Math.round(sec / 60)) + ' min left';
    var h = Math.floor(sec / 3600), m = Math.round((sec - h * 3600) / 60);
    if (m === 60) { h++; m = 0; }
    return 'about ' + h + ' h' + (m ? ' ' + m + ' min' : '') + ' left';
  }

  // The seconds a step, and the time left for the images still to make. imgs: the images made, a part of
  // one counting; steps: the steps made. Both are measured from the first step seen in this run (so the
  // model's warm-up is left out), and the time left is the average time of an image so far times the
  // images still to make (the part of the current one included).
  function pace(imgs, steps, total) {
    var now = new Date().getTime();
    var p = st.pace;
    if (!p || p.t0 !== st.t0 || steps < p.steps0 || imgs < p.imgs0) p = st.pace = { t0: st.t0, imgs0: imgs, steps0: steps, at: now };
    var secs = (now - p.at) / 1000;
    var out = [];
    if (steps > p.steps0 && secs > 0) out.push((secs / (steps - p.steps0)).toFixed(1) + ' s a step');
    var made = imgs - p.imgs0;
    if (made > 0.02 && imgs < total) out.push(leftText(secs / made * (total - imgs)));
    return out.join(' · ');
  }

  // "Image 37 of 500 · " for a run of more than one image, else ''
  function runLead() {
    var run = st.run;
    return run && run.total > 1 ? 'Image ' + Math.min(run.total, run.done + 1) + ' of ' + run.total + ' · ' : '';
  }

  // The size the final image of the settings has (a preview is drawn at it): null when the page does not know it.
  function finalSize(l) {
    if (!l) return null;
    if (l.req) return l.req.crop && l.req.source ? { w: l.req.source.width, h: l.req.source.height } : { w: l.req.w, h: l.req.h };
    if (l.hr && l.hr.on) return { w: Math.round(l.w * l.hr.scale / 8) * 8, h: Math.round(l.h * l.hr.scale / 8) * 8 };
    return { w: l.w, h: l.h };
  }

  // The prompt of a job the page follows, and the note of a page run; shown while one runs.
  function drawRunInfo() {
    text($('runprompt'), st.followPrompt || '');
    show('runprompt', st.busy && !!st.followPrompt, 'block');
    show('runnote', st.busy && !!st.run && !st.run.server, 'block');
  }

  // The progress of the job on screen, from its live preview (step, total_steps, pass; the picture while it
  // is made). The first line is the count and the step, the second the pace and the time left.
  function showRunProgress(pv) {
    var run = st.run;
    var total = run ? run.total : 1;
    var done = run ? run.done : 0;
    var passes = !st.video && !!(st.last && st.last.hr && st.last.hr.on && !st.last.req) ? 2 : 1;
    var pass = Math.min(passes, Math.max(1, pv.pass || 1)) - 1;
    // a server run's preview can still show the last step of the image before, for one poll
    if (run && run.server && run.seen !== undefined && run.seen !== run.done && pv.step >= pv.total_steps) {
      run.seen = run.done;
      status(runLead() + 'Starting the next image…');
      return;
    }
    if (run) run.seen = run.done;
    var lead = runLead();
    var what = pass >= 1 ? (lead ? 'hires pass · step ' : 'Hires pass · step ') : (lead ? 'step ' : 'Step ');
    var msg = lead + what + pv.step + ' of ' + pv.total_steps;
    status(run && run.cancelling ? 'Cancelling after this image · ' + msg : msg);
    var imgs = done + (pass + pv.step / pv.total_steps) / passes;
    var steps = (done * passes + pass) * pv.total_steps + pv.step;
    progress(imgs / total);
    st.sub = pace(imgs, steps, total);
    if (pv.b64_json) {
      st.stageItem = null;
      showImage(dataUrl(pv.b64_json), 'preview', finalSize(st.last) || st.shownDims);
    }
  }

  // The newest image of a run on the stage, once (when a preview is not on it).
  function showRunNewest() {
    if (!st.runMode || !st.results || !st.results.length) return;
    if (st.stageItem !== st.results[0]) showRunItem(0);
  }

  // The images of the run in the strip, newest first (the last 20); a click opens one in the viewer.
  function drawRunStrip() {
    var box = $('strip');
    clear(box);
    var items = st.results || [];
    stripBox(items.length > 0);
    var n = Math.min(items.length, 20);
    for (var i = 0; i < n; i++) {
      (function (idx) {
        var r = items[idx];
        var b = document.createElement('button');
        b.type = 'button';
        b.id = 'pick' + idx;
        b.title = r.name || 'Image ' + (r.no || idx + 1);
        b.className = idx === st.pick ? 'on' : '';
        var img = document.createElement('img');
        img.src = r.url || r.src;
        img.alt = 'image ' + (r.no || idx + 1);
        b.appendChild(img);
        b.onclick = function () { openRunItem(idx, b); };
        box.appendChild(b);
      })(i);
    }
  }

  // An image of the run on the stage, with its actions. The line under the bar is left alone while it runs.
  function showRunItem(i) {
    var r = st.results && st.results[i];
    if (!r) return;
    st.pick = i;
    st.stageItem = r;
    showImage(r.url || r.src, 'generated image', null);
    drawRunStrip();
    showActions(r.url || r.src, r.name, r.url, true);
    if (!st.busy) {
      var bits = [];
      var l = st.last;
      if (r.no) bits.push('image ' + r.no + (r.of > 1 ? ' of ' + r.of : ''));
      if (l) {
        var fs = finalSize(l);
        bits.push(fs.w + '×' + fs.h, l.steps + (l.steps === 1 ? ' step' : ' steps'), 'CFG ' + l.cfg);
      }
      if (r.seed !== null && r.seed !== undefined && r.seed !== '') bits.push('seed ' + r.seed);
      text($('sub'), bits.join(' · '));
    }
  }

  // A strip click: the viewer (the gallery's), the image on the stage when it is not in the gallery yet.
  function openRunItem(i, from) {
    var r = st.results && st.results[i];
    if (!r) return;
    if (!r.name) { showRunItem(i); return; }
    var at = galleryIndex(r.name);
    if (at >= 0) { openLbItem(galleryList[at], from); return; }
    loadGallery(function () {
      var j = galleryIndex(r.name);
      if (j >= 0) openLbItem(galleryList[j], from); else showRunItem(i);
    });
  }

  // The run on the server, as its job reports it (repeat: total, done, saved). The strip shows the images
  // saved so far; a new one takes the stage when no preview is on it.
  function syncRun(rp) {
    var run = st.run;
    if (!run || !run.server) {
      run = st.run = { total: rp.total, done: 0, server: true, items: [], last: '', cancelling: false, userCancel: false };
      st.results = run.items;
      st.runMode = true;
      st.pick = 0;
    }
    run.total = rp.total;
    run.done = rp.done || 0;
    var names = rp.saved || [];
    var newest = names.length ? names[names.length - 1] : '';
    if (newest === run.last && names.length === run.items.length) return;
    run.last = newest;
    run.items.length = 0;
    for (var i = names.length - 1; i >= 0; i--) {
      run.items.push({
        name: names[i], url: galleryUrl(names[i]), src: galleryUrl(names[i]), seed: null,
        no: run.done - (names.length - 1 - i), of: run.total, temp: /^tmp_/.test(names[i])
      });
    }
    drawRunStrip();
    showRunNewest();
  }

  // A run on the server is over (completed, cancelled or failed). Its images are the ones saved; one done
  // signal for the whole run, none when the user stopped it.
  function finishServerRun(data, how) {
    var run = st.run;
    var seconds = (new Date().getTime() - st.t0) / 1000;
    if (how === 'completed') run.done = run.total;
    var made = run.done;
    end();
    var msg;
    if (how === 'completed') {
      msg = made + (made > 1 ? ' images' : ' image') + ' done in ' + clock(seconds);
      progress(1);
      status(msg);
      signal(true, msg + '.');
    } else if (how === 'cancelled') {
      progress(run.total ? made / run.total : 0);
      status(made ? 'Cancelled after ' + made + ' of ' + run.total + '. The images so far are in the gallery.' : 'Cancelled.');
      if (!run.userCancel) signal(false, 'The run was cancelled.');
    } else {
      msg = data.error && data.error.message ? data.error.message : 'Generation failed.';
      status(made ? msg + ' ' + made + (made > 1 ? ' images are' : ' image is') + ' in the gallery.' : msg, true);
      signal(false, msg);
    }
    if (st.results && st.results.length) showRunItem(0);
    else text($('sub'), '');
    loadGallery(null);
  }

  // ---- a page run: the images one job after the other
  function startPageRun(p, n) {
    st.run = { total: n, done: 0, server: false, items: [], named: !!p.named, seed0: p.seed, first: p, cancelling: false, userCancel: false };
    st.results = st.run.items;
    st.runMode = true;
    st.pick = 0;
    drawRunStrip();
    pageRunStep();
  }

  // The next image of a page run: the request as it stands, one image; a named seed counts up.
  function pageRunStep() {
    var run = st.run;
    var q = {};
    for (var k in run.first) q[k] = run.first[k];
    q.repeat = 1;
    q.seed = run.done === 0 ? run.seed0 : run.named ? run.seed0 + run.done : Math.floor(Math.random() * 2147483647);
    st.seed = q.seed;
    st.last = q;
    st.known = snapshotGallery();
    startAsync(q);
  }

  // One image of a page run is in: on the stage and in the strip, its gallery name read (or saved, for an
  // image put back into the picture), and the next image made. The last one ends the run.
  function pageRunImage(results, edited) {
    var run = st.run;
    var r = results[0];
    var known = st.known;
    run.done++;
    r.no = run.done;
    r.of = run.total;
    run.items.unshift(r);
    // not kept: its tmp_ file (an image put back into the picture is only in the page until Keep)
    if (st.last && st.last.temp) {
      r.temp = true;
      if (!edited) matchTemporary([r], 'image');
    }
    showRunItem(0);
    if (!r.temp) loadGallery(function (list) {
      if (!known || !list) return;
      var fresh = [];
      for (var i = 0; i < list.length; i++) if (!known[list[i].name]) fresh.push(list[i]);
      if (edited) { saveEdits([r], fresh); return; }
      if (fresh.length === 1) {
        r.name = fresh[0].name;
        r.url = fresh[0].url;
        if (st.runMode && st.results === run.items) drawRunStrip();
      }
    });
    if (run.done < run.total && st.run === run) { pageRunStep(); return; }
    finishPageRun(run);
  }

  function finishPageRun(run) {
    var seconds = (new Date().getTime() - st.t0) / 1000;
    end();
    progress(1);
    var msg = run.done > 1 ? run.done + ' images done in ' + clock(seconds) : 'Done in ' + clock(seconds);
    status(msg);
    showRunItem(0);
    signal(true, msg + '.');
  }

  // ---- the queue
  // A Generate while something runs: the same settings, one job (its count as "repeat"), queued on the server.
  function addToQueue(p, n) {
    if (!st.useJobs) return;
    if (pageRunNeeded(p, n)) {
      toast('An edit with a mask puts each image back into this page, so it cannot wait in the queue. Run it when nothing else runs.', 7000);
      return;
    }
    p.repeat = n;
    postJob(p, function (code, data) {
      if (code === 202 && data && data.id) {
        st.queued[data.id] = p;
        toast(n > 1 ? 'Added to the queue: ' + n + ' images.' : 'Added to the queue.');
        refreshQueue();
        return;
      }
      toast(code === 0 ? 'Cannot reach the server.' :
        'The server refused the request' + (data && data.error ? ': ' + (data.message || data.error) : ' (' + code + ').'), 6000);
    });
  }

  // GET /ps5sd/v1/jobs: the queued and running jobs, oldest first. An idle page takes up the oldest one it
  // does not know (not one it stopped waiting for); the others count as "+ k waiting".
  function refreshQueue() {
    if (st.queueBusy || !st.useJobs) return;
    st.queueBusy = true;
    req('GET', '/ps5sd/v1/jobs', null, function (code, data) {
      st.queueBusy = false;
      if (code !== 200 || !data || !data.jobs) return;
      var list = data.jobs, pick = null, i, j;
      for (i = 0; i < list.length; i++) {
        j = list[i];
        if (j && j.id !== st.job && !st.gone[j.id] && !pick) pick = j;
      }
      if (!st.busy && pick) follow(pick.id, { t0: new Date().getTime(), seed: null, prompt: pick.prompt, repeat: pick.repeat });
      var waiting = 0;
      for (i = 0; i < list.length; i++) {
        j = list[i];
        if (j && j.id !== st.job && j.status === 'queued') waiting++;
      }
      st.waiting = waiting;
      drawWaiting();
    });
  }

  function drawWaiting() {
    show('waitrow', st.useJobs && st.waiting > 0, 'flex');
    text($('waitnum'), '+ ' + st.waiting + ' waiting');
  }

  // Cancel all: the queued jobs go, and the one that runs is stopped after its step (pollJob ends it).
  function cancelAll() {
    if (!window.confirm('Cancel every queued image, and stop the one that is making now?')) return;
    req('POST', '/ps5sd/v1/jobs/cancel', {}, function (code, data) {
      if (code !== 200) { toast(errText(code, data, 'Could not cancel the queue'), 6000); return; }
      var n = data && data.cancelled_queued ? data.cancelled_queued : 0;
      toast(n + (n === 1 ? ' queued image' : ' queued images') + ' cancelled.');
      refreshQueue();
    });
  }

  // The queue is read every 2 s, while the Create tab is open or a run goes on.
  function queueWatch() {
    if (st.view === 'create' || st.busy) refreshQueue();
    setTimeout(queueWatch, 2000);
  }

  // The page takes up a job it did not start (a reload, another device, the next one in line): its progress
  // and images show as for its own run, and Cancel stops it. The request of a job this page queued is known.
  function follow(id, info) {
    if (st.busy) return;
    var own = st.queued[id] || null;
    delete st.queued[id];
    st.t0 = info.t0 || new Date().getTime();
    st.seed = info.seed !== null && info.seed !== undefined ? info.seed : (own ? own.seed : '');
    st.last = own;
    st.known = snapshotGallery();
    st.followPrompt = info.prompt || (own ? own.prompt : '') || '';
    st.run = null;
    st.results = null;
    st.runMode = false;
    st.stageItem = null;
    st.pick = 0;
    drawStrip();
    save('job', id + ' ' + st.t0 + ' ' + st.seed);
    begin(id);
    status('Back on a running job…');
    pollJob();
  }
