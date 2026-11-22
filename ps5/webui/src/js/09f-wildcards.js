  // ---------- wildcards ----------
  // {red|blue|green} in a prompt is one of its options, picked anew for each image. __name__ is a random entry of
  // the collection "name" (09h), and __lora__ is the trigger words of the LoRAs that go into the job. The page
  // expands them before it sends a job (generate, pageRunStep, queueEach), so the server and every API client
  // see a plain prompt, and the image keeps the expanded one. A brace without a "|" stays as it is. An option
  // may be empty ({a|}), and a pair inside another ({x|{y|z}}) is expanded from the inside out. An entry of a
  // collection may hold braces and other __names__ itself; up to 8 passes, so an entry that names its own
  // collection stops there. A __name__ without a collection (or with an empty one) stays as typed.
  var WILD_RE = /\{([^{}|]*\|[^{}]*)\}/g;
  var NAME_RE = /__([A-Za-z0-9_-]{1,40})__/g;
  var WILD_PASSES = 8;
  var bookList = null;     // [{name, time, entries: [{id, prompt, negative}]}] from the console; null: none known
  var booksOk = true;      // false: the app has no such route (404), so __name__ stays as typed

  function hasWildcard(t) { return /\{[^{}|]*\|[^{}]*\}/.test(t || ''); }

  // The collection of a name (without regard to case), or null.
  function findBook(name) {
    var list = booksOk && bookList ? bookList : [], low = String(name).toLowerCase();
    for (var i = 0; i < list.length; i++) if (list[i].name.toLowerCase() === low) return list[i];
    return null;
  }

  function isLoraName(n) { return String(n).toLowerCase() === 'lora'; }
  function hasLoraPlaceholder(t) { return /__lora__/i.test(t || ''); }

  // The names of the __x__ in a text, in order, each once (as typed the first time).
  function placeholderNames(t) {
    var out = [], seen = {}, m, re = new RegExp(NAME_RE.source, 'g');
    while ((m = re.exec(t || ''))) {
      var k = m[1].toLowerCase();
      if (!seen[k]) { seen[k] = 1; out.push(m[1]); }
    }
    return out;
  }

  // Does a text give another result for each image: braces, or a __name__ with a collection with entries?
  function isRandom(t) {
    if (hasWildcard(t)) return true;
    var names = placeholderNames(t);
    for (var i = 0; i < names.length; i++) {
      var b = isLoraName(names[i]) ? null : findBook(names[i]);
      if (b && b.entries.length) return true;
    }
    return false;
  }

  function pickIndex(n, rnd) { return Math.min(n - 1, Math.floor(rnd() * n)); }

  // Where a placeholder left nothing, the separators around it go: ", ," is ",", and a comma or space at the
  // start or the end goes.
  function tidySeparators(t) {
    return t.replace(/,(\s*,)+/g, ',').replace(/^[\s,]+/, '').replace(/[\s,]+$/, '');
  }

  // ctx: {find: name -> collection or null, lora: the words for __lora__, or null to leave it as typed}
  function expandText(t, ctx, rnd) {
    t = String(t);
    var emptied = false;
    for (var pass = 0; pass < WILD_PASSES; pass++) {
      var again = false;
      t = t.replace(WILD_RE, function (m, inner) {
        again = true;
        var options = inner.split('|');
        return options[pickIndex(options.length, rnd)];
      });
      if (ctx.find || ctx.lora !== null) {
        t = t.replace(NAME_RE, function (m, name) {
          var out = null;
          if (isLoraName(name)) {
            if (ctx.lora !== null && ctx.lora !== undefined) out = ctx.lora;
          } else {
            var b = ctx.find ? ctx.find(name) : null;
            if (b && b.entries.length) out = b.entries[pickIndex(b.entries.length, rnd)].prompt;
          }
          if (out === null) return m;
          again = true;
          if (!out) emptied = true;
          return out;
        });
      }
      if (!again) break;
    }
    return emptied ? tidySeparators(t) : t;
  }

  // pick: a function giving a number from 0 up to (not including) 1; Math.random unless a test hands one in.
  // Only the {a|b} part (the other callers of this).
  function expandWildcards(t, pick) {
    return expandText(t, { find: null, lora: null }, pick || Math.random);
  }

  // The trigger words of the LoRAs that go into a job (loraBody: on, strength not 0, on the console), in the
  // order they are picked, joined with ", ".
  function loraWordsOf(loras) {
    var used = loraBody({ loras: loras || [] }), words = [];
    for (var i = 0; i < used.length; i++) {
      var w = String(loraTrigger(used[i].path) || '').replace(/^[\s,]+|[\s,]+$/g, '');
      if (w) words.push(w);
    }
    return words.join(', ');
  }

  function joinParts(a, b) {
    var out = [];
    if (a) out.push(a);
    if (b) out.push(b);
    return out.join(', ');
  }

  // One expansion for one image: p.template and p.negTemplate (what was typed) give p.prompt and p.negative.
  // A prompt that is exactly one __name__ takes the negative prompt of the entry it picked, before the typed
  // one. Used by generate, a page run (every image after the first) and the queue.
  function expandJob(p, pick) {
    var rnd = pick || Math.random;
    var ctx = { find: findBook, lora: loraWordsOf(p.loras) };
    var negCtx = { find: findBook, lora: null };
    var tpl = String(p.template), extra = '', prompt;
    var m = tpl.match(/^\s*__([A-Za-z0-9_-]{1,40})__\s*$/);
    var book = m && !isLoraName(m[1]) ? findBook(m[1]) : null;
    if (book && book.entries.length) {
      var e = book.entries[pickIndex(book.entries.length, rnd)];
      prompt = expandText(e.prompt, ctx, rnd);
      if (e.negative) extra = expandText(e.negative, negCtx, rnd);
    } else {
      prompt = expandText(tpl, ctx, rnd);
    }
    var typed = expandText(String(p.negTemplate || ''), negCtx, rnd);
    p.prompt = prompt;
    p.negative = joinParts(extra, typed);
  }

  // The words of the picked LoRAs for the hint: how many there are.
  function loraWordCount() {
    var w = loraWordsOf(loraSel);
    return w ? w.split(',').length : 0;
  }

  function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

  // The note under the prompt while it has a placeholder: what each image picks from.
  function wildHintText(t) {
    var parts = [], stay = [], i, m, re = /\{[^{}|]*\|[^{}]*\}/g;
    var names = placeholderNames(t);
    for (i = 0; i < names.length; i++) {
      var n = names[i];
      if (isLoraName(n)) {
        var c = loraWordCount();
        parts.push('__lora__ (' + (c ? plural(c, 'word', 'words') : 'no words') + ')');
        continue;
      }
      var b = findBook(n);
      if (b && b.entries.length) parts.push('__' + n + '__ (' + b.entries.length + ')');
      else if (booksOk && bookList) stay.push('__' + n + '__');
    }
    var seen = {};
    while ((m = re.exec(t || ''))) {
      if (!seen[m[0]]) { seen[m[0]] = 1; parts.push(m[0].length > 24 ? '{' + m[0].slice(1, 20) + '…}' : m[0]); }
    }
    var out = parts.length ? 'Each image picks from ' + parts.join(', ') + '.' : '';
    if (stay.length) out += (out ? ' ' : '') + stay.join(', ') + ' matches no collection, so it stays as typed.';
    return out;
  }

  // The note under the prompt while it has a placeholder.
  function drawWildHint() {
    var t = $('prompt').value, s = wildHintText(t);
    text($('wild-text'), s);
    show('wild-hint', !!s, 'block');
  }
