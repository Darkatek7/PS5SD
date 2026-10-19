# PS5SD

stable-diffusion.cpp on the PS5 GPU — a native title running ggml-vulkan on RADV, with an
OpenAI/A1111-compatible image API, a browser UI and a TV dashboard.

> **Status: work in progress.** The title runs on a console and generates images; the API and the
> dashboard are not wired up yet.

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
  - ftpsrv
- A host with a Vulkan-capable toolchain for building the payload and the driver patches.

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
