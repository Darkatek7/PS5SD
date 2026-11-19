  // ---------- a name, tags and a star for a file of the gallery ----------
  // Kept on the console next to the gallery (POST /ps5sd/v1/app/gallery/labels), not in the file: the
  // PNG stays as it was made. The search finds names and tags, and "#tag" is one tag. The star ("fav")
  // is a favourite: the gallery's filter Favourites shows those.
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
      // the star is not part of this dialog: the answer says whether the file has it (an older app: as it was)
      var fav = data && typeof data.fav === 'boolean' ? data.fav : !!it.fav;
      if (data && (data.title || (data.tags && data.tags.length) || fav)) galleryLabels[it.name] = { title: data.title || '', tags: data.tags || [], fav: fav };
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

  // The star of a file: a button (class cls) that shows whether it is a favourite and toggles it.
  function favButton(it, cls) {
    var b = el('button', cls + (it.fav ? ' starred' : ''), it.fav ? '★' : '☆');
    b.type = 'button';
    b.title = it.fav ? 'Remove from the favourites' : 'Add to the favourites';
    b.setAttribute('aria-label', b.title);
    b.setAttribute('aria-pressed', it.fav ? 'true' : 'false');
    b.onclick = function (e) {
      e = e || window.event;
      if (e.stopPropagation) e.stopPropagation();
      toggleFav(it);
      return false;
    };
    return b;
  }

  // The star in the viewer: shown where the app keeps favourites.
  function drawLbFav(it) {
    var b = $('lb-fav');
    show('lb-fav', galleryFavs, 'inline-block');
    text(b, (it.fav ? '★ ' : '☆ ') + 'Favourite');
    b.className = it.fav ? 'starred' : '';
    b.setAttribute('aria-pressed', it.fav ? 'true' : 'false');
    b.title = it.fav ? 'Remove from the favourites' : 'Add to the favourites';
  }

  var favBusy = false;
  // Turns the star of a file on or off (POST gallery/labels with "fav" only: the name and tags stay).
  function toggleFav(it) {
    if (favBusy) return;
    favBusy = true;
    var want = !it.fav;
    req('POST', APP + '/gallery/labels', { name: it.name, fav: want }, function (code, data) {
      favBusy = false;
      if (code !== 200 || !data) { toast(errText(code, data, 'Could not change the star'), 6000); return; }
      if (typeof data.fav !== 'boolean') {
        // an app from before 1.6.0 answers without the field: it does not keep stars
        galleryFavs = false;
        drawGallery();
        if (st.lb >= 0) drawLbFav(lbItem());
        toast('This PS5SD is too old to keep favourites. Update it.', 6000);
        return;
      }
      it.fav = data.fav;
      if (data.fav || data.title || (data.tags && data.tags.length)) galleryLabels[it.name] = { title: data.title || '', tags: data.tags || [], fav: data.fav };
      else delete galleryLabels[it.name];
      drawGallery();
      if (st.lb >= 0) {
        // in the Favourites filter an item that loses its star leaves the viewer's list
        var cur = lbItem();
        if (cur === it) drawLbFav(it);
        else { var i = galleryView().indexOf(it); if (i >= 0) showLb(i); else showLb(st.lb); }
      }
    });
  }
