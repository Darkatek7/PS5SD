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

  // What the mode does now, from the console route's fan_control.
  function drawFan(fc) {
    if (fan.mode === null) return;
    var note;
    var stock = fc && fc.stock !== null ? fc.stock : null;
    if (fan.mode === 'quiet') {
      note = 'The console\'s own fan control: the fan speeds up when the chip nears ' + (stock || 91) + ' °C.';
    } else if (!fc || fc.state === 'starting' || (fc.state === 'running' && fc.target === null)) {
      note = 'Starting ps5sd-fan…';
    } else if (fc.state === 'no ELF loader') {
      note = 'The fan modes need an ELF loader on port 9021; none answered.';
    } else if (fc.state === 'not answering') {
      note = 'ps5sd-fan was sent to the ELF loader three times and does not answer on port 9075.';
    } else {
      var want = fc.wanted !== null ? fc.wanted : null;
      note = 'Target ' + fc.target + ' °C' + (stock ? ' (the console\'s own: ' + stock + ' °C)' : '') + '.';
      if (want !== null && fc.target > want) note += ' It goes down one degree every 20 seconds, to ' + want + ' °C.';
    }
    text($('fan-note'), note);
  }

  function postFan(body) {
    req('POST', APP + '/settings', body, function (code, data) {
      if (code === 200 && data && typeof data.fan === 'string') {
        fan.mode = data.fan;
        fan.target = data.fan_target;
        drawFanButtons();
        drawFan(con.data ? con.data.fan_control : null);
      } else {
        toast('Could not change the fan mode' + (data && data.error ? ': ' + data.error : '.'), 6000);
        loadFan();
      }
    });
  }

  function setFanMode(mode) {
    if (mode === fan.mode) return;
    fan.mode = mode;
    drawFanButtons();
    text($('fan-note'), '');
    postFan({ fan: mode });
  }

  function setFanTarget(t) {
    t = Math.max(70, Math.min(95, Math.round(t)));
    if (isNaN(t)) t = fan.target;
    fan.target = t;
    $('fan-target').value = t;
    clearTimeout(fan.timer);
    fan.timer = setTimeout(function () { postFan({ fan_target: t }); }, 600);
  }

  function loadFan() {
    req('GET', APP + '/settings', null, function (code, data) {
      if (code !== 200 || !data || typeof data.fan !== 'string') return;
      fan.mode = data.fan;
      fan.target = data.fan_target;
      drawFanButtons();
      show('pnl-fan', true);
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
