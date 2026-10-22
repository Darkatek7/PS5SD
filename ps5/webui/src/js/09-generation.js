  // ---------- generation ----------
  function readParams() {
    var f = getForm();
    if (isNaN(f.seed) || f.seed < 0) f.seed = Math.floor(Math.random() * 2147483647);
    f.prompt = $('prompt').value;
    f.negative = $('neg').value;
    f.batch = batchCount();
    f.req = null;
    if (st.video) { f.batch = 1; f.req = null; }
    if (st.init && !st.video) {
      f.strength = num('init-str', 0.6, 0.05, 1);
      f.req = initRequest(f);
    }
    return f;
  }

  // Images per run: for every model, kept apart from the per-model settings.
  function batchCount() {
    var n = Math.round(num('batch', 1, 1, st.maxBatch));
    $('batch').value = n;
    return n;
  }

  function generate() {
    if (st.busy) return;
    if (st.sw) { toast('The model is switching. Try again in a moment.'); return; }
    if (st.mdl && st.mdl.standby) { toast('No model is loaded. Pick or download one first.'); setView('models'); return; }
    var p = readParams();
    if (!p.prompt.replace(/^\s+|\s+$/g, '')) {
      status('Type a prompt first.', true);
      $('prompt').focus();
      return;
    }
    persist();
    wakeAudio();
    document.title = st.title;
    st.seed = p.seed;
    st.last = p;
    st.results = null;
    st.pick = 0;
    drawStrip();
    st.t0 = new Date().getTime();
    st.known = null;
    if (galleryLoaded) {
      st.known = {};
      for (var i = 0; i < galleryList.length; i++) st.known[galleryList[i].name] = true;
    }
    status('Sending…');
    showActions(null);
    if (window.innerWidth < 900) {
      try { $('stage').scrollIntoView(); } catch (e) {}
    }
    if (st.useJobs) startAsync(p); else startSync(p);
  }

  // A video: the same job API, WebM out. No fallback to the synchronous route.
  function startVideo(p) {
    p.frames = frames4k1(num('frames', 33, 5, 33));
    p.fps = Math.round(num('fps', 16, 4, 30));
    var body = {
      prompt: p.prompt,
      negative_prompt: p.negative,
      width: p.w,
      height: p.h,
      seed: p.seed,
      video_frames: p.frames,
      fps: p.fps,
      sample_params: { sample_steps: p.steps, guidance: { txt_cfg: p.cfg } },
      preview: 'proj',
      preview_interval: 1,
      output_format: 'webm'
    };
    if (p.sampler !== 'default') body.sample_params.sample_method = p.sampler;
    if (p.scheduler !== 'default') body.sample_params.scheduler = p.scheduler;
    if (loraBody(p).length) body.lora = loraBody(p);
    begin(null);
    var token = st.token;
    req('POST', '/sdcpp/v1/vid_gen', body, function (code, data) {
      if (token !== st.token) return;
      if (code === 202 && data && data.id) {
        st.job = data.id;
        save('job', data.id + ' ' + st.t0 + ' ' + st.seed);
        status('Queued…');
        pollJob();
      } else {
        setOnline(code !== 0);
        fail(code === 0 ? 'Cannot reach the server.' :
          'The server refused the request' + (data && data.error ? ': ' + (data.message || data.error) : ' (' + code + ').'));
      }
    });
  }

  // Asynchronous: the server queues a job; we poll it once a second.
  function startAsync(p) {
    if (st.video) { startVideo(p); return; }
    var body = {
      prompt: p.prompt,
      negative_prompt: p.negative,
      width: p.w,
      height: p.h,
      seed: p.seed,
      batch_count: p.batch,
      sample_params: { sample_steps: p.steps, guidance: { txt_cfg: p.cfg } },
      preview: 'proj',
      preview_interval: 1,
      output_format: 'png'
    };
    if (p.sampler !== 'default') body.sample_params.sample_method = p.sampler;
    if (p.scheduler !== 'default') body.sample_params.scheduler = p.scheduler;
    if (loraBody(p).length) body.lora = loraBody(p);
    if (p.req) {
      body.init_image = p.req.init;
      if (p.req.mask) body.mask_image = p.req.mask;
      body.strength = p.strength;
      body.width = p.req.w;
      body.height = p.req.h;
    } else if (p.hr.on) {
      body.hires = { enabled: true, upscaler: p.hr.up, scale: p.hr.scale, steps: p.hr.steps, denoising_strength: p.hr.den };
    }
    // the guide image (ControlNet), at the final size of the request
    if (st.cn && st.cnModel && !st.video) {
      body.control_image = guideData(body.width, body.height);
      body.control_strength = num('cn-str', 0.6, 0.1, 1.5);
      if ($('cn-canny').checked) body.image_preprocess = 'target=control,canny=true';
    }
    begin(null);
    var token = st.token;
    req('POST', '/sdcpp/v1/img_gen', body, function (code, data) {
      if (token !== st.token) return;
      if (code === 202 && data && data.id) {
        st.job = data.id;
        save('job', data.id + ' ' + st.t0 + ' ' + st.seed);
        status('Queued…');
        pollJob();
      } else if (code === 404 || code === 405 || code === 501) {
        // an older server without the job API
        st.useJobs = false;
        end();
        startSync(p);
      } else {
        setOnline(code !== 0);
        fail(code === 0 ? 'Cannot reach the server.' :
          'The server refused the request' + (data && data.error ? ': ' + (data.message || data.error) : ' (' + code + ').'));
      }
    });
  }

  function pollJob() {
    var token = st.token;
    var job = st.job;
    if (!job) return;
    req('GET', '/sdcpp/v1/jobs/' + job, null, function (code, data) {
      if (token !== st.token) return;
      if (code === 0) {
        st.fails++;
        st.online = false;
        setPill();
        if (st.fails > 300) { fail('Lost the connection to the server.'); return; }
        status('Connection lost, retrying…');
        setTimeout(pollJob, 1000);
        return;
      }
      st.fails = 0;
      if (!st.online) { st.online = true; setPill(); }
      if (code === 404 || code === 410) { fail('The server no longer has this job.'); return; }
      if (!data) { setTimeout(pollJob, 1000); return; }

      if (data.status === 'completed') {
        if (data.result && data.result.b64_json && /^video\//.test(data.result.mime_type || '')) {
          succeedVideo(data.result.b64_json, data.result.mime_type, st.seed);
          return;
        }
        if (data.kind === 'vid_gen') { fail('The job finished without a video.'); return; }
        var images = data.result && data.result.images;
        var list = [];
        for (var i = 0; images && i < images.length; i++) if (images[i] && images[i].b64_json) list.push(images[i].b64_json);
        if (list.length) succeed(list, st.seed);
        else fail('The job finished without an image.');
        return;
      }
      if (data.status === 'failed' || data.status === 'cancelled') {
        fail(data.error && data.error.message ? data.error.message : (data.status === 'cancelled' ? 'Cancelled.' : 'Generation failed.'));
        return;
      }
      if (data.status === 'queued') {
        status(data.queue_position > 1 ? 'Queued, ' + (data.queue_position - 1) + ' ahead of you…' : 'Queued…');
        st.sub = '';
      } else {
        text($('cancel'), st.canCancelGen ? 'Cancel' : 'Stop waiting');
        var pv = data.preview;
        if (st.video && pv && pv.total_steps > 0 && pv.step >= pv.total_steps) {
          // the last step is done; the server decodes the video now (about 2 minutes)
          status('Decoding the video…');
          progress(0);
          st.sub = '';
        } else if (pv && pv.total_steps > 0) {
          // pass counts the sampling passes: the images of a batch, then (hires fix) the second
          // pass of each, in the same order
          var n = st.video ? 1 : ((st.last && st.last.batch) || 1);
          var hr = !st.video && !!(st.last && st.last.hr && st.last.hr.on && !st.last.req);
          var passes = hr ? 2 * n : n;
          var pass = Math.min(passes, Math.max(1, pv.pass || 1)) - 1;
          var second = pass >= n;
          var at = second ? pass - n : pass;
          var frac = (pass + pv.step / pv.total_steps) / passes;
          status((n > 1 ? 'Image ' + (at + 1) + ' of ' + n + ' · ' : '') + (second ? 'hires step ' : (n > 1 ? 'step ' : 'Step ')) +
            pv.step + ' of ' + pv.total_steps);
          progress(frac);
          st.sub = eta(frac);
          if (pv.b64_json) showImage(dataUrl(pv.b64_json), 'preview');
        } else {
          status('Generating…');
          st.sub = '';
        }
      }
      setTimeout(pollJob, 1000);
    });
  }

  // frac: how much of the run is done, 0..1
  function eta(frac) {
    if (frac <= 0 || frac >= 1) return '';
    var elapsed = (new Date().getTime() - st.t0) / 1000;
    // the clock started before the first step (model warm-up), so this reads long early on
    return 'about ' + clock(elapsed / frac * (1 - frac)) + ' left';
  }

  // Synchronous (older servers): one long POST, progress from /sdapi/v1/progress.
  function startSync(p) {
    var task = 'ps5sd-' + new Date().getTime();
    var body = {
      id_task: task,
      prompt: p.prompt,
      negative_prompt: p.negative,
      width: p.w,
      height: p.h,
      steps: p.steps,
      cfg_scale: p.cfg,
      seed: p.seed,
      batch_size: p.batch
    };
    if (p.sampler !== 'default') body.sampler_name = p.sampler;
    if (p.scheduler !== 'default') body.scheduler = p.scheduler;
    if (loraBody(p).length) body.lora = loraBody(p);
    var route = '/sdapi/v1/txt2img';
    if (p.req) {
      route = '/sdapi/v1/img2img';
      body.init_images = [p.req.init];
      if (p.req.mask) body.mask = p.req.mask;
      body.denoising_strength = p.strength;
      body.width = p.req.w;
      body.height = p.req.h;
    } else if (p.hr.on) {
      body.enable_hr = true;
      body.hr_upscaler = p.hr.up;
      body.hr_scale = p.hr.scale;
      body.hr_steps = p.hr.steps;
      body.denoising_strength = p.hr.den;
    }
    begin(null);
    var token = st.token;
    status('Generating…');
    req('POST', route, body, function (code, data) {
      if (token !== st.token) return;
      if (code === 200 && data && data.images && data.images.length) {
        succeed(data.images, st.seed);
      } else {
        setOnline(code !== 0);
        fail(code === 0 ? 'Lost the connection to the server.' :
          'Generation failed' + (data && (data.message || data.error) ? ': ' + (data.message || data.error) : ' (' + code + ').'));
      }
    });
    setTimeout(function () { pollProgress(token, task); }, 1000);
  }

  function pollProgress(token, task) {
    if (token !== st.token) return;
    req('GET', '/sdapi/v1/progress?id_task=' + encodeURIComponent(task), null, function (code, data) {
      if (token !== st.token) return;
      var state = data && data.state;
      if (state && state.sampling_steps > 0) {
        var n = state.job_count > 1 ? state.job_count : 1;
        var at = Math.min(n - 1, Math.max(0, state.job_no || 0));
        status((n > 1 ? 'Image ' + (at + 1) + ' of ' + n + ' · step ' : 'Step ') + state.sampling_step + ' of ' + state.sampling_steps);
        progress(data.progress || 0);
        st.sub = eta(data.progress || 0);
        if (data.current_image) showImage(dataUrl(data.current_image), 'preview');
      }
      setTimeout(function () { pollProgress(token, task); }, 1000);
    });
  }

  function cancel() {
    if (!st.busy) return;
    var job = st.job;
    var queued = st.useJobs && job;
    end();
    text($('sub'), '');
    if (queued) {
      // A queued job is cancelled; one that is generating cannot always be interrupted,
      // so the page then stops waiting and the image still lands in the gallery.
      status('Cancelling…');
      req('POST', '/sdcpp/v1/jobs/' + job + '/cancel', {}, function (code) {
        if (code === 200) status('Cancelled.');
        else status('Stopped waiting. The server finishes this image; it will appear in the gallery.');
        setTimeout(refreshGallery, 2000);
      });
    } else {
      status('Stopped waiting. The server finishes this image.');
    }
  }

  // Back on the page while a job is still running on the server.
  function resume() {
    if (st.resumed) return;
    st.resumed = true;
    var saved = load('job');
    if (!saved) return;
    var parts = saved.split(' ');
    if (parts.length < 3) { drop('job'); return; }
    req('GET', '/sdcpp/v1/jobs/' + parts[0], null, function (code, data) {
      if (code !== 200 || !data) { drop('job'); return; }
      if (st.busy) return;
      st.t0 = parseInt(parts[1], 10) || new Date().getTime();
      st.seed = parts[2];
      if (data.status === 'completed' || data.status === 'queued' || data.status === 'generating') {
        begin(parts[0]);
        status('Back on a running job…');
        pollJob();
      } else {
        drop('job');
      }
    });
  }

