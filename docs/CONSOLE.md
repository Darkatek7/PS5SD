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
