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
- The HTTP server, which `main.cpp` does not start yet.
- The build with a display.
