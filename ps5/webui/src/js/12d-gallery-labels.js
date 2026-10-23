  // ---------- a name and tags for a file of the gallery ----------
  // Kept on the console next to the gallery (POST /ps5sd/v1/app/gallery/labels), not in the file: the
  // PNG stays as it was made. The search finds names and tags, and "#tag" is one tag.
  var labelFor = null;

  // Every tag in use, most used first: the ones the box offers.
  function tagsInUse() {
    var count = {}, out = [];
    for (var n in galleryLabels) {
      if (!galleryLabels.hasOwnProperty(n)) continue;
      var t = galleryLabels[n].tags || [];
      for (var i = 0; i < t.length; i++) count[t[i]] = (count[t[i]] || 0) + 1;
    }
    for (var k in count) if (count.hasOwnProperty(k)) out.push(k);
    out.sort(function (a, b) { return count[b] - count[a] || (a < b ? -1 : 1); });
    return out;
  }

  // "night, red brick , night" -> ["night", "red brick"]
  function splitTags(s) {
    var parts = String(s).split(','), out = [], seen = {};
    for (var i = 0; i < parts.length; i++) {
      var t = parts[i].replace(/^[\s#]+|\s+$/g, '').replace(/\s+/g, ' ');
      if (t && !seen[t.toLowerCase()]) { seen[t.toLowerCase()] = true; out.push(t); }
    }
    return out;
  }

  function openLabels() {
    var it = lbItem();
    if (!it) return;
    labelFor = it;
    $('ld-name').value = it.title || '';
    $('ld-tags').value = (it.tags || []).join(', ');
    text($('ld-file'), it.name);
    var box = $('ld-used');
    clear(box);
    var used = tagsInUse();
    for (var i = 0; i < used.length && i < 24; i++) {
      (function (tag) {
        var b = el('button', 'ltag', '#' + tag);
        b.type = 'button';
        b.onclick = function () {
          var now = splitTags($('ld-tags').value);
          for (var j = 0; j < now.length; j++) if (now[j].toLowerCase() === tag.toLowerCase()) return;
          now.push(tag);
          $('ld-tags').value = now.join(', ');
        };
        box.appendChild(b);
      })(used[i]);
    }
    show('ld-usedrow', used.length > 0);
    $('ld-save').disabled = false;
    show('ldlg', true, 'flex');
    $('ld-name').focus();
  }

  function closeLabels() {
    show('ldlg', false);
    labelFor = null;
    try { $('lb-label').focus(); } catch (e) {}
  }

  function saveLabels() {
    var it = labelFor;
    if (!it) return;
    var body = { name: it.name, title: $('ld-name').value.replace(/^\s+|\s+$/g, ''), tags: splitTags($('ld-tags').value) };
    $('ld-save').disabled = true;
    req('POST', APP + '/gallery/labels', body, function (code, data) {
      $('ld-save').disabled = false;
      if (code !== 200) { toast(errText(code, data, 'Could not save the name and tags'), 6000); return; }
      if (data && (data.title || (data.tags && data.tags.length))) galleryLabels[it.name] = { title: data.title || '', tags: data.tags || [] };
      else delete galleryLabels[it.name];
      labelItem(it);
      closeLabels();
      drawGallery();
      if (st.lb >= 0) {
        var i = galleryView().indexOf(it);
        if (i >= 0) showLb(i); else closeLb();
      }
    });
  }
