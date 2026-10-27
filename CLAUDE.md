# CLAUDE.md: PS5SD

PS5SD runs stable-diffusion.cpp on the PS5 GPU (ggml-vulkan on RADV). It is a title, `PPSA99576`, for
ShadowMountPlus: sd-server with a web UI, A1111 and OpenAI compatible APIs, a model downloader, a dashboard on
the TV and a fan payload. It is listed in the PS5 homebrew catalog
([darkxex/ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog), `apps/PPSA99576.json`).

The test console is FW 13.00 at `10.0.0.101` (etaHEN, ShadowMountPlus, elfldr, ftpsrv, klogsrv).

## Read first

| File | What it holds |
|---|---|
| `README.md` | What the title does, with one sentence per release in the top paragraph |
| `ROADMAP.md` | What comes next, and a "Done" line per release |
| `docs/CONSOLE.md` | Measurements on the console, each with its date and build. Read it before you change behaviour that a measurement justified |
| `docs/BUILDING.md` | The toolchain, `.deps` and its pins, building without a display |
| `docs/PORTING.md` | How the app is put together: server hooks, downloader, catalog, routes |
| `docs/API.md`, `docs/MODELS.md` | User docs. The title ships both, and the web UI's Help tab shows them |

## Structure

| Path | What it is |
|---|---|
| `ps5/app/` | The title: `main.cpp`, the dashboard, the downloader (`downloader.cpp`, `download_core.cpp`, `ftp_writer.cpp`, `https_client.cpp`), the model library (`models.cpp`), memory planning (`mem_fit.cpp`, `sd_plan.cpp`), `catalog.hpp` (the starter catalog and its presets) |
| `ps5/app/sce_sys/` | `param.json`, icon and backgrounds |
| `ps5/app/tests/` | Host tests (CMake, ctest), against stable-diffusion.cpp on the CPU |
| `ps5/webui/src/` | The web UI: `page.html`, `app.css`, `js/*.js`. The JS parts share one closure (`00-start.js` opens it, `23-start-up.js` closes it) |
| `ps5/fan/` | The fan payload (`fan_policy.h` has the modes) |
| `patches/sdcpp`, `patches/ggml`, `patches/mesa`, `patches/ftpsrv` | Every change to the submodules and to PS5_Mesa. The submodules stay clean; `build-app.sh` applies the series |
| `third_party/` | Submodules: stable-diffusion.cpp, ps5-homebrew-ui, ftpsrv |
| `scripts/` | Build, deploy, packaging and dev tools (below) |
| `docker/` | The Arch build image; `packages.txt` is the one package list, CI uses it too |

## Build

The toolchain runs on x86-64 Arch Linux only (in Docker, WSL2 or CI).

- **CI.** Every push to `main` builds the title in `.github/workflows/build.yml`. Job `title` gives the
  artefacts `ps5sd-title`, `ps5sd-tools` and `ui-preview`. Job `host` runs the host tests.
- **Fetch CI's build.** `scripts/fetch-artifact.sh [run-id]` puts the title in `build/app-ps5/title/` and the
  launch payload in `build/tools/`. The PC then needs no toolchain.
- **Local.** `scripts/setup-deps.sh` once (slow: RADV, the payload SDK), then `scripts/build-app.sh`. Docker:
  see `docs/BUILDING.md`. `build/local-sync.sh` copies the Windows tree into a Docker build volume.
- **Docker on this PC.** Docker Desktop runs here, so the title builds locally; do not leave a change to
  `ps5/app` or the patches as "only CI can build it". Once: `docker build -t ps5sd-build docker/`. Then (the
  volume keeps `.deps`, so `setup-deps.sh` is slow only the first time):

  ```sh
  MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/heel/Documents/repos/PS5SD":/src:ro -v ps5sd-work:/work \
    ps5sd-build bash -c "bash /src/build/local-sync.sh && scripts/setup-deps.sh && scripts/build-app.sh"
  ```

  The same works for sd-server on the CPU: build `third_party/stable-diffusion.cpp` with the series applied
  (`-DSD_BUILD_EXAMPLES=ON -DSD_SERVER_BUILD_FRONTEND=OFF`) in a Linux container with a published port, start
  it with a model from `models/`, and point `scripts/webui-dev.py --target` and the browser at it. So the
  server's routes and the web UI are tested in a browser before the console. In Git Bash, set
  `MSYS_NO_PATHCONV=1` for `docker exec` and `docker run`, or `/build` becomes a Windows path.
- **Display.** `PS5SD_DISPLAY=auto|1|0`. A release needs a display build (`package-release.sh` checks for
  `assets/fonts`).

## Tests and dev tools

- **Host tests.**

  ```sh
  cmake -S ps5/app/tests -B build/app-tests -DCMAKE_BUILD_TYPE=Release
  cmake --build build/app-tests && ctest --test-dir build/app-tests --output-on-failure
  ```

  New logic without console code goes into a host-testable file with a test in `ps5/app/tests/`.
- **Web UI without a build.** `python scripts/webui-dev.py --target http://10.0.0.101:7860` serves
  `ps5/webui/src` and forwards the API to the console. `--mock-downloads`, `--mock-models` and
  `--mock-standby` fake the routes, so the UI works without a console and a delete cannot touch it.
- **Dashboard without a console.** `scripts/preview-app-ui.sh` renders the TV screens into `build/app-preview/`.
- **Server on the PC.** `scripts/host-server.sh` builds sd-server natively (CPU or Metal) on `127.0.0.1:7860`.

## Console

`scripts/ps5sd-app.sh` drives the title over ftpsrv (2121), elfldr (9021) and klogsrv (3232):
`status`, `deploy [--force]`, `launch`, `close`, `cycle` (close, deploy, launch). Logs are in
`/data/PS5SD/app.log` and the kernel log.

Facts that bite (`docs/CONSOLE.md` has the measurements):

- **Write limit.** The title's own writes drop to about 2 MB/s after 1.4 GB. The downloader therefore writes
  through the bundled ftpsrv on loopback (`ftp_writer.cpp`, `patches/ftpsrv`). Keep it that way.
- **One memory pool.** `malloc`, the weights and the GPU buffers all come from the same direct memory. A full
  CPU heap ends the title, and freed GPU memory comes back in pieces. Measure memory changes on the console,
  with the model that needs most (FLUX.2 Klein 9B with a LoRA).
- **Close.** `ps5sd-app.sh close` asks the app to end itself (`/data/PS5SD/quit`). The home screen's Close
  Game suspends the title before it kills it; `docs/CONSOLE.md` has what the app does then.
- **Numbers.** Times and sizes in the docs and the catalog come from runs on the console. Add new ones to
  `docs/CONSOLE.md` with the date and the build.

## Versions

- `PS5SD_VERSION` in `ps5/app/version.hpp` is semver, for example `0.9.14`.
- `contentVersion` in `ps5/app/sce_sys/param.json` follows it as major+1, minor, patch: `0.9.14` is
  `01.009.014`. Consoles compare this one.
- Bump both together. `build-app.sh` fails when they disagree.
- The tag is `v<version>`, the release title `PS5SD <version>`.

## Releasing

1. **Bump the version** (above). Add the release's sentence to the top paragraph of `README.md` and its line
   under "Done" in `ROADMAP.md`. Commit as `PS5SD <ver>` and push `main`.
2. **Build.** Do not wait for CI: build the release commit in Docker (Build, "Docker on this PC") with a clean
   working tree (`git status` empty, since `local-sync.sh` copies the tree as it is), and copy
   `/work/build/app-ps5/title/PPSA99576` from the volume to `build/app-ps5/title/`. The `build` run of that
   commit goes on in CI as a check. Without Docker: wait for that run, then `scripts/fetch-artifact.sh <run-id>`.
3. **Package.** `scripts/package-release.sh` makes `dist/PPSA99576.zip` and `dist/PPSA99576.json` (the
   catalog record with the zip's SHA-256 and the icon URL pinned to the tag). Nothing is uploaded.
4. **Tag and release.**

   ```sh
   git tag v<ver> && git push origin v<ver>
   gh release create v<ver> dist/PPSA99576.zip --title "PS5SD <ver>" --notes-file notes.md
   ```

   The notes: one line on what the release finishes (with the issue links), a section per change, then
   "Install" (the homebrew store, or unpack `PPSA99576.zip` into `/data/homebrew/` and let ShadowMountPlus
   scan) and "Next" (`ROADMAP.md` at the tag). Never replace an asset of a published release; a fix is a new
   version.
5. **Catalog.** The catalog's `ps5-catalog-bot` checks every listed app for a newer release three times a
   day (07:37, 15:37 and 23:37 UTC, `.github/workflows/updates.yml` upstream). It opens one PR with all
   updates, and a maintainer merges it (0.9.14 came in this way, in #874). So first look at what upstream
   lists:

   ```sh
   gh api repos/darkxex/ps5-homebrew-catalog/contents/apps/PPSA99576.json --jq .content | base64 -d
   gh pr list --repo darkxex/ps5-homebrew-catalog --search PPSA99576 --state all --limit 5
   ```

   Open a PR yourself only if the version is still old and no bot PR has it. Use the fork
   `Darkatek7/ps5-homebrew-catalog`:
   1. Branch `ps5sd-<ver>` from upstream `main`.
   2. Copy `dist/PPSA99576.json` to `apps/PPSA99576.json`.
   3. Run `python -m catalog check` and `python -m catalog verify PPSA99576`.
   4. Push, then `gh pr create --repo darkxex/ps5-homebrew-catalog --title "PS5SD <ver>"`. The body: one
      paragraph on the changes, the release link, and the line that check and verify pass.
6. **Close the issues** the release finishes, with a link to the release.

## Working rules

- **Ask first.** Pushing `main`, tags, GitHub releases, catalog PRs and anything posted on GitHub need the
  user's OK.
- **Language.** Talk to the user in German. Write GitHub text (commits, releases, PRs, issues) and the docs in
  English.
- **Docs style.** Plain, short sentences, as in the existing docs. Keep `docs/API.md` and `docs/MODELS.md`
  in step with what the title does.
- **Staging.** Stage files by name, never with `git add -A`, and check `git diff --cached` before you commit.
  `build/`, `dist/`, `out/` and `models/` are not committed.
- **No trailer.** Commits get no `Co-Authored-By: Claude` line.
- **Subagents.** Hand self-contained changes to subagents: haiku for mechanical work, sonnet for normal work,
  always with an explicit model. The main session plans, reviews the diff, tests and commits. Subagents never
  commit or push, and two of them never edit the same files at the same time.
