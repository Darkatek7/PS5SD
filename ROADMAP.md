# Roadmap

What is planned for PS5SD, in order, up to 1.0. Each version is one release in the homebrew store. Ideas
and wishes go into [issues](https://github.com/Darkatek7/PS5SD/issues); this file says what comes next and
why.

The goal for 1.0: a title that someone who has never heard of stable-diffusion.cpp can install, open on a
phone, and get good images and videos from without reading anything first; and an API that other programs
can rely on.

## Done

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

## 0.9.0: gallery and video

**Gallery**

- [ ] **Thumbnails**: small copies made when an image is saved, so a gallery of hundreds of images and
  4096 px upscales opens fast, on the console's own browser too.
- [ ] **What made an image**: the prompt, model, seed and settings saved with each image (in the PNG, as
  A1111 does, so other tools read them too), shown in the viewer, with **Use these settings** to make it
  again or change it.
- [ ] **Find and tidy**: newest or oldest first, a search in the prompts, and several images selected for
  one delete or download (images or videos only is in 0.8.0).
- [ ] **The gallery in pages** in the API, with each image's prompt and settings.

**Video**

- [ ] **Longer clips (49 and 81 frames)**: measure memory and time with CausVid.
- [ ] **Faster decoding**: the VAE decode is now about 2 of the 3 minutes 40 s. Try 192 px tiles, and a
  small decoder for Wan (as TAESD is for images).
- [ ] **Image to video**: a start image. Wan 2.1 T2V 1.3B is text only, so this means Wan 2.2 TI2V 5B; first
  measure whether it fits with the Wan 2.2 VAE.
- [ ] **Flow shift on the Create tab**, set in the preset only for now (3 for Wan 2.1).

**Memory**

- [ ] **Freed GPU memory back to the pool**: memory that a run frees stays with the title, and the model
  manager sees less free memory after each larger run (docs/CONSOLE.md, "Memory"). With it back, a large
  run would not need a restart to find room.

**Images**

- [x] **Edit with a reference image** (FLUX.2 Klein): a picture and a sentence ("make it night", "the house in
  red brick") instead of a mask. The Create tab's Edit mode sends the start image as a reference for a
  FLUX.2 model ("Edit by the prompt"), and `ref_images` does it in the API. About 65 s at 4 steps with Klein 4B.
- [ ] **More ControlNets**: OpenPose and depth for SDXL. Without a preprocessor on the console they need
  a pose or depth image, so only if people ask for them.
- [ ] **Face and detail fix** (like ADetailer): sd.cpp can find faces with a YOLOv8 model; with 0.5.0's
  masked inpainting this is a button that inpaints each face it finds.

## 1.0.0: ready

No large features; 1.0 is the version that keeps its promises. It is out when all of these hold:

- [ ] **Every open measurement is done** and in [docs/CONSOLE.md](docs/CONSOLE.md): the largest SDXL size
  with the hires fix, an Upscale of 1024 to 4096, inpainting on the console, a guide image with the hires
  fix.
- [ ] **No run fails without a reason**: before a run that will not fit, the web UI says so and what to
  lower; a run that fails anyway says why.
- [ ] **The API is fixed**: one error shape everywhere; progress as server-sent events besides polling;
  `POST /sdapi/v1/options` waits until the model has loaded, as A1111 does; the `model` of
  `/v1/images/generations` switches the model (or answers `409`); an OpenAPI description at
  `/ps5sd/v1/app/openapi.json`. After 1.0, a change that breaks a client needs a new major version.
- [ ] **Close during a stalled main loop**: a thread that passes the GPU suspend point every 100 ms, so the
  25 s limit is never hit (see [docs/CONSOLE.md](docs/CONSOLE.md)).
- [ ] **An update keeps everything**: models, LoRAs, presets, gallery, settings and the API key survive an
  update from every 0.x version, tested from 0.5.0 on.
- [ ] **The docs match the title**: README, Help tab, MODELS.md and API.md read against 1.0, with the
  screenshots of 0.8.0.
- [ ] **No known crash** and no open bug labelled `bug` older than a release.

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
