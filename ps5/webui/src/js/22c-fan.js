  // ---------- the fan mode ----------
  // AppSettings' "fan" and "fan_target" (GET/POST /ps5sd/v1/app/settings); the console route's
  // "fan_control" says what the payload does with them (console_stats.hpp). Hidden on servers
  // without them.
  var fan = { mode: null, target: 85, timer: 0 };

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

  // Each mode is a highest temperature: the console's own fan control keeps the fan at its minimum until
  // the chip reaches it, then turns it as fast as it needs to hold it there (it takes no fan speed, only
  // that target: docs/CONSOLE.md, "The fan"). Balanced and Cool are 3 and 6 °C under the console's own
  // target, Manual is the user's, never under 70 °C. FAN_MEASURED: FLUX.2 Klein 9B at 1024x1024, one image
  // after another (CONSOLE.md, 2026-11-21).
  var FAN_BELOW = { quiet: 0, balanced: 3, cool: 6 };
  var FAN_LOWEST = 70, FAN_MIN_DUTY = 23;
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
    var how = 'the fan stays at its minimum, about ' + FAN_MIN_DUTY + '%, until the chip reaches ' + limit +
      ' °C, and then turns as fast as it needs to keep it there.';
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

  // The chip's temperature across, the fan down: the mode's line (flat at the minimum, then straight up at
  // its highest temperature), the console's own beside it, the readings of the last 2 minutes and now.
  function drawFanPlot(limit, stock) {
    var svg = $('fan-plot');
    if (!svg) return;
    while (svg.firstChild) svg.removeChild(svg.firstChild);
    var L = 34, R = 310, T = 10, B = 140, t0 = 45, t1 = 95;
    var x = function (c) { return L + (Math.max(t0, Math.min(t1, c)) - t0) / (t1 - t0) * (R - L); };
    var y = function (p) { return B - Math.max(0, Math.min(100, p)) / 100 * (B - T); };
    var i;
    for (i = 50; i <= 90; i += 10) {
      svg.appendChild(fanSvg('line', { 'class': 'fgrid', x1: x(i), x2: x(i), y1: T, y2: B }));
      svg.appendChild(fanSvg('text', { 'class': 'ftick', x: x(i), y: B + 13, 'text-anchor': 'middle' }, i + ' °C'));
    }
    for (i = 0; i <= 100; i += 25) {
      svg.appendChild(fanSvg('line', { 'class': 'fgrid', x1: L, x2: R, y1: y(i), y2: y(i) }));
      svg.appendChild(fanSvg('text', { 'class': 'ftick', x: L - 5, y: y(i) + 3, 'text-anchor': 'end' }, i + '%'));
    }
    var own = limit !== stock;
    show('fan-key-own', own, 'inline');
    if (own) {
      svg.appendChild(fanSvg('path', { 'class': 'fown',
        d: 'M' + x(t0) + ',' + y(FAN_MIN_DUTY) + ' L' + x(stock) + ',' + y(FAN_MIN_DUTY) + ' L' + x(stock) + ',' + y(100) }));
    }
    svg.appendChild(fanSvg('path', { 'class': 'fmode',
      d: 'M' + x(t0) + ',' + y(FAN_MIN_DUTY) + ' L' + x(limit) + ',' + y(FAN_MIN_DUTY) + ' L' + x(limit) + ',' + y(100) }));
    svg.appendChild(fanSvg('text', { 'class': 'flabel', x: x(limit) - 5, y: T + 9, 'text-anchor': 'end' }, 'up to ' + limit + ' °C'));
    var d = con.data, label = 'Up to ' + limit + ' °C.';
    if (d && d.seconds && d.seconds.soc_c && d.seconds.fan) {
      var ts = d.seconds.soc_c, fs = d.seconds.fan;
      for (i = 0; i < ts.length && i < fs.length; i++) {
        if (ts[i] === null || fs[i] === null) continue;
        svg.appendChild(fanSvg('circle', { 'class': 'fread', cx: x(ts[i]), cy: y(fs[i] * 100), r: 2.2 }));
      }
    }
    if (d && d.now && d.now.soc_c !== null && d.now.fan !== null) {
      var nc = d.now.soc_c, nf = Math.round(d.now.fan * 100);
      svg.appendChild(fanSvg('circle', { 'class': 'fnow', cx: x(nc), cy: y(nf), r: 5 }));
      var right = x(nc) < (L + R) / 2;
      svg.appendChild(fanSvg('text', { 'class': 'fnowt', x: x(nc) + (right ? 9 : -9), y: y(nf) - 8,
        'text-anchor': right ? 'start' : 'end' }, 'now ' + Math.round(nc) + ' °C · ' + nf + '%'));
      label += ' Now ' + Math.round(nc) + ' °C, the fan at ' + nf + '%.';
    }
    svg.setAttribute('aria-label', label);
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
    loadFan();
  }
