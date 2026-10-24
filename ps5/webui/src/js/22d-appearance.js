  // ---------- appearance: the theme, the folded sidebar, the prompt that grows ----------
  // The theme and the fold live in this browser (localStorage, read in <head> before the page draws);
  // html[data-theme] picks the colours in app.css, no attribute follows the system.
  function drawTheme() {
    var t = load('theme') || 'auto';
    var bs = $('theme').getElementsByTagName('button');
    for (var i = 0; i < bs.length; i++) {
      var on = bs[i].getAttribute('data-theme') === t;
      bs[i].className = on ? 'on' : '';
      bs[i].setAttribute('aria-pressed', on ? 'true' : 'false');
    }
  }

  function setTheme(t) {
    var h = document.documentElement;
    if (t === 'light' || t === 'dark') { save('theme', t); h.setAttribute('data-theme', t); }
    else { drop('theme'); h.removeAttribute('data-theme'); }
    drawTheme();
  }

  function setSideMini(on) {
    var h = document.documentElement;
    h.className = h.className.replace(/\s*\bside-mini\b/g, '') + (on ? ' side-mini' : '');
    save('sideMini', on ? '1' : '0');
    $('sidefold').setAttribute('aria-pressed', on ? 'true' : 'false');
    $('sidefold').title = on ? 'Show the sidebar with its names' : 'Fold the sidebar to its icons';
  }

  // The prompt grows with its text, up to about half the screen; past that it scrolls.
  function growPrompt() {
    var ta = $('prompt');
    var max = Math.max(160, Math.round((window.innerHeight || 800) * 0.45));
    ta.style.height = 'auto';
    var want = ta.scrollHeight + 2;
    ta.style.height = Math.min(want, max) + 'px';
    ta.style.overflowY = want > max ? 'auto' : 'hidden';
  }

  function initAppearance() {
    var bs = $('theme').getElementsByTagName('button');
    for (var i = 0; i < bs.length; i++) bs[i].onclick = function () { setTheme(this.getAttribute('data-theme')); };
    drawTheme();
    $('sidefold').onclick = function () { setSideMini(!/\bside-mini\b/.test(document.documentElement.className)); };
    setSideMini(load('sideMini') === '1');
    var ta = $('prompt');
    var old = ta.oninput;
    ta.oninput = function (e) { if (old) old.call(ta, e); growPrompt(); };
    window.addEventListener('resize', growPrompt);
    growPrompt();
  }
