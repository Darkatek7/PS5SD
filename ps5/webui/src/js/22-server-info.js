  // ---------- server info ----------
  function loadInfo() {
    if (st.infoBusy) return;
    st.infoBusy = true;
    req('GET', '/sdapi/v1/samplers', null, function (c1, samplers) {
      req('GET', '/sdapi/v1/schedulers', null, function (c2, schedulers) {
        req('GET', '/sdcpp/v1/capabilities', null, function (c3, caps) {
          st.useJobs = false;
          st.video = false;
          st.serverRepeat = false;
          if (c3 === 200 && caps) {
            var modes = caps.supported_modes || [];
            var hasImg = false, hasVid = false;
            for (var i = 0; i < modes.length; i++) {
              if (modes[i] === 'img_gen') hasImg = true;
              if (modes[i] === 'vid_gen') hasVid = true;
            }
            st.useJobs = hasImg || hasVid;
            st.video = hasVid && !hasImg;
            st.canCancelGen = !!(caps.features && caps.features.cancel_generating);
            setUpscalers(caps.upscalers || []);
            // "repeat" on img_gen (the count next to Generate); without it the page makes the run itself
            var mr = caps.limits && caps.limits.max_repeat;
            st.serverRepeat = typeof mr === 'number' && mr >= 1;
            if (st.serverRepeat) st.maxRepeat = Math.floor(mr);
            drawCount();
            summary();
            applyVideoMode(caps);
          } else {
            applyVideoMode(null);
          }
          fill($('sampler'), names(samplers));
          fill($('scheduler'), names(schedulers));
          loadLoras(null);
          loadModels(function () {
            modelName(function (name, reachable) {
              st.infoBusy = false;
              if (!reachable) { setOnline(false); return; }
              var entry = st.mdl && st.mdl.loaded ? findModel(st.mdl.loaded) : null;
              // the same names as the Models tab (titleOf, readable: 13-models.js)
              st.label = entry ? titleOf(entry) : (name ? readable(name) : 'no model loaded');
              applyModel(name, entry);
              drawCurrent();
              resume();
            });
          });
        });
      });
    });
  }

  // The model in use: A1111's sd_model_checkpoint (the PS5SD app answers with its library id),
  // else the first entry of sd-models or /v1/models (a plain sd-server lists only its own).
  function modelName(cb) {
    req('GET', '/sdapi/v1/options', null, function (c0, opts) {
      if (c0 === 200 && opts && opts.sd_model_checkpoint) { cb(String(opts.sd_model_checkpoint), true); return; }
      modelNameFromLists(cb);
    });
  }

  function modelNameFromLists(cb) {
    req('GET', '/sdapi/v1/sd-models', null, function (code, data) {
      var name = '';
      if (code === 200 && data && data.length) name = data[0].model_name || data[0].title || data[0].filename || '';
      if (name || code === 0) { cb(name, code !== 0); return; }
      req('GET', '/v1/models', null, function (c, d) {
        var n = '';
        if (c === 200 && d && d.data && d.data.length) n = d.data[0].id;
        cb(n, c !== 0);
      });
    });
  }

  // Cheap check every few seconds: is the server there, and is someone else using it?
  function health() {
    if (st.busy) { setTimeout(health, 4000); return; }
    req('GET', '/sdapi/v1/progress', null, function (code, data) {
      if (!st.busy) {
        if (code === 0) {
          st.offFails++;
          if (st.offFails >= 2) setOnline(false);
        } else {
          var s = data && data.state;
          st.others = !!(s && (s.job_count > 0 || s.sampling_steps > 0));
          setOnline(true);
          setPill();
        }
      }
      setTimeout(health, st.online ? 4000 : 3000);
    }, 6000);
  }

