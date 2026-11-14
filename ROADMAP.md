# Roadmap

What is planned for PS5SD, in order. Each version is one release in the homebrew store. Ideas
and wishes go into [issues](https://github.com/Darkatek7/PS5SD/issues); this file says what comes next and
why.

The goal for 1.0: a title that someone who has never heard of stable-diffusion.cpp can install, open on a
phone, and get good images and videos from without reading anything first; and an API that other programs
can rely on.

## Done

- **1.1.1**: a preset keeps a LoRA whose name has spaces or signs in it; width and height take any size from
  64 to 4096; the home screen background is back (1.1.0 had a plain gradient).
- **1.1.0**: the stable-diffusion.cpp of November (upstream `32d516c`); images carry its new generation info,
  which the gallery reads beside the old; the times and memory are those of 1.0.
- **1.0.0**: an update keeps everything (tested from 0.5.0 on), a soak test, the docs read against the
  title with new screenshots, a home screen background of its own.
- **0.9.18**: the open measurements; a guide image with the hires fix no longer ends PS5SD; the fit hint
  says what to lower and knows a guide image.
- **0.9.17**: an OpenAPI description of every route; a Close from the home screen while the main loop hangs
  closes cleanly.
- **0.9.16**: a model switch without stopping the server; options waits for the model; the model of an
  OpenAI request; the switch in the event stream.
- **0.9.15**: one shape for every error answer, with a code; the jobs and their steps as server-sent events
  (`GET /ps5sd/v1/events`), which the web UI uses.
- **0.9.14**: no model Delete among the presets, a Fields box and help link in Edit text, suggestions while
  typing a preset (issue 10).
- **0.9.13**: a star per preset, Update preset, a Presets list with Apply settings, no header bar on a PC,
  images without their settings in the file (issues 10 and 12).
- **0.9.12**: presets under their model with a default for each, Edit text for every preset, the time
  and model in the image, Use all in the viewer, LoRAs on and off, several uploads at once (issues 10 and 11).
- **0.9.11**: the web UI: a Create tab without scrolling, a denser look, light and dark, a sidebar that
  folds, Delete under the result, CLIP skip as a number (issues 7, 8 and 9).
- **0.9.10**: fan modes, Quiet, Balanced, Cool and Manual: the fan starts earlier, and long runs stay at
  about 84 °C instead of up to 95 °C.
- **0.9.9**: loading weights takes half the CPU memory (Klein 9B with a LoRA runs right after a start); a run
  out of memory tries once more; the cause of an end in `app.log`.
- **0.9.8**: Klein 9B LoRAs in diffusers names load fully; a run out of CPU memory fails instead of ending
  PS5SD; less CPU memory per prompt.
- **0.9.7**: FLUX.2 Klein 9B in the catalog, featured: 1024x1024 in about 50 s, with its LoRAs.
- **0.9.6**: video from a picture with Wan 2.2 TI2V 5B, Flow shift on the Create tab, and CLIP skip for
  every model that reads the prompt with CLIP.
- **0.9.5**: a zoom in the gallery viewer, copy buttons, names and tags for images, images that are not
  kept on the console, CLIP skip, prompt starters in a preset and Danbooru tag completion
  ([issue 6](https://github.com/Darkatek7/PS5SD/issues/6)).
- **0.9.4**: downloads and uploads work with ftpsrv up to 0.15 (PASV), or through PS5SD's own copy when
  the one on 2121 cannot write; a file that is only a part of a model is marked and says what it needs,
  and a model that does not load says why ([issue 5](https://github.com/Darkatek7/PS5SD/issues/5)).
- **0.9.3**: the console's temperature, power, fan and load on the TV, in the web UI's header and as charts
  in Settings, through ps5-exporter, which the title carries and starts.
- **0.9.2**: SDXL in 30 s instead of 70 (dpm++2m with the `ays` schedule at 10 steps, larger VAE tiles,
  the weights loaded at the start), and DMD2, Lightning and CausVid set their own settings when added.
- **0.9.1**: videos of up to 81 frames (5 s); the video decode in half the time (larger tiles that overlap
  less) and Wan 2.1 with TAEHV, a small decoder; the gallery viewer on a phone.
- **0.9.0**: edit with a reference image (FLUX.2 Klein); gallery thumbnails, the settings saved in each PNG
  with **Use these settings**, search, order and multi-select, and the gallery in pages in the API; GPU
  memory that earlier runs freed usable again (buffers of several blocks, `patches/mesa/0010`).
- **0.8.1**: FLUX.2 Klein 4B in the catalog; runs that do not fit in one piece run in segments (SDXL at
  2048x2048 after other images, FLUX.2 Klein 4B at 1536x1536); a hint before a run that may not fit; the
  family of a model or LoRA read from its tensors; the tabs as a sidebar on a PC.
- **0.8.0**: a clearer web UI: five tabs (Create, Gallery, Models, Settings, Help) with the tabs at the
  bottom on a phone; the Create tab with the model picker in place, one mode at a time, settings in groups,
  the result next to them, a count next to Generate that queues any number of images; the Models tab in
  Installed, Get more and Downloads with readable names and tags; the catalog with the measured time per
  image; a gallery filter for images and videos; the long explanations in Help.
- **0.7.1**: the hires fix at ×1 (a refine pass at the same size), a reason for every failed run (out of
  memory, the last error, cancelled) in the web UI, the API and on the TV, whole error lines in `app.log`,
  and no sideways scrolling on a phone.
- **0.7.0**: ControlNet with SDXL (a guide image on the Create tab, Canny on the console, SDXL + Canny in
  the catalog), and the CausVid LoRA: a 2-second video in 3 minutes 40 s instead of about 14.5 minutes.
- **0.6.0**: video with Wan 2.1 T2V 1.3B (WebM, played in the page and the gallery; in the catalog),
  and DMD2 and SDXL-Lightning one click away on the LoRAs panel.
- **0.5.2**: the TV shows the console's own address and a QR code that opens the web UI, with the API key
  in it while the TV shows the key; after
  [Machete PS5SD](https://github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID) by VenezuelanBiggie24.
- **0.5.1**: a loaded preset's settings are the API's defaults too (from
  [#4](https://github.com/Darkatek7/PS5SD/pull/4), by nikay99); the A1111 and OpenAI model lists show
  models only; `txt2img` without a size takes the default size.
- **0.5.0**: image to image and inpainting (a start image, a mask painted in the page, only the masked area
  made at the model's size); SDXL and SD 1.5 inpainting models in the catalog.
- **0.4.0**: the hires fix and the Upscale button (ESRGAN); two upscalers one click away.
- **0.3.1**: an API key for the whole API; a Presets tab.
- **0.3.0**: batch, own size presets, no image on the TV, a done signal
  ([#3](https://github.com/Darkatek7/PS5SD/issues/3)).
- **0.2.0**: LoRAs, several per model, each with its own strength.
- **0.1.0**: the title with server, web UI, model downloader, uploads from the PC, gallery, Help tab, TV
  dashboard.

## 0.7.1: what [#3](https://github.com/Darkatek7/PS5SD/issues/3) asked for, done

Small, and promised in the issue.

- [x] **Hires fix at ×1**: a second pass at the same size, to refine an image without making it larger.
  sd.cpp takes a scale of 1; at 832x1216 with 10 steps and a strength of 0.35 it adds about 27 s.
- [x] **Say why a run failed**: "generate_image returned no results" tells nobody anything. Now a run
  that runs out of memory says so and what to lower; other failures give the server's last error, on the
  web UI, in the API and on the TV. `app.log` keeps whole error lines again (their start was written over).
- [x] **No sideways scrolling on a phone**: the tab bar was wider than a phone screen, so Help was cut off
  and the whole page scrolled sideways; now the bar scrolls inside itself.

## 0.8.0: a clearer web UI, done

The web UI grew one feature at a time, and it shows. Measured on 0.7.0: the Create tab's Settings drawer
is 2,329 px tall, with 29 fields, 47 buttons and 14 hint paragraphs in one list, where the steps of an
image sit next to the API key and the sound. The Models tab is one list of eight panels. This version
changes how the page is built, not what it can do; every feature stays.

**Where things are**

- [x] **Five tabs: Create, Gallery, Models, Settings, Help.** The Presets tab goes into Create (pick a
  preset at the top) and Models (manage them).
- [x] **A Settings tab** for everything about the app, not the image: the done signal, the TV (images,
  API key), the Upscale button's model, the API key, and the version and sd.cpp build. The Create tab keeps
  only what makes the image.
- [x] **On a phone, the tabs sit at the bottom** of the screen, where a thumb reaches them, and the
  Generate button stays in view while the settings scroll.

**The Create tab**

- [x] **The model is picked where it is used**: a list of the installed models (with what each is: SDXL,
  FLUX, video, ControlNet) opens on the Create tab instead of jumping to the Models tab. A switch shows its
  progress in place.
- [x] **One mode at a time**: Image, Edit (a start image, with the mask), Guide (ControlNet, with a model
  that has one) and Video (with a video model). The mode shows only its own fields, so a video model never
  shows the hires fix and an SD 1.5 model never shows ControlNet.
- [x] **Settings in groups**: Size, Quality (steps, CFG, sampler, scheduler), Style (LoRAs), Hires fix,
  Seed and batch. Each group is closed by default and shows its values in one line, as the Settings
  summary does now. The fields of a group open in place.
- [x] **The result is the biggest thing on the page**: on a PC it stays in view next to the settings, at
  the size of the window; on a phone it comes first after a run.
- [x] **Progress that means something**: the step, the time per step and the time left; for video, which
  part is running (text, steps, decoding).
- [x] **Less text on the page**: a hint is shown where a choice needs one (a LoRA of the wrong family, a
  size that will not fit), not as a paragraph under every field. The long explanations go to Help, one tap
  away (an ⓘ next to the field).
- [x] **Keys**: Ctrl+Enter generates, Escape closes the viewer, menus and boxes (it does not cancel a run:
  too easy to press by mistake), the arrows step through the gallery.

**The Models tab**

- [x] **Three parts: Installed, Get more, Downloads.** Installed lists models, LoRAs and upscalers, each in
  its own group; Get more has the catalog, a link, an upload and the Hugging Face search; Downloads shows
  what is running, and the finished ones can be cleared.
- [x] **Names a person reads**: "iLustMix v11.1", not `ilustmix_v111`; a tag for the family (SD 1.5, SDXL,
  FLUX, Z-Image, Wan) and one for what it does (video, ControlNet, inpainting); "Model" and "Settings for a
  model" instead of "Checkpoint" and "Model with options".
- [x] **The catalog says what each model is good at**: photos, anime, fast, video; with the time an image
  takes on the console, measured (not yet for the ControlNet and the two inpainting entries).
- [x] **Delete is out of the way**: in a menu on each entry, not a button as large as Use.

**Under the page**

- [x] **The web UI in parts**: `ps5/webui/index.html` is 4,500 lines of HTML, CSS and script in one file.
  It becomes a few files (`app.css`, one script per tab) that the build puts back into one page, as
  `embed-file.sh` embeds it now; still no npm and no framework.
- [x] **Screenshots in the README and the store listing**, taken from the new page on a phone and a PC.

## 0.8.1: memory and LoRAs, done

- [x] **Say before a run that it may not fit**: measured per model family right after a start, and against
  the largest run since the model loaded (freed GPU memory is not all usable again). A hint under Generate,
  `POST /ps5sd/v1/app/fit` in the API. It never blocks a run.
- [x] **Run in segments when one piece does not fit**: sd.cpp planned a whole graph that then found no
  room; now it is built again in segments (`patches/sdcpp/0067`). FLUX.3 Klein has no segments in sd.cpp,
  so Q6_K stays at 1024x1024 at most.
- [x] **LoRAs by their tensors**: the family (FLUX.1, FLUX.2 Klein 4B, SDXL, Wan 1.3B, ...) from the shapes,
  not the name or the metadata, which is wrong for some FLUX.1 LoRAs.
- [x] **No double entries**: a file that a model's settings load is not listed again.
- [x] **FLUX.2 Klein 4B** (Q8_0) in the catalog: 1024x1024 in 4 steps, about 30 s.
- [x] **The tabs as a sidebar** on a PC.

## 0.9.0: gallery, editing by a sentence and memory, done

**Gallery**

- [x] **Thumbnails**: small copies made when an image is saved, so a gallery of hundreds of images and
  4096 px upscales opens fast, on the console's own browser too. A JPEG of 320 px; older PNGs get theirs
  when the gallery first shows them, videos from their first frame.
- [x] **What made an image**: the prompt, model, seed and settings saved with each image (in the PNG, as
  A1111 does, so other tools read them too), shown in the viewer, with **Use these settings** to make it
  again or change it.
- [x] **Find and tidy**: newest or oldest first, a search in the prompts, and several images selected for
  one delete or download (images or videos only is in 0.8.0).
- [x] **The gallery in pages** in the API, with each image's prompt and settings (`offset`, `order`, `q`,
  `type`, and `/ps5sd/v1/gallery/info/<name>`).

**Memory**

- [x] **Freed GPU memory usable again**: it goes back to the pool, but the pool falls apart in pieces (after
  the first image, 7.2 GiB free in 13 parts, the largest 4.2 GiB; docs/CONSOLE.md, "Why freed GPU memory is
  not usable again"), and each GPU buffer needed one block. A buffer is now made of several when no block
  holds it, and the heap budget is the sum of the free parts (`patches/mesa/0010`): FLUX.2 Klein 4B runs
  1536 and 1280 in one piece after 1024, and 2048; SDXL 2048 after 1024 and 1536.

**Images**

- [x] **Edit with a reference image** (FLUX.2 Klein): a picture and a sentence ("make it night", "the house in
  red brick") instead of a mask. The Create tab's Edit mode sends the start image as a reference for a
  FLUX.2 model ("Edit by the prompt"), and `ref_images` does it in the API. About 65 s at 4 steps with Klein 4B.

## 0.9.1: video, done

**Gallery**

- [x] **The viewer on a phone**: the picture across the whole width with the arrows on it, the whole prompt
  under it, and the actions three to a row below, instead of a bar of buttons squeezed above the picture.
  The thumbnail shows at once and the image replaces it when it is in.

**Video**

- [x] **Longer clips (49 and 81 frames)**: measured with CausVid at 4 steps, the scene kept to the last
  frame; the web UI offers up to 81 (5 s).
- [x] **Faster decoding**: the VAE decode was about 2 of the 3 minutes 40 s. Now 256 px tiles that overlap
  by a quarter (33 frames in 2 min 15 s, 81 in 8 min), and a catalog entry with TAEHV, a small decoder
  (1 min 30 s and 6 min, a little softer).

## 0.9.2: faster SDXL, done

- [x] **SDXL presets**: dpm++2m with the `ays` schedule at 10 steps instead of euler_a at 25, the same
  picture; 640 px VAE tiles (1024x1024 in 4 tiles instead of 9); `--eager-load`, so that the first image
  after a switch is not slower. 68 s an image before, 29 s now; the presets of 0.9.1 are updated when the
  title starts.
- [x] **Fast LoRAs set their settings**: adding DMD2 sets `lcm`, 4 steps and CFG 1 (11 s an SDXL image),
  Lightning, CausVid and LCM-LoRA theirs; removing it brings the old ones back.
- [x] Measured in [CONSOLE.md](docs/CONSOLE.md#faster-sdxl-2026-10-22): which LoRA suits which finetune,
  the VAE tiles at 1024 to 2048 px.

## 0.9.3: the console on the TV and in the web UI, done

- [x] **Temperature, power, GPU and CPU load, fan**: on the TV under the memory, in the web UI's header and
  as charts in Settings (2 minutes, 1 hour), and as `GET /ps5sd/v1/app/console`. Temperature, the SoC's
  power and the fan's duty through ps5-exporter, which PS5SD carries and starts through the ELF loader;
  CPU load from `getrusage`; "GPU busy" is the share of the time a job runs. A fan speed is not
  readable, only its duty.

## 0.9.5: gallery, prompts and presets ([issue 6](https://github.com/Darkatek7/PS5SD/issues/6)), done

What blurymind asked for in issue 6. Some of it is in 0.9.0 already: the prompt, model, seed and settings in
each PNG, shown in the viewer with **Use these settings**, a search in the prompts, and several images
selected for one delete or download.

**Gallery**

- [x] **Zoom in the viewer**: the wheel or a pinch zooms, a drag moves the picture, a double tap or click
  switches between the whole picture and 100 %.
- [x] **Copy the prompt**: buttons for the prompt, the negative prompt and the whole settings text, as other
  tools paste it.
- [x] **More of the recipe in the PNG**: the preset's name and the LoRAs with their strengths, besides what
  0.9.0 writes; shown in the viewer.
- [x] **Names and tags**: a name and a few tags for each image, kept on the console next to the gallery
  (the PNG stays as it was made); the search finds names and tags, and a tag filters the gallery.
- [x] **Images that are not kept**: a choice on the Create tab, "Don't keep on the console". The image comes
  back to the page with Download and **Keep on the console**, is saved under a `tmp_` name that the title
  deletes when it starts, and is not in the gallery.

**Presets and prompts**

- [x] **CLIP skip**: a field in the Quality group and `clip_skip` in `# defaults:`. sd-server takes it
  already, and sd.cpp uses 2 for SDXL when nothing is set, so Pony models run right today. The field
  shows for SD 1.5 models only: with 1 an SDXL model makes noise (measured), so the server ignores a 1
  for every model that is not SD 1.x.
- [x] **Prompt starters in a preset**: `# prompt:` and `# negative:` lines whose text is put before the
  prompt (Pony's `score_9, score_8_up, ...`), shown above the prompt field, and used by the API when that
  preset is loaded.
- [x] **Copy** buttons at the prompt fields. Paste only where the browser allows it: none reads the
  clipboard for a page over plain HTTP, as the console serves it.
- [x] **Tag completion**: Danbooru tags suggested while typing, from tag files you put in
  `/data/PS5SD/tags` (the CSV of a1111-sd-webui-tagcomplete). PS5SD brings no tag list.

## 0.9.6: image to video, done

- [x] **Image to video** with Wan 2.2 TI2V 5B (Q8_0, 5 GB, and the Wan 2.2 VAE, 1.3 GB): a start image on the
  Create tab when a video model takes one, sent as `init_image`. Measured on the console: it fits (about
  10 GB of weights, loaded in parts); 832x480 with 33 frames takes 11.7 s a step and 88 s to decode. A start
  image works at any size; text alone only at the model's own 1280x704 (smaller comes out washed out), so
  the catalog entry defaults to 1280x704 and Wan 2.1 stays the model for text to video.
- [x] **Flow shift on the Create tab**, for video models; set in the preset only so far (3 for Wan 2.1,
  5 for Wan 2.2 TI2V 5B).
- [x] **CLIP skip for every model** ([issue 6](https://github.com/Darkatek7/PS5SD/issues/6#issuecomment-6364558574)):
  the field shows for all of them and says what Auto is for the loaded one ("Auto (2)" for SDXL); no 1 for
  a model that is not SD 1.x.

## 0.9.7: FLUX.2 Klein 9B, done

- [x] **FLUX.2 Klein 9B** (Q4_0, with Qwen3-8B Q4_K_M, 11 GB) in the catalog, featured: 1024x1024 in 4 steps
  in about 50 s (100 s right after a start), with its LoRAs. Measured on the console: it fits, with 0.9 GiB
  left in one block after a 1024x1024 image. FLUX Non-Commercial License.

## 0.9.8: LoRAs and memory, done

- [x] **FLUX.2 LoRAs in diffusers names** (PEFT): sd.cpp applied only their attention layers (128 of 280
  tensors); `patches/sdcpp/0070` maps the rest, and the Models tab shows them as FLUX.2 Klein 9B.
- [x] **Out of memory ends a run, not PS5SD**: a series of Klein 9B images ended the title when the CPU heap
  could not grow while the weights were loaded again. `patches/sdcpp/0071` fails the run with a reason.
- [x] **One prompt cached**, not four: about 30 MB less CPU memory per prompt with Klein 9B.

## 0.9.9: stability, done

- [x] **Weights loaded in pieces** (`patches/sdcpp/0073`): the loader's threads read each tensor whole into
  a buffer of their own, up to 85 MB each with Klein 9B; now 8 MB pieces. The CPU heap's peak went from
  1.33 to 0.72 GiB, and Klein 9B with a LoRA no longer runs out of memory right after a start.
- [x] **One more try after out of memory** (`patches/sdcpp/0072`), unless the client stopped the run.
- [x] **The cause of an end in `app.log`**: a `std::terminate` handler writes the exception and the memory;
  the downloader's and the console readings' threads log an exception instead of ending the title.

## 0.9.10: fan modes, done

- [x] **Fan modes** (`ps5/fan`): a payload lowers the console's target temperature, one degree every 20 s,
  and puts it back when PS5SD closes or the mode is Quiet. Balanced is 3 °C under the console's own, Cool
  6 °C, Manual from 70 °C. Measured in [docs/CONSOLE.md](docs/CONSOLE.md#the-fan-2026-10-24).

## 0.9.11: the web UI, denser and with everything in reach ([issue 7](https://github.com/Darkatek7/PS5SD/issues/7)), done

- [x] **Create without scrolling** on a PC and a tablet: the settings scroll inside their panel with
  Generate fixed at its foot, the picture fills the rest and its actions stay under it. On a phone Generate
  sits above the tab bar.
- [x] **Apple's look**: the system font, a denser scale on a PC and a phone (the PS5's browser keeps the
  large one its cursor needs), grouped surfaces, segmented controls, feedback on the press.
- [x] **Light and dark** (Settings, Appearance: Auto, Light, Dark), kept in the browser. The viewer stays
  dark.
- [x] **The sidebar folds to its icons** on a PC; the prompt grows with its text.
- [x] **Delete under the result** ([issue 8](https://github.com/Darkatek7/PS5SD/issues/8)), with a second
  press to be sure.
- [x] **CLIP skip as a number** ([issue 9](https://github.com/Darkatek7/PS5SD/issues/9)): 1 to 12 typed or
  with − and +, empty for Auto.
- [x] **Tested in the PS5's own browser**: all five tabs.

## 0.9.12: presets and LoRAs ([issue 10](https://github.com/Darkatek7/PS5SD/issues/10), [issue 11](https://github.com/Darkatek7/PS5SD/issues/11)), done

- [x] **Presets under their model**: Models lists each model once with its presets; the Create tab's picker
  shows the same. Every preset has Use, Edit text, Make default and Delete.
- [x] **A default preset per model** (★), kept on the console in `default_presets`; Use on the model loads it.
- [x] **Time and model in the image**: `Time: 52.3 s`, and the A1111 `Model:` field for FLUX, Z-Image and
  Wan (patch 0074).
- [x] **Use all** in the viewer switches to the image's preset, then fills Create with its settings. On a
  PC or a device on its side the viewer's text floats over the picture.
- [x] **LoRAs on and off** on the Create tab, keeping their strength; grouped by family under Models, with a
  search past 8. Their order does not change the image, so they keep no order.
- [x] **Several uploads at once**, one after the other, with Cancel.
- [x] **Klein 9B's preset** at 8 steps with dpm2 (about 3 minutes an image).

## 0.9.13: presets, more room, and images without metadata ([issue 10](https://github.com/Darkatek7/PS5SD/issues/10), [issue 12](https://github.com/Darkatek7/PS5SD/issues/12)), done

- [x] **A star per preset**: a click on ☆ makes it the model's default.
- [x] **Update preset**: the form's settings into the loaded preset (`POST presets/<id>` with `defaults`), with
  Reset beside it; both show only when the form differs from the preset.
- [x] **Models, Presets**: every preset by name with its model and a search; **Apply settings** for another
  model of the same family (two SDXL checkpoints, the two Klein 4B files), without a switch.
- [x] **More room**: no header bar on a PC (the status is at the sidebar's foot); on a phone it scrolls away.
- [x] **Leave the settings out of the file** (issue 12): the PNG carries nothing, the console keeps them
  for the gallery (patch 0075).

## 0.9.14: presets, safer and easier to write ([issue 10](https://github.com/Darkatek7/PS5SD/issues/10)), done

- [x] **No model Delete in the Presets list**: its ⋯ deletes saved presets only; a model is deleted under
  Installed, where the main preset's row no longer offers it either (the header does).
- [x] **Fields** under Edit text: each # line with an example and a + that adds it; More in the help opens
  MODELS.md at Presets.
- [x] **Suggestions while typing** a preset: # lines, the keys of # defaults, samplers after sampler=,
  LoRA files after loras=.

## 0.9.15: one error shape and progress as events, done

The first part of 1.0's "The API is fixed".

- [x] **One error shape**: every answer of 400 and more is `{"error": {"code", "message"}}`, as a failed job
  has it; codes such as `out_of_memory`, `queue_full`, `standby`, `switching` (docs/API.md, Errors).
- [x] **Progress as server-sent events**: `GET /ps5sd/v1/events` with the queue, every step and each job's
  end; at most four streams, a ping every 15 s.
- [x] **The web UI on the stream**: it asks for its job when the stream says it moved and takes the queue
  from it; without the stream it polls as before.

## 0.9.16: a model switch without stopping the server, done

The second part of 1.0's "The API is fixed".

- [x] **The server stays up during a switch** (`patches/sdcpp/0078`): the next model loads into the running
  server. The web UI, the library, the job list and the event stream keep answering; a request that needs
  the model waits for it. Measured on the console against 0.9.15: the same times and memory, a lower CPU
  heap, no request without an answer ([CONSOLE.md](docs/CONSOLE.md#a-model-switch-without-stopping-the-server-2026-10-28)).
- [x] **`POST /sdapi/v1/options` waits** until the model has loaded, as A1111 does.
- [x] **The `model` of an OpenAI request** switches to a model of the library; other names, such as
  `dall-e-3`, run on the loaded one.
- [x] **The switch in the event stream** (a `model` event), which the web UI follows.

## 0.9.17: OpenAPI and a safe Close, done

- [x] **An OpenAPI description** of every route at `/ps5sd/v1/app/openapi.json` (OpenAPI 3.1, open without
  the key); CI checks that it lists every route the server and the app register.
- [x] **A Close during a stalled main loop**: a thread passes the GPU suspend point every 100 ms. Measured on
  the console: a Close during a main loop stuck in a frame took 7.8 s and ended in a crash report without it,
  85 to 91 ms and a clean end with it ([CONSOLE.md](docs/CONSOLE.md#a-close-during-a-stalled-main-loop-2026-10-28)).
- [x] Tested: `options` with a model that does not load (`500 load_failed`), and a switch in the web UI from
  the page and from outside.

## 0.9.18: the open measurements, done

- [x] **Measured** on the console ([CONSOLE.md](docs/CONSOLE.md#the-open-measurements-and-two-ends-of-ps5sd-found-with-them-2026-10-28)):
  SDXL with the hires fix up to 3072x3072, an upscale of 1024 to 4096, inpainting with three models, a guide
  image with the hires fix; the catalog's ControlNet and inpainting entries say their time.
- [x] **A guide image with the hires fix ended PS5SD**: the guide now has the hires pass's size
  (`patches/sdcpp/0082`), and an out-of-memory in ggml's own code fails the run instead of the title
  (`patches/sdcpp/0083`, the app's abort callback).
- [x] **The fit hint**: "tight" says what to lower, a family nobody measured says so above 1024x1024, and a
  guide image has limits of its own.
- [x] **The docs** read against the title (README, MODELS.md, API.md, the Help tab); links to a heading work
  in the Help tab.

## 1.0.0: ready, done

No large features; 1.0 is the version that keeps its promises. It is out when all of these hold:

- [x] **Every open measurement is done** and in [docs/CONSOLE.md](docs/CONSOLE.md): the largest SDXL size
  with the hires fix, an Upscale of 1024 to 4096, inpainting on the console, a guide image with the hires
  fix (0.9.18).
- [x] **No run fails without a reason**: before a run that will not fit, the web UI says so and what to
  lower; a run that fails anyway says why (0.9.18).
- [x] **The API is fixed**: one error shape everywhere and progress as server-sent events besides polling
  (both in 0.9.15); `POST /sdapi/v1/options` waits until the model has loaded, as A1111 does, and the `model`
  of `/v1/images/generations` switches the model (both in 0.9.16); an OpenAPI description at
  `/ps5sd/v1/app/openapi.json` (0.9.17). After 1.0, a change that breaks a client needs a new major version.
- [x] **Close during a stalled main loop**: a thread that passes the GPU suspend point every 100 ms, so the
  25 s limit is never hit (0.9.17, see [docs/CONSOLE.md](docs/CONSOLE.md)).
- [x] **An update keeps everything**: models, LoRAs, presets, gallery, settings and the API key survive an
  update from every 0.x version, tested from 0.5.0 on (26 releases, `scripts/update-test.py`,
  [CONSOLE.md](docs/CONSOLE.md#an-update-keeps-everything-2026-10-28)).
- [x] **The docs match the title**: README, Help tab, MODELS.md and API.md read against 1.0, with new
  screenshots of the web UI.
- [x] **No known crash** and no open bug labelled `bug` older than a release: the two ends 0.9.18's
  measurements found are fixed, and a 38-minute soak test ran without a failure
  ([CONSOLE.md](docs/CONSOLE.md#a-soak-test-before-10-2026-10-28)).

## 1.1.0: the stable-diffusion.cpp of November, done

`third_party/stable-diffusion.cpp` was at `b13a482`. On 2026-11-12 it moved to upstream `master`
`32d516c` (111 commits), its ggml to `d803c23`. The numbers below are upstream's pull requests.

- [x] **Rebase** `patches/sdcpp` and `patches/ggml` onto upstream (2026-11-12). The ggml patches and
  0001 to 0003, 0070 and 0073 applied as they were; the rest was ported:
  - 0055: upstream cancels a running job itself now (#2190), so the patch keeps only what `docs/API.md`
    promises on top: `202` for a running job, `/ps5sd/v1/jobs`, A1111's `interrupt` and `skip`.
  - 0058 is gone: the A1111 routes refresh the LoRA cache themselves (#2176).
  - 0066 (repeat), 0071 and 0072 (out of memory) run around upstream's new generation API
    (`sd_image_generation_result_t`, ADetailer, the cancel monitor).
  - 0067 moved to `execution_engine.cpp`, where upstream put the graph's execution (#2321): the
    engine says when a whole graph failed before any of it ran, and `GGMLRunner::compute` builds it
    again and runs it in segments.
  - 0068 and 0075: a saved image's parameters come from upstream's generation info (#2206), A1111's
    text with sd.cpp's JSON at the end, as upstream's own PNGs have them. The JSON's schema changed
    (`sdcpp.generation/v1`); the gallery viewer reads both, so older images keep their info. The
    gallery writes its text (with `Preset:` and `Time:`) into the PNG itself, since upstream's encoder
    now takes the JSON and writes its own text.
  - 0077: upstream counts the jobs' times in milliseconds now; the job routes and the events give
    seconds, as in 1.0, so a client keeps working.
  - 0078: upstream manages models itself now (#2176, recipes), but refuses a switch while a job is
    queued and answers 503 while one loads. The app keeps loading through `sd_server_load()`; upstream's
    model routes are not registered, and its model status stays filled for `/sdcpp/v1/capabilities`.
  - 0082: upstream scales the guide for the hires pass of Z-Image's ControlNet (#2187); the patch
    keeps it for the others.
  - 0084 is new: upstream checks each LoRA path with `fs::canonical` now (#2176), which needs
    `realpath()`, and that fails on the PS5. The server then listed no LoRA. Where `realpath()` fails,
    an absolute path that exists counts as it is.
- [x] **Checked on the PC** (2026-11-12): the series gives the tree it was built from, sd-server builds
  on the CPU, and with SD-Turbo a `repeat` job, the gallery's info, a cancel of a running job, A1111's
  `interrupt` and `txt2img`, the events and `scripts/check-openapi.py` work. The app's one-shot mode uses
  upstream's new `generate_image` result (`ps5/app/main.cpp`).
- [x] **Build and test the title** (2026-11-12, CI's build of `sdcpp-november`): switches between presets,
  an image with a LoRA on Klein 9B, a cancel, a repeat job, a video, the hires fix with a guide image, the
  gallery's info of an image from 1.0 and a new one, and a soak test of 25 minutes with no failure
  (`docs/CONSOLE.md`). It found 0084 (no LoRA was listed) and a job's `created` in milliseconds.

## 1.1.1: any LoRA name in a preset, any size ([issue 15](https://github.com/Darkatek7/PS5SD/issues/15), [issue 16](https://github.com/Darkatek7/PS5SD/issues/16)), done

- [x] **A preset keeps a LoRA of any name** (issue 16): a value in `# defaults:` is one word of letters,
  digits and `. _ + - : , / %`, so **Save as preset** failed for a LoRA with a space, `[`, `]` or `@` in its
  name. The web UI writes the LoRA's path percent-encoded now, as it did the upscaler's name, and reads both.
- [x] **Any width and height** (issue 15): the fields take any whole number from 64 to 4096 typed in; their
  arrows still step by 64. Before, a size was rounded to a multiple of 8 and kept to 2048. sd.cpp rounds it up
  to the multiple the model works in. A preset's `width` and `height` take the same.
- [x] **The home screen background is back**: 1.1.0 shipped a plain dark gradient as `pic0.dds` and
  `pic1.dds`. CI built PS5_Vulkan's asset tools fresh in that run, and the picture came out plain; nothing
  checked it. `scripts/build-app.sh` now counts the different BC7 blocks of each background (a few dozen for a
  gradient, about 400,000 for the picture), makes a plain one again once, and then stops the build.

## After 1.0

1.x adds what does not break a client of the API; a change that breaks one (a route, a field, an error
code that goes away or changes its meaning) waits for 2.0.

The plan as of 2026-11-04, in order (1.1.0 was taken by the stable-diffusion.cpp of November on
2026-11-12, so the rest moved up one number). 1.4.0 only touches the web UI and the app's own routes, so it
can be worked on next to 1.2.0 and 1.3.0. The times below are those of 1.0.0 on the console
([CONSOLE.md](docs/CONSOLE.md)); every new number goes there with its date and build.

## 1.2.0: two pictures for Edit, and how fast a model loads ([issue 13](https://github.com/Darkatek7/PS5SD/issues/13))

Issue 13 asks for a pose (a skeleton of one or more people, for sprite sheets) and a character from a
reference picture, with a prompt and no setup. FLUX.2 Klein takes several reference pictures, so this
needs no ControlNet and no preprocessor.

- [ ] **Try it through the API first**: `POST /sdcpp/v1/img_gen` with two `ref_images` (a pose skeleton
  and a character) on Klein 4B Q8_0 and Klein 9B Q4_0, 1024x1024, 4 steps. Write down whether the pose
  holds, whether the character holds, the time, and the pool after the run. Klein 9B leaves under 1 GiB in
  its largest part after one image, so a second reference may not fit there; then try smaller references,
  or offer it on Klein 4B only.
- [ ] **A second picture in Edit**: the web UI sends one reference (`ps5/webui/src/js/09-generation.js`,
  `ref_images = [p.req.init]`). "Edit by the prompt" gets a second, optional slot, shown only for a model
  that takes references. The gallery's Edit and Use all fill the first one.
- [ ] **A preset "Pose and character"** in the catalog (`ps5/app/catalog.hpp`), on Klein 4B, with prompt
  starters that name the two pictures, so the issue's "download the preset, give it two pictures and a
  prompt" works. `docs/MODELS.md` and `docs/API.md` say how to use it.
- [ ] **Pictures in by drag and drop and by paste**: a file dropped on a picture slot (Edit, Guide, the
  references), or Ctrl+V of a picture, fills it. The `paste` event carries the picture over plain HTTP;
  only `navigator.clipboard.read` needs HTTPS.
- [x] **Where a model's load time goes**: sd.cpp's loader read with `std::ifstream` from 11 threads, about
  320 MB/s. On the console `std::ifstream` splits a large read into small ones, and large reads from several
  threads at once are many times slower than one stream (a read test in the title: one thread with 8 MiB
  reads 1.7 GB/s, four threads 0.3 to 0.4). `patches/sdcpp/0085` reads with one thread and `pread()`, in the
  file's order: 1.4 to 2.7 GB/s. Klein 9B's first image after a start takes 55 s instead of 102, a switch to
  `sdxl-base` 4.4 s instead of 9.1 ([CONSOLE.md](docs/CONSOLE.md#faster-loading-and-fewer-steps-2026-11-13)).
- [ ] **`sdxl-full` converted once**: the f16 checkpoint (6.9 GB) is converted to Q8_0 at every load, so a
  switch takes 9.8 s against 4.4 s for `sdxl-base` (17.7 s and 8.6 s before `0085`). Convert it once after the download and load the GGUF.
  sd.cpp's own conversion holds the whole model in CPU memory, which the title does not have (about
  2 GB), and the title's writes slow down after 1.4 GB, so it has to go tensor by tensor and through
  `ftp_writer.cpp`, with a host test. If that turns out too large, point the entry at a ready Q8_0 GGUF
  instead.

## 1.3.0: what the stable-diffusion.cpp of November brings

1.1.0 moved PS5SD onto it with the times of 1.0. This release uses what is new in it.

- [x] **Spectrum** in the catalog's FLUX.3 Klein, Z-Image Turbo and Wan 2.1 presets (`--cache-mode spectrum`):
  it predicts the steps it can and skips them, with the same picture. FLUX.3 Klein 73 s instead of 114,
  Z-Image Turbo 55 s instead of 71, a Wan video at 20 steps 86 s instead of 140. `cache-dit` was faster on
  FLUX.3 Klein but broke the text in the picture. A preset that 1.1.1 wrote gets it at the next start.
- [x] **The shader cache** in `/data/PS5SD/.cache/mesa`: Mesa kept none under the title's own `HOME`. It
  saves about a second a start.
- [x] Looked at and left: ggml's 8-bit dot products (the PS5's GPU has no working `v_dot4`), direct
  convolution (slower on the console).
- [ ] **Speed** from upstream, measured on the console, not assumed: Vulkan kernels, graph fusion and
  reused workspaces (#2251, #2252), fused GEGLU and GLU (#2245, #2250; the SDXL and SD 1.5 UNets use
  them), Z-Image fusions and BF16 GEMM (#2265), position embeddings kept between runs (#2211). The
  baseline: Z-Image Turbo 1024x1024 89 s, SDXL Base 29 s, Klein 9B 49 s warm, Wan 2.1 1.3B (9 frames at
  832x480) 87 s.
- [ ] **Fixes** that come with it: the hires fix keeps the step count it was given (#2227), Vulkan's free
  memory checked again after a reclaim (#2256), small-BAR heaps (#2181), Wan's VAE decode plans (#2197).
- [ ] **Face and detail fix**: upstream's server does ADetailer now (#2164). A face detector in the
  catalog, a **Fix faces** button under the result and in the viewer, and the field in
  `/sdcpp/v1/img_gen` and the A1111 routes. Measure the time it adds on an SDXL picture with two faces.
- [ ] **Z-Image ControlNet** (#2187): the Union model (pose, depth, edges) in the catalog, for Guide on
  Z-Image Turbo. With a pose picture this is a second way to issue 13's pose, on another model.
- [ ] **The gallery viewer in a browser**: its info and Use all on an image from 1.0 and one from 1.1. The
  conversion of the new JSON (`infoShape` in `12b-gallery-info.js`) was tested with node only.
- [ ] Soak test and update test again; the new times into CONSOLE.md and, where they changed, into the
  catalog and `docs/MODELS.md`.

## 1.4.0: the web UI, smaller steps

Each one tested in a browser with `scripts/webui-dev.py` (`--mock-*` without a console) on a PC, a phone
and the PS5's own browser, which needs ES5.

- [ ] **The result's buttons in one row**: on a PC the seven buttons under the result wrap, and Delete
  ends up alone on a second line (`docs/images/webui-pc-create.jpg`). Icons with a label for Download,
  Run again, Upscale, Edit and Delete; Use seed and Open full size go into a ⋯ menu.
- [ ] **Prompt history**: the last 50 prompts, with the negative prompt and the model, kept on the
  console (in `/ps5sd/v1/app/settings` or a route of their own) so every browser has them. A button at the
  prompt opens the list; a click puts one back.
- [ ] **Favourites**: a ★ in the viewer and on the gallery's tiles, and a filter for them. Kept with the
  names and tags (`/ps5sd/v1/app/gallery/labels`, a `fav` field), not in the file.
- [ ] **Open like an app on a phone**: `apple-touch-icon`, `apple-mobile-web-app-capable` and a web
  manifest with the title's icon, so a shortcut on the home screen opens without the browser's bars.
  Android installs a full web app only over HTTPS; the shortcut works without it.
- [ ] **Outpainting**: make an image larger on one or more sides (Edit, "Extend", 128 to 512 px a side).
  The page puts the picture on a larger canvas with a mask over the new part and runs the masked
  inpainting of 0.5.0.
- [ ] **Wildcards**: `{red|blue|green}` in a prompt picks one for each image of a batch. The page expands
  it before each job, so it works for every model and every API client sees a plain prompt; the image
  keeps the expanded prompt.
- [ ] `docs/API.md` for the new routes and fields; the Help tab shows it.

## 1.5.0: the gallery on the TV

- [ ] **A slideshow with the DualSense**: the gallery in full screen on the TV. Left and right step
  through it, Options starts and stops it, Circle goes back to the dashboard; new images join it while it
  runs, and a running job shows its progress in a corner. `third_party/ps5-homebrew-ui/src/platform/ps5/pad.hpp`
  reads the pad; `ps5/app/dashboard.cpp` already decodes the gallery's PNGs one per frame. The dashboard
  without a console: `scripts/preview-app-ui.sh`.

## 1.x, no version yet

- [ ] **Image to text** ([issue 14](https://github.com/Darkatek7/PS5SD/issues/14)): a description or a prompt
  from a picture, with a button that writes it into the image's metadata and one that copies it.
  stable-diffusion.cpp only encodes text, so this needs a vision-language model that generates text, run with
  llama.cpp's multimodal code on the same ggml-vulkan. Qwen3-VL 4B, which FLUX.3 Klein already loads as its
  text encoder, could be that model. First measure on the console whether it fits next to an image model,
  or only when none is loaded, and how long a caption takes.
- [ ] **React, TypeScript and Tailwind** (as in [issue 7](https://github.com/Darkatek7/PS5SD/issues/7)'s
  fork): only built down to ES5 in one page, so the PS5's browser keeps working, and with every feature of
  the current page. Not started; first a word with the people who would work on it.
- [ ] **More ControlNets**: OpenPose and depth for SDXL. Without a preprocessor on the console they need
  a pose or depth image, so only if people ask for them (1.3.0 brings one for Z-Image).
- [ ] **Tighter OpenAPI schemas**: some answers are described loosely (the download routes, the A1111
  lists, `capabilities`); each gets its fields as API.md names them.

## Later, if needed

- **Tiled upscaling** (like Ultimate SD Upscale): sd.cpp has nothing like it; it would be a tile loop over
  img2img in the app. Only if hires fix plus ESRGAN is not enough.
- **A preview while it draws**: sd.cpp can decode the latent every few steps with TAESD. It costs time on
  every step, so only as an option, and only if the console has the memory.

## Not planned

- **ComfyUI inside PS5SD**: ComfyUI's API is a world of its own (node graphs). PS5SD speaks the
  AUTOMATIC1111 and OpenAI APIs, so the way in is a ComfyUI node that uses the console as a backend, as a
  separate project. Help with that is welcome.

## Helping

- Pick an open item, say so in its issue (or open one), and send a pull request.
- Measurements on other firmwares and models are as useful as code: [docs/CONSOLE.md](docs/CONSOLE.md)
  shows how they are written down.
- Building: [docs/BUILDING.md](docs/BUILDING.md). How the parts fit: [docs/PORTING.md](docs/PORTING.md).
