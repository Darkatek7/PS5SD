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
  largest free part only; what was freed is free in smaller parts, see "Why freed GPU memory is not usable
  again").
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
  same. The limit is on the title's process; see the next section.
- ftpsrv (a payload) took a 1.5 GB upload from the PC into `/data` at 13.4 MB/s, with the title
  closed. A later upload ran at 83 MB/s (next section), so that number was not the disk's limit.

## The write limit is the title's, and the way around it (2026-10-20)

The limit is on the title's process, not on the disk: in the same minute that the title's writes were
down to 2 MB/s, ftpsrv wrote 4.86 GB from the PC into `/data/PS5SD/models` at a steady 83 MB/s
(59 s). A fresh start of the title gets the 1.4 GB at full speed again.

So the downloader now has ftpsrv write the files (`ftp_writer.cpp`): one FTP connection on
`127.0.0.1:2121`, and for each piece, in file order, `REST <offset>`, `STOR <part path>` and the piece
over a data connection. ftpsrv's `REST n` + `STOR` cuts the file at n and writes from there; a `REST`
past the end is refused (`551 Restart point beyond EOF`). Absolute paths with spaces work in `STOR`,
`SIZE` and `RNFR`/`RNTO`.

Disk only, from the title (`PS5SD_DL_WRITETEST`), one test right after the other in one process:

| Test | Rate over time | Average |
|---|---|---|
| `/data`, the title's own 8 MiB `write()`s | 223, 226, 216, then 64, 26, 21, 12, then 8.8 to 11.6 MB/s | 2282 MB in 61.2 s |
| the same, right after, through ftpsrv (8 MiB `STOR`s) | 78 to 89 MB/s from start to end; slowest piece 0.17 s | 6000 MB in 71.6 s |

The title could `unlink` the file that ftpsrv had made (uid 0, mode 0666).

With the downloader writing through ftpsrv (16 connections, 8 MiB pieces):

- SDXL Base 1.0 from the catalog (6.94 GB): 52 to 74 MB/s from start to end, done in 110.4 s
  (62.8 MB/s), the preset written. The sha256 of the file on the console (read back over FTP) is
  Hugging Face's.
- The same file, closed with `scripts/ps5sd-app.sh close` at 3.1 GB and started again: the `.part`
  held exactly the 392 pieces the record had, the next run kept them (3,288,334,336 bytes) and fetched
  the rest in 59.7 s. sha256 as above.
- The same file, closed from the home screen at about 2 GB (the system suspends and kills the title,
  so the last 5 s of the record are lost): the `.part` held 2,175,399,872 bytes, the record 232 done
  pieces. The next run kept those (1,946,157,056 bytes), its first write cut off the rest, and it
  finished; sha256 as above.

### When ftpsrv is not running

The title carries its own ftpsrv build (`/app0/ftpsrv/ps5sd-ftpsrv.elf`, from `third_party/ftpsrv` at
1.16-ng-stable with `patches/ftpsrv`). That copy listens on `127.0.0.1:2122` only, for both control and
passive data connections. It runs under the process name `ps5sd-ftpsrv`, so it and a user's
`ftpsrv.elf` never kill each other at start. A download or an upload first tries 2121, then 2122. If
nothing listens on either, the first one sends the ELF to elfldr on 9021, at most once per run of the
app, and waits up to 5 s for 2122. When it still does not answer, the file is written directly, as
before. A copy that was started stays up until the console restarts, so the next run of the app finds
it on 2122.

Measured on 2026-10-20 with a local build:

- The user's ftpsrv was stopped with its `KILL` command first, so nothing listened on 2121 or 2122.
- The title was started and SDXL Base 1.0 (6.94 GB) was downloaded from the web UI's API.
- The first download sent the ELF. `ps5sd-ftpsrv` answered on 2122 0.2 s later, and the whole file
  went through it.
- The rate was 37 to 54 MB/s the whole way, also past 1.4 GB, ending at 45.7 MB/s average (151.8 s).
  The title never fell to its 2 MB/s floor.
- The writer was busy 0.46 to 0.65 s per second, and no piece waited for it. So the download side set
  the rate, not the disk. The 62.8 MB/s run above went through the user's ftpsrv on another day.
- From the PC, 2122 stayed closed (loopback only).
- The file came out at full size and was renamed in place.

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
| Downloading through ftpsrv, 70 MB/s, at 2 GB of 6.9 | not postponed | not timed | none |
| Idle, quit file watched on its own thread (sdxl-base loaded) | not postponed | not timed | none |
| 6 Closes in a row, each followed by a new start (5 while a download ran through ftpsrv, 1 just after it finished) | not postponed, all 6 | not timed | none |

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

What changed since the failures: the title no longer writes or `fsync`s the downloaded file itself
(ftpsrv does, above), and the quit file is looked for on a thread of its own, so an `access()` that
waits on the disk cannot hold up the main loop. The failure did not come back in 8 Closes (the
table's last three rows; the first of them still without the quit thread); the old build failed 1
in 5. Which change removed the cause is not established.

Not done: a title thread that passes `sceAgcSuspendPoint()` every 100 ms, so the 25 s are never
reached while the main loop is stuck. It was built and run, but only in cases that closed cleanly
without it; the stalled case was not tried with it.

## iLustMix portrait 832x1216: 73 s to 31 s (2026-10-20)

CI build `759699f` (before 0.1.0), FW 13.42. iLustMix v11.1 (an Illustrious SDXL finetune, 7.1 GB f16
safetensors with its own fp32 VAE), loaded from `model.txt`, options from `server-args.txt`.
`POST /sdapi/v1/txt2img`, CFG 6, seed 11, the time around the whole request on the PC.

832x1216, the size Illustrious is trained for, with `--type q8_0 --vae-tiling --vae-tile-size 512
--eager-load`:

| Sampler | Request |
|---|---|
| euler_a, 25 steps (the model page's recommendation) | 73.0 s |
| dpm++2m, `ays`, 10 steps (`presets/ilustmix-portrait.txt`) | 30.5 s (35.1 s in another run) |

- Side by side the same scene, as sharp, with a little more contrast with `ays`. The Align-Your-Steps
  schedule (SDXL noise levels) needs 10 steps where euler_a needs 20 to 25.
- The preset (`scripts/ps5sd-app.sh preset presets/ilustmix-portrait.txt`) holds these settings and
  `# defaults: width=832 height=1216 steps=10 cfg=6 sampler=dpm++2m scheduler=ays`.
- `--sampling-method`, `--scheduler`, `--steps` and `--cfg-scale` in `server-args.txt` are the defaults
  of a request that does not name them, on the A1111 route too. `-W`/`-H` are not: `/sdapi/v1/txt2img`
  without a size makes 512x512, so a client has to send 832x1216. The web UI takes the size from
  `/sdcpp/v1/capabilities` unless it has one saved.

<img src="images/ilustmix-portrait.jpg" alt="An anime-style woman with silver hair in a knit sweater in an autumn park, generated on a PS5 with the iLustMix portrait preset" width="384">

The preset's settings, 30 to 35 s: "1girl, adult woman, long silver hair, cream knit sweater, autumn
park, falling leaves, golden hour, warm backlight, gentle smile, upper body, looking at viewer,
masterpiece, best quality, amazing quality, very aesthetic, detailed eyes, perfect eyes", seed 2026.

Smaller portrait sizes, same settings:

| Size | Request |
|---|---|
| 768x1152 | 30.4 s |
| 704x1024 | 26.2 s |
| 640x960 | 24.2 s |
| 576x832 | 19.6 s |

All clean; from 640 px wide down, faces are softer at full size.

How the server options got there, at 1024x1024:

| Variant | Request |
|---|---|
| no `server-args.txt`, euler_a, 20 steps | 92.6 s |
| `--type q8_0 --vae-tiling --vae-tile-size 512`, euler_a, 20 steps | 56.4 s (71.3 s the first request) |
| the same with `--cache-mode ucache --cache-option threshold=1.5` | 34.9 s, visibly noisy and smeared |
| the same without UCache, dpm++2m, karras, 14 steps | 41.5 s |
| the same, dpm++2m, ays, 10 steps | 31.6 s |
| the same with `--eager-load` | 32.1 s for the first request, 32.0 s after |

- As with IllustXL above, `--type q8_0` (converted while loading) avoids the f16 weights being moved
  out and read again; the VAE tiles keep the decode from pushing them out too.
- UCache at threshold 1.5, which cost SDXL Base only some detail, ruins this finetune's images.
- `--eager-load` takes the first request from 71 to 32 s.

## LoRAs on a q8_0 model (2026-10-20)

0.1.0, FW 13.00. iLustMix v11.1 from `presets/ilustmix-portrait.txt` (`--type q8_0`, converted while
loading), with LCM-LoRA SDXL (`latent-consistency/lcm-lora-sdxl`, 394 MB) in `/data/PS5SD/loras`.
`POST /sdapi/v1/txt2img`, 832x1216, the prompt of the iLustMix section, seed 2026, the time around the
whole request on the PC.

| Request | Sampling | Request |
|---|---|---|
| dpm++2m, `ays`, 10 steps, CFG 6, no LoRA | 25.3 s (2.5 s/step) | 30.2 s |
| `lcm`, 6 steps, CFG 1.5, LCM-LoRA at 1.0 | 16.6 s (2.5 s/step) | 26.4 s |

- sd.cpp applies the LoRA "immediately" (2364 of 2364 tensors): `apply_loras` returns after 0.09 s,
  and the merge into the weights shows in the first step (4.0 s instead of 2.5 s). The other steps cost
  the same as without it. Memory after the request was the same (3.9 GiB of the pool free).
- The next request without the LoRA gave an image byte for byte equal to the one from before the LoRA,
  so taking it out again restores the q8_0 weights exactly.
- With a model of another family loaded (FLUX.3 Klein), the same request applied 0 of the 2364 tensors
  and logged each one as unused; the image is the model's own.
- The LCM image is clean at 6 steps, with a softer, more painted look than the `ays` one.

Two LoRAs at once, 1024x1024, `lcm`, 6 steps, CFG 1.5, seed 5, a building prompt, after a fresh start of
the server; pixel-art-xl (`nerijs/pixel-art-xl`, 170 MB) downloaded with `"kind": "loras"`:

| Request (in this order) | Step 1 | Steps 2 to 6 | Sampling | Request |
|---|---|---|---|---|
| A: LCM-LoRA | 3.9 s | 2.46 s | 16.3 s | 27.7 s |
| B: LCM-LoRA + pixel-art-xl | 15.3 s | 2.45 s | 27.6 s | 45.4 s |
| A again | 13.6 s | 2.46 s | 25.9 s | 42.2 s |
| B again | 15.4 s | 2.45 s | 27.7 s | 45.5 s |
| B once more | 2.4 s | 2.45 s | 14.8 s | 24.1 s |

- Both LoRAs are applied in full (2364 and 2166 tensors). A step costs the same with none, one or two.
- A change of the set costs time once, in the first step, while sd.cpp merges the new set into the
  q8_0 weights (and takes the old one out). The same set again costs nothing.
- "A again" gave A's image byte for byte, so taking pixel-art-xl out restores the weights exactly. B
  gave the same image each time.
- Before `patches/sdcpp/0058`, B failed on `/sdapi/v1/txt2img` with "invalid lora path" because the
  file had come after the server started; the web UI's route (`/sdcpp/v1/img_gen`) already refreshed
  the list.

## Hires fix at x1, and a run that does not fit (2026-10-21)

iLustMix v11.1 (preset `ilustmix-portrait`), 832x1216, 10 steps, CFG 6, seed 7, a cabin by a lake:

| Request | Request time | Size of the image |
|---|---|---|
| no hires fix | 30 s | 832x1216 |
| hires fix x1, `Latent`, strength 0.35, steps 0 | 57 s | 832x1216 |
| hires fix x1, `Lanczos`, strength 0.35, steps 0 | 63 s | 832x1216 |

- sd.cpp takes a scale of 1: the target is the first pass's size, and the second pass runs 10 of the 28
  steps of its schedule (`hires fix: scheduler_steps=28, denoising_strength=0.35`), 25.6 s of sampling.
- Both images keep the first one's picture with crisper detail; `Lanczos` at x1 resizes nothing, so it
  is image to image over the decoded first pass.

The same model at 2048x2048, 4 steps, failed after 24 s in the first sampling step. The log said why:

```
[W] model manager cannot make enough memory available on Vulkan0: need 512.00 MB device / 2866.11 MB budget, available 0.00 MB device / unlimited budget --- model_manager.cpp:2018
[E] sampling for image 1/1 failed after 18.50s --- image.cpp:907
```

The job's error was only `generate_image returned no results` until `patches/sdcpp/0065`. After the run,
5.71 of 11.44 GiB of the pool were free again.

In 0.8.1 this size fits: the run came after smaller images, see "Memory: what fits" below.

## The catalog's models, one image each (2026-10-22)

For the catalog's "about … an image": each model with its own defaults (size, steps, sampler), the prompt
`a stone lighthouse on a cliff above the sea, evening light, detailed`, seed 42, one image through
`POST /sdapi/v1/txt2img`, timed on the PC around the whole request. The first image after a switch, then a
second one ("warm").

| Model | Size, steps | First | Warm |
|---|---|---|---|
| FLUX.3 Image Klein Q4_K | 1024×1024, 16 | 136 s | 121 s |
| Z-Image Turbo Q4_K | 1024×1024, 8 | 87 s | 74 s |
| SDXL Base 1.0 Q8_0 (GGUF) | 1024×1024, 25 | 77 s | 68 s |
| SDXL Base 1.0 f16, loaded as Q8_0 | 1024×1024, 25 | 82 s | 68 s |
| SD-Turbo Q8_0 | 512×512, 4, CFG 1 | 2.7 s | 1.6 s |
| SDXS-512 Q8_0 | 512×512, 1, CFG 1 | 2.3 s | 0.3 s |

FLUX.3 Image Klein **Q6_K does not fit at 1024×1024**: with the model (7.18 GB of weights) loaded, 4.96 GiB
of the pool is free, and the first sampling step asks for 5,366 MB on the GPU against 5,078 MB available
(`model manager cannot make enough memory available`). The run fails after 0.24 s with the out-of-memory
reason of 0.7.1. Smaller sizes fit: 896×896 in 114 s, 768×768 in 78 s, 1024×768 in 105 s, 832×1216 in
130 s. The catalog's preset now defaults to 896×896.

This run came after FLUX.3 Klein Q4_K at 1024×1024 in the same session. Right after a start, and after a
switch from SDXL at 1024×1024, Q6_K fits at 1024×1024; see "Memory: what fits" below.

## FLUX.2 Klein 4B (2026-10-22)

leejet's GGUFs of FLUX.2 Klein 4B (Apache 2.0), with the Qwen3-4B Instruct 2507 Q4_K_M text encoder that
Z-Image Turbo uses (Klein was trained with Qwen3-4B; the Instruct weights load and give clean images) and
the FLUX.2 VAE, CFG 1, Euler, the lighthouse prompt of the catalog measurements, seed 42:

| Model | Weights | Size, steps | First | Warm | Pool free after |
|---|---|---|---|---|---|
| Q8_0 (4.30 GB) | 6.64 GB | 1024×1024, 4 | 46.7 s | 30.5 s | 3.92 GiB |
| Q4_0 (2.46 GB) | | 1024×1024, 4 | 43.7 s | 26.1 s | 4.71 GiB |

The warm Q8_0 image: 34.4 s of sampling the first time, 24.1 s after; the text encoder 5.6 s once (cached
for the same prompt), the VAE decode about 6 s. The catalog has Q8_0.

## Memory: what fits, and why runs failed (2026-10-22)

Each run below right after a start of the title (`ps5sd-app.sh close` and `launch`), one step, with
`-v` in `server-args.txt` so that sd.cpp logs its compute buffers. The weights load when the first request
needs them and the model manager evicts them block by block when memory runs short, so what a run needs
depends on the run before it.

| Model | Size | 0.8.0 | 0.8.1 (`patches/sdcpp/0067`) |
|---|---|---|---|
| SDXL Base Q8_0, CFG 6 | 1024×1024 | fits (UNet 984 MB) | 13.0 s, the same |
| SDXL Base Q8_0, CFG 6 | 1536×1536 | the batched step fails, the step without batching runs (32.7 s) | batched, in 17 segments (31.7 s) |
| SDXL Base Q8_0 | 2048×2048 | fits right after a start (17 segments, 58.7 s); fails after 1024 and 1536 | fits after them too (17 segments, 42.5 s) |
| FLUX.2 Klein 4B Q8_0 | 1024×1024 | fits (diffusion 1,120 MB) | the same |
| FLUX.2 Klein 4B Q8_0 | 1536×1536 | fails | fits, 27 segments (60 s) |
| FLUX.2 Klein 4B Q8_0 | 2048×2048 | fails | fails: one segment needs 4,489 MB, 4,089 MB free. Fits since 0.9.0 (`patches/mesa/0010`), see the next section |
| FLUX.3 Klein Q6_K | 1024×1024 | fits (794 MB), also after a switch from SDXL | the same |
| FLUX.3 Klein Q6_K | 1536×1536 | fails | fails: sd.cpp has no cut points for this network |
| Z-Image Turbo Q4_K | 1536×1536 | fits | |
| Wan 2.1 T2V 1.3B | 832×480, 33 frames | fits (diffusion 670 MB, VAE 826 MB at 128 px tiles) | |

What made runs fail:

- **The plan picks one piece when two would be needed.** sd.cpp runs a graph whole when it estimates that
  it fits next to the weights, and in segments (weights streamed per block) when it does not. On the
  console the estimate says "fits", the allocation goes through, and the check after it finds no room:
  `unet segment 1/1 (graph) failed during allocated capacity check`. `patches/sdcpp/0067` builds the graph
  again and runs it in segments then (`[W] unet does not fit in one piece; retrying in segments`).
- **Memory that earlier runs freed is not all usable again: the pool falls apart in pieces.** See the next
  section. A size that ran once keeps running, and so does a smaller one. A larger one can fail where it
  would run right after a start.

Two older measurements were such runs: FLUX.3 Klein Q6_K at 1024×1024 ("The catalog's models, one image
each") failed after FLUX.3 Klein Q4_K at the same size. It fits right after a start and after a switch from
SDXL. SDXL at 2048×2048 ("Hires fix at x1") failed after smaller images, and it fits now.

The web UI says before a run when it may not fit (`POST /ps5sd/v1/app/fit`, `ps5/app/mem_fit.cpp` has the
sizes of this table).

## Why freed GPU memory is not usable again: the pool in pieces (2026-10-22)

Freed memory does go back. PS5_Mesa's winsys (`src/amd/vulkan/winsys/ps5/radv_ps5_platform.c`) makes each
Vulkan buffer of one `sceKernelAllocateDirectMemory` block, which is physically contiguous, and a
destroyed buffer unmaps it and gives it back with `sceKernelReleaseDirectMemory` once the GPU is done
(`patches/mesa/0007`). What does not come back is one large free block: the weights, which load when a
request needs them, the CPU heap (it draws from the same pool) and the compute buffers lie mixed in the
pool, and the compute buffers leave holes between the others when they go. A buffer larger than the
largest hole cannot be made, however much is free in all.

`pool free` in app.log was `sceKernelAvailableDirectMemorySize`: one free part, the largest. ggml's
`Vulkan0 free` was about the same number (the winsys reported it as the heap's budget). Since 0.9.0 the line also
gives the sum of the free parts, how many there are and the largest (`pool_free` in `ps5/app/main.cpp`).
FLUX.2 Klein 4B Q8_0, right after a start, one step each:

| When | Free in all | Parts | The largest |
|---|---|---|---|
| Before loading | 11.01 GiB | 1 | 11.01 GiB |
| After loading (the weights load later) | 10.83 GiB | 1 | 10.83 GiB |
| After 1024×1024 | 7.23 GiB | 13 | 4.21 GiB |
| After 1536×1536 (in segments) | 7.13 GiB | 15 | 4.21 GiB |
| After 1024×1024 | 7.18 GiB | 14 | 4.21 GiB |
| After 1280×1280 (in segments: one piece needed more than 4.21 GiB) | 7.02 GiB | 15 | 4.21 GiB |

About 3 GiB is free in pieces after the first image, and stays so. Two ways to use it:

- **A large buffer of several parts**, mapped one after the other at one GPU address. Done in 0.9.0, see
  below.
- **Long-lived and short-lived memory apart**: weights from one end of the pool, compute buffers from the
  other, so the holes they leave are one. Not tried.

### Buffers of several blocks (`patches/mesa/0010`, 2026-10-22)

The winsys now makes a buffer that no free block holds whole from several, the largest first: it reserves
one range of the device-memory region, allocates block after block of the pool and maps each right after
the one before, so the GPU reaches the buffer at one address. It does so only outside the 32-bit window
and for buffers of 4 MiB and more; a placed mapping or a sparse bind of such a buffer is refused (they
need one block). The pool's available bytes, which RADV's heap budget is made of, are the sum of the free
blocks of 2 MiB and more.

The budget had a second fault: RADV's APU branch adds the visible VRAM heap's allocations to the VRAM
heap's, and the PS5 heap info gave both the driver's total, so the driver's bytes counted twice. ggml adds
the heaps' free bytes of an integrated GPU, and saw that much less. A first build of the patch, with the
sum but still the double count, showed `Vulkan0 free` 5.5 GB against 7.1 GiB in parts, and 1536 and 1280
still ran in segments after the capacity check failed; the patch counts the driver's bytes once.

FLUX.2 Klein 4B Q8_0 (`flux-2-klein-4b-quality-1024`), one sequence right after a start, one step, CFG 1:

| Run | First build (driver counted twice) | `0010` |
|---|---|---|
| 1024×1024 | 35.5 s | 36.1 s |
| 1536×1536 | 48.2 s, in segments | 30.7 s, in one piece (sampling 13.1 s) |
| 1024×1024 | 15.1 s | 11.3 s |
| 1280×1280 | 37.9 s, in segments | 20.0 s, in one piece |
| 2048×2048 | | 72.9 s (sampling 39.2 s, VAE decode 31.0 s) |

0.8.1 ran 1536×1536 in 27 segments (60 s) and failed at 2048×2048 ("Memory: what fits").

SDXL Base Q8_0 (`sdxl-base`), one sequence right after a start, 6 steps, CFG 6, euler_a:

| Run | Request | Sampling | VAE decode (512 px tiles) |
|---|---|---|---|
| 1024×1024 | 30.9 s | 19.9 s | 5.9 s |
| 1536×1536 | 54.5 s | 37.5 s | 15.8 s |
| 2048×2048 | 115.1 s | 82.4 s | 30.7 s |
| 1024×1024 | 21.4 s | 15.0 s | 5.7 s |

- With Klein, the gain at 1536 and 1280 came from the budget: those runs found one block large enough
  once the capacity check let them try, and no buffer of several blocks was made. After the first image
  `Vulkan0 free` is 4.75 GB with 4.43 GiB free in 10 parts: the budget is the sum.
- Less is free after the first Klein image than with the first build (4.43 against 7.1 GiB), likely
  because the model manager now keeps the text encoder loaded: the conditioning took 0.38 s on the second
  request, so its weights were not read again.
- Buffers of several blocks were made at 2048: two of 198 MiB (2 blocks each) with Klein, one of
  2160 MiB (2 blocks) with SDXL (`radv/ps5: a buffer of ... made of N blocks` in app.log). The images
  are right, with no seam. The console did not hang.
- The other families, each in one sequence right after a start, one step, all ran with no error and no
  buffer of several blocks: Z-Image Turbo 1024, 1536, 1024, 1536 (39.6 s both times); FLUX.3 Klein Q6_K
  1024, 896, 1024 (14.3 s the second time); Wan 2.1 T2V 1.3B 832×480 with 9 frames (65.3 s), then 33
  (165.6 s).
- `mem_fit.cpp` has 2048×2048 as measured for FLUX.2 Klein 4B, after other runs too.

## Longer videos with CausVid (2026-10-22)

0.9.0 with `patches/mesa/0010`, preset `wan21-t2v-1.3b` (Wan 2.1 T2V 1.3B, VAE in 128 px tiles), the
CausVid LoRA at 1, 832×480, 4 steps, CFG 1, euler, 16 frames per second. One sequence right after a
start, through `POST /sdcpp/v1/vid_gen`:

| Frames | Video | Request | Sampling | VAE decode |
|---|---|---|---|---|
| 33 | 2 s | 225.8 s | 80.4 s | 125.4 s |
| 49 | 3 s | 331.2 s | 138.8 s | 185.1 s |
| 81 | 5 s | 636.9 s | 325.1 s | 305.0 s |

- All three ran with no error and no buffer of several blocks; after each, 4.0 GiB were free in 6 or 7
  parts. The decode grows with the frames, the sampling faster (attention over all frames).
- The 81 frames keep the scene from the first to the last (a lighthouse on a cliff, waves breaking).
- The web UI and `mem_fit.cpp` take 81 frames as measured since 0.9.1.

## Faster video decoding (2026-10-22)

Half of a CausVid video's time was the VAE decode. The preset decoded in 128 px tiles at sd.cpp's default
overlap of 0.5: a tile moves on by half its size, so each pixel is decoded about three times. The same
33-frame video as above (seed 7), the tiling changed per request (`vae_tiling_params`):

| Tiles | Overlap | VAE decode | Request |
|---|---|---|---|
| 128 px | 0.5 (the preset up to 0.9.0) | 125.4 s | 225.8 s |
| 128 px | 0.25 | 69.6 s | 150.6 s |
| 192 px | 0.25 | 68.8 s | 150.6 s |
| 256 px | 0.25 | 54.9 s | 135.5 s |
| 128 px | 0 | 48.7 s | 130.4 s |

- Against the preset's decode, the middle frame differs by 0.7 of 255 on average at an overlap of 0.25,
  with no pattern. At an overlap of 0 the difference image shows the tile grid: the seams are visible.
- 256 px at 0.25 with 81 frames: decode 134.2 s instead of 305.0 s, the request 466.8 s instead of
  636.9 s. It made a buffer of 1296 MiB of 2 blocks (`patches/mesa/0010`); 256 px tiles ran out of memory
  before 0.9.0. The frames differ from the preset's as little as at 33.
- The catalog's Wan preset has `--vae-tile-size 256 --vae-tile-overlap 0.25` since 0.9.1, and the title
  gives the preset that 0.9.0 wrote the new text when it starts, unless it was changed
  (`update_catalog_presets` in `ps5/app/download_core.cpp`).

A small decoder instead of the VAE, `--taesd` (sd.cpp's TAEHV), with the same 33 and 81 frames:

| Decoder | 33 frames: decode | request | 81 frames: decode | request |
|---|---|---|---|---|
| Wan 2.1 VAE, 256 px at 0.25 | 54.9 s | 135.5 s | 134.2 s | 466.8 s |
| TAEHV `taew2_1` (madebyollin, 22.6 MB) | 6.85 s | 105.5 s | 14.5 s | 356.4 s |
| lightx2v's `lighttaew2_1` (45.3 MB) | 6.81 s | 110.5 s | 14.4 s | 356.4 s |

- TAEHV's frames are a little softer and darker than the VAE's (8.6 of 255 apart on average), the scene and
  its motion the same over all 81 frames. The catalog has it as a second Wan entry, `wan21-t2v-1.3b-tae`,
  which shares the model files.
- `lighttaew2_1` came out washed out, with a fine pattern of stripes (28.4 apart): sd.cpp b13a482 does not
  read it right. Not used.
- Whole, TAEHV asks for 20 GB for 33 frames. sd.cpp then retries with temporal tiling (7 GB, fails too)
  and then with tiles of 416×240 as well, which works; the log has the failed allocations and the system's
  "Internal Memory is running out". The decode times above include that. Tiled from the start
  (`--vae-tiling --temporal-tiling`, 256 px at 0.25), on 0.9.1's release build: decode 3.63 s, request
  90.4 s, with no error; the frame differs from the fallback's by 0.35 of 255. The catalog entry does that.
- The title passed `--tae` to sd-server unresolved (a path relative to the models folder) and the model did
  not load; it knew only `--taesd`. 0.9.1 resolves both.

## Faster SDXL (2026-10-22)

0.9.1's release build, FW 13.00. `POST /sdcpp/v1/img_gen`, seed 5, the request time on the PC and the
phases from `app.log` (sampling, `decode_first_stage`). Each request came after one that had loaded the
model and, with a LoRA, after one that had merged it into the weights.

SDXL Base 1.0 Q8_0 (preset `sdxl-base`), 1024x1024, a windmill above a village:

| Settings | Sampling | VAE decode | Request |
|---|---|---|---|
| euler_a, 25 steps, CFG 6 (the preset up to 0.9.1) | 61.7 s | 5.7 s | 68.1 s |
| dpm++2m, `ays`, 10 steps, CFG 6 | 24.6 s | 5.7 s | 31.6 s |
| DMD2 LoRA, `lcm`, 4 steps, CFG 1 | 6.4 s | 5.7 s | 13.3 s |
| DMD2 LoRA, `lcm`, 6 steps, CFG 1 | 9.6 s | 5.7 s | 16.3 s |
| SDXL-Lightning 8-step LoRA, euler, `sgm_uniform`, 8 steps, CFG 1 | 12.9 s | 5.7 s | 20.4 s |

- All five images are clean. `ays` at 10 steps is as detailed as euler_a at 25, with another layout of
  the same scene.
- A step takes 2.46 s at CFG 6 and 1.6 s at CFG 1, where the pass for the negative prompt is left out.

Illustrious finetunes, the prompt in tags (`no humans, scenery, stone lighthouse on a cliff, sea,
sunset, ...`):

| Model, size | Settings | Request |
|---|---|---|
| Raehoshi IllustXL 1.1, 1024x1024 | euler_a, 28 steps, CFG 6 (its preset) | 75.3 s |
| | dpm++2m, `ays`, 10 steps, CFG 6 | 31.6 s |
| | DMD2, `lcm`, 4 steps, CFG 1 | 12.2 s, grey and flat |
| | DMD2, `lcm`, 8 steps, CFG 1.5 | 26.5 s |
| iLustMix v11.1, 832x1216 | dpm++2m, `ays`, 10 steps, CFG 6 (its preset) | 30.6 s |
| | DMD2, `lcm`, 4 steps, CFG 1 | 10.2 s |
| | DMD2, `lcm`, 6 steps, CFG 1 | 13.3 s |

- iLustMix with DMD2 is clean at 4 steps.
- IllustXL with DMD2 at CFG 1 comes out grey and flat, with a fine pattern: with `lcm` and with
  `sgm_uniform`, at 4 and 6 steps, at a strength of 1 and of 0.8, and with euler. At CFG 1.5 and 8 steps
  it is clean, but then each step computes the negative prompt again: 26.5 s (sampling 19.6 s), against
  31.6 s for `ays` at 10 steps without the LoRA.

The VAE decode, with DMD2 at 4 steps (IllustXL; iLustMix for 832x1216):

| Size | 512 px tiles | 640 px tiles |
|---|---|---|
| 1024x1024 | 5.7 s (3x3 tiles) | 3.6 s (2x2) |
| 1536x1536 | 10.1 s | 8.0 s |
| 2048x2048 | 15.9 s | 14.2 s |
| 832x1216 | 3.9 s | 3.7 s |

- The images with 640 px tiles differ from the ones with 512 px by 0.5 to 0.7 of 255 on average, with no
  seams.
- No 640 px run fell back to smaller tiles (`app.log` has no "Reducing VAE decode tiles from 640x640").
- Whole (no tiles), 1024x640 and 1024x768 tiles at 1024x1024 ran out of memory, and sd.cpp retried with
  tiles of half the size. 1024x512 tiles took 4.3 s (3 tiles), 768 px tiles 5.3 s; at 832x1216 576 px
  took 4.4 s, and 448x640 2.6 s, but that suits only a portrait image.
- An overlap of 0.25 in the request changed nothing at 1536 and 2048 px: the image and the time were
  those of a request without `vae_tiling_params`. An overlap of 0.5 in the request made 1536x1536 slower
  (16.2 s instead of 10.4 s, 5x5 tiles instead of 4x4). So an image request without tiling settings is
  decoded as at 0.25, not at the 0.5 that `/sdcpp/v1/capabilities` lists; the reason is not found. Videos
  are decoded at 0.5 (see "Faster video decoding").
- At 1024 px the overlap cannot matter: sd.cpp's plan (`src/runtime/tiling.cpp`) needs neighbouring tiles
  to overlap and no point covered by three, which leaves 3 tiles of 512 px on a 1024 px side at any
  overlap. For the same reason 0 and 0.25 gave the same image at 1536 px.

`--eager-load` loads the weights when the model starts instead of at the first image.
`POST /sdapi/v1/txt2img` at the preset's defaults, right after switching to the preset:

| Preset | Switch | First image | Second image |
|---|---|---|---|
| `sdxl-base` up to 0.9.1 (euler_a, 25 steps, 512 px tiles) | 3.1 s | 77.3 s | 68.0 s |
| `ays`, 10 steps, 640 px tiles, `--eager-load` | 11.6 s | 29.3 s | 28.8 s |

- The same in a second round (12.1 s, 29.3 s, 28.8 s).

On a build with these changes (before 0.9.2), after the title had updated the presets that 0.9.1 wrote (`app.log`: "preset sdxl-full
updated to the catalog's current settings", the same for `sdxl-base`):

| Preset | Switch | First image | Second image |
|---|---|---|---|
| `sdxl-full` (the f16 checkpoint, converted to Q8_0 while it loads) | 17.7 s | 29.1 s | 28.7 s |
| `sdxl-base` | 8.6 s | 29.2 s | 28.8 s |
| `sdxl-base`, DMD2, `lcm`, 4 steps, CFG 1 | | 12.2 s | 11.2 s (decode 3.5 s) |

- 4.7 GiB of the pool free after the DMD2 images, no allocation failed.
- The catalog's `sdxl-base` and `sdxl-full` have these settings since 0.9.2, and the title gives the
  presets that 0.9.1 wrote the new text when it starts, unless they were changed. Adding DMD2, Lightning,
  CausVid or LCM-LoRA on the Create tab sets their sampler, steps and CFG.

## The console's readings (2026-10-22)

A build with the 0.9.3 changes, FW 13.00, `GET /ps5sd/v1/app/console` and the TV.

- A title cannot read the temperature, the power or the fan: the sensor calls answer `0x80020002`.
  ps5-exporter 0.2.0, a payload, can, and serves them on `127.0.0.1:9100/metrics`. Its metrics are in
  `ps5/app/tests/fixtures/ps5-exporter-0.2.0.txt`: 17 temperatures (`cpu`, `soc0` to `soc15`), the SoC's
  power as a raw value whose low 32 bits are milliwatts, the fan's duty from 0 to 1, and the CPU clock.
  There is no GPU reading and no fan speed.
- `std::thread::hardware_concurrency()` in the title is 16; PS5SD's CPU share is its `getrusage` time
  over 16 CPUs.

| | Idle, the model loaded | FLUX.2 Klein 4B, 1024x1024, 10 steps |
|---|---|---|
| SoC, hottest sensor | 44 °C | 68 °C after 20 s, 79 °C after 60 s |
| Power | 19.2–20.1 W | 94–109 W once it computes |
| Fan duty | 23.1% | 23.1% |
| PS5SD's CPU | 0–0.4% | up to 5% |

- The fan's duty did not move in that minute at 79 °C.
- "GPU busy" is the share of a second in which a job runs, not a reading of the GPU: it is 100% from
  the request to the image. For the first 20 s of that request it was 100% while the power stayed at
  20 W, the idle value, before the steps began.
- After a restart of the console (the exporter not running), PS5SD logged `ps5-exporter does not answer
  on 9100; sent the bundled copy to the ELF loader` at its first look, and the readings were there
  within seconds (0.9.3 with the 0.9.4 changes, 2026-10-22).

## Image to video: Wan 2.2 TI2V 5B (2026-10-23)

QuantStack's Q8_0 of Wan 2.2 TI2V 5B (5.40 GB), umt5-xxl Q4_K_M and the Wan 2.2 VAE (1.41 GB, f16), each test
right after a start of the title, with `--vae-tiling --vae-tile-size 256 --vae-tile-overlap 0.25`:

- sd.cpp counts 9977 MB of weights (text encoder 3480, model 5153, VAE 1344); they load as each part runs.
- From a picture (`init_image`), 1280x704 with 33 frames: 36 s a step, 196 s to decode, 2.48 GiB free in
  one block afterwards. 832x480 with 33 frames from a prompt: 11.7 s a step, 88 s to decode, 1.98 GiB left.
  512x288 with 9 frames: about 60 s at 6 steps.
- From a prompt alone, only 1280x704 came out right (9 frames, 20 steps, 222 s). At 832x480 and 512x288 the
  frames were washed out, almost white, with the shapes of the scene in them, at 6, 10 and 20 steps, at
  CFG 5 and 6 and flow shift 3 and 5, and with the VAE's tiling on or off. From a picture the same sizes
  were clean: the first frame is the picture and the last a little changed, so the VAE decodes right. The
  model is trained at 1280x704 (and 704x1280).
- `--tensor-type-rules "^vae\.=f32"` did not change the VAE's type (still f16, 1344 MB), and the VAE on
  the CPU (`--vae-on-cpu`) does not fit: the title has about 2 GB of CPU memory.
