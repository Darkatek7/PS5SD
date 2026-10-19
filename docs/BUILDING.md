# Building PS5SD

The app is a PS5 title (`ps5/app`): stable-diffusion.cpp linked with ggml-vulkan against
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s RADV build, a dashboard drawn with
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui), packed as a title folder for
ShadowMountPlus. The toolchain runs on x86-64 Linux only; PS5_Vulkan supports Arch (CachyOS), so
everything here builds on Arch, directly, in a container or in WSL2. There is no macOS build of the
title (the `host` CI job only builds stable-diffusion.cpp on the CPU to check the patches). The setup
mirrors [PS5LM](https://github.com/cobanov/PS5LM)'s.

## A. GitHub Actions (nothing to install)

Every push builds the title in `.github/workflows/build.yml` (job `title`, about an hour the first
time, much less with the `.deps` cache). Download it with the GitHub CLI:

```sh
gh run list --workflow build --limit 5
gh run download <run-id> -n ps5sd-title -D build/app-ps5/title   # the title folder
gh run download <run-id> -n ui-preview -D build/app-preview      # screenshots of every screen
```

Or take the artifacts from the run's page. Fork the repository and run the workflow
(`workflow_dispatch`) to build your own changes without a local toolchain.

## B. Docker (macOS, Windows, Linux)

[docker/Dockerfile](../docker/Dockerfile) is `archlinux` with the packages of
[docker/packages.txt](../docker/packages.txt), the one list CI uses too. On Apple Silicon or ARM Linux
Docker emulates x86-64, which works but makes the RADV build take hours; use CI there.

```sh
git clone --recursive https://github.com/Darkatek7/PS5SD && cd PS5SD
docker build --platform linux/amd64 -t ps5sd-build docker/
docker run --rm -it --platform linux/amd64 -v "$PWD":/work ps5sd-build scripts/setup-deps.sh
docker run --rm -it --platform linux/amd64 -v "$PWD":/work ps5sd-build scripts/build-app.sh
# build/app-ps5/title/<TITLE_ID>
```

`setup-deps.sh` is the slow step (RADV, the payload SDK) and only needs to run again when it or
`patches/mesa` change. Keep `.deps` between runs; it is in the mounted repository.

## C. WSL2 or a native Arch

On Arch (or an Arch distribution in WSL2, with the repository on the Linux filesystem, not `/mnt/c`):

```sh
sudo pacman -Syu --needed $(sed 's/#.*//' docker/packages.txt | xargs)
git clone --recursive https://github.com/Darkatek7/PS5SD && cd PS5SD
scripts/setup-deps.sh
scripts/build-app.sh
```

Without `--recursive`: `git submodule update --init third_party/stable-diffusion.cpp
third_party/ps5-homebrew-ui && git -C third_party/stable-diffusion.cpp submodule update --init ggml`.

## Dependencies (`.deps`)

`scripts/setup-deps.sh` puts PS5_Vulkan and its forks in `.deps/src`, each at a commit pinned at the top
of the script (override with `PS5_VULKAN_REV`, `PS5_MESA_REV`, `PS5_PAYLOADSDK_REV`, `PS5_PROTOCOL_REV`
and the matching `_URL`), applies [patches/mesa](../patches/mesa/README.md) to PS5_Mesa with `git am`,
then runs PS5_Vulkan's own setup: `tools/setup-native-dependencies.sh`, `tools/build-radv.sh release`
and `make app` (whose demo title may fail to link, which does not matter: the script checks the outputs
instead). `scripts/setup-deps.sh --fetch-only` only clones and patches, on any host with git.

| Path | What | Pin |
|---|---|---|
| `.deps/src/PS5_Vulkan` | PS5 Vulkan project, its scripts do the rest | `f410e67` (main, 2026-10-13) |
| `.deps/src/PS5_Mesa` | Mesa 26.2 with the PS5 winsys, plus `patches/mesa` | `1ba9874`, the revision PS5_Vulkan pins |
| `.deps/src/PS5_PayloadSDK` | payload SDK fork | `b83202b`, as PS5_Vulkan pins |
| `.deps/src/ps5-homebrew-dev-protocol` | launch payload for the deploy script | `51dc332` |
| `.deps/src/PS5_Vulkan/.deps/native/ps5-payload-sdk` | the SDK, installed | |
| `.deps/src/PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a` | RADV | |
| `.deps/src/PS5_Vulkan/build/host/ps5-native-tool` | fSELF converter | |
| `.deps/src/PS5_Vulkan/runtime/libc.prx` | clean-room libc | |
| `.deps/ps5-payload-sdk` | link to the SDK above, for the launch payload | |

`scripts/build-app.sh` also fetches ps5-opengl through ps5-homebrew-ui's pinned, checksummed script.

## Troubleshooting

- `git am` fails in PS5_Mesa: `patches/mesa` no longer fits the pinned Mesa revision; delete
  `.deps/src/PS5_Mesa` and fix the patch or the pin.
- Out of disk: the RADV build tree is several GiB; CI frees the runner's preinstalled toolchains first.
- `fatal: detected dubious ownership` in a container: the image sets `safe.directory '*'`; a different
  image needs `git config --global --add safe.directory '*'`.
