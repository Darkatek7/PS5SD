  // ---------- prompt history ----------
  // The last 50 prompts with their negative prompt and the model, kept on the console (GET, POST and DELETE
  // /ps5sd/v1/app/prompts) so every browser has them. A run adds its prompt as it starts; the History button
  // under the prompt opens the list, a click puts a prompt (and its negative prompt) back.
  var promptHist = null;   // [{id, prompt, negative, model, time}], the newest first; null: not asked yet
  var histOk = true;       // false: the app has no such route (404): the button stays hidden
  var HIST_MAX = 50;
  var panelTab = 'hist';   // the panel under the prompt: 'hist' or 'books' (09h)

  function drawHistButton() {
    show('prompt-hist', histOk && promptHist !== null, 'inline-flex');
    if (!histOk && !booksOk) show('phist', false);
  }

  function loadPromptHistory(then) {
    req('GET', APP + '/prompts', null, function (code, data) {
      if (code === 200 && data && data.prompts && data.prompts.length !== undefined) {
        promptHist = data.prompts;
        histOk = true;
      } else if (code === 404) {
        histOk = false;
      }
      drawHistButton();
      if ($('phist').style.display !== 'none' && panelTab === 'hist') drawHistory();
      if (then) then();
    });
  }

  function trimmed(s) { return String(s || '').replace(/^\s+|\s+$/g, ''); }

  // A prompt that goes into a run: put at the front of the list, on the console and here. The same prompt
  // (with the same negative prompt and model) as the newest one is not sent again.
  function rememberPrompt(p) {
    if (!histOk) return;
    var prompt = trimmed(p.template !== undefined ? p.template : p.prompt);
    var neg = trimmed(p.negTemplate !== undefined ? p.negTemplate : p.negative);
    var model = trimmed(st.label || st.model || '').substring(0, 80);
    if (!prompt) return;
    var top = promptHist && promptHist[0];
    if (top && top.prompt === prompt && top.negative === neg && top.model === model) return;
    req('POST', APP + '/prompts', { prompt: prompt, negative: neg, model: model }, function (code, data) {
      if (code === 404) { histOk = false; drawHistButton(); return; }
      if (code !== 201 || !data || !data.id) return;
      var list = promptHist || [], keep = [data];
      for (var i = 0; i < list.length; i++) if (list[i].id !== data.id) keep.push(list[i]);
      promptHist = keep.slice(0, HIST_MAX);
      drawHistButton();
      if ($('phist').style.display !== 'none' && panelTab === 'hist') drawHistory();
    });
  }

  function drawHistory() {
    var box = $('ph-list'), list = promptHist || [];
    clear(box);
    show('ph-none', !list.length, 'block');
    show('ph-clear', list.length > 0, 'inline-flex');
    for (var i = 0; i < list.length; i++) {
      (function (e) {
        var row = el('div', 'phrow');
        var use = el('button', 'phuse');
        use.type = 'button';
        use.title = 'Put this prompt back';
        use.appendChild(el('span', 'pht', e.prompt));
        var meta = [];
        if (e.negative) meta.push('Negative: ' + e.negative);
        if (e.model) meta.push(e.model);
        if (e.time) meta.push(when(e.time));
        if (meta.length) use.appendChild(el('span', 'phm', meta.join(' · ')));
        use.onclick = function () { useHistoryPrompt(e); };
        var x = el('button', 'phx', '×');
        x.type = 'button';
        x.title = 'Remove it from the history';
        x.setAttribute('aria-label', 'Remove this prompt from the history');
        x.onclick = function () { removeHistoryPrompt(e); };
        row.appendChild(use);
        row.appendChild(x);
        box.appendChild(row);
      })(list[i]);
    }
  }

  function histToggle() {
    if ($('phist').style.display !== 'none' && panelTab === 'hist') { histShow(false); return; }
    panelTab = 'hist';
    histShow(true);
  }

  function histShow(on) {
    show('phist', on, 'block');
    $('prompt-hist').setAttribute('aria-expanded', on && panelTab === 'hist' ? 'true' : 'false');
    $('prompt-books').setAttribute('aria-expanded', on && panelTab === 'books' ? 'true' : 'false');
    if (on) panelSwitch(histOk || !booksOk ? panelTab : 'books');   // draws the tab and asks for what another browser added
  }

  function useHistoryPrompt(e) {
    $('prompt').value = e.prompt;
    $('neg').value = e.negative || '';
    negSync();
    persist();
    histShow(false);
    var randOff = bkRand;
    if (randOff) bkRandSet(false);
    toast((e.negative ? 'The prompt and its negative prompt are back.' : 'The prompt is back.') +
      (randOff ? ' Random prompt is off now.' : ''));
    try { $('prompt').focus(); } catch (x) {}
  }

  function removeHistoryPrompt(e) {
    req('DELETE', APP + '/prompts/' + e.id, null, function (code, data) {
      if (code !== 200 && code !== 404) { toast(errText(code, data, 'Could not remove the prompt'), 5000); return; }
      var keep = [];
      for (var i = 0; i < (promptHist || []).length; i++) if (promptHist[i].id !== e.id) keep.push(promptHist[i]);
      promptHist = keep;
      drawHistory();
    });
  }

  function clearHistory() {
    if (!promptHist || !promptHist.length) return;
    if (!window.confirm('Remove all ' + promptHist.length + ' prompts from the history?')) return;
    req('DELETE', APP + '/prompts', null, function (code, data) {
      if (code !== 200) { toast(errText(code, data, 'Could not empty the history'), 5000); return; }
      promptHist = [];
      drawHistory();
    });
  }
