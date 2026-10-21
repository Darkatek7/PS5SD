# Roadmap

What is planned for PS5SD, in order. Each version is one release in the homebrew store. Ideas and wishes
go into [issues](https://github.com/Darkatek7/PS5SD/issues); this file says what comes next and why.

## Done

- **0.1.0**: the title with server, web UI, model downloader (through ftpsrv, at full speed to the
  end), uploads from the PC, gallery, Help tab, TV dashboard.
- **0.2.0**: LoRAs, several per model, each with its own strength; download, upload, list and delete.
- **0.3.0**: web UI options from [#3](https://github.com/Darkatek7/PS5SD/issues/3), below.
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

## 0.4.0: high resolution and upscaling (#3)

sd-server already supports both; PS5SD does not expose them yet.

- [ ] **Hires fix / second pass**: generate, scale up (latent, Lanczos or an upscaler model), and run a
  few more steps at the larger size. A "Hires fix" section on the Create tab.
- [ ] **Upscale an image**: an ESRGAN model in `/data/PS5SD/upscalers` (also passed to sd-server as
  `--hires-upscalers-dir`) and an Upscale button in the image viewer (`POST /sdcpp/v1/upscale`).
- [ ] **Measure first**: how large an SDXL image can get on the console (2048x2048 and up: memory, time,
  VAE tiling), before the UI offers it.

## 0.5.0: ControlNet (#3)

- [ ] A ControlNet model in a preset (`--control-net`), a control image upload on the Create tab, the
  strength, and sd.cpp's built-in Canny preprocessing.
- [ ] **Measure first**: an SDXL ControlNet is about 2.5 GB on top of the model; with an SDXL model
  loaded, about 3.9 GiB of the console's pool is free. Whether SDXL + ControlNet fits, or it is SD 1.5
  only, decides how this is offered.

## Later, if needed

- **Tiled upscaling** (like Ultimate SD Upscale): sd.cpp has nothing like it; it would be a tile loop over
  img2img in the app. Only if hires fix plus ESRGAN (0.4.0) is not enough.
- **Preset defaults for API clients**: presets could set the sampler, scheduler and steps for clients that
  send none (for example `ays` for iLustMix at 10 steps; the web UI sets them already).
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
