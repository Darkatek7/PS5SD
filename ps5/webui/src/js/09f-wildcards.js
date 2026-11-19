  // ---------- wildcards ----------
  // {red|blue|green} in a prompt is one of its options, picked anew for each image. The page expands it
  // before it sends a job (generate, pageRunStep, queueEach), so the server and every API client see a
  // plain prompt, and the image keeps the expanded one. A brace without a "|" stays as it is. An option
  // may be empty ({a|}), and a pair inside another ({x|{y|z}}) is expanded from the inside out.
  var WILD_RE = /\{([^{}|]*\|[^{}]*)\}/g;

  function hasWildcard(t) { return /\{[^{}|]*\|[^{}]*\}/.test(t || ''); }

  // pick: a function giving a number from 0 up to (not including) 1; Math.random unless a test hands one in.
  function expandWildcards(t, pick) {
    var rnd = pick || Math.random;
    t = String(t);
    for (var pass = 0; pass < 8; pass++) {
      var again = false;
      t = t.replace(WILD_RE, function (m, inner) {
        again = true;
        var options = inner.split('|');
        return options[Math.min(options.length - 1, Math.floor(rnd() * options.length))];
      });
      if (!again) break;
    }
    return t;
  }

  // The note under the prompt while it has a wildcard.
  function drawWildHint() {
    show('wild-hint', hasWildcard($('prompt').value), 'block');
  }
