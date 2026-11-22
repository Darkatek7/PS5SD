# Roadmap

What is planned for PS5SD, in order. Each version is one release in the homebrew store. Ideas
and wishes go into [issues](https://github.com/Darkatek7/PS5SD/issues); this file says what comes next and
why.

The goal for 1.0: a title that someone who has never heard of stable-diffusion.cpp can install, open on a
phone, and get good images and videos from without reading anything first; and an API that other programs
can rely on.

## Done

- **1.8.5**: a download waits when the server limits downloads (HTTP 429) and says so; the memory hint
  under Generate tells the truth after a switch, and FLUX.3 Klein runs 1024x1024 after other models; a search
  in the prompt collections; the steps of a run show again after the tab was in the background.
- **1.8.4**: why a model cannot run ([issue 25](https://github.com/Darkatek7/PS5SD/issues/25)): files made for
  ComfyUI marked and refused when they cannot run, Krea 2 known, the load error on the model's row, Make a
  preset for a diffusion model, Export of a preset.
- **1.8.3**: Random prompt for each image from the ticked collections, also through `prompts/expand`; the fan
  chart as the chip and the fan over time.
- **1.8.2**: a LoRA's trigger words as a list, each on or off, all or some of them at random for each image,
  `__lora:NAME__`, and all of it through the API.
- **1.8.1**: prompt collections on the console, `__name__` and `__lora__` in a prompt, the prompt as typed in
  each image for Use all, and all of it through the API.
- **1.8.0**: MOSS-Image in the catalog (issue 19); Get more in four tabs, a Civitai search, each LoRA's info
  and trigger words looked up on Civitai, and a LoRA search on the Create tab (issue 24); the fan's Manual
  target kept when another payload left 50 °C, and each fan mode shown as its highest temperature in a chart.
- **1.7.0**: the gallery as a slideshow on the TV, with the DualSense.
- **1.6.0**: the web UI's smaller steps: the result's buttons in one row, the prompt history, favourites,
  Extend (outpainting) and wildcards.
- **1.5.0**: a pose with Z-Image Turbo's Union ControlNet, and a pose with a character in two steps
  (issue 13); the upstream speed work measured.
- **1.4.1**: the gallery shows every file, not only the newest 1000 (issue 22).
- **1.4.0**: Edit by the prompt with every model that takes a reference (issue 21), the references sent at
  768 px (a third faster), a second picture, drag and drop and paste; the free space, and a download that does
  not fit stops at once; the gallery copied to a USB drive (issue 17); a model search, the last image after a
  reload, an update notice, an icon and a manifest.
- **1.3.1**: larger matrix tiles on the GPU: FLUX.3 Klein 56 s instead of 67, Klein 9B's preset 137 s instead
  of 165, Z-Image Turbo 48 s instead of 55, with the same images.
- **1.3.0**: the gallery as one ZIP (issue 20); FLUX.3 Klein's Spectrum from the fourth step (67 s instead
  of 74); how to run Krea 2 Turbo, which is not in the catalog for its licence (issue 19).
- **1.2.1**: a LoRA's strength from -100 to 100, not -2 to 2 (issue 18).
- **1.2.0**: models load about five times faster (one reader with `pread()`); Spectrum in the FLUX.3
  Klein, Z-Image Turbo and Wan 2.1 presets (FLUX.3 Klein 73 s instead of 114); Mesa's shader cache kept.
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

## 1.2.0: faster loading and fewer steps, done

Measured on the console on 2026-11-13 ([CONSOLE.md](docs/CONSOLE.md#faster-loading-and-fewer-steps-2026-11-13)).

- [x] **Where a model's load time goes**: sd.cpp's loader read with `std::ifstream` from 11 threads, about
  320 MB/s. On the console `std::ifstream` splits a large read into small ones, and large reads from several
  threads at once are many times slower than one stream (a read test in the title: one thread with 8 MiB
  reads 1.7 GB/s, four threads 0.3 to 0.4). `patches/sdcpp/0085` reads with one thread and `pread()`, in the
  file's order: 1.4 to 2.7 GB/s. Klein 9B's first image after a start takes 55 s instead of 102, a switch to
  `sdxl-base` 4.4 s instead of 9.1 ([CONSOLE.md](docs/CONSOLE.md#faster-loading-and-fewer-steps-2026-11-13)).
- [x] **Spectrum** in the catalog's FLUX.3 Klein, Z-Image Turbo and Wan 2.1 presets (`--cache-mode spectrum`):
  it predicts the steps it can and skips them, with the same picture. FLUX.3 Klein 73 s instead of 114,
  Z-Image Turbo 55 s instead of 71, a Wan video at 20 steps 86 s instead of 140. `cache-dit` was faster on
  FLUX.3 Klein but broke the text in the picture. A preset that 1.1.1 wrote gets it at the next start.
- [x] **The shader cache** in `/data/PS5SD/.cache/mesa`: Mesa kept none under the title's own `HOME`. It
  saves about a second a start.
- [x] Looked at and left: ggml's 8-bit dot products (the PS5's GPU has no working `v_dot4`), direct
  convolution (slower on the console).

## 1.2.1: LoRA strengths past 2 ([issue 18](https://github.com/Darkatek7/PS5SD/issues/18)), done

- [x] **A LoRA's strength from -100 to 100**: the web UI kept it to -2..2, and slider LoRAs are made for
  -8 to 8 and more. The range is ComfyUI's; sd-server and sd.cpp take any strength.

## 1.3.0: the gallery as one ZIP ([issue 20](https://github.com/Darkatek7/PS5SD/issues/20)), done

Measured on the console on 2026-11-17 ([CONSOLE.md](docs/CONSOLE.md#spectrum-from-the-fourth-step-klein-9bs-samplers-krea-2-2026-11-17)).

- [x] **The gallery as one ZIP**: **Download all** in the gallery, and **Download** with two or more images
  selected. The console makes the ZIP while it sends it (`patches/sdcpp/0086`), with each image's parameters as
  a text file beside it: 1.06 GB in 24 s. `GET` and `POST /ps5sd/v1/gallery/archive`; a form may post it with
  only the login cookie.
- [x] **Spectrum from the fourth step** in the FLUX.3 Klein presets (`--cache-option warmup=3`): 67 s instead
  of 74, with the same picture. From the third step hands and objects moved. A preset that 1.2.x wrote gets it
  at the next start.
- [x] **Krea 2 Turbo** ([issue 19](https://github.com/Darkatek7/PS5SD/issues/19)): it runs, about 3 minutes
  an image at 1024x1024 and 8 steps. Its licence asks everyone who deploys it for a content filter, which
  PS5SD does not have, so it is not in the catalog. `docs/MODELS.md` says how to set it up.
- [x] Looked at and left: VAE tiles of 768 instead of 512 (0.3 s), flash attention (on already), Klein 9B's
  preset (dpm2 at 8 steps stays; Euler at 4 steps takes 48 s instead of 165, and the catalog says so).

## 1.3.1: larger matrix tiles, done

Measured on the console on 2026-11-17 ([CONSOLE.md](docs/CONSOLE.md#larger-matrix-tiles-2026-11-17)).

- [x] **FLUX.3 Klein's slow matrix product**: the MLP's output layer (`k=9216`) ran at 5.4 TFLOPS, the other
  Q4_K products at 9.6 to 9.8. ggml-vulkan left its large tile (128x128) out on the PS5, so every large product
  ran in 64x64 tiles. `patches/ggml/0003` turns it on for products that accumulate in f16; in f32 it took about
  four times as long, so those stay in the medium tile.
- [x] The catalog's presets, 1.3.0 against 1.3.1: FLUX.3 Klein Q4 56 s instead of 67, Klein 9B (dpm2, 8 steps)
  137 s instead of 165, Klein 4B 22 s instead of 25, Z-Image Turbo 48 s instead of 55, SDXL 27 s instead of 28,
  a Wan clip of 9 frames 80 s instead of 84. The images are the same; SDXL's differ in small details.

## 1.4.0: Edit with every model, storage and USB copies ([issue 21](https://github.com/Darkatek7/PS5SD/issues/21), [issue 17](https://github.com/Darkatek7/PS5SD/issues/17)), done

Measured on the console on 2026-11-18 and 2026-11-19
([CONSOLE.md](docs/CONSOLE.md#reference-pictures-for-edit-2026-11-18)).

- [x] **Edit by the prompt with every model that takes a reference** (issue 21): `patches/sdcpp/0087` puts
  the model's reference preset into `GET /sdcpp/v1/capabilities`, and the web UI offers Edit for every model
  that has one (FLUX.2 and FLUX.3 Klein, Qwen Image Edit and others), not by the model's name.
- [x] **Smaller references**: the web UI sends a reference at most 768 px a side and keeps it at that size
  (`resize_before_vae=false`). Klein 4B 36 s instead of 52, Klein 9B 64 s instead of 95, FLUX.3 Klein 102 s
  instead of 148.
- [x] **A second picture in Edit**: an optional slot, shown for a model that takes references. "The goblin
  from image 1 in front of the lighthouse from image 2" keeps the scene; Klein 4B with two references took
  51 s.
- [x] **Issue 13's pose, tried**: no model took the pose from a second picture (a skeleton or a mannequin,
  at 512 and 1024, in either order). A pose needs a ControlNet (1.5.0 and 1.x below).
- [x] **Pictures in by drag and drop and by paste**: a file dropped on Edit's slots or on Guide fills it;
  Ctrl+V fills the start picture, then an empty second slot.
- [x] **The free space**: `GET /ps5sd/v1/app/storage`, a Storage card in Settings, the free space in Get more
  and Downloads, and a catalog entry that does not fit says so. A download that does not fit with 1 GiB to
  spare fails at once. The title's own `statvfs` gives made-up numbers, so the app asks ftpsrv (`AVBL`).
- [x] **Copies on a USB drive** (issue 17): the gallery, or the images you select, to `/mnt/usbN/PS5SD/`
  through ftpsrv, and each new image after its job if you like (`usb_copy`).
- [x] **Smaller things**: the last image on Create after a reload, the viewer's info beside the picture on a
  PC, a search on the Models tab, a notice when a new release is out (GitHub, once a day), the title's icon
  and a manifest for a shortcut on a phone's home screen (`patches/sdcpp/0088`), and one load of the
  settings and the gallery at start instead of two or three.

## 1.4.1: the whole gallery ([issue 22](https://github.com/Darkatek7/PS5SD/issues/22)), done

- [x] **More than 1000 files**: the web UI asked for the list once, and the server gives at most 1000 entries
  an answer, so the count stopped at 1000 and the older files were missing. The page now reads the list in
  pages of 1000 (`offset`) until one is short. `scripts/webui-dev.py --mock-gallery N` tries it without a
  console.

## 1.5.0: a pose with Z-Image's ControlNet ([issue 13](https://github.com/Darkatek7/PS5SD/issues/13)), done

Measured on the console on 2026-11-19 ([CONSOLE.md](docs/CONSOLE.md#z-images-union-controlnet-and-adetailer-2026-11-19)).

- [x] **Z-Image ControlNet** (#2187): the catalog's **Z-Image Turbo (Q4_K) + Union ControlNet** adds
  alibaba-pai's Union ControlNet 2.1 in Q8_0 (3.6 GB). A pose skeleton, a depth map or edges set the layout;
  about 80 s an image at 1024x1024. At 0.9 the figure takes the pose from a prompt that does not name it, so
  the web UI starts at 0.9 for it (0.6 for SDXL's Canny one).
- [x] **Issue 13's pose and character**, in two steps: the pose with this ControlNet, then the character
  into it with Edit and two pictures on FLUX.2 Klein ("the goblin from image 2, in the same pose"). Klein 4B
  took 51 s for the second step. `docs/MODELS.md` says how.
- [x] **Speed** from upstream: in since 1.1.0 and on by default. Graph fusion saves about 2 per cent on
  Z-Image; the transfer queue makes no difference; direct convolution fails sd.cpp's probe on the PS5 and
  stays off; #2265 is CUDA only.
- [x] **Fixes** that came with it (#2227, #2256, #2181, #2197): in since 1.1.0.
- [x] **The gallery viewer in a browser**: the info and Use all of an image of 1.0 and one of 1.1 work.
- [ ] **Fix faces** (ADetailer, #2164): it runs on the console, but the faces come out smeared or in blocks
  of pixels, not sharper. Not offered; moved to 1.x below.

## After 1.0

1.x adds what does not break a client of the API; a change that breaks one (a route, a field, an error
code that goes away or changes its meaning) waits for 2.0.

The plan as of 2026-11-04, in order (1.1.0 was taken by the stable-diffusion.cpp of November on
2026-11-12, 1.2.0 by faster loading on 2026-11-14 and 1.3.0 by the gallery as a ZIP on 2026-11-17, so the
rest moved up three numbers; 1.4.0 took the web UI's storage and USB items on 2026-11-19, and the pose of
issue 13 and the `sdxl-full` conversion moved on; 1.5.0 took the pose, 1.6.0 the web UI's smaller steps,
1.7.0 the slideshow, 1.8.0 MOSS-Image and the LoRA info of issue 24, 1.8.1 prompt collections, 1.8.2 the trigger words as a list, 1.8.3 random prompts from collections, 1.8.4 why a model cannot run, 1.8.5 downloads that the server limits and a search in the collections). The times below are those of 1.0.0 on the console ([CONSOLE.md](docs/CONSOLE.md));
every new number goes there with its date and build.

## 1.6.0: the web UI, smaller steps, done

Each one tested in a browser with `scripts/webui-dev.py` (`--mock-*` without a console) and on the console
([CONSOLE.md](docs/CONSOLE.md#the-web-ui-of-160-2026-11-19)). The page stays ES5 for the PS5's own browser.

- [x] **The result's buttons in one row**: on a PC the seven buttons under the result wrap, and Delete
  ends up alone on a second line (`docs/images/webui-pc-create.jpg`). Icons with a label for Download,
  Run again, Upscale, Edit and Delete; Use seed and Open full size go into a ⋯ menu.
  Delete stays in the row, on its right.
- [x] **Prompt history**: the last 50 prompts, with the negative prompt and the model, kept on the
  console (`/ps5sd/v1/app/prompts`) so every browser has them. History under the prompt opens the list; a
  click puts one back.
- [x] **Favourites**: a ★ in the viewer and on the gallery's tiles, and a filter for them. Kept with the
  names and tags (`/ps5sd/v1/app/gallery/labels`, a `fav` field), not in the file.
- [x] **Open like an app on a phone**: `apple-touch-icon`, `apple-mobile-web-app-capable` and a web
  manifest with the title's icon, so a shortcut on the home screen opens without the browser's bars.
  Android installs a full web app only over HTTPS; the shortcut works without it. Done in 1.4.0.
- [x] **Outpainting**: make an image larger on one or more sides (Edit, "Extend", 128 to 512 px a side).
  The page puts the picture on a larger canvas with a mask over the new part and runs the masked
  inpainting of 0.5.0, with as much of the picture next to it as the new part is wide.
- [x] **Wildcards**: `{red|blue|green}` in a prompt picks one for each image of a batch. The page expands
  it before each job, so it works for every model and every API client sees a plain prompt; the image
  keeps the expanded prompt.
- [x] `docs/API.md` for the new routes and fields; the Help tab shows it.

## 1.7.0: the gallery on the TV, done

Tested on the console with a DualSense ([CONSOLE.md](docs/CONSOLE.md#the-slideshow-on-the-tv-2026-11-19)).

- [x] **A slideshow with the DualSense**: the gallery in full screen on the TV. Left and right step
  through it, Options starts and stops it, Circle goes back to the dashboard; new images join it while it
  runs, and a running job shows its progress in a corner. `third_party/ps5-homebrew-ui/src/platform/ps5/pad.hpp`
  reads the pad; `ps5/app/dashboard.cpp` already decodes the gallery's PNGs one per frame. The dashboard
  without a console: `scripts/preview-app-ui.sh`.

## 1.8.0: MOSS-Image, Civitai and LoRA info, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#moss-image-lora-info-and-the-fan-2026-11-21)).

- [x] **MOSS-Image** ([issue 19](https://github.com/Darkatek7/PS5SD/issues/19)): two catalog entries, a
  4-step model (about 35 s an image) and the SFT model at CFG 3 (about 4 min 35 s at 20 steps), with Qwen3.5
  9B and its tokenizer. MOSS-Image models and LoRAs are told by their tensors.
- [x] **Get more in four tabs** ([issue 24](https://github.com/Darkatek7/PS5SD/issues/24)): Catalog, Civitai,
  Hugging Face and Upload; Add by link on top of Civitai and Hugging Face. `civitai.red` links work.
- [x] **Search Civitai** in the web UI, with the base model of the loaded model picked first, and a
  download into the loras or models folder.
- [x] **LoRA info**: trigger words, preferred strength, notes, description, pictures, in A1111's sidecar
  file. Looked up on Civitai by the file's SHA-256 (or by the version or Hugging Face page it was downloaded
  from), on its own after a download, one by one or all at once; edited by hand.
- [x] **Trigger words into the prompt** with one button, a preferred strength used when a LoRA is added,
  and **Add a LoRA** as a search field once there are more than 8.
- [x] **Tokens** for Civitai and Hugging Face, saved from the web UI.
- [x] **The fan**: a console target under 70 °C, left by another payload, is taken as 91 °C again, so
  Manual, Balanced and Cool work and Quiet puts 91 °C back.
- [x] **The fan modes shown**: each mode says the highest temperature it holds the chip at (Quiet 91 °C,
  Balanced 88, Cool 85, Manual 70 to 91), with a chart of the fan by the chip's temperature, the readings of
  the last 2 minutes on it, and how the mode did on the console.

## 1.8.1: prompt collections, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#prompt-collections-2026-11-22)).

- [x] **Collections**: named lists of prompts or words, kept on the console for every model and every
  browser. **Save prompt** under the prompt, a Collections tab next to History, Import and Export as
  Dynamic Prompts' `.txt` files.
- [x] **`__name__`** in a prompt: a random entry of the collection, picked anew for each image; entries may
  hold `{a|b}` and other `__names__`. A prompt that is only `__name__` takes the entry's negative prompt too.
- [x] **`__lora__`**: the trigger words of the picked LoRAs at that place; **Add trigger words** puts them at
  the cursor.
- [x] **The prompt as typed in the image** (`Template:` in its parameters, as Dynamic Prompts writes it), so
  Use all gives it back.
- [x] **The API**: the collections' routes, `POST prompts/expand` (the same rules, with the LoRAs' stored
  words, a count and a seed), and `template` / `negative_template` on `img_gen` and `vid_gen`.

## 1.8.2: trigger words as a list, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#trigger-words-as-a-list-2026-11-22)).

- [x] **A list of chips**: each trigger word or phrase on or off, added and removed in Info and turned on and
  off under a picked LoRA on the Create tab, kept on the console. A lookup makes one chip of each of Civitai's
  trained words.
- [x] **A mode per LoRA**: all the words that are on, one at random, or some at random, picked anew for each
  image. **Add trigger words** puts the words at the cursor, or `__lora:NAME__` when the mode picks.
- [x] **`__lora:NAME__`**: one LoRA's words at that place; `__lora__` leaves out the LoRAs it names.
- [x] **The API**: `loras/info` reads and sets the list and the mode, `GET loras` lists them, and
  `prompts/expand` uses them and takes them for one call.

## 1.8.3: random prompts from collections, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#random-prompts-and-the-fan-chart-2026-11-22)).

- [x] **Random prompt for each image**: a switch in the Collections tab and a tick at each collection; each
  image takes a random entry of the ticked ones instead of the prompt box, and keeps it as its template.
- [x] **The API**: `prompts/expand` takes `books` instead of `prompt`.
- [x] **The fan chart**: the chip and the fan over the last 2 minutes, with the mode's highest temperature as a
  line, instead of a fan line that the readings did not follow.

## 1.8.4: why a model cannot run, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#files-made-for-comfyui-2026-11-22)), for
[issue 25](https://github.com/Darkatek7/PS5SD/issues/25).

- [x] **Formats**: the Models tab names a file's weight format when it is one made for ComfyUI (NVFP4, INT8,
  W4A4, W4A8, torchao, FP8). Those that cannot run on the PS5 are red, **Use** is off, and a switch is refused
  with `model_format`. ComfyUI INT8 ended the title on the console, so it is one of them; FP8 runs.
- [x] **Krea 2** is told from its tensors: its files show as a diffusion model, and its presets are no longer
  listed as video models.
- [x] **The load error** stays on the model's row until another model loads, and "get sd version from file
  failed" is said in words.
- [x] **Make a preset** on a diffusion model that came alone: the text encoder and VAE of an installed preset
  of its family, else the catalog's, with a note on what is missing. `GET models/files` lists the files,
  `POST presets` takes `{"name", "text"}`.
- [x] **Export .txt** saves a preset's file, to share it.

## 1.8.5: downloads that the server limits, the memory hint, a search in the collections, the steps, done

Tested on the console ([CONSOLE.md](docs/CONSOLE.md#flux3-klein-at-1024x1024-after-other-models-and-civitais-http-429-2026-11-22)).

- [x] **HTTP 429**: Civitai stopped a LoRA at 855 MB with "Too Many Requests". All connections of a download now
  pause together (30, 60, then 120 s, or what the server's Retry-After says, at most 300 s), up to seven times
  in a row, and the row says why it waits. A 429 at the start is asked again three times. The error says what happened.
- [x] **The memory hint**: it said "since PS5SD started" where it meant "since the model loaded", and warned at
  FLUX.3 Klein's own 1024x1024 after a switch. Q6_K at 1024x1024 after Q4_K, the run that failed in 0.7, runs
  now, so 1024x1024 is safe for the family. The hint is asked again after each image of a queue.
- [x] **Search in the collections**: a box above them finds collections by name and by their entries (all
  words, in any order, each at the start of a word); a collection shows how many of its entries match, and Tick all and Tick none act on
  the shown ones.
- [x] **The steps after a tab in the background**: the page asked for its job one look at a time, and a look
  that never came back (a browser holds requests of a background tab) left it on "Queued…" for good. A look now
  has a time limit, one that hangs 15 s is replaced, and the page looks at once when the tab is shown again.
  While the page's own image waits behind another (an image added to the queue during a run with Random prompt
  lines up before the run's next one), the page shows the one that runs: its step, the bar and its preview.

## 1.x, no version yet

- [ ] **Fix faces** (ADetailer): sd.cpp's detail pass smeared the faces on the console, or turned them into
  blocks of pixels (CONSOLE.md, 2026-11-19). Find out why (the Vulkan backend, the VAE's tiles, the crop's
  inpainting) before a face detector goes into the catalog.
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
  a pose or depth image; issue 13 asks for the pose (1.5.0 brings one for Z-Image).
- [ ] **`sdxl-full` converted once**: the f16 checkpoint (6.9 GB) is converted to Q8_0 at every load, so a
  switch takes 9.8 s against 4.4 s for `sdxl-base`. Convert it once after the download and load the GGUF.
  sd.cpp's own conversion holds the whole model in CPU memory, which the title does not have (about
  2 GB), and the title's writes slow down after 1.4 GB, so it has to go tensor by tensor and through
  `ftp_writer.cpp`, with a host test. If that turns out too large, point the entry at a ready Q8_0 GGUF
  instead.
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
