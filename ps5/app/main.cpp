// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
//
// PS5SD native app: stable-diffusion.cpp on the PS5's GPU through RADV
// (ggml-vulkan), with its state on the TV drawn by ps5-homebrew-ui through
// ps5-opengl.
//
// A title gets the console's direct memory (12 GiB) where a payload gets none,
// and RADV turns it into Vulkan device memory, so the whole model sits on the
// GPU. For now the app generates one image ("hello sd"): the model's path from
// /data/PS5SD/model.txt, any of sd-cli's arguments that gen_args.cpp knows
// from /data/PS5SD/app-args.txt (one a line), the PNG to
// /data/PS5SD/outputs/<time>.png. The generation runs on its own thread with a
// 16 MiB stack; the main thread draws the screen and waits for
// /data/PS5SD/quit. The app logs to /data/PS5SD/app.log.
//
// Built with PS5SD_NO_DISPLAY (scripts/build-app.sh with PS5SD_DISPLAY=0, or
// when the ps5-opengl SDK is not available), the app has no screen: no
// ps5-opengl, no ps5-homebrew-ui. Each phase then goes to app.log, the kernel
// log (klogsrv) and a system notification, and the sampling progress to a
// notification every quarter of the steps. The main thread only waits for
// /data/PS5SD/quit.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "app_status.hpp"
#include "gen_args.hpp"
#include "ggml-backend.h"
#include "stable-diffusion.h"
#include "version.hpp"

#include <ps5platform/heap.h>
#include <ps5platform/kernel.h>

#ifndef PS5SD_NO_DISPLAY
#include "display.hpp"
#include "gfx/renderer.hpp"
#include "status_view.hpp"
#endif

// sd.cpp's copy of stb_image_write (it takes a PNG text chunk); the library
// itself does not compile one, so the app does, privately.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

// ---- Vulkan without a loader ----------------------------------------------------
// RADV is linked in whole, and ggml-vulkan's dispatcher starts from
// vkGetInstanceProcAddr, which the ICD entry point answers for every command.
// The few commands ggml calls by name go to Mesa's common implementations,
// which dispatch through the object's own table.
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char * name);
extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char * name) {
    return vk_icdGetInstanceProcAddr(instance, name);
}
extern "C" PFN_vkVoidFunction vk_common_GetDeviceProcAddr(VkDevice device, const char * name);
extern "C" void vk_common_GetPhysicalDeviceFeatures2(VkPhysicalDevice dev, VkPhysicalDeviceFeatures2 * features);
extern "C" void vk_common_CmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t n,
                                        const VkBufferCopy * regions);
extern "C" PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char * name) {
    return vk_common_GetDeviceProcAddr(device, name);
}
extern "C" void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice dev, VkPhysicalDeviceFeatures2 * features) {
    vk_common_GetPhysicalDeviceFeatures2(dev, features);
}
extern "C" void vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t n,
                                const VkBufferCopy * regions) {
    vk_common_CmdCopyBuffer(cmd, src, dst, n, regions);
}

extern "C" {
int sceSystemServiceHideSplashScreen(void);
int sceKernelSendNotificationRequest(uint32_t device, void * request, size_t size, int blocking);
int sceKernelUsleep(uint32_t microseconds);
int sceSystemServiceLoadExec(const char * path, const char * const * args);
int sceKernelDebugOutText(int channel, const char * text);
#ifdef PS5SD_NO_DISPLAY
int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void * param);
#endif
}

namespace {

using namespace ps5sd;

constexpr const char * kDir     = "/data/PS5SD";
constexpr const char * kLog     = "/data/PS5SD/app.log";
constexpr const char * kArgs    = "/data/PS5SD/app-args.txt";
constexpr const char * kModel   = "/data/PS5SD/model.txt";
constexpr const char * kEnv     = "/data/PS5SD/env.txt";  // driver and ggml switches, for measuring
constexpr const char * kQuit    = "/data/PS5SD/quit";
constexpr const char * kOutputs = "/data/PS5SD/outputs";
#ifndef PS5SD_NO_DISPLAY
constexpr const char * kFont    = "/app0/assets/fonts/dejavu-sans-mono.huifont";
#endif
constexpr double       kGiB     = 1073741824.0;

AppStatus g_status;

std::vector<std::string> read_lines(const char * path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            out.push_back(line);
        }
    }
    return out;
}

// A system notification on the TV.
void notify(const char * text) {
    struct {
        uint8_t reserved[45];
        char    message[3075];
    } request = {};
    std::snprintf(request.message, sizeof(request.message), "%s", text);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

// One line to the kernel log, which klogsrv streams to the PC
// (scripts/ps5sd-app.sh log -f).
void klog(const std::string & line) {
    sceKernelDebugOutText(0, (line + "\n").c_str());
}

// What the app is doing: on the screen, the log and the kernel log, and
// without a screen (PS5SD_NO_DISPLAY) in a notification too.
void set_phase(const std::string & phase, const std::string & detail = {}) {
    g_status.set_phase(phase, detail);
    const std::string line = "PS5SD: " + phase + (detail.empty() ? "" : ": " + detail);
    std::printf("ps5sd: status: %s\n", line.c_str());
    klog(line);
#ifdef PS5SD_NO_DISPLAY
    notify(line.c_str());
#endif
}

// A notification for an outcome the screen also shows; the no-display build
// has already sent one for the phase (set_phase).
void notify_outcome(const char * text) {
#ifndef PS5SD_NO_DISPLAY
    notify(text);
#else
    (void) text;
#endif
}

// A quit request from the PC: the app ends itself the way the system expects
// (exit() and _Exit() end in SIGSYS for a title; killing one that is
// rendering has been followed by lost consoles).
void quit_if_asked() {
    if (access(kQuit, F_OK) != 0) {
        return;
    }
    unlink(kQuit);
    std::printf("ps5sd: quit requested\n");
    std::fflush(nullptr);
    sceSystemServiceLoadExec("exit", nullptr);
}

// Where the memory is: the direct memory pool's free part (models, the GPU
// heap and the platform's malloc all come from it), the CPU heap, and what
// ggml says about each device.
void log_memory(const char * when) {
    int64_t       at   = 0;
    size_t        free = 0;
    const int64_t pool = sceKernelGetDirectMemorySize();
    sceKernelAvailableDirectMemorySize(0, pool, 2 * 1024 * 1024, &at, &free);
    struct ps5_heap_stats heap = {};
    ps5_heap_stats(&heap);
    std::printf("ps5sd: memory %s: pool free %.2f of %.2f GiB, cpu heap %.2f GiB (peak %.2f)", when, free / kGiB,
                pool / kGiB, heap.mapped_bytes / kGiB, heap.peak_bytes / kGiB);
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        size_t dev_free = 0, dev_total = 0;
        ggml_backend_dev_memory(ggml_backend_dev_get(i), &dev_free, &dev_total);
        std::printf(", %s free %zu total %zu", ggml_backend_dev_name(ggml_backend_dev_get(i)), dev_free, dev_total);
    }
    std::printf("\n");
    g_status.set_memory(free / kGiB, pool / kGiB);
}

// stable-diffusion.cpp's and ggml's log: to app.log, and the readable part to
// the screen.
void on_log(enum sd_log_level_t level, const char * text, void *) {
    static const char * const kLevels[] = { "debug", "verbose", "info", "warn", "error" };
    if (level == SD_LOG_DEBUG) {
        return;
    }
    std::printf("sd %s: %s", kLevels[level], text);
    if (level >= SD_LOG_INFO) {
        g_status.log(text);
    }
}

// Every step of loading (bytes) and sampling (denoiser steps), with its time.
void on_progress(int step, int steps, float seconds, void *) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last;
    const clock::time_point  now = clock::now();
    const double wall = step <= 1 ? 0.0 : std::chrono::duration<double>(now - last).count();
    last = now;
    std::printf("ps5sd: step %d/%d, %.3f s/it (%.3f s since the last)\n", step, steps, seconds, wall);
    g_status.set_progress(step, steps, seconds);
#ifdef PS5SD_NO_DISPLAY
    // Without a screen, a notification at each quarter of the steps.
    static int last_quarter = -1;
    const int  quarter      = steps > 0 ? step * 4 / steps : 0;
    if (step <= 1) {
        last_quarter = 0;
    }
    if (steps > 0 && quarter > last_quarter) {
        last_quarter = quarter;
        char line[128];
        std::snprintf(line, sizeof(line), "PS5SD: step %d/%d, %.2f s/it", step, steps, seconds);
        klog(line);
        notify(line);
    }
#endif
}

// /data/PS5SD/outputs/YYYYMMDD-HHMMSS.png, in the console's local time.
std::string output_path() {
    const std::time_t t = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&t, &tm);
    char name[64];
    std::snprintf(name, sizeof(name), "%s/%04d%02d%02d-%02d%02d%02d.png", kOutputs, tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return name;
}

// The one-shot generation, on the worker thread: open the GPU, load the
// model, make one image, save it, free the model.
void generate_once() {
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        size_t free = 0, total = 0;
        ggml_backend_dev_memory(dev, &free, &total);
        std::printf("ps5sd: device %zu %s (%s): %.2f / %.2f GiB free\n", i, ggml_backend_dev_name(dev),
                    ggml_backend_dev_description(dev), free / kGiB, total / kGiB);
    }
    log_memory("before loading");

    GenOptions options;
    if (const auto model = read_lines(kModel); !model.empty()) {
        options.model = model[0];
    }
    std::string error;
    if (!parse_gen_args(read_lines(kArgs), options, error)) {
        std::printf("ps5sd: %s\n", error.c_str());
        set_phase("Nothing to do", error);
        notify_outcome(("PS5SD: " + error).c_str());
        return;
    }
    sd_ctx_params_t     ctx_params;
    sd_img_gen_params_t gen_params;
    options.to_params(ctx_params, gen_params);
    if (char * text = sd_ctx_params_to_str(&ctx_params)) {
        std::printf("ps5sd: context\n%s", text);
        std::free(text);
    }

    const std::string model = options.model.empty() ? options.diffusion_model : options.model;
    set_phase("Loading the model", model);
    const auto t0 = std::chrono::steady_clock::now();
    sd_ctx_t * ctx = new_sd_ctx(&ctx_params);
    const auto t1 = std::chrono::steady_clock::now();
    if (ctx == nullptr) {
        std::printf("ps5sd: the model did not load\n");
        set_phase("The model did not load", "See /data/PS5SD/app.log");
        notify_outcome("PS5SD: the model did not load, see /data/PS5SD/app.log");
        log_memory("after a failed load");
        return;
    }
    std::printf("ps5sd: context ready in %.1f s\n", std::chrono::duration<double>(t1 - t0).count());
    log_memory("after loading");

    if (char * text = sd_img_gen_params_to_str(&gen_params)) {
        std::printf("ps5sd: generation\n%s", text);
        std::free(text);
    }
    set_phase("Generating", options.prompt);
    sd_image_t * images = nullptr;
    int          count  = 0;
    const bool   ok     = generate_image(ctx, &gen_params, &images, &count);
    const auto   t2     = std::chrono::steady_clock::now();
    std::printf("ps5sd: generate_image %s, %d image(s) in %.1f s\n", ok ? "done" : "failed", count,
                std::chrono::duration<double>(t2 - t1).count());
    log_memory("after generating");

    if (ok && count > 0 && images[0].data != nullptr) {
        mkdir(kOutputs, 0777);
        const std::string path = output_path();
        char parameters[1024];
        std::snprintf(parameters, sizeof(parameters),
                      "%s\nSteps: %d, CFG scale: %.1f, Seed: %lld, Size: %dx%d, Sampler: %s, Model: %s, PS5SD %s",
                      options.prompt.c_str(), options.steps, options.cfg_scale, (long long) options.seed,
                      options.width, options.height, sd_sample_method_name(options.sample_method), model.c_str(),
                      PS5SD_VERSION);
        const sd_image_t & im = images[0];
        const bool saved = stbi_write_png(path.c_str(), (int) im.width, (int) im.height, (int) im.channel, im.data, 0,
                                          parameters) != 0;
        std::printf("ps5sd: %s %s\n", saved ? "saved" : "could not save", path.c_str());
        if (saved) {
            g_status.set_result(path);
        }
        set_phase(saved ? "Done" : "The image could not be saved", path);
        notify_outcome(saved ? "PS5SD: image saved to /data/PS5SD/outputs" : "PS5SD: the image could not be saved");
    } else {
        set_phase("Generation failed", "See /data/PS5SD/app.log");
        notify_outcome("PS5SD: generation failed, see /data/PS5SD/app.log");
    }
    free_sd_images(images, count);
    free_sd_ctx(ctx);
    log_memory("after freeing the model");
}

// A thread with a 16 MiB stack, as PS5LM gives llama-server: the console's
// default is small, and a deep frame overflowed one before (PS5SX2 30814bd).
void start_thread(std::function<void()> & fn) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    pthread_t t{};
    pthread_create(&t, &attr, [](void * p) -> void * {
        try {
            (*static_cast<std::function<void()> *>(p))();
        } catch (const std::exception & e) {
            std::printf("ps5sd: exception: %s\n", e.what());
            set_phase("Stopped by an error", e.what());
        } catch (...) {
            std::printf("ps5sd: unknown exception\n");
            set_phase("Stopped by an error", "unknown exception");
        }
        std::fflush(nullptr);
        return nullptr;
    }, &fn);
    pthread_attr_destroy(&attr);
}

}  // namespace

int main(int, char **) {
    mkdir(kDir, 0777);
    // stdout and stderr to a file the PC can fetch over FTP.
    if (std::freopen(kLog, "w", stdout) != nullptr) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    if (std::freopen(kLog, "a", stderr) != nullptr) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    std::printf("ps5sd: version " PS5SD_VERSION ", stable-diffusion.cpp %s (%s)\n", sd_version(), sd_commit());
    // One device-local heap over the whole pool (RADV splits an APU's 2/3 to
    // 1/3), and a HOME, which a title lacks and libraries look up.
    setenv("radv_enable_unified_heap_on_apu", "true", 1);
    setenv("HOME", kDir, 0);
    // Driver and backend switches to measure with, one NAME=value a line in
    // env.txt, set before the GPU opens; only the driver's, ggml's, sd's and
    // the app's own (PS5SD_VIDEOOUT).
    for (const auto & line : read_lines(kEnv)) {
        const size_t      eq   = line.find('=');
        const std::string name = line.substr(0, eq == std::string::npos ? 0 : eq);
        bool known = false;
        for (const char * prefix : { "RADV_", "MESA_", "ACO_", "GGML_", "SD_", "PS5SD_" }) {
            known = known || name.rfind(prefix, 0) == 0;
        }
        if (known) {
            setenv(name.c_str(), line.c_str() + eq + 1, 1);
        }
        std::printf("ps5sd: env %s%s\n", line.c_str(), known ? "" : " (ignored)");
    }
    sd_set_log_callback(on_log, nullptr);
    sd_set_progress_callback(on_progress, nullptr);
    std::printf("ps5sd: %s\n", sd_get_system_info());

#ifndef PS5SD_NO_DISPLAY
    // The screen first: it takes from the same memory as the model.
    Display display;
    if (!display.open(1920, 1080)) {
        std::printf("ps5sd: no display\n");
    }
    hui::gfx::Renderer renderer;
    StatusView         view;
    const bool         ui_ok = renderer.init() && view.load(renderer, kFont);
    std::printf("ps5sd: screen %s\n", ui_ok ? "ready" : "failed");
    // TODO(dashboard): a ps5-homebrew-ui dashboard replaces StatusView here; it
    // draws into the same DrawList each frame.
    hui::gfx::DrawList scene;
    const auto render = [&] {
        scene.clear();
        view.draw(g_status.snapshot(), PS5SD_VERSION, scene);
        renderer.begin();
        renderer.draw(scene);
        display.present(renderer);
    };
    set_phase("Opening the GPU");
    if (ui_ok) {
        render();
        display.swap();
    }
    sceSystemServiceHideSplashScreen();
    notify("PS5SD: starting");
#else
    std::printf("ps5sd: built without a display (PS5SD_NO_DISPLAY): status in this log, klog and notifications\n");
    // Nothing opens the video output in this build. PS5SD_VIDEOOUT=1 in
    // env.txt opens the main one for the system user, in case the shell wants
    // a title to hold one (not seen in PS5LM; see docs/PORTING.md).
    if (const char * v = std::getenv("PS5SD_VIDEOOUT"); v != nullptr && std::string(v) == "1") {
        const int handle = sceVideoOutOpen(255, 0, 0, nullptr);
        std::printf("ps5sd: sceVideoOutOpen: %d (0x%08x)\n", handle, (unsigned) handle);
    }
    sceSystemServiceHideSplashScreen();
    set_phase("Opening the GPU", "version " PS5SD_VERSION ", no display");
#endif

    // TODO(server): run sd_server(argc, argv) from sd-server-lib
    // (patches/sdcpp/0050, SD_SERVER_LIBRARY=ON in scripts/build-app.sh) here
    // in place of the one-shot generation, with sd-server's arguments from
    // app-args.txt, and define the activity hooks of server_hooks.h to feed
    // g_status. sd_server_terminate() before a quit.
    //
    // The worker is never joined or detached: the platform's pthread wraps
    // (radv-link.sh) own its stack, and the title never ends on its own.
    std::function<void()> work = generate_once;
    start_thread(work);

#ifndef PS5SD_NO_DISPLAY
    // The screen and the quit request until the system closes the title: a
    // title may not return from main or exit.
    for (uint64_t n = 0;; ++n) {
        if (ui_ok) {
            render();
        }
        if (!display.swap() || !ui_ok) {
            sceKernelUsleep(16 * 1000);  // no vsync to wait on
        }
        if (n % 60 == 0) {
            quit_if_asked();
        }
    }
#else
    // The quit request until the system closes the title: a title may not
    // return from main or exit.
    for (;;) {
        sceKernelUsleep(500 * 1000);
        quit_if_asked();
    }
#endif
}
