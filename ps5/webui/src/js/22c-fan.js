  // ---------- the fan mode ----------
  // AppSettings' "fan" and "fan_target" (GET/POST /ps5sd/v1/app/settings); the console route's
  // "fan_control" says what the payload does with them (console_stats.hpp). Hidden on servers
  // without them.
  var fan = { mode: null, target: 85, timer: 0, hover: -1, limit: 91, stock: 91 };

  function drawFanButtons() {
    var bs = $('fan-mode').getElementsByTagName('button');
    for (var i = 0; i < bs.length; i++) {
      var on = bs[i].getAttribute('data-fan') === fan.mode;
      bs[i].className = on ? 'on' : '';
      bs[i].setAttribute('aria-pressed', on ? 'true' : 'false');
    }
    show('fan-manual', fan.mode === 'manual');
    $('fan-target').value = fan.target;
  }

  // Each mode is a highest temperature: the console's own fan control turns the fan as fast as it needs to
  // keep the chip there (it takes no fan speed, only that target: docs/CONSOLE.md, "The fan"). The chip's
  // reading swings with each step while the fan follows slowly (CONSOLE.md, 2026-11-22), so the chart shows
  // both over time, not a fan speed for each temperature. Balanced and Cool are 3 and 6 °C under the
  // console's own target, Manual is the user's, never under 70 °C. FAN_MEASURED: FLUX.2 Klein 9B at
  // 1024x1024, one image after another (CONSOLE.md, 2026-11-21).
  var FAN_BELOW = { quiet: 0, balanced: 3, cool: 6 };
  var FAN_LOWEST = 70;
  var FAN_MEASURED = {
    quiet: { fan: 30, avg: 85, hot: 91 },
    balanced: { fan: 33, avg: 83, hot: 88 },
    cool: { fan: 35, avg: 82, hot: 89 }
  };

  function fanStock(fc) { return fc && fc.stock ? fc.stock : 91; }

  // The highest temperature a mode holds the chip at.
  function fanLimit(mode, fc) {
    var stock = fanStock(fc);
    var t = mode === 'manual' ? fan.target : stock - (FAN_BELOW[mode] || 0);
    if (t > stock) t = stock;
    if (t < FAN_LOWEST) t = stock < FAN_LOWEST ? stock : FAN_LOWEST;
    return t;
  }

  // What the mode does, what it did on the console, and what the payload does now.
  function drawFan(fc) {
    if (fan.mode === null) return;
    var stock = fanStock(fc), limit = fanLimit(fan.mode, fc);
    var how = 'the console\'s own fan control turns the fan as fast as it needs to keep the chip there. The ' +
      'chip\'s temperature swings with each step and the fan follows slowly, so short peaks over the line are normal.';
    var what;
    if (fan.mode === 'quiet') what = 'Up to ' + limit + ' °C, the console\'s own setting: ' + how + ' PS5SD changes nothing.';
    else if (fan.mode === 'manual') what = 'Up to ' + limit + ' °C, as you set it (70 to ' + stock + ' °C): ' + how;
    else what = 'Up to ' + limit + ' °C, ' + FAN_BELOW[fan.mode] + ' °C under the console\'s own: ' + how;
    text($('fan-what'), what);
    var m = FAN_MEASURED[fan.mode], q = FAN_MEASURED.quiet, b = FAN_MEASURED.balanced, c = FAN_MEASURED.cool;
    text($('fan-meas'), m
      ? 'On our console, FLUX.2 Klein 9B making one image after another: the fan up to ' + m.fan + '%, the chip ' +
        m.avg + ' °C on average and ' + m.hot + ' °C at the most.'
      : 'On our console, FLUX.2 Klein 9B making one image after another: the fan went up to ' + q.fan + '% in Quiet, ' +
        b.fan + '% in Balanced and ' + c.fan + '% in Cool. The lower the temperature, the louder.');
    var note = '';
    if (fan.mode === 'quiet') {
      note = '';
    } else if (!fc || fc.state === 'starting' || (fc.state === 'running' && fc.target === null)) {
      note = 'Starting ps5sd-fan…';
    } else if (fc.state === 'no ELF loader') {
      note = 'The fan modes need an ELF loader on port 9021; none answered.';
    } else if (fc.state === 'not answering') {
      note = 'ps5sd-fan was sent to the ELF loader three times and does not answer on port 9075.';
    } else if (fc.target !== null) {
      var want = fc.wanted !== null ? fc.wanted : null;
      note = 'The console holds ' + fc.target + ' °C now.';
      if (want !== null && fc.target > want) note += ' It goes down one degree every 20 seconds, to ' + want + ' °C, so the fan does not jump.';
    }
    text($('fan-note'), note);
    show('fan-note', !!note);
    drawFanPlot(limit, stock);
  }

  function fanSvg(tag, attrs, txt) {
    var e = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (txt !== undefined) e.appendChild(document.createTextNode(txt));
    return e;
  }

  // The last 2 minutes, one reading a second: the chip's temperature above with the mode's highest temperature
  // (and the console's own) as lines, the fan below, on the same time axis. The pointer shows one reading.
  function drawFanPlot(limit, stock) {
    var svg = $('fan-plot');
    if (!svg) return;
    fan.limit = limit;
    fan.stock = stock;
    while (svg.firstChild) svg.removeChild(svg.firstChild);
    var d = con.data, s = d && d.seconds ? d.seconds : {};
    var ts = s.soc_c || [], fs = s.fan || [], n = Math.max(ts.length, fs.length, 2), i;
    var L = 34, R = 310, T = 10, B = 114, FT = 138, FB = 172;
    // from the lowest reading's ten (at most 70, at least 30) up to 100 °C
    var lo = 70, hi = 100;
    for (i = 0; i < ts.length; i++) if (ts[i] !== null && ts[i] < lo + 1) lo = Math.max(30, Math.floor((ts[i] - 1) / 10) * 10);
    var x = function (k) { return L + k / (n - 1) * (R - L); };
    var y = function (c) { return B - (Math.max(lo, Math.min(hi, c)) - lo) / (hi - lo) * (B - T); };
    var yf = function (p) { return FB - Math.max(0, Math.min(100, p)) / 100 * (FB - FT); };
    for (i = lo; i <= hi; i += 10) {
      svg.appendChild(fanSvg('line', { 'class': 'fgrid', x1: L, x2: R, y1: y(i), y2: y(i) }));
      svg.appendChild(fanSvg('text', { 'class': 'ftick', x: L - 5, y: y(i) + 3, 'text-anchor': 'end' }, i + ' °C'));
    }
    for (i = 0; i <= 100; i += 50) {
      svg.appendChild(fanSvg('line', { 'class': 'fgrid', x1: L, x2: R, y1: yf(i), y2: yf(i) }));
      svg.appendChild(fanSvg('text', { 'class': 'ftick', x: L - 5, y: yf(i) + 3, 'text-anchor': 'end' }, i + '%'));
    }
    svg.appendChild(fanSvg('text', { 'class': 'ftick fpane', x: L + 3, y: FT - 3 }, 'fan'));
    var step = d && d.seconds_interval ? d.seconds_interval : 1, span = (n - 1) * step;
    var marks = [[0, Math.round(span / 60) + ' min ago'], [0.5, Math.round(span / 120) + ' min ago'], [1, 'now']];
    if (span < 90) marks = [[0, Math.round(span) + ' s ago'], [1, 'now']];
    for (i = 0; i < marks.length; i++) {
      svg.appendChild(fanSvg('text', { 'class': 'ftick', x: L + marks[i][0] * (R - L), y: FB + 13,
        'text-anchor': marks[i][0] === 0 ? 'start' : marks[i][0] === 1 ? 'end' : 'middle' }, marks[i][1]));
    }
    // the console's own and the mode's highest temperature
    var own = limit !== stock;
    show('fan-key-own', own, 'inline');
    if (own) svg.appendChild(fanSvg('line', { 'class': 'fown', x1: L, x2: R, y1: y(stock), y2: y(stock) }));
    svg.appendChild(fanSvg('line', { 'class': 'fmode', x1: L, x2: R, y1: y(limit), y2: y(limit) }));
    // its label above the higher of the two lines, where the chip seldom is
    svg.appendChild(fanSvg('text', { 'class': 'flabel', x: L + 4, y: y(own ? Math.max(limit, stock) : limit) - 5 }, 'up to ' + limit + ' °C'));
    var line = function (pts, f, cls) {
      var p = '', pen = false;
      for (var k = 0; k < pts.length; k++) {
        if (pts[k] === null || pts[k] === undefined) { pen = false; continue; }
        p += (pen ? 'L' : 'M') + x(k + n - pts.length).toFixed(1) + ' ' + f(pts[k]).toFixed(1);
        pen = true;
      }
      if (p) svg.appendChild(fanSvg('path', { 'class': cls, d: p }));
    };
    line(ts, y, 'fchip');
    line(fs, function (v) { return yf(v * 100); }, 'ffan');
    var label = 'Up to ' + limit + ' °C.';
    var lastT = null, lastF = null;
    for (i = ts.length - 1; i >= 0 && lastT === null; i--) if (ts[i] !== null) lastT = { k: i + n - ts.length, v: ts[i] };
    for (i = fs.length - 1; i >= 0 && lastF === null; i--) if (fs[i] !== null) lastF = { k: i + n - fs.length, v: fs[i] };
    var hv = fan.hover >= 0 && fan.hover < n ? fan.hover : -1;
    if (hv >= 0) {
      svg.appendChild(fanSvg('line', { 'class': 'fcross', x1: x(hv), x2: x(hv), y1: T, y2: FB }));
      var hT = ts[hv - (n - ts.length)], hF = fs[hv - (n - fs.length)];
      if (hT !== null && hT !== undefined) svg.appendChild(fanSvg('circle', { 'class': 'fnow', cx: x(hv), cy: y(hT), r: 4 }));
      if (hF !== null && hF !== undefined) svg.appendChild(fanSvg('circle', { 'class': 'fnow', cx: x(hv), cy: yf(hF * 100), r: 4 }));
    } else {
      if (lastT) {
        svg.appendChild(fanSvg('circle', { 'class': 'fnow', cx: x(lastT.k), cy: y(lastT.v), r: 4.5 }));
        svg.appendChild(fanSvg('text', { 'class': 'fnowt', x: x(lastT.k) - 8, y: y(lastT.v) - 8, 'text-anchor': 'end' }, Math.round(lastT.v) + ' °C'));
        label += ' Now ' + Math.round(lastT.v) + ' °C';
      }
      if (lastF) {
        svg.appendChild(fanSvg('circle', { 'class': 'fnow', cx: x(lastF.k), cy: yf(lastF.v * 100), r: 4.5 }));
        svg.appendChild(fanSvg('text', { 'class': 'fnowt', x: x(lastF.k) - 8, y: yf(lastF.v * 100) - 7, 'text-anchor': 'end' }, Math.round(lastF.v * 100) + '%'));
        label += (lastT ? ', the fan at ' : ' Now the fan at ') + Math.round(lastF.v * 100) + '%';
      }
      if (lastT || lastF) label += '.';
    }
    svg.setAttribute('aria-label', label);
    drawFanRead(ts, fs, hv, n, step);
  }

  // The line under the chart: the range of the last 2 minutes, or the reading under the pointer.
  function drawFanRead(ts, fs, hv, n, step) {
    var vals = function (a, m) { var o = []; for (var k = 0; k < a.length; k++) if (a[k] !== null) o.push(a[k] * m); return o; };
    var tv = vals(ts, 1), fv = vals(fs, 100), out = '';
    if (hv >= 0) {
      var hT = ts[hv - (n - ts.length)], hF = fs[hv - (n - fs.length)], ago = (n - 1 - hv) * step;
      out = (ago ? ago + ' s ago' : 'Now') + ': the chip ' + (hT === null || hT === undefined ? 'not read' : Math.round(hT) + ' °C') +
        ', the fan ' + (hF === null || hF === undefined ? 'not read' : Math.round(hF * 100) + '%') + '.';
    } else if (tv.length && fv.length) {
      var sum = 0;
      for (var k = 0; k < tv.length; k++) sum += tv[k];
      out = 'The last 2 minutes: the chip ' + Math.round(Math.min.apply(null, tv)) + ' to ' + Math.round(Math.max.apply(null, tv)) +
        ' °C, ' + Math.round(sum / tv.length) + ' °C on average; the fan ' + Math.round(Math.min.apply(null, fv)) + ' to ' +
        Math.round(Math.max.apply(null, fv)) + '%.';
    } else {
      out = 'No readings of the chip and the fan yet.';
    }
    text($('fan-read'), out);
  }

  function initFanPlot() {
    var svg = $('fan-plot');
    if (!svg || !svg.addEventListener) return;
    var at = function (ev) {
      var d = con.data, s = d && d.seconds ? d.seconds : {};
      var n = Math.max((s.soc_c || []).length, (s.fan || []).length, 2);
      var r = svg.getBoundingClientRect();
      var vx = (ev.clientX - r.left) / Math.max(1, r.width) * 320;
      fan.hover = Math.max(0, Math.min(n - 1, Math.round((vx - 34) / (310 - 34) * (n - 1))));
      drawFanPlot(fan.limit, fan.stock);
    };
    svg.addEventListener('pointermove', at);
    svg.addEventListener('pointerdown', at);
    svg.addEventListener('pointerleave', function () { fan.hover = -1; drawFanPlot(fan.limit, fan.stock); });
  }

  function postFan(body) {
    req('POST', APP + '/settings', body, function (code, data) {
      if (code === 200 && data && typeof data.fan === 'string') {
        fan.mode = data.fan;
        fan.target = data.fan_target;
        drawFanButtons();
        drawFan(con.data ? con.data.fan_control : null);
      } else {
        toast('Could not change the fan mode' + (errMessage(data) ? ': ' + errMessage(data) : '.'), 6000);
        loadFan();
      }
    });
  }

  function setFanMode(mode) {
    if (mode === fan.mode) return;
    fan.mode = mode;
    drawFanButtons();
    drawFan(con.data ? con.data.fan_control : null);
    postFan({ fan: mode });
  }

  function setFanTarget(t) {
    t = Math.max(70, Math.min(con.data && con.data.fan_control && con.data.fan_control.stock ? con.data.fan_control.stock : 91, Math.round(t)));
    if (isNaN(t)) t = fan.target;
    fan.target = t;
    $('fan-target').value = t;
    drawFan(con.data ? con.data.fan_control : null);
    clearTimeout(fan.timer);
    fan.timer = setTimeout(function () { postFan({ fan_target: t }); }, 600);
  }

  function loadFan() {
    loadSettings(function (code, data) {
      if (code !== 200 || !data || typeof data.fan !== 'string') return;
      fan.mode = data.fan;
      fan.target = data.fan_target;
      drawFanButtons();
      show('pnl-fan', true);
      drawFan(con.data ? con.data.fan_control : null);
    });
  }

  function initFan() {
    var bs = $('fan-mode').getElementsByTagName('button');
    for (var i = 0; i < bs.length; i++) {
      bs[i].onclick = function () { setFanMode(this.getAttribute('data-fan')); };
    }
    $('fan-target-').onclick = function () { setFanTarget(fan.target - 1); };
    $('fan-target+').onclick = function () { setFanTarget(fan.target + 1); };
    $('fan-target').onchange = function () { setFanTarget(parseInt(this.value, 10)); };
    initFanPlot();
    loadFan();
  }
