# Roadmap

What is planned for PS5SD, in order. Each version is one release in the homebrew store. Ideas and wishes
go into [issues](https://github.com/Darkatek7/PS5SD/issues); this file says what comes next and why.

## Done

- **0.1.0**: the title with server, web UI, model downloader (through ftpsrv, at full speed to the
  end), uploads from the PC, gallery, Help tab, TV dashboard.
- **0.2.0**: LoRAs, several per model, each with its own strength; download, upload, list and delete.
- **0.3.0**: web UI options from [#3](https://github.com/Darkatek7/PS5SD/issues/3), below.
- **0.5.2**: the TV shows the console's own address (it showed `<console ip>` before) and a QR code that
  opens the web UI, with the API key in it while the TV shows the key; after
  [Machete PS5SD](https://github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID) by VenezuelanBiggie24.
- **0.5.1**: a loaded preset's settings are the API's defaults too (from
  [#4](https://github.com/Darkatek7/PS5SD/pull/4), by nikay99), set at run time so presets of one model
  still switch at once; the A1111 and OpenAI model lists show models only; deleting a saved preset through
  the API no longer touches the model's file; `txt2img` without a size takes the default size.
- **0.5.0**: image to image and inpainting: a start image (a file, a gallery image, the last result), a
  mask painted in the page, only the masked area made at the model's size and put back with a soft edge;
  SDXL and SD 1.5 inpainting models in the catalog.
- **0.4.0**: the hires fix (a second pass at a larger size, on the Create tab and in presets) and the
  Upscale button (ESRGAN, the result saved to the gallery); upscaler models in `/data/PS5SD/upscalers`,
  two of them one click away on the Models tab.
- **0.3.1**: an API key for the whole API (Bearer, Basic or the web UI's login; made by the console and
  shown on the TV), CORS without credentials, a 64 MB limit on request bodies; a Presets tab (save the
  Create tab's settings as a preset, edit, delete; a preset of the loaded model switches at once), and
  the Models tab shows only models.

## 0.3.0: web UI options ([#3](https://github.com/Darkatek7/PS5SD/issues/3)), done

Small changes, all in the web UI and the dashboard; nothing new for the console's memory.

- [x] **Batch**: several images per run (sd-server's `batch_count`, up to 8), all shown and saved in the gallery.
- [x] **Own size presets**: save the sizes you use and pick them with one click.
- [x] **No image on the TV**: a switch that hides the images and prompts on the PS5 screen (dashboard).
- [x] **Done signal**: a sound and a mark in the tab's title when a run ends; a browser notification
  where the browser allows one (only on HTTPS or localhost, so not on the console's plain HTTP page).

## 0.4.0: high resolution and upscaling ([#3](https://github.com/Darkatek7/PS5SD/issues/3))

- [x] **Hires fix / second pass**: generate, scale up (latent, Lanczos or an upscaler model), and run
  more steps at the larger size. A "Hires fix" part of the Create tab's Settings; presets keep it.
- [x] **Upscale an image**: an ESRGAN model in `/data/PS5SD/upscalers` (passed to sd-server as
  `--hires-upscalers-dir`), an Upscale button under the result and in the image viewer
  (`POST /sdcpp/v1/upscale`, patch 0061 saves the result to the gallery). Download, upload and delete on
  the Models tab, with Real-ESRGAN x4plus and its anime model one click away.
- [ ] **Measure on the console**: how large an SDXL image can get with the hires fix (2048x2048 and up:
  memory, time, VAE tiling), and how long an Upscale of 1024 to 4096 takes. The web UI warns above
  2048 until then.

## 0.5.0: image to image and inpainting ([#3](https://github.com/Darkatek7/PS5SD/issues/3))

sd-server takes an init image and a mask already (`init_images` and `mask` on `/sdapi/v1/img2img`,
`init_image` and `mask_image` on `/sdcpp/v1/img_gen`), and sd.cpp loads the inpainting checkpoints of
SD 1.x, SD 2.x and SDXL. What is missing is the web UI.

- [x] **Image to image**: start from an image (a file, an image of the gallery with Edit, or the last
  result) with a strength. Less than 1 runs only that share of the steps, so a light change is fast.
- [x] **Inpainting**: paint a mask over the image (brush size, eraser, invert, clear) in a canvas that takes
  a mouse, a finger and the DualSense cursor; only the masked part changes, and the rest stays pixel for
  pixel (the result is blended back in the page).
- [x] **Only the masked area**: a box around the mask, made at the model's size and put back with a soft
  edge (A1111's "inpaint only masked"). The finished image goes to the gallery
  (`POST /ps5sd/v1/app/gallery`) in place of the raw one.
- [x] SDXL Inpainting 0.1 and SD 1.5 Inpainting (Q8_0 GGUF) in the catalog.
- [ ] **Measure on the console**: a masked fix in a 1024x1024 SDXL image against the whole image, and
  the inpainting models (only tested on a PC with a normal model so far).

Image input is what ControlNet (0.7.0) needs as well.

## 0.6.0: video

sd.cpp makes videos with Wan (`vid_gen`, `POST /sdcpp/v1/vid_gen`); sd-server has the route.
[Machete PS5SD](https://github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID) showed the idea; its
presets do not load as they are (the text encoder goes in `--t5xxl`, Wan 2.2 TI2V 5B needs the Wan 2.2
VAE), and LTX-Video 2B is not in this sd.cpp.

- [x] **A video the browser can play**: libwebp and libwebm are built for the PS5, so the default output
  is WebM (VP8). It plays on the page in Chrome, Firefox and Edge; Safari on iPhone may need a download.
- [x] **Measure on the console**: Wan 2.1 T2V 1.3B at 832x480, 33 frames, 20 steps: about 14.5 minutes
  (36 s a step, 2 minutes to decode), 4.0 of 11.44 GiB free after the run; 256 px VAE tiles ran out of
  memory, 128 px fit.
- [x] **Web UI and gallery**: a video model switches the Create tab to frames and frames per second; the
  result plays on the page; the gallery shows videos and plays them in the viewer. Wan 2.1 is in the catalog.
- [ ] **Wan 2.2 TI2V 5B**: measure whether it fits (it needs the Wan 2.2 VAE).
- [ ] **Flow shift in the web UI**: set in the preset only for now (3 for Wan 2.1).
- [ ] **Image to video**: a start image for a Wan model (Wan 2.1 T2V is text only).
- [ ] **Longer clips (49 and 81 frames)**: measure memory and time.
- [ ] **Faster video**: a step-distilled Wan LoRA (CausVid or lightx2v) for 4 to 8 steps at CFG 1.

## 0.7.0: ControlNet (#3)

- [ ] A ControlNet model in a preset (`--control-net`), a control image on the Create tab (the image input
  of 0.5.0), the strength, and sd.cpp's built-in Canny preprocessing.
- [ ] **Measure first**: an SDXL ControlNet is about 2.5 GB on top of the model; with an SDXL model
  loaded, about 3.9 GiB of the console's pool is free. Whether SDXL + ControlNet fits, or it is SD 1.5
  only, decides how this is offered.

## Later, if needed

- **Tiled upscaling** (like Ultimate SD Upscale): sd.cpp has nothing like it; it would be a tile loop over
  img2img in the app. Only if hires fix plus ESRGAN (0.4.0) is not enough.
- **Gallery thumbnails**: the gallery shows every image at full size, which for 4096 px upscales is
  slow on the console's own browser. Small copies made when an image is saved would fix that.
- **Face and detail fix** (like ADetailer): sd.cpp's `sd-cli` can find faces with a YOLOv8 model and
  inpaint each one cropped. sd-server does not have it; with 0.5.0's masked inpainting it could be a
  button that runs the detector and inpaints what it finds.
- **The API, for clients from outside**: progress pushed as server-sent events instead of polling; a
  wait on `POST /sdapi/v1/options` until the model has loaded, as A1111 does; the `model` field of
  `/v1/images/generations` switching the model (or a `409`) instead of being ignored; one error shape
  everywhere; an OpenAPI description at `/ps5sd/v1/app/openapi.json`; the gallery in pages, with each
  image's prompt and settings.
- **Close during a stalled main loop**: a thread that passes the GPU suspend point every 100 ms, so the
  25 s limit is never hit (see [docs/CONSOLE.md](docs/CONSOLE.md)). Closes have been clean since 0.1.0's
  download fix; this would be a safety net.

## Not planned

- **ComfyUI inside PS5SD**: ComfyUI's API is a world of its own (node graphs). PS5SD speaks the
  AUTOMATIC1111 and OpenAI APIs, so the way in is a ComfyUI node that uses the console as a backend, as a
  separate project. Help with that is welcome.

## Helping

- Pick an open item, say so in its issue (or open one), and send a pull request.
- Measurements on other firmwares and models are as useful as code: [docs/CONSOLE.md](docs/CONSOLE.md)
  shows how they are written down.
- Building: [docs/BUILDING.md](docs/BUILDING.md). How the parts fit: [docs/PORTING.md](docs/PORTING.md).
