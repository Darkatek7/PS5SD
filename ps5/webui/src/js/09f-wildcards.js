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
  // __lora:NAME__: NAME is a LoRA's path or file name; letters, digits, - . / and spaces among them; a single _
  // is fine, two (__) end it.
  var LORA_NAMED_RE = /__lora:([^_\s](?:(?:[^_\n]|_(?!_))*[^_\s])?)__/gi;
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

  // The names of the __lora:NAME__ in a text, in order, each once (case folded).
  function namedPlaceholders(t) {
    var out = [], seen = {}, m, re = new RegExp(LORA_NAMED_RE.source, 'gi');
    while ((m = re.exec(t || ''))) {
      var k = m[1].toLowerCase();
      if (!seen[k]) { seen[k] = 1; out.push(m[1]); }
    }
    return out;
  }

  // Does a LoRA give other words for each image (one or some of more than it takes)?
  function loraPicks(e) {
    var n = loraOnTexts(e).length, mode = loraModeOf(e);
    return (mode === 'one' && n > 1) || (mode === 'some' && loraCountOf(e) < n);
  }

  // What a LoRA puts in: "all 3", "one of 3", "2 of 5".
  function loraPickText(e) {
    var n = loraOnTexts(e).length, mode = loraModeOf(e);
    if (mode === 'one' && n > 1) return 'one of ' + n;
    if (mode === 'some' && loraCountOf(e) < n) return loraCountOf(e) + ' of ' + n;
    return 'all ' + n;
  }

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
    if (hasLoraPlaceholder(t)) {
      for (var k = 0; k < loraSel.length; k++) if (loraPicks(loraEntry(loraSel[k].path))) return true;
    }
    var nm = namedPlaceholders(t);
    for (var j = 0; j < nm.length; j++) if (loraPicks(loraByName(nm[j]))) return true;
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

  // ctx: {find: name -> collection or null,
  //        lora: function (named) -> the words for __lora__ (named: the paths a __lora:NAME__ took), or null
  //        to leave __lora__ as typed, named: function (name, rnd) -> {path, words} or null (no such LoRA)}
  function expandText(t, ctx, rnd) {
    t = String(t);
    var emptied = false, loraDone = false, took = {};
    for (var pass = 0; pass < WILD_PASSES; pass++) {
      var again = false;
      t = t.replace(WILD_RE, function (m, inner) {
        again = true;
        var options = inner.split('|');
        return options[pickIndex(options.length, rnd)];
      });
      if (ctx.find) {
        t = t.replace(NAME_RE, function (m, name) {
          var b = isLoraName(name) ? null : ctx.find(name);
          if (!b || !b.entries.length) return m;
          again = true;
          var out = b.entries[pickIndex(b.entries.length, rnd)].prompt;
          if (!out) emptied = true;
          return out;
        });
      }
      if (ctx.named) {
        t = t.replace(LORA_NAMED_RE, function (m, name) {
          var r = ctx.named(name, rnd);
          if (!r) return m;
          took[r.path] = true;
          again = true;
          if (!r.words) emptied = true;
          return r.words;
        });
      }
      // __lora__ last, when nothing else is left to expand: it leaves out the LoRAs that were named
      if (!again && !loraDone && ctx.lora) {
        loraDone = true;
        t = t.replace(/__lora__/gi, function () {
          var out = ctx.lora(took);
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
    return expandText(t, { find: null, lora: null, named: null }, pick || Math.random);
  }

  // ---- a LoRA's trigger words: a list of {text, on}, a mode (all, one, some) and a count (for some)
  function loraItems(e) {
    var out = [], i;
    if (!e) return out;
    if (e.triggers && e.triggers.length !== undefined) {
      for (i = 0; i < e.triggers.length; i++) {
        var t = e.triggers[i];
        var tx = String(t && typeof t === 'object' ? t.text : t).replace(/^\s+|\s+$/g, '');
        if (tx) out.push({ text: tx, on: !(t && typeof t === 'object' && t.on === false) });
      }
      return out;
    }
    // an older app: the one string, split at commas, all on
    var parts = String(e.trigger || '').split(',');
    for (i = 0; i < parts.length; i++) {
      var w = parts[i].replace(/^\s+|\s+$/g, '');
      if (w) out.push({ text: w, on: true });
    }
    return out;
  }

  // Does the console keep the list (it sent "triggers")? Without it the mode is "all" and cannot be saved.
  function loraListed(e) { return !!(e && e.triggers && e.triggers.length !== undefined); }

  function loraModeOf(e) {
    return e && loraListed(e) && (e.trigger_mode === 'one' || e.trigger_mode === 'some') ? e.trigger_mode : 'all';
  }

  function loraCountOf(e) {
    var n = e ? Math.floor(Number(e.trigger_count)) : 0;
    return n >= 1 && n <= 20 ? n : 2;
  }

  function loraOnTexts(e) {
    var items = loraItems(e), out = [];
    for (var i = 0; i < items.length; i++) if (items[i].on) out.push(items[i].text);
    return out;
  }

  // The words of a LoRA for one image: all (the ones that are on), one at random, or count different ones
  // at random in list order. A string, joined with ", " (empty when none is on).
  function loraPickWords(e, rnd) {
    var on = loraOnTexts(e), mode = loraModeOf(e);
    if (mode === 'one' && on.length > 1) return on[pickIndex(on.length, rnd)];
    if (mode === 'some') {
      var n = Math.min(loraCountOf(e), on.length);
      if (n < on.length) {
        var idx = [], i, chosen = [];
        for (i = 0; i < on.length; i++) idx.push(i);
        for (i = 0; i < n; i++) chosen.push(idx.splice(pickIndex(idx.length, rnd), 1)[0]);
        chosen.sort(function (a, b) { return a - b; });
        var out = [];
        for (i = 0; i < chosen.length; i++) out.push(on[chosen[i]]);
        return out.join(', ');
      }
    }
    return on.join(', ');
  }

  // A path without its extension.
  function loraNoExt(path) { return String(path).replace(/\.(gguf|safetensors|ckpt|sft|pt|bin)$/i, ''); }

  // The installed LoRA a __lora:NAME__ means: its path without extension, else its file name without
  // extension (without regard to case; the path wins). null: none.
  function loraByName(name) {
    var list = loraList || [], low = String(name).toLowerCase(), i, stem = null;
    for (i = 0; i < list.length; i++) if (loraNoExt(list[i].path).toLowerCase() === low) return list[i];
    for (i = 0; i < list.length; i++) {
      if (!stem && loraNoExt(list[i].path).replace(/^.*\//, '').toLowerCase() === low) stem = list[i];
    }
    return stem;
  }

  // The LoRAs that go into a job (loraBody: on, strength not 0, on the console), their paths in order.
  function loraUsedPaths(loras) {
    var used = loraBody({ loras: loras || [] }), out = [];
    for (var i = 0; i < used.length; i++) out.push(used[i].path);
    return out;
  }

  // The words for __lora__: the job's LoRAs in order, each by its list and mode, leaving out the paths in skip.
  function loraWordsOf(loras, rnd, skip) {
    var paths = loraUsedPaths(loras), words = [];
    for (var i = 0; i < paths.length; i++) {
      if (skip && skip[paths[i]]) continue;
      var w = loraPickWords(loraEntry(paths[i]), rnd || Math.random);
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

  // The collections of these names that have entries (each once, without regard to case), and how many
  // entries they hold together.
  function booksOf(names) {
    var out = [], seen = {}, total = 0;
    for (var i = 0; i < (names || []).length; i++) {
      var b = isLoraName(names[i]) ? null : findBook(names[i]);
      if (!b || seen[b.name.toLowerCase()] || !b.entries.length) continue;
      seen[b.name.toLowerCase()] = 1;
      out.push(b);
      total += b.entries.length;
    }
    return { books: out, total: total };
  }

  // A random entry of several collections together, each entry as likely as any other; null: none.
  function pickFromBooks(names, rnd) {
    var got = booksOf(names);
    if (!got.total) return null;
    var i = pickIndex(got.total, rnd);
    for (var k = 0; k < got.books.length; k++) {
      var es = got.books[k].entries;
      if (i < es.length) return es[i];
      i -= es.length;
    }
    return null;
  }

  // One expansion for one image: p.template and p.negTemplate (what was typed) give p.prompt and p.negative.
  // A prompt that is exactly one __name__ takes the negative prompt of the entry it picked, before the typed
  // one. Used by generate, a page run (every image after the first) and the queue.
  // p.books (Random prompt for each image, 09h): the typed prompt is not used; a random entry of these
  // collections is this image's template (p.pickTemplate; p.pickNegTemplate is its negative prompt, then the
  // typed one), the same rule as POST prompts/expand with "books".
  function expandJob(p, pick) {
    var rnd = pick || Math.random;
    var ctx = { find: findBook, lora: function (took) { return loraWordsOf(p.loras, rnd, took); },
      named: function (name, r) {
        var e = loraByName(name);
        return e ? { path: e.path, words: loraPickWords(e, r) } : null;
      } };
    var negCtx = { find: findBook, lora: null, named: null };
    p.pickTemplate = undefined;
    p.pickNegTemplate = undefined;
    if (p.books && p.books.length) {
      var pe = pickFromBooks(p.books, rnd), neg = String(p.negTemplate || '');
      if (pe) {
        p.pickTemplate = pe.prompt;
        p.pickNegTemplate = joinParts(pe.negative || '', neg);
        p.prompt = expandText(pe.prompt, ctx, rnd);
        p.negative = joinParts(pe.negative ? expandText(pe.negative, negCtx, rnd) : '', expandText(neg, negCtx, rnd));
        return;
      }
    }
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

  function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

  // The note under the prompt while it has a placeholder: what each image picks from.
  function wildHintText(t) {
    var parts = [], stay = [], i, m, re = /\{[^{}|]*\|[^{}]*\}/g;
    var names = placeholderNames(t);
    for (i = 0; i < names.length; i++) {
      var n = names[i];
      if (isLoraName(n)) {
        var named = {}, nn = namedPlaceholders(t), q, lp = loraUsedPaths(loraSel), say = [];
        for (q = 0; q < nn.length; q++) { var ne = loraByName(nn[q]); if (ne) named[ne.path] = true; }
        for (q = 0; q < lp.length; q++) {
          var le = loraEntry(lp[q]);
          if (named[lp[q]] || !loraOnTexts(le).length) continue;
          say.push(loraName(lp[q]) + ': ' + loraPickText(le));
        }
        parts.push('__lora__ (' + (say.length ? say.join('; ') : 'no words') + ')');
        continue;
      }
      var b = findBook(n);
      if (b && b.entries.length) parts.push('__' + n + '__ (' + b.entries.length + ')');
      else if (booksOk && bookList) stay.push('__' + n + '__');
    }
    var lnames = namedPlaceholders(t), lstay = [];
    for (i = 0; i < lnames.length; i++) {
      var le2 = loraByName(lnames[i]);
      if (le2) parts.push('__lora:' + lnames[i] + '__ (' + (loraOnTexts(le2).length ? loraPickText(le2) : 'no words') + ')');
      else if (loraList) lstay.push('__lora:' + lnames[i] + '__');
    }
    var seen = {};
    while ((m = re.exec(t || ''))) {
      if (!seen[m[0]]) { seen[m[0]] = 1; parts.push(m[0].length > 24 ? '{' + m[0].slice(1, 20) + '…}' : m[0]); }
    }
    var out = parts.length ? 'Each image picks from ' + parts.join(', ') + '.' : '';
    if (stay.length) out += (out ? ' ' : '') + stay.join(', ') + ' matches no collection, so it stays as typed.';
    if (lstay.length) out += (out ? ' ' : '') + lstay.join(', ') + ' matches no LoRA, so it stays as typed.';
    return out;
  }

  // The note under the prompt while it has a placeholder.
  function drawWildHint() {
    var t = $('prompt').value, s = randomBooks() ? '' : wildHintText(t);
    text($('wild-text'), s);
    show('wild-hint', !!s, 'block');
  }
