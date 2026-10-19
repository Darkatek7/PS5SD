# Building PS5SD

The app is a PS5 title (`ps5/app`): stable-diffusion.cpp linked with ggml-vulkan against
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s RADV build, a dashboard drawn with
[ps5-homebrew-ui](https://github.com/Phononenom/ps5-homebrew-ui), packed as a title folder for
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
| `.deps/ps5-opengl-sdk` | ps5-opengl (EGL, OpenGL 4.6), for the screen; `.deps/ps5-opengl-sdk.stamp` says where it came from | 1.0.0 (`dce7491`) |

### ps5-opengl

The screen draws through [ps5-opengl](https://github.com/leolobato/ps5-opengl). ps5-homebrew-ui pins
its 1.0.0 release download, which went away with the `blackbearreloaded` GitHub account (October
2026). `scripts/build-opengl-sdk.sh` (run by `setup-deps.sh`) sets the SDK up in `.deps/ps5-opengl-sdk`:

1. kept when it is there already, made from the same pins, and its `manifest.sha256` still verifies;
2. else the original release archive, accepted only with its pinned sha256 and manifest (so if the
   release comes back, nothing is built);
3. else built from source: `leolobato/ps5-opengl` (a fork with the original history) at `dce7491`,
   the commit of the v1.0.0 tag, the way its release workflow built 1.0.0: payload SDK v0.42
   (`ps5-payload-dev/sdk`, sha256-pinned), the `dynamic` display profile (2160p120 capacity, 1080p60
   at start, the mode chosen at run time), present/multi-draw/deferred-draw batching on. Its own
   `tools/fetch-sources.py` fetches Mesa 26.2.0 and the opengnm, opengnm-psbc, SPIRV-Headers,
   Vulkan-Headers, imgui, nanovg and sokol commits it pins and checks them. The steps are those of
   its `make sdk-gl46` minus the host-only shader-cache test (it calls `clang-18` by name, which
   Arch does not have). About 30 to 60 minutes; CI caches the result on its own
   (`ps5-opengl-sdk-<hash of the script>`). The fork's `main` adds a varying-linkage fix and later
   work; 1.0.0 is what ps5-homebrew-ui and PS5LM were built and run against, so that is the pin
   (`PS5_OPENGL_REV` overrides it). A source-built SDK is not byte-identical to the release (other
   compiler, Arch's LLVM through the SDK's `llvm-config`), so its manifest differs.

`scripts/build-app.sh` takes `PS5_OPENGL_SDK` (`scripts/env.sh` exports it), else
`.deps/ps5-opengl-sdk`, else tries the kit's own download.

### Building without a display

Without a ps5-opengl SDK there is no screen. `PS5SD_DISPLAY` says what `build-app.sh` does
(`setup-deps.sh` skips ps5-opengl with `PS5SD_DISPLAY=0`, and fails without it with `1`):

| `PS5SD_DISPLAY` | Build |
|---|---|
| `auto` (default) | with the screen when an SDK is found, else without one (and a warning) |
| `1` | with the screen, or fail |
| `0` | without a screen: no ps5-opengl and no ps5-homebrew-ui in the title; the app reports its state in `app.log`, the kernel log and notifications |

```sh
PS5SD_DISPLAY=0 scripts/build-app.sh
docker run --rm -it --platform linux/amd64 -e PS5SD_DISPLAY=0 -v "$PWD":/work ps5sd-build scripts/build-app.sh
```

## Troubleshooting

- `git am` fails in PS5_Mesa: `patches/mesa` no longer fits the pinned Mesa revision; delete
  `.deps/src/PS5_Mesa` and fix the patch or the pin.
- Out of disk: the RADV build tree is several GiB; CI frees the runner's preinstalled toolchains first.
- `fatal: detected dubious ownership` in a container: the image sets `safe.directory '*'`; a different
  image needs `git config --global --add safe.directory '*'`.
