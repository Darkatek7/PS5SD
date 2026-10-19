# On the console

Measurements of PS5SD on a real console. Each section names the build and the console it was taken on;
the figures come from `/data/PS5SD/app.log` and the kernel log (klogsrv), not from estimates.

## First run (2026-10-19)

| | |
|---|---|
| Console | FW 13.00, etaHEN, ShadowMountPlus, elfldr, ftpsrv, klogsrv |
| Build | CI run 39299629364 (`3771013`), without a display (`PS5SD_NO_DISPLAY`) |
| stable-diffusion.cpp | `b13a482` |
| Model | `sdxs-512-tinySDdistilled_Q8_0.gguf` (683 MB; f16 401 tensors, q8_0 209) |
| Request | the defaults: `a photograph of an astronaut riding a horse`, 512x512, 1 step, CFG 1, euler_a, seed 42 |
| Result | a correct 512x512 RGB PNG in `/data/PS5SD/outputs` |

### Start and end

- `scripts/ps5sd-app.sh deploy` uploads the title folder but does not register it. ShadowMountPlus has to
  rescan (`POST http://<console>:10101/api/v1/scan`, body `{"reset_attempts":true}`); after that
  `/user/app/PPSA99576` and `/user/appmeta/PPSA99576` exist and the launch payload can start it.
- `launch.elf` started the title: `EXEC /app0/eboot.bin [user] ... category=native_game`, then the payload
  exited with 0. The eboot's container magic is `4F 15 3D 1D` (the PS4 one; `libc.prx` has the PS5 one,
  `54 14 F5 EE`), and the system loads it anyway.
- A title that never opens a video output is not closed by the shell. It hid the splash and ran to the end;
  `PS5SD_VIDEOOUT` was not needed.
- `scripts/ps5sd-app.sh close` ended it within a few seconds. The kernel log shows
  `[AppMgr] Exit spawn failed : 0x80aa001a ELF path=exit`, followed by `onAppExit()` and `ProcessTerm()`:
  that failure is how `sceSystemServiceLoadExec("exit")` ends a title, not an error of the app.

### Devices and threads

- Vulkan0 is `PlayStation 5 GPU (RADV NAVI21)` with 11.44 GiB (12,280,922,112 bytes): the unified heap,
  the same pool as the CPU side's direct memory, as PS5LM measured.
- The CPU backend reports 2.13 GiB (2,291,924,992 bytes); it holds no weights in this run.
- `n_threads` is 11. By `patches/sdcpp/0002` (the mask's popcount minus two) the title's affinity mask has
  13 CPUs, against PS5LM's 5 for a payload.

### Times

| Phase | Time |
|---|---|
| Context (metadata only; weights are loaded lazily) | 0.6 s |
| Text encoder: load 196 tensors | 0.86 s |
| Text encoder: compute | 1.00 s |
| UNet: load 280 tensors, then 1 step | 0.72 s, then 1.24 s/it (1.25 s sampling) |
| TAESD: load 67 tensors, then decode | 0.20 s, then 0.32 s |
| `generate_image`, end to end | 2.58 s |

The text encoder's time includes its first-use costs; a second image in the same process should show how
much of the 1.00 s and the 1.24 s/it is warm-up. Not measured yet.

The auto probes on Vulkan0: flash attention on (normal 1.329 ms, flash 0.787 ms); conv2d and conv3d direct
off ("probe output check failed").

### Memory

`pool free` is the free part of the title's direct memory (`sceKernelAvailableDirectMemorySize`), out of
11.44 GiB. `cpu heap` is the SDK platform heap that `malloc` is wrapped onto.

| Point | Pool free | CPU heap (peak) |
|---|---|---|
| Before loading | 11.37 GiB | 0.05 GiB (0.05) |
| After loading the context | 11.21 GiB | 0.19 GiB (0.19) |
| After generating | 10.08 GiB | 0.39 GiB (0.64) |
| After freeing the model | 10.08 GiB | 0.39 GiB (0.64) |

- Weights: 651.92 MB, all on Vulkan0 (text encoder 159.13 MB, UNet 488.12 MB, TAESD 4.67 MB).
- Compute buffers, peak per phase: CLIP 1.42 MB, UNet 118.07 MB, TAESD decode 480.06 MB. At 512x512 the
  decoder needs four times the UNet's buffer.
- So one SDXS image at 512x512 costs about 1.3 GiB of the pool, and leaves about 10 GiB free.
- Freeing the model released 649.58 MB of parameter buffers in sd.cpp, but the pool's free figure did not
  rise: the memory stays with the allocator (RADV or the platform heap) rather than going back to the
  system. Whether a second load reuses it is not measured yet.

### Not measured yet

- SD-Turbo (`sd_turbo-f16-q8_0.gguf`, already on the console) and larger images.
- A second image in the same process (warm times, memory reuse).

## With a display (2026-10-19)

CI run 39307383624 (`0eb661e`), the first build with ps5-opengl (source-built at `217da45`), run one-shot
(`PS5SD_ONESHOT=1`) with the same request as above.

- `display: egl 1.4, 1920x1080, PS5 AGC`, then `screen ready`: the GL stack and RADV share the console.
- The screen costs about 0.35 GiB of the pool: 11.02 GiB free before loading, against 11.37 without it.
- `generate_image` took 1.9 s (2.6 s on the first run). The PNG is byte for byte the first run's.
- `close` ended it as before.

## The server, first start (2026-10-19)

Same build, server mode. It stopped before loading the model with a `std::filesystem` exception on the
path `""`: sd-server checks its embeddings directory (`--embd-dir`, empty by default) with the throwing
`fs::exists`, and on the console `stat("")` fails with `EINVAL`, where Linux gives `ENOENT`. Fixed in
`759699f` by never leaving that directory empty (docs/PORTING.md).

## The server (2026-10-19)

CI run 39310117858 (`759699f`), with a display, SDXS Q8_0.

- The model loaded and the server answered on `0.0.0.0:7860` within seconds; the app's loopback probe
  saw it and switched the status to "Ready". Pool free with the model loaded: 10.90 of 11.44 GiB.
- From the PC: `/sdcpp/v1/capabilities`, `/sdapi/v1/sd-models`, `/v1/models`, `/sdapi/v1/loras`, the
  web UI at `/` (28,600 bytes) and `/ps5sd/v1/gallery` (it lists `/data/PS5SD/outputs`) all answer.
- `POST /sdapi/v1/txt2img`, 512x512, one step, CFG 1, euler_a: 2.33 s for the first request, then
  0.28 to 0.32 s per image, measured on the PC around the whole request (HTTP, generation, PNG, base64).

## SDXL in the server (2026-10-19)

Local build (`51a0264`), with a display, SDXL Base 1.0 Q8_0 (5.04 GB) and `sdxl_vae_fp16_fix`
(`--vae`), both from `server-args.txt`. Two `POST /sdapi/v1/txt2img` requests, 1024x1024, 25 steps,
CFG 6, euler_a.

| Part | Time |
|---|---|
| Whole request | 77.4 s (the second one the same) |
| Sampling | 66.4 s: 2.46 s per step, 7.2 s for the first |
| Conditioning (both text encoders) | 4.3 to 4.6 s |
| VAE decode | 6.3 s, tiled (below) |
| Weights read for the request | about 8.8 s of the above: 3.9 s text encoders, 4.4 s UNet, 0.3 s VAE |

- The server loads the weights again for each request (the model manager keeps the context but not the
  tensors on the device). `--eager-load` has not been tried yet.
- Pool free: 11.02 GiB before loading, 10.89 GiB with the server up, 5.61 GiB after a request (the
  allocator keeps what it freed).
- The VAE decode wanted 6.05 GB at once with 5.74 GB free, so sd.cpp fell back to 512x512 tiles by
  itself. The image shows no seams.

## Model switch, IllustXL and speed (2026-10-19)

Local build (model switch, `patches/sdcpp/0054`), with a display. Every variant below was a preset,
started through `POST /ps5sd/v1/app/models/active` without restarting the title.

- A switch takes 10 to 18 s from the request to "Ready": the server stops (after the job that runs),
  frees the model and loads the next one. The allocator reuses what the old model freed: from SDXL Q8_0
  to IllustXL f16, pool free went from 5.54 to 2.56 GiB, not by the new model's full 7 GB.
- Error answers: 404 for an unknown id, 400 for a body without one, 409 for a second switch while the
  first one runs.
- `--eager-load` reads every weight while the model loads (8.1 s for SDXL Q8_0), so the first request
  does not (68.4 s instead of 77 s). It does not keep them, see the VAE below.

SDXL Base Q8_0, 1024x1024, 25 steps, CFG 6, euler_a, seed 11:

| Variant | Request | Sampling | VAE decode |
|---|---|---|---|
| server defaults, second request | 76.4 s | 65.9 s | 5.7 s (tiles after an out-of-memory) |
| `--vae-tiling` (256 px tiles) | 70.3 s | 61.8 s | 8.0 s |
| `--vae-tiling --vae-tile-size 512` | 68.3 s | 61.8 s | 5.6 s |
| the same with `--conv-direct on` | 76.7 s | | |
| the same with `--cache-mode ucache` (`reset=0`) | 69.7 s | 63.2 s, no step skipped | |
| the same with `--cache-mode ucache --cache-option threshold=1.5` | 42.1 s | 35.6 s, 11 of 25 steps skipped | |
| `--vae-tiling --vae-tile-size 512`, build without a display | 67.9 s | 61.5 s | 5.5 s |

- **The reload on every request came from the VAE.** Without tiling, the decode of a 1024x1024 latent
  wants 5.95 GB at once. The model manager first moves the text encoders' and the UNet's weights out to
  make room, then still falls back to tiles. The next request reads those weights in again (about 9 s).
  With `--vae-tiling` from the start nothing is moved out and requests stay at 68 s.
- Direct convolution: sd.cpp's probe turns it off on this GPU ("probe output check failed"). Forced on,
  it is slower.
- UCache at threshold 1.5 gives the same scene with slightly softer detail, at 62% of the time
  (preset `sdxl-fast`).
- The dashboard costs nothing measurable: 2.46 s per step with and without the display build.
- Raehoshi IllustXL 1.1 (an SDXL finetune, 6.9 GB f16 safetensors, with the SDXL fp16-fix VAE), 1024x1024,
  28 steps: 185 s in f16, at 6.3 s per step, because with 2.56 GiB free the model manager streams UNet
  weights in during sampling. With `--type q8_0` (converted while loading, 13.9 s) it takes 75.6 s at
  2.46 s per step, and the image is almost the same.

## FLUX.3 Image Klein (2026-10-19)

Same build, preset `flux3-klein-q4`: `--diffusion-model flux3-image-klein-Q4_K.gguf` (leejet, 3.51 GB),
`--llm Qwen3VL-4B-Instruct-Q4_K_M.gguf` (2.50 GB), `--vae flux2-vae.safetensors`, `--vae-tiling
--vae-tile-size 512`. Two `POST /sdapi/v1/txt2img` requests, 1024x1024, 16 steps, CFG 1, euler.

| Part | First request | Second request |
|---|---|---|
| Whole request | 139.8 s | 127.7 s |
| Sampling | 124.3 s | 114.3 s, 7.1 s per step |
| Conditioning (Qwen3-VL) | | 7.1 s, of it 6.9 s reading its weights again |
| VAE decode (tiled) | 6.0 s | 5.7 s |

- The images are good, and text in the prompt comes out right ("a sign that says 'PS5SD'").
- The switch from SDXL took about 5 s to "Ready": sd.cpp loads FLUX's weights on the first request.
- Pool free after a request: 3.99 of 11.44 GiB. The text encoder's weights are moved out for the
  sampling and read in again by the next request (6.9 s).
- A ComfyUI int8 (`int8_tensorwise`, convrot) export of Klein from Civitai (5.83 GB) does not load:
  `sd_server()` returns 1 right after it has opened the three files, with no error in the log, with the
  int8 text encoder and with the Qwen3-VL GGUF alike. Not looked into further.

## The model downloader (2026-10-19/20)

Local builds with the downloader (16 connections, 8 MiB pieces), FW 13.00, Hugging Face over the
xet CDN. The PC on the same network fetched SDXS (683 MB) through the same code at 82 MB/s.

- `POST hf/search` (DNS through libSceNet, TLS with mbedTLS): answer in 0.57 s.
- FLUX.3 Klein Q6_K (4.87 GB), first build (each 256 KiB chunk `pwrite`n where it belongs, the
  `.part` `ftruncate`d to its size first): 66, 48, 20, 8, then 1 to 2 MB/s. A cancel stopped the
  transfer in about 1 s; the resume kept the 1.70 GB on disk; the run that finished it took 690 s
  (7.06 MB/s on average). The preset was written at the end.
- An SDXL Q8_0 generation while a download ran: 30 to 94 s per step instead of 2.46 s, and the
  download dropped to about 1 MB/s.
- Z-Image Turbo Q4_K (3.86 GB), the same build with timings: 64.6, 47.5, 18.3, 24.3, 7.9, 8.4, then
  0.5 to 2.3 MB/s, with the connections together in `pwrite` 9 to 23 s per second and an `fsync` taking
  up to 7.6 s. No piece failed.
- Whole pieces, one `pwrite` at a time, no `ftruncate` (the build now in the tree): the rest of the
  Z-Image file (1.25 GB) at 72 MB/s right after a start, done in 20 s; the next file (Qwen3-4B
  Q4_K_M, 2.50 GB, new) 28, 28, 22, 10, then 1.94 MB/s steady, with the single writer busy about 1 s
  per second and the `fsync` every 5 s taking 3 to 8 s.
- Two unplanned ends of the title (closed from the home screen; with no suspend point the system
  kills it after 40 s): the next start carried on from the `.part` (1.90 of 2.50 GB kept), not from
  zero.

Disk only, no network (`PS5SD_DL_WRITETEST`): 8 MiB `write()`s one after the other from the title,
90 s at most per test, 30 s pause between tests, in this order, while the downloader was idle:

| Test | Rate over time | Average |
|---|---|---|
| `/data`, new file, no `fsync` | 224, 230, 162, 26 MB/s | 1500 MB in 11.6 s |
| `/data`, new file, `fsync` every 64 MB | 34, 26, 37, 21, then 9 to 12, then 1.5 to 2.9 MB/s; single writes up to 6.7 s | 956 MB in 92.7 s |
| `/data`, preallocated (`ftruncate`), no `fsync` | 12.9, 8.7, then 1.9 MB/s; single writes up to 8.6 s | 235 MB in 92.8 s |
| USB (`/mnt/usb0`, exFAT), new file | 39, then 8 to 16 MB/s | 1107 MB in 90.3 s |
| `/data`, new file, no `fsync`, again | 9 to 16, then 1.5 to 2.9 MB/s | 461 MB in 92.4 s |

- About 1.4 GB go in at 230 MB/s; after that the title's writes to `/data` slow to about 2 MB/s, and
  30 s of rest do not bring the rate back. With or without `fsync` and preallocation the floor is the
  same. Not established: whether the limit is per process, a write budget, or the writeback of a
  cache that the first 1.4 GB filled.
- For comparison, ftpsrv (a payload) took a 1.5 GB upload from the PC into `/data` at 13.4 MB/s
  sustained, with the title closed. The network may limit that number.

## Close from the home screen (2026-10-20)

Local build of the current tree (0.1.0, the downloader's new write path), with a display, plus a klog
heartbeat of the main loop for this test. The title was closed through the home screen's options menu
("Close Game") while the kernel log was recorded.

What the system does on "Close Game":

1. SceShellUI shows `AppClosingScene`, then `KillApp() appId=... withSuspend={1}, forceKill={0}`.
2. SceLncService suspends the title first (`suspendApp ... before kill`, `Suspend process`). The kernel
   needs the title to have passed a GPU suspend point (`sceAgcSuspendPoint`) recently. RADV passes one
   after every submission, ps5-opengl at the next present after a frame's submission. The kernel then
   saves the running waves itself (`gc_acq_cwsr_suspend_internal: all acqs are suspended`).
3. `BlockingKill()`, `Kill process`, `ProcessTerm()`.

No event reaches the title before it is suspended: `sceSystemServiceReceiveEvent` returned none, and
`sceSystemServiceGetStatus` only showed the title in the background. So a title cannot run its own quit
path (`/data/PS5SD/quit`) on a home-screen Close.

| Case | Suspend | Close to `ProcessTerm()` | 0xa0d0c00f / coredump |
|---|---|---|---|
| Idle (sdxl-base loaded) | 92 ms | 0.3 s | none |
| Downloading, 70 MB/s, fsync every 5 s | 300 ms | 0.5 s | none |
| Downloading, fsync only at the end (`PS5SD_DL_SYNC_S=0`) | 694 ms | 0.9 s | none |
| SDXL 1024x1024 generation, step 4 of 25 | 253 ms | 0.5 s | none |
| Downloading, main loop stalled (below) | never | killed after 7.8 s | **0xa0d0c00f, coredump** |

The failing case (00:33, pid 191, a 6.9 GB download at 65 to 70 MB/s, no generation):

- The main loop had stalls while the download ran: one pass took 13.4 s, several 1 to 2 s. From
  00:32:46 on it stopped completely; the heartbeat never came back. A separate thread of the title kept
  running.
- The Close came at 00:33:08. The kernel logged `Suspend pid=191 phase3 is postponed`, then
  `### Can't suspend pid:191 by timeout. (No suspendPoint for 25sec)` and
  `# exception: 0xa0d0c00f (CPU_FAULT_SUSPENDPOINT_TIMEOUT_IN_SUSPEND_ASYNC)`. The graphics queue showed a
  frame in flight that never finished (`gc_gfx_queue_status : in_frame=1`, `EVENT_WRITE does not
  finish`). The first failure (00:04, during a download on the earlier write path) looked the same.
- So the timeout is counted from the title's last suspend point, not from the Close. While the main loop
  is stuck, ps5-opengl passes none, and an idle server (RADV) passes none either.
- What stalls the main loop during a download is not known yet. It does not take the downloader's lock.
  Candidates: the GL present (`eglSwapBuffers`) waiting on that unfinished frame, and the once-a-second
  `access()` of `/data/PS5SD/quit`, on the disk the download is writing to.

Other observations:

- The download queue carries on after a Close: an SDXL checkpoint (6.94 GB) went on from the bytes
  already on disk after each of two Closes and finished complete.
- In the background the main loop keeps drawing at 60 Hz while idle. During a generation the frames
  take 300 to 870 ms, and the kernel logs `### suspendPoint timeout 200msec`; that did not stop the
  suspend.

Not done yet: a title thread that passes `sceAgcSuspendPoint()` every 100 ms, so the 25 s are never
reached while the main loop is stuck. It was built and run, but only in cases that closed cleanly
without it; the stalled case was not tried with it.
