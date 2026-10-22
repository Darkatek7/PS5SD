  // ---------- the console: temperature, power, fan and load ----------
  // GET /ps5sd/v1/app/console (console_stats.hpp): the readings now, a point a second for 2 minutes and
  // one per 30 s for the hour, null where nothing was read. The header shows the temperature and the
  // power; the Settings tab charts all five. Asked every 2 s while the charts show, every 5 s
  // otherwise, and not at all while the page is in the background.
  var CONSOLE_CHARTS = [
    { key: 'soc_c', title: 'Temperature', fmt: function (v) { return Math.round(v) + ' °C'; } },
    { key: 'watts', title: 'Power', fmt: function (v) { return Math.round(v) + ' W'; } },
    { key: 'job', title: 'GPU busy', pct: true, full: true },
    { key: 'cpu', title: 'CPU (PS5SD)', pct: true },
    { key: 'fan', title: 'Fan', pct: true, full: true }
  ];
  var con = { data: null, range: 'seconds', timer: 0, built: false };

  function conValue(c, v) {
    if (v === null || v === undefined) return '–';
    return c.pct ? Math.round(v * 100) + '%' : c.fmt(v);
  }

  function pollConsole() {
    clearTimeout(con.timer);
    if (document.hidden) return;  // visibilitychange starts it again
    req('GET', APP + '/console', null, function (code, d) {
      con.data = code === 200 && d && d.now ? d : null;
      drawConsole();
      con.timer = setTimeout(pollConsole, st.view === 'settings' ? 2000 : 5000);
    }, 4000);
  }

  function setConsoleRange(range) {
    con.range = range;
    var b = [['con-sec', 'seconds'], ['con-min', 'minutes']];
    for (var i = 0; i < b.length; i++) {
      $(b[i][0]).className = b[i][1] === range ? 'on' : '';
      $(b[i][0]).setAttribute('aria-pressed', b[i][1] === range ? 'true' : 'false');
    }
    save('conRange', range);
    drawConsole();
  }

  // The header's chip: the temperature and the power, while there are any.
  function drawConsoleChip(now) {
    var t = now && now.soc_c !== null ? Math.round(now.soc_c) + '°C' : '';
    var w = now && now.watts !== null ? Math.round(now.watts) + ' W' : '';
    text($('cchip-t'), t);
    text($('cchip-w'), t && w ? ' · ' + w : w);
    show('cchip', !!(t || w), 'flex');
  }

  // One chart a reading: the value now as the headline, its line over the time shown with the
  // lowest and highest point labelled, and a crosshair with the value under the pointer.
  function buildConsoleCharts() {
    var box = $('con-charts');
    clear(box);
    for (var i = 0; i < CONSOLE_CHARTS.length; i++) {
      var c = CONSOLE_CHARTS[i];
      var card = el('div', 'ccard');
      var head = el('div', 'chead');
      head.appendChild(el('span', 'ctitle', c.title));
      c.now = el('span', 'cnow', '–');
      head.appendChild(c.now);
      card.appendChild(head);
      c.svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      c.svg.setAttribute('class', 'cplot');
      c.svg.setAttribute('viewBox', '0 0 300 72');
      c.svg.setAttribute('preserveAspectRatio', 'none');
      c.svg.setAttribute('role', 'img');
      card.appendChild(c.svg);
      // the lowest and highest point, or the point under the pointer
      c.foot = el('div', 'cfoot', '');
      card.appendChild(c.foot);
      (function (c) {
        var at = function (ev) {
          var pts = c.points || [];
          if (!pts.length) return;
          var r = c.svg.getBoundingClientRect();
          var i = Math.round((ev.clientX - r.left) / Math.max(1, r.width) * (pts.length - 1));
          i = Math.max(0, Math.min(pts.length - 1, i));
          c.hover = i;
          drawConsoleChart(c);
        };
        c.svg.addEventListener('pointermove', at);
        c.svg.addEventListener('pointerdown', at);
        c.svg.addEventListener('pointerleave', function () { c.hover = -1; drawConsoleChart(c); });
      })(c);
      c.hover = -1;
      box.appendChild(card);
    }
    con.built = true;
  }

  function svgEl(tag, attrs) {
    var e = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    return e;
  }

  // "45 s ago", "12 min ago": how long before now the point at index i was.
  function conAgo(i, n) {
    var step = con.data ? (con.range === 'seconds' ? con.data.seconds_interval : con.data.minutes_interval) : 1;
    var s = (n - 1 - i) * (step || 1);
    if (s === 0) return 'now';
    return s < 120 ? s + ' s ago' : Math.round(s / 60) + ' min ago';
  }

  function drawConsoleChart(c) {
    var svg = c.svg;
    while (svg.firstChild) svg.removeChild(svg.firstChild);
    var pts = c.points || [];
    var vals = [];
    for (var i = 0; i < pts.length; i++) if (pts[i] !== null) vals.push(pts[i]);
    svg.setAttribute('aria-label', c.title + ': ' + conValue(c, con.data ? con.data.now[c.key] : null));
    if (vals.length < 2) {
      text(c.foot, vals.length ? '' : 'not read');
      c.foot.className = 'cfoot';
      return;
    }
    var lo = Math.min.apply(null, vals), hi = Math.max.apply(null, vals);
    // shares from 0: the fan and GPU busy up to 100%, PS5SD's CPU up to a little over its highest;
    // the others with a little room so a flat line is not on the edge
    if (c.pct) { lo = 0; hi = c.full ? 1 : Math.max(hi * 1.15, 0.05); }
    else { var pad = Math.max((hi - lo) * 0.15, c.key === 'soc_c' ? 2 : 1); lo -= pad; hi += pad; }
    var n = Math.max(pts.length, 2);
    var x = function (i) { return i / (n - 1) * 300; };
    var y = function (v) { return 68 - (v - lo) / (hi - lo) * 64; };
    svg.appendChild(svgEl('line', { x1: 0, x2: 300, y1: 68, y2: 68, 'class': 'cbase' }));
    var d = '', pen = false;
    for (i = 0; i < pts.length; i++) {
      if (pts[i] === null) { pen = false; continue; }
      d += (pen ? 'L' : 'M') + x(i).toFixed(1) + ' ' + y(pts[i]).toFixed(1);
      pen = true;
    }
    svg.appendChild(svgEl('path', { d: d, 'class': 'cline', 'vector-effect': 'non-scaling-stroke' }));
    if (c.hover >= 0 && c.hover < pts.length) {
      svg.appendChild(svgEl('line', { x1: x(c.hover), x2: x(c.hover), y1: 0, y2: 72, 'class': 'ccross', 'vector-effect': 'non-scaling-stroke' }));
      text(c.foot, conValue(c, pts[c.hover]) + ', ' + conAgo(c.hover, pts.length));
      c.foot.className = 'cfoot ctip';
    } else {
      var vmin = Math.min.apply(null, vals), vmax = Math.max.apply(null, vals);
      text(c.foot, 'low ' + conValue(c, vmin) + ' · high ' + conValue(c, vmax));
      c.foot.className = 'cfoot';
    }
  }

  function drawConsole() {
    var d = con.data;
    drawConsoleChip(d ? d.now : null);
    if (st.view !== 'settings') return;
    if (!con.built) buildConsoleCharts();
    for (var i = 0; i < CONSOLE_CHARTS.length; i++) {
      var c = CONSOLE_CHARTS[i];
      c.points = d && d[con.range] ? d[con.range][c.key] || [] : [];
      text(c.now, conValue(c, d ? d.now[c.key] : null));
      drawConsoleChart(c);
    }
    var note = !d ? 'The console does not answer.'
      : d.exporter === 'running' ? (con.range === 'seconds' ? 'A point a second.' : 'A point every 30 seconds, the mean of them.')
        : d.exporter === 'no ELF loader'
          ? 'Temperature, power and fan need ps5-exporter, which PS5SD starts through an ELF loader on port 9021; none answered.'
          : d.exporter === 'not answering'
            ? 'Temperature, power and fan: ps5-exporter was sent to the ELF loader twice and does not answer on port 9100.'
            : 'Starting ps5-exporter for the temperature, power and fan…';
    text($('con-note'), note);
  }

  function initConsole() {
    $('con-sec').onclick = function () { setConsoleRange('seconds'); };
    $('con-min').onclick = function () { setConsoleRange('minutes'); };
    $('cchip').onclick = function () { setView('settings'); };
    if (load('conRange') === 'minutes') setConsoleRange('minutes');
    document.addEventListener('visibilitychange', function () { if (!document.hidden) pollConsole(); });
    pollConsole();
  }
