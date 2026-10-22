  // ---------- help ----------
  // docs/MODELS.md and docs/API.md as the title carries them, through a small
  // Markdown renderer: headings, paragraphs, lists, tables, code, links. All
  // text is escaped; a link to another file of the repository goes to GitHub.
  var DOCS = { models: 'MODELS.md', api: 'API.md' };
  var DOC_REPO = 'https://github.com/Darkatek7/PS5SD/blob/main/';
  var docName = 'models';
  var docText = {};

  function esc(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function docLink(label, url) {
    var path = url.split('#')[0];
    var hash = url.indexOf('#') >= 0 ? url.slice(url.indexOf('#')) : '';
    for (var k in DOCS) {
      if (DOCS.hasOwnProperty(k) && (path === DOCS[k] || path === 'docs/' + DOCS[k])) {
        return '<a href="#help" data-doc="' + k + '">' + label + '</a>';
      }
    }
    var href = /^https?:\/\//i.test(url) ? url
      : path.indexOf('../') === 0 ? DOC_REPO + path.slice(3)
      : DOC_REPO + (path.indexOf('docs/') === 0 ? path : 'docs/' + path) + hash;
    return '<a href="' + esc(href) + '" target="_blank" rel="noopener">' + label + '</a>';
  }

  function mdInline(s) {
    var parts = s.split('`');
    var out = '';
    for (var i = 0; i < parts.length; i++) {
      if (i % 2 === 1 && i < parts.length - 1) {
        out += '<code>' + esc(parts[i]) + '</code>';
        continue;
      }
      var t = esc(i % 2 === 1 ? '`' + parts[i] : parts[i]);
      t = t.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (m, label, url) {
        return docLink(label, url.replace(/&amp;/g, '&'));
      });
      out += t.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    }
    return out;
  }

  // A table row's cells; a | inside `code` is not a border.
  function mdCells(line) {
    var cells = [];
    var cur = '';
    var code = false;
    line = line.replace(/^\s*\|/, '').replace(/\|\s*$/, '');
    for (var i = 0; i < line.length; i++) {
      var c = line.charAt(i);
      if (c === '`') code = !code;
      if (c === '|' && !code) { cells.push(cur); cur = ''; } else cur += c;
    }
    cells.push(cur);
    return cells;
  }

  function mdRender(text) {
    var lines = text.replace(/\r/g, '').split('\n');
    var html = '';
    var para = [];
    var lists = [];  // open lists: {indent, tag}
    var i, m;
    function flush() {
      if (para.length) html += '<p>' + mdInline(para.join(' ')) + '</p>';
      para = [];
    }
    function closeLists(indent) {
      while (lists.length && lists[lists.length - 1].indent >= indent) html += '</li></' + lists.pop().tag + '>';
    }
    for (i = 0; i < lines.length; i++) {
      var line = lines[i];
      var indent = line.match(/^ */)[0].length;
      if (/^\s*```/.test(line)) {
        flush();
        closeLists(0);
        var code = [];
        for (i++; i < lines.length && !/^\s*```/.test(lines[i]); i++) code.push(lines[i]);
        html += '<pre><code>' + esc(code.join('\n')) + '</code></pre>';
      } else if ((m = /^(#{1,4})\s+(.*)$/.exec(line))) {
        flush();
        closeLists(0);
        html += '<h' + m[1].length + '>' + mdInline(m[2]) + '</h' + m[1].length + '>';
      } else if (/^\s*\|/.test(line)) {
        flush();
        closeLists(0);
        var rows = [];
        for (; i < lines.length && /^\s*\|/.test(lines[i]); i++) rows.push(lines[i]);
        i--;
        html += '<div class="tw"><table>';
        for (var r = 0; r < rows.length; r++) {
          if (r === 1 && /^[\s|:\-]+$/.test(rows[r])) continue;
          var cells = mdCells(rows[r]);
          var tag = r === 0 ? 'th' : 'td';
          html += '<tr>';
          for (var c = 0; c < cells.length; c++) html += '<' + tag + '>' + mdInline(cells[c].replace(/^\s+|\s+$/g, '')) + '</' + tag + '>';
          html += '</tr>';
        }
        html += '</table></div>';
      } else if ((m = /^(\s*)([-*]|\d+\.)\s+(.*)$/.exec(line))) {
        flush();
        var ltag = /\d/.test(m[2]) ? 'ol' : 'ul';
        while (lists.length && lists[lists.length - 1].indent > indent) html += '</li></' + lists.pop().tag + '>';
        var top = lists[lists.length - 1];
        if (top && top.indent === indent && top.tag === ltag) {
          html += '</li><li>';
        } else {
          if (top && top.indent === indent) html += '</li></' + lists.pop().tag + '>';
          lists.push({ indent: indent, tag: ltag });
          html += '<' + ltag + '><li>';
        }
        html += mdInline(m[3]);
      } else if (/^\s*$/.test(line)) {
        flush();
      } else if (lists.length && indent > 0) {
        html += ' ' + mdInline(line.replace(/^\s+/, ''));
      } else {
        closeLists(0);
        para.push(line.replace(/^\s+/, ''));
      }
    }
    flush();
    closeLists(0);
    return html;
  }

  // The views of the Help tab: 'page' is the static text of this page (#doc-notes, no request).
  function showDoc(name) {
    docName = name;
    var views = ['page', 'models', 'api'];
    for (var v = 0; v < views.length; v++) $('doc-' + views[v]).className = views[v] === name ? 'on' : '';
    show('doc-notes', name === 'page');
    show('doc', name !== 'page');
    if (name === 'page') return;
    if (docText[name] !== undefined) {
      $('doc').innerHTML = mdRender(docText[name]);
      return;
    }
    text($('doc'), 'Loading');
    var x = new XMLHttpRequest();
    x.open('GET', '/ps5sd/v1/app/docs/' + DOCS[name], true);
    x.onreadystatechange = function () {
      if (x.readyState !== 4) return;
      if (x.status === 200) {
        docText[name] = x.responseText;
        if (docName === name) $('doc').innerHTML = mdRender(x.responseText);
      } else if (docName === name) {
        text($('doc'), 'This version of the PS5SD app does not carry its docs. They are on GitHub (link above).');
      }
    };
    x.send();
  }

  function onDocClick(e) {
    e = e || window.event;
    var a = e.target || e.srcElement;
    while (a && a !== this && a.tagName !== 'A') a = a.parentNode;
    if (a && a.getAttribute && a.getAttribute('data-doc')) {
      showDoc(a.getAttribute('data-doc'));
      window.scrollTo(0, 0);
      if (e.preventDefault) e.preventDefault();
      return false;
    }
    return true;
  }

  // The ⓘ buttons: data-help="<id of a section of This page>" open the Help tab at that section.
  function helpAt(id) {
    showDoc('page');
    setView('help');
    var at = $(id);
    if (at && at.scrollIntoView) at.scrollIntoView(true);
  }

  function initHelpLinks() {
    document.addEventListener('click', function (e) {
      e = e || window.event;
      var t = e.target || e.srcElement;
      while (t && t !== document && !(t.getAttribute && t.getAttribute('data-help'))) t = t.parentNode;
      if (!t || t === document) return;
      helpAt(t.getAttribute('data-help'));
      if (e.preventDefault) e.preventDefault();
    });
  }

