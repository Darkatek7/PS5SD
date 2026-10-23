  // ---------- upload ----------
  // PUT with the file as the body; the app passes it on to ftpsrv as it comes.
  var upXhr = null;

  function upStart() {
    var input = $('up-file');
    var f = input.files && input.files[0];
    input.value = '';
    if (!f || upXhr) return;
    var kind = $('up-kind').value;
    var name = f.name;
    var okName = kind === 'presets' ? /\.txt$/i : kind === 'upscalers' ? /\.(pth|pt|safetensors|gguf)$/i : kind === 'tags' ? /\.(csv|txt)$/i
      : /\.(gguf|safetensors|ckpt|sft)$/i;
    if (!okName.test(name)) {
      toast(kind === 'presets' ? 'A preset is a .txt file.' : kind === 'upscalers' ? 'An upscaler is a .pth, .pt, .safetensors or .gguf file.'
        : kind === 'tags' ? 'A tag file is a .csv (as a1111-sd-webui-tagcomplete has them) or a .txt file.'
        : 'Only .gguf, .safetensors, .ckpt and .sft files.', 6000);
      return;
    }
    var x = new XMLHttpRequest();
    var t0 = Date.now();
    upXhr = x;
    text($('up-name'), name);
    $('up-fill').style.width = '0%';
    text($('up-meta'), 'Starting');
    show('up-box', true);
    $('up-pick').disabled = true;
    $('up-kind').disabled = true;
    x.upload.onprogress = function (e) {
      if (!e.lengthComputable) return;
      var s = (Date.now() - t0) / 1000;
      $('up-fill').style.width = (100 * e.loaded / e.total).toFixed(1) + '%';
      text($('up-meta'), bytes(e.loaded) + ' of ' + bytes(e.total) + (s > 1 ? ', ' + (e.loaded / 1e6 / s).toFixed(1) + ' MB/s' : ''));
    };
    x.onreadystatechange = function () {
      if (x.readyState !== 4) return;
      var data = null;
      try { data = JSON.parse(x.responseText); } catch (e) {}
      upDone(x.status, data, name, kind);
    };
    x.open('PUT', '/ps5sd/v1/app/uploads/' + kind + '/' + encodeURIComponent(name), true);
    x.setRequestHeader('Content-Type', 'application/octet-stream');
    x.setRequestHeader('X-PS5SD-Request', '1');
    x.send(f);
  }

  function upDone(code, data, name, kind) {
    if (!upXhr) return;
    upXhr = null;
    show('up-box', false);
    $('up-pick').disabled = false;
    $('up-kind').disabled = false;
    if (code === 200) {
      var listed = kind === 'models' || kind === 'presets' || kind === 'loras' || kind === 'upscalers';
      toast(name + ' is on the console' + (listed ? ' and in the list.' : kind === 'tags' ? '; the prompt fields suggest its tags.' : '.'), 6000);
      if (kind === 'loras') loadLoras(null);
      else if (kind === 'upscalers') { loadUpscalers(); loadInfo(); }
      else if (listed) loadModels(null);
      else if (kind === 'tags') tagsCheck();
    } else if (code === 0) {
      toast('The upload of ' + name + ' stopped.', 6000);
    } else {
      toast((data && data.error) || ('The upload failed (' + code + ').'), 8000);
    }
  }

