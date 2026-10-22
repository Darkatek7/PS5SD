# Porting notes

What it takes to run stable-diffusion.cpp as a PS5 title, and why. Much of it is
[PS5LM](https://github.com/cobanov/PS5LM)'s native app (GPL-3.0), whose recipe PS5SD follows
and whose files it adapts; each adapted file says so in its header. Upstream is pinned as
submodules: stable-diffusion.cpp `b13a482` (2026-10-18) and its ggml, leejet/ggml `dc99877`.

Nothing here has run on a console yet. What is marked *unverified* is reasoning from PS5LM's
measurements and the SDK's code, not a measurement of PS5SD.

## The target

The SDK compiles with clang's own PS5 target, `x86_64-sie-ps5`, which defines `__PROSPERO__`,
`__SCE__` and `__FreeBSD__`, not `__linux__`. Code that checks `__linux__` (sd.cpp's
`posix_fadvise`, `MAP_POPULATE`, `madvise`, `/proc/meminfo`, `/sys` CPU topology) simply
compiles out; code that checks `__FreeBSD__` takes BSD paths that may not exist on the console,
which is what the patches below are about.

The app is a **title**, not a payload: an fSELF `eboot.bin` on PS5_Vulkan's start code
(`app_crt.cpp`) and clean-room `libc.prx`, registered through ShadowMountPlus as PPSA99576. A
title gets the console's direct memory (12 GiB, PS5LM measured an 11.44 GiB unified device heap
with `radv_enable_unified_heap_on_apu`) and the GPU; a payload gets neither. In exchange:

- it may not `exit()` or return from `main` (SIGSYS); it leaves through
  `sceSystemServiceLoadExec("exit")` when `/data/PS5SD/quit` appears;
- imports that only `libScePosixForWebKit` or `libkernel_sys` define in the SDK's stubs are
  null at run time;
- its own flexible heap is small (448 MiB): PS5_Vulkan's link recipe wraps `malloc` and friends
  onto the SDK platform's heap in direct memory, and `pthread_create` so threads that ask for no
  stack get 2 MiB there.

## The build (`scripts/build-app.sh`)

PS5LM's `build-app.sh`, with llama.cpp replaced by stable-diffusion.cpp:

1. **Patches**, applied as a series per tree (the pinned commit with the series applied, built
   in a scratch index, is compared with the working tree, so a rerun is a no-op and a hand-edited
   tree is refused rather than patched twice).
2. **sd.cpp and ggml as static libraries** through `ps5/app/toolchain.cmake` (the PS5_Vulkan
   compiler wrapper, `CMAKE_SYSTEM_NAME FreeBSD`, try-compiles as static libraries since nothing
   is linked by CMake): `SD_VULKAN=ON`, `GGML_NATIVE=OFF`, AVX/AVX2/FMA/F16C/BMI2 on and AVX-512
   off (Zen 2), `GGML_OPENMP=OFF` (ggml's own pool), `GGML_BACKEND_DL=OFF` and
   `GGML_CPU_ALL_VARIANTS=OFF` (one static image), `BUILD_SHARED_LIBS=OFF`,
   `SD_WEBP=OFF`/`SD_WEBM=OFF` (their submodules are not fetched), `SD_BUILD_EXAMPLES=OFF`,
   `MATH_LIBRARY=SceLibcInternal` (there is no `libm`; the math functions are in Sony's libc),
   `-fexceptions -fcxx-exceptions -frtti` (off by default for the target; sd.cpp and ggml throw
   and catch), `Vulkan_LIBRARY` = RADV's `libvulkan_radeon.ps5.a`, the host's `glslc` for
   ggml-vulkan's shaders, and the host's Vulkan/SPIR-V headers copied out of `/usr/include` so
   the cross build never sees the host libc next to them.
3. **The app** (`ps5/app`), PS5_Vulkan's `app_crt.cpp`, ps5-homebrew-ui's kit as an archive, and
   ps5-opengl linked into one relocatable object with only `gl*`/`egl*` global (`ld -r` then
   `objcopy --keep-global-symbols`): the GL stack is a Mesa of its own and shares thousands of
   names with RADV's.
4. **AGC stubs** generated from `llvm-nm -u` over both drivers, so the title imports exactly
   what they use.
5. **The link**: `prospero-lld` with PS5_Vulkan's `radv_link_recipe` (its linker script, RADV
   whole-archive, libc++/libc++abi/libunwind, clang builtins, the SDK platform layer and its
   `--wrap`/`--defsym` bindings), minus its `getaddrinfo`/`freeaddrinfo` binding (ours is used),
   plus `--wrap=sceAgcInit`, `ps5/app/app-symbols.map` (the C++ allocation operators stay
   local), `--exclude-libs=ALL` (nothing from an archive is exported: the title converter refuses
   exports), `-e _start`. Then `ps5-native-tool link` and `self --sign`, and the title folder:
   `eboot.bin`, `sce_sys` (param.json, icon, backgrounds made by PS5_Vulkan's asset tools),
   `sce_module/libc.prx`, `assets/fonts`.
6. **Without a display** (`PS5SD_DISPLAY=0`, or `auto` when no ps5-opengl SDK is found): steps 3
   and 4 lose their GL parts. `main.cpp` is built with `-DPS5SD_NO_DISPLAY`, `display.cpp`,
   `status_view.cpp`, the kit archive and the GL object are left out, the AGC stubs come from
   RADV's imports alone, `--wrap=sceAgcInit` is not used (it exists only so the two drivers share
   one initialisation; RADV alone calls it once, as PS5_Vulkan's own titles do) and the title has
   no `assets/fonts`. See "The app" for what the app does instead of drawing.
7. **Two reports for CI**: before the link, `build/app-ps5/missing-symbols.txt` lists names the
   inputs reference that nothing defines (objects, archives, SDK stubs, AGC stubs, `--defsym`,
   `--wrap`); unused archive members count too, so it is a list of suspects. After the link,
   imports that only a null module defines are printed as warnings: each wants a definition in
   `compat_app.c`.

## ggml patches (`patches/ggml`, applied to `third_party/stable-diffusion.cpp/ggml`)

1. **`0001-ps5-no-fork-backtrace.patch`.** `ggml_abort` calls `ggml_print_backtrace`, which on
   Linux, the BSDs and macOS forks and `execlp`s gdb or lldb and `waitpid`s for it. A title
   cannot start processes: `fork` resolves to nothing in a title and `execlp` is not exported at
   all (PS5LM's `compat_app.c` had to define both for llama.cpp's copy). On `__PROSPERO__` the
   function takes ggml's existing "platform not supported" no-op branch, so an assertion prints
   its message and aborts instead of jumping through NULL first.
2. **`0002-ps5-thread-affinity-and-priority.patch`.** PS5LM's `patches/0002` moved to leejet's
   ggml-cpu.c. With `__FreeBSD__` and no `__linux__`, ggml's thread pinning and priority were the
   "unsupported platform" no-ops. On `__PROSPERO__` they use `scePthreadSetaffinity` and
   `scePthreadSetprio`; priority is only ever lowered (`GGML_SCHED_PRIO_LOW` gives 720, threads
   start at 700, raising a spinning worker above system threads starves them), and the calling
   thread is never pinned, because threads it starts later inherit its mask (PS5SX2 f9abb4a).
   sd.cpp itself sets no priority or mask today, so this changes nothing by default; it makes
   the threadpool parameters mean on the console what they mean elsewhere.

No ggml patch is needed for the static build itself: `GGML_BACKEND_DL=OFF` registers CPU and
Vulkan at start, and the `dlopen` family that `ggml-backend-dl.cpp` still names is stubbed at
link time (below). ggml-vulkan calls four Vulkan commands by name (`vkGetInstanceProcAddr`,
`vkGetDeviceProcAddr`, `vkGetPhysicalDeviceFeatures2`, `vkCmdCopyBuffer`), the same four as in
PS5LM's llama.cpp; `main.cpp` forwards them to RADV's ICD entry point and Mesa's common
implementations, since there is no Vulkan loader.

## stable-diffusion.cpp patches (`patches/sdcpp/0001`–`0049`)

`0050` and later are the HTTP server's (examples/server as a library, activity hooks, the
embedded page, the gallery), described with that work.

1. **`0001-ps5-available-ram.patch`.** `backend_fit.cpp`'s `available_ram_bytes()` reads
   `/proc/meminfo`, Windows or Mach; on the PS5 it returned -1. On `__PROSPERO__` it returns the
   free part of direct memory, `sceKernelAvailableDirectMemorySize(0,
   sceKernelGetDirectMemorySize(), 2 MiB, ...)`, the figure PS5LM's `stats.cpp` shows as the
   pool's free memory: everything large in a title (the platform heap, RADV's device memory)
   comes from there. Only auto-fit reads it, and the app turns auto-fit off (the CPU "RAM" and the
   GPU heap are the same pool, so auto-fit's split between them would count it twice); the patch
   keeps `--auto-fit` meaningful for the server.
2. **`0002-ps5-default-thread-count.patch`.** `sd_get_num_physical_cores()` (the default
   `n_threads` for the context, the model loader and the upscaler) falls back to
   `hardware_concurrency()`, which reports 16 on the console while a process may only use the
   CPUs in its affinity mask (5 for a payload on 13.60, shared with system services). Busy
   workers on all of them shut PS5LM's test console down. On `__PROSPERO__` the default is the
   mask's popcount minus two, at least one (two when the mask cannot be read), as PS5LM's
   `patches/0001` does for llama.cpp. `--threads` still overrides it. *Unverified for a title:*
   PS5LM measured the mask in a payload; a title's may differ, and the app logs the thread count
   it uses (`sd_ctx_params_to_str`).
3. **`0003-ps5-file-stamp-without-times.patch`.** The model loader stamps every model file with
   `std::filesystem::last_write_time` and its size, and refuses to load a file it cannot stamp.
   Whether the console reports times on every file system it mounts (`/data`, exFAT USB drives) is
   not known; on `__PROSPERO__`, when the time cannot be read but the file exists, the stamp falls
   back to the size alone. A missing file still fails the same way.

Checked and left alone: sd.cpp's `mmap` path (`enable_mmap` is off by default and the app keeps it
off; PS5SX2 found `mmap` failing in the app sandbox), `posix_fadvise`, `MAP_POPULATE` and
`madvise` (all under `__linux__`), the `/sys` thread-sibling scan (under `__linux__`), miniz's
file I/O (the plain `fopen`/`fseeko` branch; `utime`/`chmod` are only on its extraction path,
which sd.cpp does not use), and `std::wstring_convert<std::codecvt_utf8<char32_t>>` in the
tokenizer (libc++ implements it without the platform locale; *unverified on the console*).

## Link-time shims (`ps5/app/compat_app.c`, in an archive)

From PS5LM's `compat_app.c`, with llama.cpp-specific notes dropped and the `dlopen` family
added. Archive members are linked only when referenced, and `--exclude-libs=ALL` keeps every one
local (a stub module defines some of the same names; exporting ours to interpose them is refused
by the title converter). Where PS5_Vulkan's recipe binds the same name to the SDK platform's
`ps5_` version with `--defsym` (it does for `localeconv`, `mkstemp`, `readlink`, `link`, `symlink`
when the pinned SDK has them), the `--defsym` wins.

| Shim | Why |
|---|---|
| `dlopen`, `dlsym`, `dlerror`, `dlclose` | `ggml-backend-dl.cpp` and `ggml_backend_load_all()` still name them with `GGML_BACKEND_DL=OFF`. A title cannot load ELF modules: `dlopen` returns NULL, as ggml expects for a missing backend file. Weak, so another definition in the link wins without a duplicate-symbol error while ours still beats a stub module's import. |
| `localeconv` | The console reports an empty decimal point while `strtod` reads `.`; nlohmann::json (sd.cpp's safetensors headers, metadata, and the server's requests) then mis-parses every fraction. Returns the C locale's. |
| `isatty`, `fork`, `pathconf`, `readlink`, `link`, `symlink`, `mkstemp` | Defined only by `libScePosixForWebKit`'s or `libkernel_sys`'s stub: null in a title (`isatty` was PS5LM's first crash). |
| `execlp`, `pipe2`, `getpwuid`, `posix_madvise` | Referenced by libraries in the link (PS5LM's set); none is on the generation path, each fails cleanly. |
| `getaddrinfo`, `freeaddrinfo`, `getnameinfo` | The SDK fork's `getaddrinfo` always fails, and cpp-httplib resolves even the address it binds; numeric only. `build-app.sh` drops the recipe's `--defsym` for the first two. For the HTTP server. |
| `accept4` | Declared but not exported; implemented on `accept` and `fcntl`, logging `errno` when `accept` fails. For the HTTP server. |
| `timegm` | cpp-httplib's date handling. |

`ps5/app/cpp_runtime.cpp` is PS5LM's copy of PS5_Vulkan's allocation operators: `operator new`
goes to `malloc` (so to the platform heap), and a failed allocation throws `std::bad_alloc`
instead of trapping, so `main.cpp` reports an out-of-memory load or generation as an error.
`app-symbols.map` (ps5-native-app-boilerplate's) keeps those operators local.

## The app (`ps5/app`)

- `main.cpp`: PS5LM's boot order: log to `/data/PS5SD/app.log` (stdout and stderr both appending, or
  stdout writes over the start of the error lines), `radv_enable_unified_heap_on_apu`
  and `HOME`, `env.txt` switches (`RADV_`, `MESA_`, `ACO_`, `GGML_`, `SD_`, `PS5SD_`) before the GPU opens,
  the screen first (it takes from the same memory as the model), splash hidden, then the work on
  a 16 MiB-stack thread while the main thread draws and polls for `quit`. The work is the HTTP server
  (below), or with `PS5SD_ONESHOT=1` in `env.txt` one image:
  the model from `model.txt`, options from `app-args.txt` (`gen_args.cpp`: `--model`,
  `--diffusion-model`, `--vae`, `--taesd`, `--clip_l`, `--t5xxl`, `--llm`, `-p`, `-n`, `-W`, `-H`,
  `--steps`, `--cfg-scale`, `--sampling-method`, `--scheduler`, `--seed`, `--vae-tiling`,
  `--flash-attn`, `--backend`, `--threads`), SD-Turbo defaults (512x512, 1 step, CFG 1, euler_a,
  seed 42), backend `Vulkan`, auto-fit and mmap off, per-step timing from the progress callback
  and the direct-memory figure before and after each phase in the log, the PNG (with a
  `parameters` text chunk) in `/data/PS5SD/outputs`.
- The server: `sd_server()` from `sd-server-lib` (`build-app.sh` builds it with `SD_SERVER_LIBRARY=ON`
  and the web UI embedded, put together from `ps5/webui/src` by `scripts/bundle-webui.sh`), started with the active model's arguments, `--listen-ip 0.0.0.0`,
  `--listen-port 7860`, `--output-dir /data/PS5SD/outputs`, `--backend Vulkan`, `--auto-fit off`,
  `--lora-model-dir /data/PS5SD/loras` and `--embd-dir /data/PS5SD/embeddings` (neither has to exist;
  an empty one would stop the server at start, because sd-server checks the embeddings directory with
  the throwing `std::filesystem::exists` and on the console `stat("")` fails with `EINVAL` rather than
  `ENOENT`, which libc++ throws on; measured on the first server start), then the lines of `/data/PS5SD/server-args.txt` (`scripts/ps5sd-app.sh server-args <file>`). A default
  is left out when the file names that option, since sd-server appends a string option given twice.
  `patches/sdcpp/0064` saves a `vid_gen` video into the output directory too
  (`sd_<date>_<time>_<n>.webm`), hands its path to `sd_activity_job_end` (which keeps it off the TV,
  the dashboard draws PNGs only), and lets the gallery list and serve videos with their type.
  `patches/sdcpp/0065` keeps the WARN and ERROR lines of the log while a job runs, so a failed job says
  why: `Not enough memory on the console ...` when the log names a memory failure, otherwise the last
  error, and `Cancelled` after a stop. The job's error, the A1111 and OpenAI answers and
  `sd_activity_job_end` (for the TV) carry it.
  The request's own settings (steps, CFG, size) win over the server's defaults, which are sd.cpp's, not
  the one-shot's SD-Turbo ones. `main.cpp` defines the activity hooks of `server_hooks.h` (in its own
  object: from an archive, lld could take the weak no-ops instead), which feed the status and the
  notifications. The main loop connects to the port on loopback to say when the API is up. A quit
  request calls `sd_server_terminate()` and ends the title only once `sd_server()` has returned (its
  HTTP threads and job worker ended, the model freed), at most 120 s later, so no generation is on the
  GPU when the title goes.
- The models (`models.cpp`): `model.txt` holds the id of a library entry, or (as before) a
  checkpoint's path. An entry is a checkpoint directly in `/data/PS5SD/models` (`.gguf`,
  `.safetensors`, `.ckpt`, `.sft`; its id is the file name, loaded with `-m`) or a preset,
  `/data/PS5SD/presets/<id>.txt`: sd-server arguments one a line, with `# name:` and
  `# defaults: steps=25 cfg=6 ...` comment lines. A relative path after a model option (`-m`, `--vae`,
  `--llm`, ...) is taken from the models directory, so the parts of a model can sit in subdirectories
  without being listed as checkpoints. With an entry, the model options of `server-args.txt` are left
  out (logged), the rest of it still applies. The entry's own `# defaults:` (`steps`, `cfg`, `sampler`,
  `scheduler`, `width`, `height`; a value that cannot be one is left out, `defaults_to_args`) are applied
  at run time with `sd_server_set_request_defaults` (`patches/sdcpp/0063`, with the values of
  `preset_settings`), over the same options of `server-args.txt`: a request that names none of them gets
  the preset's settings and one that names them overrides them. They are not command-line options, so a
  switch between presets of one model needs no restart (`ask_switch`).
  `patches/sdcpp/0062` makes `/sdapi/v1/txt2img` take the server's default size, not 512x512. The model
  lists of the A1111 and OpenAI APIs skip a saved preset (`# base:`) and a checkpoint that a preset
  without a base stands for, and `ModelLibrary::remove` deletes a saved preset's own file only.
  Without `model.txt`, `server-args.txt` must name the
  model itself. `models.cpp` uses only the `error_code` overloads of `std::filesystem` (the `EINVAL`
  above) and dumps JSON with invalid UTF-8 replaced, since it runs on the server's HTTP threads.
- The app's routes (`patches/sdcpp/0054`, `sd_app_request` in `main.cpp`): `GET /ps5sd/v1/app/models`
  gives the library with `active` (`model.txt`), `loaded` (what the server has up, empty while it
  loads) and `switching`. `POST /ps5sd/v1/app/models/active` with `{"id": ...}` writes `model.txt`
  (through a `.tmp` and a rename) and answers 202 with the same JSON; 400 for a body without an id,
  404 for an unknown id or a preset whose files are missing, 409 while a switch or a quit runs, 200
  when that model is loaded already. The switch itself waits for the main loop, which calls
  `sd_server_terminate()` after the answer is out; the worker then starts `sd_server()` again with the
  new arguments (the job that runs ends first). When the new model does not load (`sd_server()`
  returns non-zero), the worker writes back the last model that came up and starts that, so the web UI
  stays reachable. A quit between two starts ends the loop before the next one.
- `DELETE /ps5sd/v1/app/models/<id>` deletes a preset with the files it names, or a checkpoint
  (`ModelLibrary::remove`). A file that another preset or the model in use names stays, and the answer
  lists it under `kept` next to `deleted` (with the library as `GET models` gives it). 409 for the model
  in use, during a switch, and for a checkpoint that a preset names; 404 for an unknown id. Only files
  under `/data/PS5SD/models` are deleted, and a preset whose files are missing can be deleted too.
- Uploads (`patches/sdcpp/0057`, `sd_app_upload` in `main.cpp`, `upload.cpp`): `PUT
  /ps5sd/v1/app/uploads/<folder>/<name>` is registered with cpp-httplib's `ContentReader` handler,
  so the body is never held whole; the hook gets a read function that passes it on piece by piece.
  `upload.cpp` sends it in 1 MiB blocks to ftpsrv as one `STOR` of `<name>.part` (`ftp_writer.cpp`),
  or writes it itself when ftpsrv does not answer, and renames it at the end. A refused upload's body
  is not read, so the server answers it with `Connection: close`.
- LoRAs: `--lora-model-dir /data/PS5SD/loras`; `GET /ps5sd/v1/app/loras` (`loras_json` in
  `models.cpp`) lists the files with their sizes, `DELETE .../loras/<path>` deletes one. Downloads
  carry a `kind` (`models` or `loras`, `download_core.cpp`); a Civitai version whose model type is
  LORA, LoCon or DoRA gets `loras` by itself. `patches/sdcpp/0058` makes the A1111 routes refresh
  sd-server's LoRA cache before they look a `lora` path up, as its sdcpp routes do; before, a LoRA
  added after the start failed there with "invalid lora path" until a client listed the LoRAs.
- Without a display (`PS5SD_NO_DISPLAY`): no `Display`, no renderer. Every phase (`set_phase`)
  goes to `app.log` as `ps5sd: status: ...`, to the kernel log through `sceKernelDebugOutText`
  (klogsrv, `scripts/ps5sd-app.sh log -f`) and to a system notification; the sampling progress
  sends a notification at each quarter of the steps. The splash is hidden at once and the main
  thread only polls for `quit` (and leaves through `LoadExec("exit")`, never `exit()`).
  The launch payload starts the title for the foreground user, so the ~20 s end of a title
  started without a user does not apply. Whether a title that never opens a video output is a
  problem: nothing in PS5LM says so (its CONSOLE.md has the title hide the splash itself and
  show its state through notifications, and has no note of a title closed for having no
  screen); RADV opens no video output (ggml computes, it makes no swapchain). *Unverified.* If
  the shell closes the title or keeps the splash, `PS5SD_VIDEOOUT=1` in `env.txt` makes the app
  call `sceVideoOutOpen(255 /* system user */, 0 /* main bus */, 0, NULL)` before hiding the
  splash, and logs the handle; nothing is drawn into it.
- `app_status.cpp`: what the app is doing (phase, progress, memory, result, the log's last
  lines), shared by the screen and the no-display build; no ps5-homebrew-ui in it.
- `display.cpp`: PS5LM's EGL/OpenGL 4.6 context through ps5-opengl, the `sceAgcInit` wrap (both
  drivers initialise AGC; the first call does it, the others wait for its answer) and the GL
  runtime's empty thread-local initialiser.
- `status_view.cpp`: the screen for now, one monospaced font drawn with ps5-homebrew-ui into a
  DrawList, the slot a dashboard takes later.
- `tests/`: host-side tests for the parts with no console code (the argument parser, the model
  library, the downloader's URLs, catalog and queue in `test_downloads.cpp`), and
  `smoke_downloader.cpp`, the downloader against the real Hugging Face and Civitai from the PC
  (needs the network and a host mbedTLS; not run by ctest).

The icon (`ps5/app/sce_sys/icon0.png`) was made by PS5SD itself, on the console: FLUX.3 Image Klein
Q4_K (preset `flux3-klein-q4`), 1024x1024, 16 steps, CFG 1, euler, seed 5050, with the prompt "a
four-pointed sparkle star made of liquid iridescent glass in blue, violet and pink, with two small
sparkles beside it, square app icon, one bold symbol centered with generous margin, dark background
with a soft deep blue to violet gradient glow, glossy 3D, soft studio lighting, clean minimal design,
modern game console homebrew store icon, high quality render, no text". FLUX drew it as a rounded
tile with a rim, but the home screen rounds the tiles itself and wants the picture to fill the
square; with its own corners the icon looked like a tile in a tile. So 64 px of 1024 are cut from
each edge, inside FLUX's rounded corners. The rest is scaled to 512x512 and saved as RGBA, as
Prospero Themes' icon is. The home screen does not draw icon0.png: it draws `sce_sys/icon0.dds`,
the same picture as a 512x512 BC7 texture with one mip and a DX10 header. That file is committed;
after a new icon0.png, make it again on Windows with
`texconv -f BC7_UNORM -m 1 -y -o ps5/app/sce_sys ps5/app/sce_sys/icon0.png`. build-app.sh stops if it is
older than the PNG. Without it the tile showed no icon at all. The home screen reads both from
`/user/appmeta/PPSA99576`, which `scripts/ps5sd-app.sh deploy` (or `appmeta`) keeps in step. The home screen background (`sce_sys/source/background.png`) is still a
generated placeholder.

## The model downloader (`downloader.cpp`, `https_client.cpp`, `download_core.cpp`, `catalog.hpp`)

Models are downloaded on the console itself, from a starter catalog or from any Hugging Face or
Civitai URL. The recipe is PS5LM's: its payload's downloader (`ps5/ps5lm/app.cpp`: 8 MiB pieces over
16 connections, a record of finished pieces, retries with backoff, signed links fetched again when
they expire) and its app's queue (`ps5/app/market.cpp`: `downloads.json`, carried on after a
restart).

- **The HTTP stack.** PS5LM measured three things: `libSceHttp2` cannot send a request from a
  payload, though its title uses it (2 connections, 7 to 9.8 MB/s, and the library refuses more than
  4 requests); the kernel caps `SO_RCVBUF` at 64 KB, so one connection to a CDN about 150 ms away
  tops out near 0.9 MB/s; and 16 connections with their own TLS (cpp-httplib over mbedTLS 3.6.7) reach
  about 12.5 MB/s in a payload. PS5SD takes the last route, but not through cpp-httplib: sd-server
  already links cpp-httplib 0.28, which has no mbedTLS backend, and a second version of it in the
  same link would share its symbol names. `https_client.cpp` is a small HTTP/1.1 client (keep-alive,
  `Content-Length` and chunked bodies, redirects followed by hand) over plain sockets and mbedTLS,
  with the Mozilla CA list (`ps5/app/cacert.pem`, PS5LM's copy) embedded by `build-app.sh` through
  `embed-file.sh`. Certificates are verified; a failure says to check the console's date.
- **Names.** The title's `getaddrinfo` (`compat_app.c`) takes numeric addresses only, since the SDK
  fork's always fails. The client resolves names itself through libSceNet: `sceNetInit`, one
  `sceNetPoolCreate` (64 KiB), and a `sceNetResolverCreate`/`sceNetResolverStartNtoa`/`Destroy` per
  lookup (the prototypes of the SDK fork's own `libc/netdb.c`), cached for five minutes. On the PC
  it uses `getaddrinfo`.
- **The console's own address** (the TV's `http://<address>:<port>` and its QR code): `lan_address()`
  in `main.cpp` connects a UDP socket to 8.8.8.8 (nothing is sent) and reads the local end with
  `getsockname`, so it needs a default route; `PS5SD_ADDRESS` in `env.txt` sets it by hand. It looks
  every 5 s until it has one, then once a minute. No `getifaddrs`: a title that imports a function the
  console does not resolve does not start, and the socket calls are imported already.
- **mbedTLS** (`scripts/build-mbedtls.sh`, run by `build-app.sh` and `setup-deps.sh`, skipped when the
  stamp matches): 3.6.7, sha256-checked, built with the title's compilers (`toolchain.cmake`) into
  `.deps/mbedtls-ps5`, with `MBEDTLS_THREADING_C` and `MBEDTLS_THREADING_PTHREAD` (16 handshakes at
  once share PSA's key store). The SDK is FreeBSD 11.4 (`__FreeBSD_version` 1104001): no
  `getrandom`, so mbedTLS's platform entropy is `sysctl(KERN_ARND)`, which libkernel exports. Its
  archives go into the link's archive group, so `--exclude-libs` keeps them local. The new imports
  are `connect`, `send`, `recv`, `setsockopt`, `sysctl`, `gmtime`, `pwrite`, `ftruncate`, `fsync`
  and the five `sceNet*` above; none is null in a title.
- **One file.** A one-byte range request, following the redirects with a token only for its own
  host (`huggingface.co` gets `/data/PS5SD/hf-token.txt`, `civitai.com` gets
  `/data/PS5SD/civitai-token.txt`, as `Authorization: Bearer`; the CDNs' signed URLs never see it),
  gives the size (`Content-Range`) and the signed URL. Up to 16 threads, each with its own
  connection, fetch 8 MiB pieces into a buffer of their own. The console slows a title's own writes
  to `/data` to about 2 MB/s after 1.4 GB, but not a payload's (docs/CONSOLE.md), so ftpsrv writes
  the file (`ftp_writer.cpp`): the pieces go into `<dest>.part` in file order, each as `REST <offset>`
  + `STOR` over a loopback FTP connection, and a piece is done once ftpsrv answered 226. A piece
  that is ready early waits for the ones before it. A resume keeps the leading done pieces that lie
  wholly inside the `.part` (`pieces_in_order`), and the first write cuts off anything after them.
  The finished file is renamed with `RNFR`/`RNTO`. When ftpsrv does not answer on port 2121 (or with
  `PS5SD_DL_FTP=0`), the title writes itself, with one `pwrite` per whole piece, one piece at a
  time, and the download shows why it is slow. The `.part` grows as pieces land (no `ftruncate` up
  front), and a resume keeps the recorded pieces only while each lies inside it. For measuring,
  `env.txt` takes `PS5SD_DL_CONNECTIONS`,
  `PS5SD_DL_PIECE_MIB`, `PS5SD_DL_TRUNCATE=1` (make the `.part` its full size first),
  `PS5SD_DL_ORDERED=1` (pieces written in file order only), `PS5SD_DL_SYNC_S` (seconds between
  `fsync`s, 0 only at the end) and `PS5SD_DL_WRITETEST` (disk write tests without the network before
  the queue starts, see `write_test`; a dir of `ftp/data/...` writes through ftpsrv). Every 5 s the
  map of finished pieces is copied, the file `fsync`ed (direct writes only), and the map
  saved in `downloads.json` (written whole: `.tmp`, `fsync`, rename), so a piece is never recorded
  before its bytes are on disk. A failed piece goes back in the queue; its connection waits 1, 2, 4, 8
  then 16 s, and gives up after 8 failures in a row; a 403, 410 or redirect on a piece fetches a
  fresh signed URL once for all connections; a 4xx other than 403, 408, 410 and 429 fails the file.
  When every piece is there: `fsync`, rename to `<dest>`. A server without ranges is fetched in one
  stream (no resume). There is no free-space check (`statfs` is only in `libkernel_sys`, null in a
  title); a full disk shows up as `ENOSPC` and fails the file.
- **The queue.** One file at a time, oldest first, on a thread of its own, started in `main()`
  before the server and independent of it, so a download goes on while the server generates or is
  stopped and started for a model switch. A file's id is a hash of its destination, so two requests
  for the same destination are the same entry: 409 while it is queued or running, taken up again
  (from its `.part`) when it was cancelled or failed. A quit stops the transfers without waiting
  and saves the queue; a file that was running is queued again at the next start and carries on
  from the recorded pieces.
- **The catalog** (`catalog.hpp`) is data compiled into the app: entries of files (URL, destination
  under `models/`, size, and optionally a second size that counts as the same file) and a preset
  text. Installing one queues the files that are not there at their size (a VAE two entries share is
  fetched once), tagged with the entry's id; when the last one is done, the preset is written to
  `presets/<id>.txt` (`.tmp` and rename), so the model shows in `GET models`. A file at a
  destination with another size is in the way: 409, nothing is overwritten.
- **Destinations** are relative to `/data/PS5SD/models`: no absolute path, no `..`, no empty, hidden
  or backslashed part, at most four parts, and a model extension (`.gguf`, `.safetensors`, `.ckpt`,
  `.sft`). Hugging Face URLs are `https://huggingface.co/<owner>/<repo>/resolve/<rev>/<path>`
  (`/blob/` is rewritten to `/resolve/`; the default destination is the path's last part); Civitai
  URLs are `https://civitai.com/models/<id>?modelVersionId=<v>` or
  `https://civitai.com/api/download/models/<v>[?fileId=<f>]`, whose file name comes from
  `/api/v1/model-versions/<v>` (the primary model file, or `fileId`), else from the download's
  `Content-Disposition`. Other hosts are refused.

The routes, under `/ps5sd/v1/app/` (`sd_app_request` hands them to `downloader_route`; JSON in and
out, errors as `{"error": ...}`, nothing thrown across the hook):

| Route | Body | Answer |
|---|---|---|
| `GET catalog` | | `{"entries": [{"id", "name", "description", "featured", "good_for", "bytes", "installed", "files": [{"url", "dest", "bytes", "installed"}], "preset"}]}`, featured first |
| `GET downloads` | | `{"downloads": [{"id", "url", "dest", "bytes", "total", "state", "error", "bytes_per_second", "catalog"}]}`, oldest first; `state` is `queued`, `running`, `done`, `failed` or `cancelled` |
| `POST downloads` | `{"url", "dest"?, "catalog"?}`, or `{"catalog"}` alone to install an entry | 202 `{"downloads": [...]}` with what was queued; 200 and `[]` for an entry with every file there (the preset is written); 400 bad URL or destination, 404 unknown entry, 409 queued already, running, stopping, downloaded or in the way, 502 Civitai's API failed |
| `DELETE downloads/<id>` | | queued or running: cancelled, `.part` kept (200); cancelled or failed: the entry and its `.part` go; done: the entry goes, the file stays; 404 unknown, 409 while it is still stopping |
| `POST downloads/<id>/retry` | | a failed or cancelled one queued again, from its `.part` (202) |
| `POST downloads/clear` | | the finished ones leave the list (200, the list) |
| `POST hf/search` | `{"q", "limit"?}` (default 20, at most 50) | `{"results": [{"repo", "downloads", "likes", "files": [{"path", "bytes", "url"}]}]}`: Hugging Face's `/api/models` sorted by downloads, repositories without a model file left out, the sizes from `/api/models/<repo>?blobs=true` (four connections), `url` the file's resolve URL on `main` |
