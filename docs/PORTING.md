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

- `main.cpp`: PS5LM's boot order: log to `/data/PS5SD/app.log`, `radv_enable_unified_heap_on_apu`
  and `HOME`, `env.txt` switches (`RADV_`, `MESA_`, `ACO_`, `GGML_`, `SD_`, `PS5SD_`) before the GPU opens,
  the screen first (it takes from the same memory as the model), splash hidden, then the work on
  a 16 MiB-stack thread while the main thread draws and polls for `quit`. The work is one image:
  the model from `model.txt`, options from `app-args.txt` (`gen_args.cpp`: `--model`,
  `--diffusion-model`, `--vae`, `--taesd`, `--clip_l`, `--t5xxl`, `--llm`, `-p`, `-n`, `-W`, `-H`,
  `--steps`, `--cfg-scale`, `--sampling-method`, `--scheduler`, `--seed`, `--vae-tiling`,
  `--flash-attn`, `--backend`, `--threads`), SD-Turbo defaults (512x512, 1 step, CFG 1, euler_a,
  seed 42), backend `Vulkan`, auto-fit and mmap off, per-step timing from the progress callback
  and the direct-memory figure before and after each phase in the log, the PNG (with a
  `parameters` text chunk) in `/data/PS5SD/outputs`.
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
- `tests/`: host-side tests for the parts with no console code (the argument parser).

The icon and background in `ps5/app/sce_sys` are generated placeholders, to be replaced with real
artwork.
