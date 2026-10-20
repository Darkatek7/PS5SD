# PS5SD

stable-diffusion.cpp on the PS5 GPU — a native title running ggml-vulkan on RADV, with an
OpenAI/A1111-compatible image API, a browser UI and a TV dashboard.

> **Status: 0.1.0, work in progress, no release yet.** The title runs the server, the web UI, the
> model downloader and the TV dashboard on the console. Downloads run at 50 to 75 MB/s from start to
> end and resume after a close. Closing from the home screen during a download used to end in a
> crash report about 1 time in 5; that has not happened in 8 Closes since the download fix. The
> measurements are in [docs/CONSOLE.md](docs/CONSOLE.md).

## Milestone: the first image made on a PS5

<img src="docs/images/first-image.png" alt="An astronaut riding a horse above the Earth, generated on a PS5" width="384">

2026-10-19, FW 13.00: `a photograph of an astronaut riding a horse`, SDXS (Q8_0), 512x512, one step,
seed 42, on the console's GPU through ggml-vulkan and RADV, in 2.6 s. Measurements in
[docs/CONSOLE.md](docs/CONSOLE.md).

## Requirements

- A jailbroken PS5 on firmware 7.00–13.60, with:
  - [etaHEN](https://github.com/LightningMods/etaHEN) and kstuff-lite
  - ShadowMountPlus
  - elfldr
  - ftpsrv (port 2121): the model downloader writes its files through it. Without it, downloads
    slow to about 2 MB/s after 1.4 GB.
- A host with a Vulkan-capable toolchain for building the payload and the driver patches.

## Models

The title downloads models itself, from the web UI: a starter catalog (SDXL Base 1.0, FLUX.3 Image
Klein, Z-Image Turbo and a few smaller ones; one click installs the files and a preset) or any
Hugging Face or Civitai file URL, with progress, cancel and resume. Downloads keep going while
images are generated. A gated or private Hugging Face repository needs a token in
`/data/PS5SD/hf-token.txt`, a Civitai model that asks for a login one in
`/data/PS5SD/civitai-token.txt` (one line each).

A model from anywhere else (your own merge or training, another site, a file already on the PC) goes
onto the console by FTP; [docs/MODELS.md](docs/MODELS.md) has the steps, the preset format for models
of several files, and which models fit. How the downloader works is in
[docs/PORTING.md](docs/PORTING.md#the-model-downloader-downloadercpp-https_clientcpp-download_corecpp-cataloghpp).

## API

Port 7860 serves the web UI and the A1111, OpenAI and sd-server APIs, plus PS5SD's own routes:
- the model library;
- switching and deleting models;
- the downloader;
- the job queue and cancel.

With no model yet, the title starts in standby, so a fresh install can fetch its first model from the
web UI. Every route is in [docs/API.md](docs/API.md).

## Building

See [docs/BUILDING.md](docs/BUILDING.md).

## Credits

- [PS5LM](https://github.com/cobanov/PS5LM) by cobanov — the project PS5SD is modelled on.
- [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) by leejet, and
  [ggml](https://github.com/ggml-org/ggml).
- [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) and
  [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) by mihawk-99 — the Vulkan driver stack.
- ps5-homebrew-ui by blackbearreloaded (GPL-3.0). The original repository is gone; the submodule uses the copy at [Phononenom/ps5-homebrew-ui](https://github.com/Phononenom/ps5-homebrew-ui), pinned at the same commit.

## License

PS5SD is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later);
see [LICENSE](LICENSE). Third-party parts keep their own licences.
