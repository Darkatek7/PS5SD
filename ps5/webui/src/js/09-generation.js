  // ---------- generation ----------
  // readParams() reads the form into a request, generate() sends it. The count next to Generate makes a
  // run (09b-runs-and-queue.js): the server repeats the request, or the page sends one job after the other.
  function readParams() {
    var f = getForm();
    f.named = !isNaN(f.seed) && f.seed >= 0;   // a named seed counts up over a run; else each image gets a new one
    if (!f.named) f.seed = Math.floor(Math.random() * 2147483647);
    f.prompt = $('prompt').value;
    f.negative = $('neg').value;
    f.batch = 1;   // one image per job; the count goes as "repeat"
    f.repeat = 1;
    f.req = null;
    // only the image of the active mode goes with the request (an Edit image waits while in Image mode)
    if (st.mode === 'edit' && st.init && !st.video) {
      f.strength = num('init-str', 0.6, 0.05, 1);
      f.req = initRequest(f);
    }
    f.guide = !st.video && st.mode === 'guide' && !!st.cn && !!st.cnModel;
    f.vinit = st.vinit && vidStartCapable() ? videoStartData(f.w, f.h) : null;
    f.temp = $('nokeep').checked;
    return f;
  }

  function generate() {
    if (st.sw) { toast('The model is switching. Try again in a moment.'); return; }
    if (st.mdl && st.mdl.standby) { toast('No model is loaded. Pick or download one first.'); setView('models'); return; }
    if (st.mode === 'edit' && !st.init) { status('Pick a start image first, or press Edit on an image of the gallery.', true); return; }
    if (st.mode === 'guide' && !st.cn) { status('Pick a guide image first, or press Guide on an image of the gallery.', true); return; }
    var p = readParams();
    if (!p.prompt.replace(/^\s+|\s+$/g, '')) {
      status('Type a prompt first.', true);
      $('prompt').focus();
      return;
    }
    var n = runCount();
    // while something runs, the same settings wait in the server's queue
    if (st.busy) { addToQueue(p, n); return; }
    persist();
    wakeAudio();
    document.title = st.title;
    st.seed = p.seed;
    st.last = p;
    st.results = null;
    st.runMode = false;
    st.run = null;
    st.stageItem = null;
    st.pick = 0;
    st.followPrompt = '';
    drawStrip();
    st.t0 = new Date().getTime();
    st.known = snapshotGallery();
    status('Sending…');
    showActions(null);
    // on a phone the result comes into view (the settings are above it)
    if (window.innerWidth <= 720) {
      try { $('view-create').getElementsByClassName('result')[0].scrollIntoView(); } catch (e) {}
    }
    if (pageRunNeeded(p, n)) { startPageRun(p, n); return; }
    p.repeat = n;
    if (st.useJobs) startAsync(p); else startSync(p);
  }

  // The request of a picture: the body of POST /sdcpp/v1/img_gen.
  function imageBody(p) {
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
    if (p.req && p.req.ref) {
      body.ref_images = [p.req.init];
      body.width = p.req.w;
      body.height = p.req.h;
    } else if (p.req) {
      body.init_image = p.req.init;
      if (p.req.mask) body.mask_image = p.req.mask;
      body.strength = p.strength;
      body.width = p.req.w;
      body.height = p.req.h;
    } else if (p.hr.on) {
      body.hires = { enabled: true, upscaler: p.hr.up, scale: p.hr.scale, steps: p.hr.steps, denoising_strength: p.hr.den };
    }
    // the guide image (ControlNet), at the final size of the request
    if (p.guide) {
      body.control_image = guideData(body.width, body.height);
      body.control_strength = num('cn-str', 0.6, 0.1, 1.5);
      if ($('cn-canny').checked) body.image_preprocess = 'target=control,canny=true';
    }
    if (p.repeat > 1) body.repeat = p.repeat;
    if (p.clip > 0) body.clip_skip = p.clip;
    if (p.temp) body.temporary = true;
    return body;
  }

  // The request of a video: the body of POST /sdcpp/v1/vid_gen (WebM out).
  function videoBody(p) {
    p.frames = frames4k1(num('frames', 33, 5, 81));
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
    if (p.shift > 0) body.sample_params.flow_shift = p.shift;
    if (p.vinit) body.init_image = p.vinit;
    if (loraBody(p).length) body.lora = loraBody(p);
    if (p.clip > 0) body.clip_skip = p.clip;
    if (p.temp) body.temporary = true;
    return body;
  }

  // Posts a job (an image or a video, by the model in use). cb(code, data) when the server answers.
  function postJob(p, cb) {
    if (st.video) req('POST', '/sdcpp/v1/vid_gen', videoBody(p), cb);
    else req('POST', '/sdcpp/v1/img_gen', imageBody(p), cb);
  }

  // Asynchronous: the server queues a job; the page follows it (pollJob) once a second.
  function startAsync(p) {
    begin(null);
    var token = st.token;
    postJob(p, function (code, data) {
      if (token !== st.token) return;
      if (code === 202 && data && data.id) {
        st.job = data.id;
        save('job', data.id + ' ' + st.t0 + ' ' + st.seed);
        status('Queued…');
        pollJob();
      } else if (!st.video && (code === 404 || code === 405 || code === 501)) {
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
      // a run of several images on the server: its count, and the images it has saved
      if (data.repeat && data.repeat.total > 1) syncRun(data.repeat);
      var served = !!(st.run && st.run.server);

      if (data.status === 'completed') {
        if (served) { finishServerRun(data, 'completed'); return; }
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
        if (served) { finishServerRun(data, data.status); return; }
        fail(data.error && data.error.message ? data.error.message : (data.status === 'cancelled' ? 'Cancelled.' : 'Generation failed.'));
        return;
      }
      if (data.status === 'queued') {
        status(runLead() + (data.queue_position > 1 ? 'Queued, ' + (data.queue_position - 1) + ' ahead of you…' : 'Queued…'));
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
          showRunProgress(pv);
        } else {
          status(runLead() + 'Generating…');
          st.sub = '';
          showRunNewest();
        }
      }
      setTimeout(pollJob, 1000);
    });
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
    if (p.clip > 0) body.clip_skip = p.clip;
    var route = '/sdapi/v1/txt2img';
    if (p.req && p.req.ref) {
      body.extra_images = [p.req.init];
      body.width = p.req.w;
      body.height = p.req.h;
    } else if (p.req) {
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
        if (data.current_image) showImage(dataUrl(data.current_image), 'preview', null);
      }
      setTimeout(function () { pollProgress(token, task); }, 1000);
    });
  }

  // Synchronous runs only: the time left for the progress of one long request.
  function eta(frac) {
    if (frac <= 0 || frac >= 1) return '';
    var elapsed = (new Date().getTime() - st.t0) / 1000;
    // the clock started before the first step (model warm-up), so this reads long early on
    return 'about ' + clock(elapsed / frac * (1 - frac)) + ' left';
  }

  // Cancel: a run on the server stops after the image it makes now (pollJob ends it, the saved images stay);
  // a queued job is removed; a job that is making its image cannot always be stopped, so the page then
  // stops waiting for it and the image still lands in the gallery.
  function cancel() {
    if (!st.busy) return;
    var job = st.job;
    if (st.run && st.run.server && job) {
      st.run.cancelling = true;
      st.run.userCancel = true;
      status('Cancelling…');
      req('POST', '/sdcpp/v1/jobs/' + job + '/cancel', {}, function (code) {
        if (code === 200 || code === 202) return;
        if (st.run) st.run.cancelling = false;
        status('Could not cancel it. The run goes on.', true);
      });
      return;
    }
    var queued = st.useJobs && job;
    if (job) st.gone[job] = true;
    end();
    text($('sub'), '');
    if (queued) {
      status('Cancelling…');
      req('POST', '/sdcpp/v1/jobs/' + job + '/cancel', {}, function (code) {
        if (code === 200) status('Cancelled.');
        else if (code === 202) status('Stopped. The server ends this job after its current step.');
        else status('Stopped waiting. The server finishes this image; it will appear in the gallery.');
        setTimeout(refreshGallery, 2000);
      });
    } else {
      status('Stopped waiting. The server finishes this image.');
    }
  }

  // Back on the page while a job is still running on the server (or waiting): the page follows it.
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
      if (data.status === 'completed' || data.status === 'queued' || data.status === 'generating') {
        follow(parts[0], { t0: parseInt(parts[1], 10) || new Date().getTime(), seed: parts[2], prompt: data.prompt || '', repeat: data.repeat });
      } else {
        drop('job');
      }
    });
  }
