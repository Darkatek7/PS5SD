  // ---- the event stream
  // GET /ps5sd/v1/events (0.9.15): the queue and every step as they happen. With it the page asks for its job
  // when the stream says the job moved (a step, its end) and reads the queue from the stream; without it (an
  // older console, a browser without EventSource, a fifth open tab) it polls as before.
  var ev = { es: null, open: false, failedAt: 0, jobs: null };

  function eventsOn() {
    return ev.open;
  }

  function readEvent(e) {
    try { return JSON.parse(e.data); } catch (x) { return null; }
  }

  // Opens the stream; called with the queue's watch every 2 s, so a refused stream is tried again after 30 s.
  function startEvents() {
    if (ev.es || !window.EventSource || !st.useJobs) return;
    if (ev.failedAt && new Date().getTime() - ev.failedAt < 30000) return;
    var es;
    try { es = new EventSource('/ps5sd/v1/events'); } catch (x) { ev.failedAt = new Date().getTime(); return; }
    ev.es = es;
    es.onopen = function () { ev.open = true; };
    es.onerror = function () {
      ev.open = false;
      // A dropped stream (a model switch, the console gone) is opened again by the browser itself; a refused
      // one (429, 401, an older console without the route) is closed for good, and the page polls.
      if (es.readyState === 2) {
        ev.es = null;
        ev.jobs = null;
        ev.failedAt = new Date().getTime();
      }
    };
    es.addEventListener('jobs', function (e) {
      var d = readEvent(e);
      if (!d || !d.jobs) return;
      ev.jobs = d.jobs;
      if (st.view === 'create' || st.busy) takeJobs(d.jobs);
      if (st.job) pollSoon();
    });
    es.addEventListener('progress', function (e) {
      var d = readEvent(e);
      if (d && d.job && d.job === st.job) pollSoon();
    });
    es.addEventListener('job', function (e) {
      var d = readEvent(e);
      if (d && d.id === st.job) pollSoon();
    });
  }
