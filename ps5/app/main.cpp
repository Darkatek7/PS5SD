// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
//
// PS5SD native app: stable-diffusion.cpp on the PS5's GPU through RADV
// (ggml-vulkan), with its state on the TV drawn by ps5-homebrew-ui through
// ps5-opengl.
//
// A title gets the console's direct memory (12 GiB) where a payload gets none,
// and RADV turns it into Vulkan device memory, so the whole model sits on the
// GPU. The app runs stable-diffusion.cpp's HTTP server (sd_server() from
// sd-server-lib, patches/sdcpp/0050-0054) on port 7860: the OpenAI and A1111
// image APIs, sd.cpp's own, the web UI (ps5/webui) at / and the gallery of
// /data/PS5SD/outputs. /data/PS5SD/model.txt names the model: an entry of the
// library (models.hpp: a checkpoint in /data/PS5SD/models or a preset in
// /data/PS5SD/presets) or a checkpoint's path; more of sd-server's arguments
// come from /data/PS5SD/server-args.txt (one a line). The app's own routes,
// /ps5sd/v1/app/models (list) and /ps5sd/v1/app/models/active (switch), are
// answered here (sd_app_request), and so are the model downloader's
// (downloader.hpp: the catalog, the download queue, Hugging Face search),
// which runs on threads of its own. The server runs on its own thread and stays
// up across a model switch: a worker with a 16 MiB stack loads each model into
// it (sd_server_load, patches/sdcpp/0078). The main thread draws the screen and
// waits for /data/PS5SD/quit, which stops the server before the title ends.
//
// PS5SD_ONESHOT=1 in /data/PS5SD/env.txt makes one image instead ("hello
// sd"): the model from model.txt, any of sd-cli's arguments that gen_args.cpp
// knows from /data/PS5SD/app-args.txt (one a line), the PNG to
// /data/PS5SD/outputs/<time>.png. The app logs to /data/PS5SD/app.log.
//
// Built with PS5SD_NO_DISPLAY (scripts/build-app.sh with PS5SD_DISPLAY=0, or
// when the ps5-opengl SDK is not available), the app has no screen: no
// ps5-opengl, no ps5-homebrew-ui. Each phase then goes to app.log, the kernel
// log (klogsrv) and a system notification, and the sampling progress to a
// notification every quarter of the steps. The main thread only waits for
// /data/PS5SD/quit.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <cerrno>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "app_status.hpp"
#include "console_stats.hpp"
#include "gen_args.hpp"
#include "ggml-backend.h"
#include "download_core.hpp"
#include "gallery_labels.hpp"
#include "prompt_tags.hpp"
#include "downloader.hpp"
#include "https_client.hpp"
#include "upload.hpp"
#include "models.hpp"
#include "json.hpp"
#include "mem_fit.hpp"
#include "server_hooks.h"
#include "stable-diffusion.h"
#include "version.hpp"

// patches/sdcpp/0065 (server_activity.h): the WARN and ERROR lines sd.cpp logged
// since begin(), read back as a reason; here for a model that does not load.
void        sd_server_failure_log_begin();
std::string sd_server_failure_reason(const std::string & fallback);

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
// sd-server-lib (patches/sdcpp/0050, 0078): serves until sd_server_terminate();
// sd_server_load() loads another model (or none: --standby) while it serves.
int  sd_server(int argc, char ** argv);
int  sd_server_load(int argc, char ** argv);
void sd_server_terminate(void);
// patches/sdcpp/0060 (auth.h): the key every request needs; "" for none.
void sd_server_set_api_key(const char * key);
// patches/sdcpp/0063 (runtime.h): the defaults of API requests, set at run time; -1 or "" for not set.
void sd_server_set_request_defaults(int steps, float cfg, const char * sampler, const char * scheduler, int width, int height);
// patches/sdcpp/0069 (runtime.h): the loaded preset's CLIP skip (-1 for none), its prompt starters and its name
// (into each saved PNG); "" for none.
void sd_server_set_preset_extras(int clip_skip, const char * prompt_starter, const char * negative_starter,
                                 const char * preset_name);
}

namespace {

using namespace ps5sd;

constexpr const char * kDir     = "/data/PS5SD";
constexpr const char * kLog     = "/data/PS5SD/app.log";
constexpr const char * kArgs    = "/data/PS5SD/app-args.txt";
constexpr const char * kServerArgs = "/data/PS5SD/server-args.txt";
constexpr const char * kModel   = "/data/PS5SD/model.txt";
constexpr const char * kEnv     = "/data/PS5SD/env.txt";  // driver and ggml switches, for measuring
constexpr const char * kQuit    = "/data/PS5SD/quit";
constexpr const char * kSettings = "/data/PS5SD/settings.json";  // GET/POST settings (AppSettings)
constexpr const char * kApiKey  = "/data/PS5SD/api-key.txt";  // the API key, one line; no file: no key
constexpr const char * kDocs    = "/app0/docs";  // the Help tab's docs (scripts/build-app.sh)
constexpr const char * kOutputs = "/data/PS5SD/outputs";
constexpr const char * kLoras   = "/data/PS5SD/loras";
constexpr const char * kEmbeddings = "/data/PS5SD/embeddings";
constexpr const char * kUpscalers = "/data/PS5SD/upscalers";  // ESRGAN models: hires fix and the Upscale button
constexpr const char * kModels  = "/data/PS5SD/models";
constexpr const char * kPresets = "/data/PS5SD/presets";
constexpr const char * kTags    = "/data/PS5SD/tags";  // tag files for the prompt's completion
#ifndef PS5SD_NO_DISPLAY
constexpr const char * kFont    = "/app0/assets/fonts/dejavu-sans-mono.huifont";
#endif
constexpr double       kGiB     = 1073741824.0;
constexpr int          kPort    = 7860;  // sd-server's listen port unless server-args.txt names one
constexpr int          kStopWaitSeconds = 120;  // how long a quit waits for the server to stop

AppStatus g_status;
ConsoleStats g_console;  // the temperature, power, fan and load (console_stats.hpp)

const ModelLibrary g_library{ kModels, kPresets };
// The names and tags of the gallery's files (gallery_labels.hpp).
GalleryLabels g_labels{ kOutputs };
// The tags the prompt fields suggest, from the files in kTags (prompt_tags.hpp).
TagIndex g_tags{ kTags };
std::mutex         g_settings_mutex;  // the HTTP threads read and change g_settings
AppSettings        g_settings;
std::mutex         g_key_mutex;  // the HTTP threads read and change g_api_key
std::string        g_api_key;

// The server's life, for the quit request: started just before sd_server() is
// called, done once it has returned (its HTTP threads and its job worker have
// ended, the model is freed). The worker is busy until then.
std::atomic<bool> g_server_started{false};
std::atomic<bool> g_server_done{false};
std::atomic<bool> g_worker_busy{false};
std::atomic<int>  g_server_port{kPort};
// For the memory hint (mem_fit.hpp): the models loaded since PS5SD started, the largest
// width x height that worked since the last one loaded, and the size of the request running.
std::atomic<int>     g_model_loads{0};
std::atomic<int64_t> g_largest_ok{0};
std::atomic<int64_t> g_job_size{0};
std::atomic<bool> g_quit_requested{false};
std::atomic<bool> g_quit_file{false};  // kQuit seen by watch_quit_file()

// A model switch: asked for by POST /ps5sd/v1/app/models/active (pending),
// taken up by the worker, which loads the new model into the running server,
// and over once it is ready or has failed (switching). g_switch_cv wakes the
// worker for a switch, a quit and the server's end.
std::atomic<bool>       g_switch_pending{false};
std::atomic<bool>       g_switching{false};
std::mutex              g_switch_mutex;
std::condition_variable g_switch_cv;
// The server runs without a model (sd_server_load --standby, patches/sdcpp/0078).
std::atomic<bool> g_standby{false};

// The id of the model the server has ready ("" while it loads), and of the
// last one that came up, to go back to when a switch fails.
std::mutex  g_model_mutex;
std::string g_loaded_id;
std::string g_good_id;

std::string loaded_id() {
    std::lock_guard<std::mutex> lock(g_model_mutex);
    return g_loaded_id;
}

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
// rendering has been followed by lost consoles). A running server is stopped
// first and the title ends only once sd_server() has returned, so no
// generation is on the GPU when it goes. The request is latched: the quit file
// stays until the very end (scripts/ps5sd-app.sh close waits for it to go),
// and a withdrawn file does not cancel a stop that has begun.
// The quit file is looked for on a thread of its own, once a second: an
// access() on /data can wait behind other I/O, and the main loop must keep
// presenting frames, or a Close from the home screen finds no GPU suspend
// point and ends in a crash report (docs/CONSOLE.md).
void watch_quit_file() {
    std::thread([] {
        while (!g_quit_file) {
            if (access(kQuit, F_OK) == 0) {
                g_quit_file = true;
            }
            sceKernelUsleep(1000 * 1000);
        }
    }).detach();
}

void quit_if_asked() {
    using clock = std::chrono::steady_clock;
    static clock::time_point asked;
    if (!g_quit_requested) {
        if (!g_quit_file) {
            return;
        }
        g_quit_requested = true;
        asked            = clock::now();
        std::printf("ps5sd: quit requested\n");
        // Downloads stop where they are (the queue keeps what is on disk) and
        // carry on at the next start; the quit does not wait for them.
        downloader_stop();
        if (g_server_started && !g_server_done) {
            set_phase("Stopping the server");
            sd_server_terminate();
        }
        g_switch_cv.notify_all();
    }
    if (g_worker_busy) {
        if (clock::now() - asked < std::chrono::seconds(kStopWaitSeconds)) {
            return;  // asked again on the main loop's next pass
        }
        std::printf("ps5sd: the server did not stop in %d s; ending anyway\n", kStopWaitSeconds);
    }
    unlink(kQuit);
    std::fflush(nullptr);
    sceSystemServiceLoadExec("exit", nullptr);
}

// The free parts of the direct memory pool that a buffer of 2 MiB and more can
// use (2 MiB aligned): their sum, the largest and how many. Each GPU buffer is
// one physically contiguous block of the pool (PS5_Mesa's radv_ps5_memory_alloc),
// so the largest part is the largest buffer that can be made now.
// sceKernelAvailableDirectMemorySize gives one free part of a range, so the
// ranges left and right of it are asked again.
struct PoolFree {
    size_t total   = 0;
    size_t largest = 0;
    int    parts   = 0;
};

PoolFree pool_free(int64_t pool) {
    constexpr size_t kAlign = 2 * 1024 * 1024;
    PoolFree         out;
    std::vector<std::pair<int64_t, int64_t>> ranges = { { 0, pool } };
    while (!ranges.empty() && out.parts < 100000) {
        const auto range = ranges.back();
        ranges.pop_back();
        if (range.second - range.first < static_cast<int64_t>(kAlign)) {
            continue;
        }
        int64_t at   = 0;
        size_t  size = 0;
        if (sceKernelAvailableDirectMemorySize(range.first, range.second, kAlign, &at, &size) != 0 || size == 0 ||
            at < range.first || at + static_cast<int64_t>(size) > range.second) {
            continue;
        }
        out.total += size;
        out.largest = std::max(out.largest, size);
        out.parts++;
        ranges.push_back({ range.first, at });
        ranges.push_back({ at + static_cast<int64_t>(size), range.second });
    }
    return out;
}

// Where the memory is: the direct memory pool's free part (models, the GPU
// heap and the platform's malloc all come from it), the CPU heap, and what
// ggml says about each device.
void log_memory(const char * when) {
    int64_t       at   = 0;
    size_t        free = 0;
    const int64_t pool = sceKernelGetDirectMemorySize();
    sceKernelAvailableDirectMemorySize(0, pool, 2 * 1024 * 1024, &at, &free);
    const PoolFree parts = pool_free(pool);
    struct ps5_heap_stats heap = {};
    ps5_heap_stats(&heap);
    std::printf("ps5sd: memory %s: pool free %.2f of %.2f GiB (in %d parts: %.2f GiB, the largest %.2f), cpu heap %.2f GiB "
                "(peak %.2f)",
                when, free / kGiB, pool / kGiB, parts.parts, parts.total / kGiB, parts.largest / kGiB,
                heap.mapped_bytes / kGiB, heap.peak_bytes / kGiB);
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        size_t dev_free = 0, dev_total = 0;
        ggml_backend_dev_memory(ggml_backend_dev_get(i), &dev_free, &dev_total);
        std::printf(", %s free %zu total %zu", ggml_backend_dev_name(ggml_backend_dev_get(i)), dev_free, dev_total);
    }
    std::printf("\n");
    g_status.set_memory(free / kGiB, pool / kGiB);
}

// The last lines of a title that ends on an exception nothing caught: libc++
// prints its type, this adds what it said and where memory stood, which is
// what explained the one in docs/CONSOLE.md ("PS5SD ended by a full CPU heap").
// No locks (the thread that threw may hold g_status's) and as little
// allocation as can be: the cause may be a full heap.
[[noreturn]] void on_terminate() {
    static char what[256];
    std::snprintf(what, sizeof what, "%s", "no exception (std::terminate was called)");
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception & x) {
            std::snprintf(what, sizeof what, "%s", x.what());
        } catch (...) {
            std::snprintf(what, sizeof what, "%s", "an exception that is not a std::exception");
        }
    }
    std::printf("ps5sd: ending on an uncaught exception: %s\n", what);
    int64_t       at   = 0;
    size_t        free = 0;
    const int64_t pool = sceKernelGetDirectMemorySize();
    sceKernelAvailableDirectMemorySize(0, pool, 2 * 1024 * 1024, &at, &free);
    struct ps5_heap_stats heap = {};
    ps5_heap_stats(&heap);
    std::printf("ps5sd: memory at the end: pool free %.2f of %.2f GiB (the largest part), cpu heap %.2f GiB (peak %.2f)\n",
                free / kGiB, pool / kGiB, heap.mapped_bytes / kGiB, heap.peak_bytes / kGiB);
    std::fflush(stdout);
    std::abort();
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

void log_devices() {
    for (size_t i = 0; i < ggml_backend_dev_count(); i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        size_t free = 0, total = 0;
        ggml_backend_dev_memory(dev, &free, &total);
        std::printf("ps5sd: device %zu %s (%s): %.2f / %.2f GiB free\n", i, ggml_backend_dev_name(dev),
                    ggml_backend_dev_description(dev), free / kGiB, total / kGiB);
    }
}

// The one-shot generation (PS5SD_ONESHOT=1), on the worker thread: open the
// GPU, load the model, make one image, save it, free the model.
void generate_once() {
    log_devices();
    log_memory("before loading");

    GenOptions options;
    if (const auto model = read_lines(kModel); !model.empty()) {
        // A library id is the checkpoint it names; a path stays a path.
        options.model = model[0];
        ModelEntry  entry;
        std::string ignored;
        if (g_library.find(model[0], entry, ignored)) {
            for (size_t i = 0; i + 1 < entry.args.size(); i++) {
                if (entry.args[i] == "-m" || entry.args[i] == "--model") {
                    options.model = entry.args[i + 1];
                }
            }
        }
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

// One start of sd-server: its command line, and the model it loads.
struct ServerPlan {
    std::vector<std::string> args;
    std::string              id;    // the library's id ("" for server-args.txt's own -m)
    std::string              name;  // for the screen
};

// sd-server's command line: the active model's arguments (model.txt's entry
// in the library), the app's defaults, then server-args.txt (one argument a
// line). With an entry, server-args.txt's own model options are left out;
// without model.txt, server-args.txt must name the model. The entry's own
// settings ("# defaults:") are not on the command line: apply_preset_settings
// sets them at run time. sd-server appends to a string option given twice
// (--backend Vulkan --backend CPU would be "Vulkan,CPU"), so a default is only
// added when no argument names that option itself.
bool server_plan(ServerPlan & plan, std::string & error) {
    std::vector<std::string> extra  = read_lines(kServerArgs);
    const auto               active = read_lines(kModel);
    std::vector<std::string> model_args;
    if (!active.empty()) {
        ModelEntry entry;
        if (!g_library.find(active[0], entry, error)) {
            error = "model.txt: " + error;
            return false;
        }
        std::vector<std::string> kept;
        for (size_t i = 0; i < extra.size(); i++) {
            if (is_model_path_option(extra[i]) && i + 1 < extra.size()) {
                std::printf("ps5sd: server-args.txt: %s %s left out, the model is %s\n", extra[i].c_str(),
                            extra[i + 1].c_str(), entry.id.c_str());
                i++;
                continue;
            }
            kept.push_back(extra[i]);
        }
        extra      = kept;
        model_args = entry.args;
        plan.id    = entry.id;
        plan.name  = entry.name;
    }
    std::vector<std::string> given = model_args;
    given.insert(given.end(), extra.begin(), extra.end());
    const auto names = [&](std::initializer_list<const char *> options) {
        for (const auto & a : given) {
            for (const char * o : options) {
                if (a == o) {
                    return true;
                }
            }
        }
        return false;
    };
    if (!names({ "-m", "--model", "--diffusion-model" })) {
        error = "no model: put a model's id or path in /data/PS5SD/model.txt";
        return false;
    }
    if (plan.name.empty()) {
        plan.name = read_server_config(kModel, kServerArgs, kPort).model;
    }
    std::vector<std::string> & args = plan.args;
    args = { "sd-server" };
    args.insert(args.end(), model_args.begin(), model_args.end());
    // Every interface, so the PC and the PS5's browser reach it.
    if (!names({ "-l", "--listen-ip" })) {
        args.insert(args.end(), { "--listen-ip", "0.0.0.0" });
    }
    if (!names({ "--listen-port" })) {
        args.insert(args.end(), { "--listen-port", std::to_string(kPort) });
    }
    // Every image also as a PNG in outputs, listed at /ps5sd/v1/gallery.
    if (!names({ "--output-dir" })) {
        args.insert(args.end(), { "--output-dir", kOutputs });
    }
    // As in the one-shot (gen_args.cpp): the console's GPU, and no auto-fit,
    // which would count the one pool of direct memory twice (as RAM and VRAM).
    if (!names({ "--backend" })) {
        args.insert(args.end(), { "--backend", "Vulkan" });
    }
    if (!names({ "--auto-fit" })) {
        args.insert(args.end(), { "--auto-fit", "off" });
    }
    // Directories sd-server looks in (`<lora:name:1>` in a prompt, textual
    // inversions); they need not exist. Never left empty: the server checks
    // the embeddings directory with the throwing std::filesystem::exists, and
    // on the console stat("") fails with EINVAL, not ENOENT, which libc++
    // throws on.
    if (!names({ "--lora-model-dir" })) {
        args.insert(args.end(), { "--lora-model-dir", kLoras });
    }
    // The text encoder's result for the last prompt only (sd.cpp keeps 4): the
    // same prompt with a new seed still skips the encoder. Each entry is CPU heap,
    // which comes from the same pool as the model: about 30 MB for FLUX.2 Klein 9B
    // (Qwen3-8B), and 4 of them rose the heap by 0.1 GiB on the console.
    if (!names({ "--conditioning-cache-size" })) {
        args.insert(args.end(), { "--conditioning-cache-size", "1" });
    }
    if (!names({ "--embd-dir" })) {
        args.insert(args.end(), { "--embd-dir", kEmbeddings });
    }
    // Upscaler models for the hires fix and POST /sdcpp/v1/upscale (sd-server
    // lists the top of the folder).
    if (!names({ "--hires-upscalers-dir" })) {
        args.insert(args.end(), { "--hires-upscalers-dir", kUpscalers });
    }
    args.insert(args.end(), extra.begin(), extra.end());
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "--listen-port") {
            g_server_port = std::atoi(args[i + 1].c_str());
        }
    }
    return true;
}

// sd-server without a model: the web UI, the gallery and the app's routes,
// so a fresh install can download and pick its first model. The listen
// address and port as server-args.txt gives them, if it does; the server
// options of a start when there is no model (run_server adds --standby).
ServerPlan standby_plan() {
    ServerPlan plan;
    plan.name                     = "no model";
    std::string                    ip   = "0.0.0.0";
    std::string                    port = std::to_string(kPort);
    const std::vector<std::string> extra = read_lines(kServerArgs);
    for (size_t i = 0; i + 1 < extra.size(); i++) {
        if (extra[i] == "-l" || extra[i] == "--listen-ip") {
            ip = extra[i + 1];
        } else if (extra[i] == "--listen-port") {
            port = extra[i + 1];
        }
    }
    g_server_port = std::atoi(port.c_str());
    plan.args = { "sd-server", "--listen-ip", ip, "--listen-port", port, "--output-dir", kOutputs };
    return plan;
}

// model.txt, written whole or not at all.
bool write_active_model(const std::string & id) {
    const std::string tmp = std::string(kModel) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << id << "\n";
        if (!out) {
            return false;
        }
    }
    return std::rename(tmp.c_str(), kModel) == 0;
}

// The entry's settings ("# defaults:") as the server's request defaults, set
// at run time, so a switch needs no restart; nullptr clears them (standby).
void apply_preset_settings(const ModelEntry * entry) {
    if (entry == nullptr) {
        sd_server_set_request_defaults(-1, -1, "", "", -1, -1);
        sd_server_set_preset_extras(-1, "", "", "");
        return;
    }
    const PresetSettings settings = preset_settings(entry->defaults);
    sd_server_set_request_defaults(settings.steps, settings.cfg, settings.sampler.c_str(), settings.scheduler.c_str(),
                                   settings.width, settings.height);
    // a checkpoint's name is its file, which the PNG has as "Model:" already
    sd_server_set_preset_extras(settings.clip_skip, entry->prompt_starter.c_str(), entry->negative_starter.c_str(),
                                entry->kind == "preset" ? entry->name.c_str() : "");
}

void start_thread(std::function<void()> & fn);  // below

// Loads plan into the running server (standby: unloads the model). Sets the
// screen's model and phase, and the preset's settings as the request defaults.
// True when it loaded.
bool load_plan(const ServerPlan & plan, bool standby) {
    g_standby = standby;
    std::printf("ps5sd: load");
    for (const auto & a : plan.args) {
        if (&a != &plan.args.front()) {
            std::printf(" %s", a.c_str());
        }
    }
    std::printf("\n");
    g_status.set_model(plan.name);
    g_status.set_port(g_server_port.load());
    if (standby) {
        set_phase("Starting without a model", "the web UI on port " + std::to_string(g_server_port.load()));
    } else {
        set_phase("Loading the model", plan.name + ", then the API on port " + std::to_string(g_server_port.load()));
    }
    {
        std::lock_guard<std::mutex> lock(g_model_mutex);
        g_loaded_id.clear();
    }
    // The settings of the entry being loaded (none in standby, or when
    // server-args.txt names the model itself).
    ModelEntry  entry;
    std::string ignored;
    if (!standby && !plan.id.empty() && g_library.find(plan.id, entry, ignored)) {
        apply_preset_settings(&entry);
    } else {
        apply_preset_settings(nullptr);
    }
    std::vector<std::string> args = standby ? std::vector<std::string>{ "sd-server", "--standby" } : plan.args;
    std::vector<char *>      argv;
    for (auto & a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    sd_server_failure_log_begin();
    const int rc = sd_server_load((int) args.size(), argv.data());
    std::printf("ps5sd: sd_server_load returned %d\n", rc);
    return rc == 0;
}

// The server on a thread of its own, started once (patches/sdcpp/0078): it
// serves until a quit request, and a model switch loads the next model into it
// without stopping it, so the API, the web UI and their event streams stay up.
// This worker loads the model model.txt names, and again after each switch; when
// one does not load it goes back to the last one that worked, or to none
// (standby), so the web UI stays.
void run_server() {
    struct NotBusy {
        ~NotBusy() { g_worker_busy = false; }
    } not_busy;
    log_devices();
    log_memory("before loading");

    // The server's address, port and options: those of the model's arguments,
    // which sd-server reads and leaves to sd_server_load().
    static std::vector<std::string> server_args;
    {
        std::string error;
        ServerPlan  plan;
        server_args = server_plan(plan, error) ? plan.args : standby_plan().args;
        server_args.insert(server_args.begin() + 1, "--standby");
    }
    static std::function<void()> serve = [] {
        std::vector<char *> argv;
        for (auto & a : server_args) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        const int rc = sd_server((int) server_args.size(), argv.data());
        std::printf("ps5sd: sd_server returned %d\n", rc);
        log_memory("after the server");
        {
            // done however it ended, so a quit request does not wait out its time
            std::lock_guard<std::mutex> lock(g_switch_mutex);
            g_server_done = true;
        }
        g_switch_cv.notify_all();
    };
    if (g_quit_requested) {
        return;
    }
    g_server_done    = false;
    g_server_started = true;
    start_thread(serve);

    bool force_standby = false;  // a model failed to load with none to go back to
    while (!g_quit_requested && !g_server_done) {
        std::string error;
        ServerPlan  plan;
        const bool  standby = force_standby || !server_plan(plan, error);
        if (standby) {
            // No model yet (a fresh install), or model.txt names one that is
            // gone or does not load: the web UI without one, to fetch or pick one.
            std::printf("ps5sd: %s; standby without a model\n", force_standby ? "the model did not load" : error.c_str());
            plan = standby_plan();
        }
        force_standby = false;
        const bool        ok   = load_plan(plan, standby);
        const std::string port = std::to_string(g_server_port.load());
        if (g_quit_requested || g_server_done) {
            break;
        }
        if (ok && standby) {
            set_phase("No model loaded", "Web UI on port " + port + ": download or pick one under Models");
            notify_outcome(("PS5SD: no model loaded; web UI on port " + port + ", tab Models").c_str());
        } else if (ok) {
            {
                std::lock_guard<std::mutex> lock(g_model_mutex);
                g_loaded_id = plan.id;
                g_good_id   = plan.id;
            }
            ++g_model_loads;
            g_largest_ok = 0;
            log_memory("after loading");
            set_phase("Ready", "API and web UI on port " + port);
            notify_outcome(("PS5SD: ready, API and web UI on port " + port).c_str());
        } else {
            if (!plan.id.empty()) {
                // Why, for the web UI (GET models' load_error) and the log: what
                // sd.cpp said, or what the file is when it is only a part of a model.
                ModelEntry  entry;
                std::string ignored;
                std::string why = sd_server_failure_reason("");
                const std::string prefix = "Generation failed: ";
                if (why.compare(0, prefix.size(), prefix) == 0) {
                    why.erase(0, prefix.size());
                }
                if (why.empty() && g_library.find(plan.id, entry, ignored)) {
                    why = part_reason(entry);
                }
                if (why.empty()) {
                    why = "sd.cpp stopped while loading it and did not say why; /data/PS5SD/app.log has its last lines. "
                          "A damaged or incomplete file, or a model PS5SD's stable-diffusion.cpp does not know, ends so.";
                }
                std::printf("ps5sd: %s did not load: %s\n", plan.id.c_str(), why.c_str());
                set_load_error(plan.id, plan.name + " did not load: " + why);
            }
            log_memory("after the failed load");
            std::string good;
            {
                std::lock_guard<std::mutex> lock(g_model_mutex);
                good = g_good_id;
            }
            if (!good.empty() && good != plan.id && write_active_model(good)) {
                // A switch to a model that does not load (not enough memory, a
                // broken file): back to the one that worked, so the web UI stays.
                std::printf("ps5sd: %s did not load, back to %s\n", plan.id.c_str(), good.c_str());
                notify_outcome(("PS5SD: " + plan.name + " did not load, back to the last model").c_str());
                continue;
            }
            // Nothing that worked to go back to: the web UI stays up anyway.
            std::printf("ps5sd: %s did not load\n", plan.id.c_str());
            notify_outcome(("PS5SD: " + plan.name + " did not load, see /data/PS5SD/app.log").c_str());
            force_standby = true;
            continue;
        }
        g_switching = false;
        // the next switch, a quit, or the server's end
        std::unique_lock<std::mutex> lock(g_switch_mutex);
        g_switch_cv.wait(lock, [] { return g_switch_pending || g_quit_requested || g_server_done; });
        g_switch_pending = false;
        if (!g_quit_requested && !g_server_done) {
            std::printf("ps5sd: switching the model\n");
        }
    }
    g_switching = false;
    {
        // a quit stops the server (quit_if_asked); the worker is busy until it has
        std::unique_lock<std::mutex> lock(g_switch_mutex);
        g_switch_cv.wait(lock, [] { return g_server_done.load(); });
    }
    if (g_quit_requested) {
        set_phase("Server stopped");
        return;
    }
    // The port could not be bound, or an argument was wrong (the usage is in the log).
    set_phase("The server stopped", "See /data/PS5SD/app.log");
    notify_outcome("PS5SD: the server stopped, see /data/PS5SD/app.log");
}

// The console's address on the local network, for the dashboard's
// "http://<address>:<port>" and its QR code. Connecting a UDP socket sends
// nothing; the kernel only picks the interface a packet to that address would
// leave by, and getsockname() names it. Needs a default route; without one,
// PS5SD_ADDRESS in env.txt sets the address by hand. The approach comes from
// Machete PS5SD (github.com/VenezuelanBiggie24/Machete-PS5SD-IMG-and-VID).
std::string lan_address() {
    if (const char * v = std::getenv("PS5SD_ADDRESS"); v != nullptr && *v != '\0') {
        return v;
    }
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return {};
    }
    sockaddr_in to     = {};
    to.sin_family      = AF_INET;
    to.sin_port        = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808);  // 8.8.8.8; any address off the local network does
    sockaddr_in local  = {};
    socklen_t   len    = sizeof(local);
    std::string out;
    if (connect(s, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0) {
        const uint32_t a = ntohl(local.sin_addr.s_addr);
        if (a != 0 && (a >> 24) != 127) {
            char text[16];
            std::snprintf(text, sizeof(text), "%u.%u.%u.%u", a >> 24, (a >> 16) & 255u, (a >> 8) & 255u, a & 255u);
            out = text;
        }
    }
    close(s);
    return out;
}

// From the main loop: looks every 5 s while the address is not known, then
// once a minute (a new DHCP lease).
void watch_address() {
    static std::chrono::steady_clock::time_point next;
    static std::string                           known;
    const auto now = std::chrono::steady_clock::now();
    if (now < next) {
        return;
    }
    const std::string address = lan_address();
    next = now + (address.empty() ? std::chrono::seconds(5) : std::chrono::seconds(60));
    if (address != known) {
        known = address;
        g_status.set_address(address);
        std::printf("ps5sd: address %s\n", address.empty() ? "not known" : address.c_str());
    }
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

// Makes the library entry `id` the active model and has the server started
// again with it, once the request that asks has been answered (models/active,
// and A1111's options). The HTTP status; the body in out.
int ask_switch(const std::string & id, std::string & out) {
    ModelEntry  entry;
    std::string error;
    if (!g_library.find(id, entry, error)) {
        out = error_json(error, "model_not_found");
        return 404;
    }
    // Only a part of a model (a diffusion model without its text encoder, a
    // text encoder, a VAE): sd.cpp would stop while reading it, without a word.
    const std::string part = part_reason(entry);
    if (!part.empty()) {
        std::printf("ps5sd: switch to %s refused: %s\n", entry.id.c_str(), part.c_str());
        out = error_json(part, "model_part");
        return 422;
    }
    if (g_quit_requested) {
        out = error_json("the app is stopping", "stopping");
        return 409;
    }
    if (g_switching) {
        out = error_json("the server is switching the model; wait until GET /ps5sd/v1/app/models says it is done",
                         "switching");
        return 409;
    }
    set_load_error("", "");  // a new try: the last failure is old news
    if (entry.id == loaded_id()) {
        out = models_json(g_library, entry.id, entry.id, false);
        return 200;
    }
    // A preset with the same model arguments as the loaded one: the server has
    // the model ready, so no restart. Its settings become the server's request
    // defaults at run time (apply_preset_settings).
    {
        ModelEntry  loaded;
        std::string ignored;
        const std::string current = loaded_id();
        if (!current.empty() && !g_standby && g_library.find(current, loaded, ignored) && loaded.args == entry.args) {
            if (!write_active_model(entry.id)) {
                out = error_json("could not write /data/PS5SD/model.txt");
                return 500;
            }
            apply_preset_settings(&entry);
            {
                std::lock_guard<std::mutex> lock(g_model_mutex);
                g_loaded_id = entry.id;
                g_good_id   = entry.id;
            }
            g_status.set_model(entry.name);
            std::printf("ps5sd: %s uses the loaded model; no restart\n", entry.id.c_str());
            out = models_json(g_library, entry.id, entry.id, false);
            return 200;
        }
    }
    if (!write_active_model(entry.id)) {
        out = error_json("could not write /data/PS5SD/model.txt");
        return 500;
    }
    std::printf("ps5sd: switch to %s asked for\n", entry.id.c_str());
    {
        std::lock_guard<std::mutex> lock(g_switch_mutex);
        g_switching      = true;
        g_switch_pending = true;
    }
    g_switch_cv.notify_all();
    out              = models_json(g_library, entry.id, loaded_id(), true);
    return 202;
}

// The model A1111 clients see: the one loaded, or while a switch runs (or
// before the first load) the one model.txt names, as a library id.
std::string current_checkpoint() {
    const std::string loaded = loaded_id();
    if (!loaded.empty() && !g_switching) {
        return loaded;
    }
    const auto  active = read_lines(kModel);
    ModelEntry  entry;
    std::string ignored;
    if (active.empty()) {
        return {};
    }
    return g_library.find(active[0], entry, ignored) ? entry.id : active[0];
}

}  // namespace

// sd-server's activity hooks (server_hooks.h), called around every request's
// generation. Defined here, in an object of the link and not in an archive:
// inside the link's archive group lld could take the server's weak no-ops
// instead. The server sets its own progress and log callbacks, so the
// progress reaches the app only through these.
extern "C" void sd_activity_job_begin(const char * kind, const char * prompt, int w, int h, int steps) {
    std::printf("ps5sd: request %s, %dx%d, %d steps\n", kind, w, h, steps);
    g_job_size = (int64_t) w * h;
    g_status.set_progress(0, steps, 0);
    set_phase("Generating", prompt != nullptr && *prompt != '\0' ? prompt : kind);
}

extern "C" void sd_activity_progress(int step, int steps, float secs) {
    on_progress(step, steps, secs, nullptr);
}

// The image being denoised, for the screen's live preview; only called when
// the request asked for previews. The no-display build has nothing to show it on.
extern "C" void sd_activity_preview(const uint8_t * rgb, int w, int h) {
#ifndef PS5SD_NO_DISPLAY
    g_status.set_preview(rgb, w, h);
#else
    (void) rgb;
    (void) w;
    (void) h;
#endif
}

// The settings file, read once at the start; a missing or broken file leaves
// the defaults.
void load_settings() {
    std::ifstream      in(kSettings, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string error;
    if (!text.str().empty() && !apply_settings(text.str(), g_settings, error)) {
        std::printf("ps5sd: %s: %s; using the defaults\n", kSettings, error.c_str());
    }
    g_status.set_tv_images(g_settings.tv_images);
    g_console.set_fan(g_settings.fan, g_settings.fan_target);
}

// Writes the settings to a temporary file and renames it over the old one.
bool save_settings(const AppSettings & settings) {
    const std::string tmp = std::string(kSettings) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << settings_json(settings) << "\n";
        if (!out.good()) {
            return false;
        }
    }
    return std::rename(tmp.c_str(), kSettings) == 0;
}

// The API key the TV shows: the key, unless there is none or the setting
// hides it. Called with the key and the settings as they are now.
void show_api_key(const std::string & key, const AppSettings & settings) {
    g_status.set_api_key(settings.tv_api_key ? api_key_groups(key) : std::string());
}

// The key from api-key.txt, read once at the start; no file: no key.
void load_api_key() {
    const auto lines = read_lines(kApiKey);
    const std::string key = lines.empty() ? std::string() : lines[0];
    {
        std::lock_guard<std::mutex> lock(g_key_mutex);
        g_api_key = key;
    }
    sd_server_set_api_key(key.c_str());
    std::printf("ps5sd: API key %s\n", key.empty() ? "off" : "on");
    show_api_key(key, g_settings);
}

// A new key in api-key.txt (written to a temporary file and renamed), or
// none (the file deleted); the server needs it from the next request on.
// False with an error when the key cannot be made or written.
bool set_api_key(bool on, std::string & error) {
    std::string key;
    if (on) {
        unsigned char bytes[32];
        if (!random_bytes(bytes, sizeof(bytes))) {
            error = "no random source for a key";
            return false;
        }
        key = api_key_from_bytes(bytes, sizeof(bytes));
        const std::string tmp = std::string(kApiKey) + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << key << "\n";
            if (!out.good()) {
                error = std::string("could not write ") + kApiKey;
                return false;
            }
        }
        if (std::rename(tmp.c_str(), kApiKey) != 0) {
            error = std::string("could not write ") + kApiKey + ": " + std::strerror(errno);
            return false;
        }
    } else if (::unlink(kApiKey) != 0 && errno != ENOENT) {
        error = std::string("could not delete ") + kApiKey + ": " + std::strerror(errno);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_key_mutex);
        g_api_key = key;
    }
    sd_server_set_api_key(key.c_str());
    std::printf("ps5sd: API key %s\n", on ? "set to a new one" : "off");
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    show_api_key(key, g_settings);
    return true;
}

// The app's routes under /ps5sd/v1/app/ (patches/sdcpp/0054), on the
// server's HTTP threads:
//   GET  models         the library, the active model and the loaded one
//   POST models/active  {"id": "..."}: make it the active model and start the
//                       server again with it (202, with the library as GET
//                       gives it); the switch happens once this request has
//                       been answered, after the job that runs, if any.
//   DELETE models/<id>  delete a preset with its files, or a checkpoint
//                       (ModelLibrary::remove); 409 for the model in use.
//   compat/sd-models, compat/options (GET, POST), compat/models: the A1111
//                       and OpenAI model routes (patches/sdcpp/0055), from
//                       the library; POST options with sd_model_checkpoint
//                       switches like models/active.
//   catalog, downloads, downloads/<id>, hf/search: the model downloader
//                       (downloader.hpp), which runs on its own threads and
//                       keeps going while the server restarts.
//   gallery (POST)      {"image": "<base64 PNG>"}: an image the web UI made,
//                       saved to outputs (201 {"saved": "<name>"}).
//   gallery/labels (GET) {"labels": {"<file>": {"title", "tags"}}}; (POST)
//                       {"name": "<file>", "title": "...", "tags": [...]}: a
//                       file's name and tags (gallery_labels.hpp), answered
//                       with {"name", "title", "tags"} as kept.
//   gallery/keep (POST) {"name": "tmp_<file>"}: keeps an image made with
//                       "temporary" ({"kept": "<file>"}).
//   tags (GET)          {"files": [...], "tags": n}: the tag files in
//                       /data/PS5SD/tags; (POST) {"q": "...", "limit": n}:
//                       {"tags": [{"tag", "count", "alias"}]}, the tags that
//                       start with q (prompt_tags.hpp).
//   upscalers (GET), upscalers/<file> (DELETE): the upscaler models in
//                       /data/PS5SD/upscalers.
//   settings (GET, POST) the app's settings (AppSettings): {"tv_images": bool,
//                       "tv_api_key": bool, "image_metadata": bool, ...}.
//   auth (GET), auth/enable, auth/disable (POST): the API key; enable makes
//                       a new one (also when there is one).
//   presets (POST), presets/<id> (GET, POST, DELETE): saved presets, the
//                       variants of a model (ModelLibrary::create_preset).
//   console (GET)       the console's temperature, power, fan and load, now,
//                       the last 2 minutes and the last hour (console_stats.hpp).
// GET docs/<name>: docs/MODELS.md or docs/API.md, which the title carries,
// for the web UI's Help tab.
int doc_request(const std::string & name, char ** out_body, size_t * out_len, const char ** content_type) {
    std::string out;
    int         status = 404;
    *content_type      = "application/json";
    if (name == "MODELS.md" || name == "API.md") {
        std::ifstream      in(std::string(kDocs) + "/" + name, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        out = text.str();
        if (!out.empty()) {
            status        = 200;
            *content_type = "text/markdown; charset=utf-8";
        }
    }
    if (status != 200) {
        out = error_json("no doc " + name);
    }
    *out_body = static_cast<char *>(std::malloc(out.size()));
    if (*out_body != nullptr) {
        std::memcpy(*out_body, out.data(), out.size());
        *out_len = out.size();
    }
    return status;
}

// PUT uploads/<kind>/<name>: a file from the web UI (upload.hpp).
extern "C" int sd_app_upload(const char * path, uint64_t length, sd_app_reader read, void * read_ctx,
                             char ** out_body, size_t * out_len, const char ** content_type) {
    const std::string p = path;
    if (p.rfind("uploads/", 0) != 0) {
        return 0;
    }
    std::string out;
    int         status;
    if (g_quit_requested) {
        out    = error_json("the app is stopping", "stopping");
        status = 503;
    } else {
        const UploadReader reader = [&](const std::function<bool(const char *, size_t)> & sink) {
            struct Ctx {
                const std::function<bool(const char *, size_t)> * sink;
            } ctx{ &sink };
            return read(
                       read_ctx,
                       [](void * c, const char * data, size_t n) -> int {
                           return (*static_cast<Ctx *>(c)->sink)(data, n) ? 1 : 0;
                       },
                       &ctx) != 0;
        };
        status = upload_file(UploadDirs{ kModels, kLoras, kEmbeddings, kPresets, kUpscalers, kTags }, p.substr(8), length, reader,
                             out);
    }
    *out_body = static_cast<char *>(std::malloc(out.size()));
    if (*out_body != nullptr) {
        std::memcpy(*out_body, out.data(), out.size());
        *out_len = out.size();
    }
    *content_type = "application/json";
    return status;
}

extern "C" int sd_app_request(const char * method, const char * path, const char * body, size_t body_len,
                              char ** out_body, size_t * out_len, const char ** content_type) {
    const std::string m = method;
    const std::string p = path;
    if (m == "GET" && p.rfind("docs/", 0) == 0) {
        return doc_request(p.substr(5), out_body, out_len, content_type);
    }
    std::string       out;
    // The downloader's routes (downloader.hpp: catalog, downloads, hf/search);
    // it catches its own errors.
    int status = downloader_route(m, p, body != nullptr ? std::string(body, body_len) : std::string(), out);
    if (status > 0) {
        // answered
    } else if (m == "GET" && p == "models") {
        const auto active = read_lines(kModel);
        out    = models_json(g_library, active.empty() ? "" : active[0], loaded_id(), g_switching, g_standby && !g_switching);
        status = 200;
    } else if (m == "POST" && p == "models/active") {
        const std::string id = id_from_json(std::string(body, body_len));
        if (id.empty()) {
            out    = error_json("the body must be {\"id\": \"<model id>\"}");
            status = 400;
        } else {
            status = ask_switch(id, out);
        }
    } else if (m == "GET" && p == "console") {
        out    = g_console.json();
        status = 200;
    } else if (m == "GET" && p == "info") {
        out    = info_json(PS5SD_VERSION, sd_version(), sd_commit());
        status = 200;
    } else if (m == "GET" && p == "compat/sd-models") {
        out    = sd_models_json(g_library);
        status = 200;
    } else if (m == "GET" && p == "compat/models") {
        out    = openai_models_json(g_library);
        status = 200;
    } else if (m == "GET" && p == "compat/options") {
        out    = options_json(current_checkpoint());
        status = 200;
    } else if (m == "POST" && p == "compat/options") {
        // A1111 clients switch the model with {"sd_model_checkpoint": title};
        // the other options do not apply here and are ignored. A1111 answers
        // once the model has loaded; this answers at once, and GET options
        // names the new model from then on.
        const std::string checkpoint = checkpoint_from_options(std::string(body, body_len));
        const std::string id         = checkpoint.empty() ? std::string() : checkpoint_id(g_library, checkpoint);
        if (checkpoint.empty()) {
            out    = "{}";
            status = 200;
        } else if (id.empty()) {
            out    = error_json("no model " + checkpoint + "; GET /sdapi/v1/sd-models lists them", "model_not_found");
            status = 404;
        } else {
            status = ask_switch(id, out);
            if (status == 200 || status == 202) {
                out    = "{}";
                status = 200;
            }
        }
    } else if (m == "GET" && p == "settings") {
        std::lock_guard<std::mutex> lock(g_settings_mutex);
        out    = settings_json(g_settings);
        status = 200;
    } else if (m == "POST" && p == "settings") {
        std::lock_guard<std::mutex> lock(g_settings_mutex);
        AppSettings next = g_settings;
        std::string error;
        if (!apply_settings(body != nullptr ? std::string(body, body_len) : std::string(), next, error)) {
            out    = error_json(error);
            status = 400;
        } else if (!save_settings(next)) {
            out    = error_json(std::string("could not write ") + kSettings + ": " + std::strerror(errno));
            status = 500;
        } else {
            g_settings = next;
            g_status.set_tv_images(next.tv_images);
            g_console.set_fan(next.fan, next.fan_target);
            std::lock_guard<std::mutex> key_lock(g_key_mutex);
            show_api_key(g_api_key, next);
            out    = settings_json(next);
            status = 200;
        }
    } else if (m == "GET" && p == "auth") {
        std::lock_guard<std::mutex> lock(g_key_mutex);
        out    = auth_json(g_api_key);
        status = 200;
    } else if (m == "POST" && (p == "auth/enable" || p == "auth/disable")) {
        std::string error;
        if (set_api_key(p == "auth/enable", error)) {
            std::lock_guard<std::mutex> lock(g_key_mutex);
            out    = auth_json(g_api_key);
            status = 200;
        } else {
            out    = error_json(error);
            status = 500;
        }
    } else if (m == "POST" && p == "presets") {
        // {"name", "base", "defaults": {"steps": "10", ...}}: a variant of base
        // (the loaded model when base is left out).
        const nlohmann::json j = nlohmann::json::parse(body != nullptr ? std::string(body, body_len) : std::string(),
                                                       nullptr, false);
        std::vector<std::pair<std::string, std::string>> defaults;
        std::string                                      error, id;
        std::string base = j.is_object() && j.contains("base") && j["base"].is_string() ? j["base"].get<std::string>()
                                                                                       : loaded_id();
        if (j.is_object() && j.contains("defaults") && j["defaults"].is_object()) {
            for (const auto & kv : j["defaults"].items()) {
                defaults.emplace_back(kv.key(), kv.value().is_string() ? kv.value().get<std::string>() : kv.value().dump());
            }
        }
        if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
            out    = error_json("the body must be {\"name\": \"...\", \"base\": \"<model id>\", \"defaults\": {...}}");
            status = 400;
        } else if (base.empty()) {
            out    = error_json("no model is loaded; name the model as base");
            status = 400;
        } else if (!g_library.create_preset(j["name"].get<std::string>(), base, defaults, id, error)) {
            out    = error_json(error);
            status = error.rfind("no model", 0) == 0 ? 404 : 400;
        } else {
            std::printf("ps5sd: saved preset %s (base %s)\n", id.c_str(), base.c_str());
            const auto active = read_lines(kModel);
            nlohmann::json o  = nlohmann::json::parse(
                models_json(g_library, active.empty() ? "" : active[0], loaded_id(), g_switching, g_standby && !g_switching));
            o["id"] = id;
            out     = o.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            status  = 201;
        }
    } else if (p.rfind("presets/", 0) == 0 && (m == "GET" || m == "POST" || m == "DELETE")) {
        const std::string id = p.substr(8);
        std::string       error, text;
        const auto        active = read_lines(kModel);
        if (m == "GET") {
            if (g_library.read_preset(id, text, error)) {
                out    = nlohmann::json{ { "id", id }, { "text", text } }.dump(-1, ' ', false,
                                                                            nlohmann::json::error_handler_t::replace);
                status = 200;
            } else {
                out    = error_json(error);
                status = 404;
            }
        } else if (m == "POST") {
            // {"text": "..."}: the preset's file, as GET gives it. A change to
            // the loaded model's arguments counts from its next load.
            // {"defaults": {"steps": "10", ...}}: only the "# defaults:" line,
            // the rest of the file stays; the loaded preset's settings follow at once.
            const nlohmann::json j = nlohmann::json::parse(body != nullptr ? std::string(body, body_len) : std::string(),
                                                           nullptr, false);
            if (j.is_object() && !j.contains("text") && j.contains("defaults") && j["defaults"].is_object()) {
                std::vector<std::pair<std::string, std::string>> defaults;
                for (const auto & kv : j["defaults"].items()) {
                    defaults.emplace_back(kv.key(), kv.value().is_string() ? kv.value().get<std::string>() : kv.value().dump());
                }
                if (!g_library.update_defaults(id, defaults, error)) {
                    out    = error_json(error);
                    status = error.rfind("no preset", 0) == 0 ? 404 : 400;
                } else {
                    if (id == loaded_id()) {
                        ModelEntry  entry;
                        std::string ignored;
                        if (g_library.find(id, entry, ignored)) {
                            apply_preset_settings(&entry);
                        }
                    }
                    out    = models_json(g_library, active.empty() ? "" : active[0], loaded_id(), g_switching,
                                         g_standby && !g_switching);
                    status = 200;
                }
            } else if (!j.is_object() || !j.contains("text") || !j["text"].is_string()) {
                out    = error_json("the body must be {\"text\": \"<the preset file>\"} or {\"defaults\": {...}}");
                status = 400;
            } else if (!g_library.write_preset(id, j["text"].get<std::string>(), error)) {
                out    = error_json(error);
                status = error.rfind("no preset", 0) == 0 ? 404 : 400;
            } else {
                out    = models_json(g_library, active.empty() ? "" : active[0], loaded_id(), g_switching,
                                     g_standby && !g_switching);
                status = 200;
            }
        } else if ((!active.empty() && id == active[0]) || id == loaded_id()) {
            out    = error_json(id + " is in use; switch to another model or preset first", "in_use");
            status = 409;
        } else if (!g_library.remove_variant(id, error)) {
            out    = error_json(error);
            status = error.rfind("no preset", 0) == 0 ? 404 : 409;
        } else {
            std::printf("ps5sd: deleted preset %s\n", id.c_str());
            out    = models_json(g_library, active.empty() ? "" : active[0], loaded_id(), g_switching,
                                 g_standby && !g_switching);
            status = 200;
        }
    } else if (m == "POST" && p == "fit") {
        // An img_gen or vid_gen body: will it fit in memory (mem_fit.hpp)? Nothing runs.
        FitQuery    q;
        std::string error;
        if (!fit_query_from_body(body != nullptr ? std::string(body, body_len) : std::string(), q, error)) {
            out    = error_json(error);
            status = 400;
        } else {
            ModelEntry        entry;
            std::string       ignored;
            const std::string id = loaded_id();
            q.family             = !id.empty() && g_library.find(id, entry, ignored) ? model_family(entry) : "";
            q.largest_ok         = g_largest_ok;
            q.other_model_before = g_model_loads > 1;
            out                  = fit_json(q, check_fit(q));
            status               = 200;
        }
    } else if (m == "GET" && p == "loras") {
        out    = loras_json(kLoras);
        status = 200;
    } else if (m == "DELETE" && p.rfind("loras/", 0) == 0) {
        // One LoRA file (subfolders as sd-server lists them). A job that
        // uses it has read it already.
        const std::string path = p.substr(6);
        std::string       error;
        if (!valid_dest(path, error)) {
            out    = error_json("not a LoRA file: " + path);
            status = 400;
        } else if (::unlink((std::string(kLoras) + "/" + path).c_str()) == 0) {
            out    = loras_json(kLoras);
            status = 200;
        } else if (errno == ENOENT) {
            out    = error_json("no LoRA " + path);
            status = 404;
        } else {
            out    = error_json("could not delete " + path + ": " + std::strerror(errno));
            status = 500;
        }
    } else if (m == "GET" && p == "upscalers") {
        out    = upscalers_json(kUpscalers);
        status = 200;
    } else if (m == "DELETE" && p.rfind("upscalers/", 0) == 0) {
        // One upscaler file; a running hires fix or upscale has read it already.
        const std::string name = p.substr(10);
        std::string       kind, file, error;
        if (!upload_target("upscalers/" + name, kind, file, error)) {
            out    = error_json("not an upscaler file: " + name);
            status = 400;
        } else if (::unlink((std::string(kUpscalers) + "/" + name).c_str()) == 0) {
            out    = upscalers_json(kUpscalers);
            status = 200;
        } else if (errno == ENOENT) {
            out    = error_json("no upscaler " + name);
            status = 404;
        } else {
            out    = error_json("could not delete " + name + ": " + std::strerror(errno));
            status = 500;
        }
    } else if (m == "POST" && p == "gallery") {
        // An image the web UI made (an inpainting put back together in the
        // page): written to outputs like a generated one.
        std::string png, error;
        if (!png_from_json(body != nullptr ? std::string(body, body_len) : std::string(), png, error)) {
            out    = error_json(error);
            status = 400;
        } else {
            const std::string name = edited_image_name(kOutputs, static_cast<long long>(std::time(nullptr)));
            const std::string path = std::string(kOutputs) + "/" + name;
            std::ofstream     file(path, std::ios::binary | std::ios::trunc);
            file.write(png.data(), static_cast<std::streamsize>(png.size()));
            file.close();
            if (!file) {
                ::unlink(path.c_str());
                out    = error_json("could not write " + path);
                status = 500;
            } else {
                g_status.set_result(path);
                out    = nlohmann::json{ { "saved", name } }.dump();
                status = 201;
            }
        }
    } else if (m == "GET" && p == "tags") {
        out    = g_tags.info_json();
        status = 200;
    } else if (m == "POST" && p == "tags") {
        // {"q": "<what was typed>", "limit": n}: the tags that start with it, the most used first
        const nlohmann::json j = nlohmann::json::parse(body != nullptr ? std::string(body, body_len) : std::string(), nullptr, false);
        if (!j.is_object() || !j.contains("q") || !j["q"].is_string()) {
            out    = error_json("the body must be {\"q\": \"...\", \"limit\": 10}");
            status = 400;
        } else {
            const int limit = j.contains("limit") && j["limit"].is_number_integer() ? j["limit"].get<int>() : 10;
            out    = TagIndex::hits_json(g_tags.find(j["q"].get<std::string>(), static_cast<size_t>(std::max(1, std::min(50, limit)))));
            status = 200;
        }
    } else if (m == "GET" && p == "gallery/labels") {
        out    = g_labels.all_json();
        status = 200;
    } else if (m == "POST" && p == "gallery/labels") {
        const nlohmann::json j = nlohmann::json::parse(body != nullptr ? std::string(body, body_len) : std::string(), nullptr, false);
        std::vector<std::string> tags;
        bool                     ok = j.is_object() && j.contains("name") && j["name"].is_string() &&
                  (!j.contains("title") || j["title"].is_string()) && (!j.contains("tags") || j["tags"].is_array());
        if (ok && j.contains("tags")) {
            for (const auto & t : j["tags"]) {
                ok = ok && t.is_string();
                if (t.is_string()) {
                    tags.push_back(t.get<std::string>());
                }
            }
        }
        GalleryLabel label;
        std::string  error;
        if (!ok) {
            out    = error_json("the body must be {\"name\": \"<file>\", \"title\": \"...\", \"tags\": [\"...\"]}");
            status = 400;
        } else if (!gallery_name_ok(j["name"].get<std::string>())) {
            out    = error_json("not a gallery file: " + j["name"].get<std::string>());
            status = 400;
        } else if (!g_labels.set(j["name"].get<std::string>(), j.value("title", std::string()), tags, label, error)) {
            out    = error_json(error);
            status = error.rfind("no file", 0) == 0 ? 404 : 400;
        } else {
            out    = nlohmann::json{ { "name", j["name"] }, { "title", label.title }, { "tags", label.tags } }.dump(
                -1, ' ', false, nlohmann::json::error_handler_t::replace);
            status = 200;
        }
    } else if (m == "POST" && p == "gallery/keep") {
        // An image made with "temporary" (patches/sdcpp/0069) kept after all.
        const nlohmann::json j = nlohmann::json::parse(body != nullptr ? std::string(body, body_len) : std::string(), nullptr, false);
        std::string          kept, error;
        if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
            out    = error_json("the body must be {\"name\": \"tmp_<file>\"}");
            status = 400;
        } else if (!keep_temporary(kOutputs, j["name"].get<std::string>(), kept, error)) {
            out    = error_json(error);
            status = error.rfind("no file", 0) == 0 ? 404 : 409;
        } else {
            out    = nlohmann::json{ { "kept", kept } }.dump();
            status = 200;
        }
    } else if (m == "DELETE" && p.rfind("gallery/", 0) == 0) {
        // One file of the gallery (the PNGs and videos in outputs); the gallery lists
        // that folder on each request, so the file is all there is.
        const std::string name = p.substr(8);
        if (!gallery_name_ok(name)) {
            out    = error_json("not a gallery file: " + name);
            status = 400;
        } else if (::unlink((std::string(kOutputs) + "/" + name).c_str()) == 0) {
            // and what the server keeps for it: the thumbnail, a video's parameters
            ::unlink((std::string(kOutputs) + "/.thumbs/" + name + ".jpg").c_str());
            ::unlink((std::string(kOutputs) + "/.thumbs/" + name + ".txt").c_str());
            g_labels.remove(name);
            out    = "{\"deleted\": \"" + name + "\"}";
            status = 200;
        } else if (errno == ENOENT) {
            out    = error_json("no file " + name);
            status = 404;
        } else {
            out    = error_json("could not delete " + name + ": " + std::strerror(errno));
            status = 500;
        }
    } else if (m == "DELETE" && p.rfind("models/", 0) == 0) {
        // Never the model that is active or loaded, nor a file it uses (a
        // VAE that two presets share stays with the other one).
        const std::string id = p.substr(7);
        // In standby nothing is loaded, so model.txt's model may go too.
        const auto        active = g_standby ? std::vector<std::string>() : read_lines(kModel);
        const std::string loaded = loaded_id();
        std::vector<std::string> keep, deleted, kept;
        std::string              in_use_base;  // the base of the preset in use, if it is a variant
        for (const std::string & in_use : { active.empty() ? std::string() : active[0], loaded }) {
            ModelEntry  entry;
            std::string ignored;
            if (!in_use.empty() && g_library.find(in_use, entry, ignored)) {
                keep.insert(keep.end(), entry.files.begin(), entry.files.end());
                if (!entry.base.empty()) {
                    in_use_base = entry.base;
                }
            }
        }
        std::string error;
        if (g_switching || g_quit_requested) {
            out    = error_json("the server is switching the model or stopping", g_quit_requested ? "stopping" : "switching");
            status = 409;
        } else if ((!active.empty() && id == active[0]) || id == loaded) {
            out    = error_json(id + " is the model in use; switch to another one first", "in_use");
            status = 409;
        } else if (in_use_base == id) {
            out    = error_json("a preset of " + id + " is in use; switch to another model first", "in_use");
            status = 409;
        } else if (!g_library.remove(id, keep, deleted, kept, error)) {
            out    = error_json(error);
            status = error.rfind("no model", 0) == 0 || error.rfind("not a model", 0) == 0 ? 404 : 409;
        } else {
            std::printf("ps5sd: deleted model %s (%zu file(s), %zu kept)\n", id.c_str(), deleted.size(), kept.size());
            out    = removed_json(g_library, deleted, kept, active.empty() ? "" : active[0], loaded);
            status = 200;
        }
    } else {
        return 0;
    }
    *out_body = static_cast<char *>(std::malloc(out.size()));
    if (*out_body != nullptr) {
        std::memcpy(*out_body, out.data(), out.size());
        *out_len = out.size();
    }
    *content_type = "application/json";
    return status;
}

// The dashboard draws PNGs; a video (vid_gen) is in the gallery, not on the TV.
static bool is_png_path(const std::string & path) {
    if (path.size() <= 4) {
        return false;
    }
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png";
}

extern "C" void sd_activity_job_end(int ok, const char * output_path) {
    log_memory("after a request");
    if (ok && g_job_size > g_largest_ok) {
        g_largest_ok = g_job_size.load();
    }
    if (!ok) {
        // The server's reason for the failure ("Cancelled", out of memory, the
        // last error), when it knows one (patches/sdcpp/0065).
        std::string reason = output_path != nullptr ? output_path : "";
        std::printf("ps5sd: failed: %s\n", reason.empty() ? "reason not known" : reason.c_str());
        if (reason == "Cancelled") {
            set_phase("Ready", "cancelled");  // a stop is no error (AppStatus::classify)
            return;
        }
        const std::string prefix = "Generation failed: ";
        if (reason.compare(0, prefix.size(), prefix) == 0) {
            reason.erase(0, prefix.size());
        }
        set_phase("Generation failed", reason.empty() ? "See /data/PS5SD/app.log" : reason);
        return;
    }
    // One path a line: every image of a batch, in order (patches/sdcpp/0059).
    std::string        first;
    std::istringstream paths(output_path);
    for (std::string path; std::getline(paths, path);) {
        if (!path.empty()) {
            if (is_png_path(path)) {
                g_status.set_result(path);
            }
            if (first.empty()) {
                first = path;
            }
        }
    }
    // A stop or a switch asked for during the job goes on once it ends; not ready then.
    if (g_quit_requested || g_switching) {
        return;
    }
    set_phase("Ready", first.empty() ? "image sent" : std::string("saved ") + first);
}

int main(int, char **) {
    mkdir(kDir, 0777);
    for (const char * dir : { kModels, kLoras, kEmbeddings, kPresets, kUpscalers, kTags }) {
        mkdir(dir, 0777);  // uploads, downloads and sd-server's lists expect them
    }
    // stdout and stderr to a file the PC can fetch over FTP. Both append: with
    // "w", stdout kept its own offset and wrote over what stderr had appended
    // (the start of every error line). The file is emptied first instead.
    if (std::FILE * log = std::fopen(kLog, "w")) {
        std::fclose(log);
    }
    if (std::freopen(kLog, "a", stdout) != nullptr) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    if (std::freopen(kLog, "a", stderr) != nullptr) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    std::printf("ps5sd: version " PS5SD_VERSION ", stable-diffusion.cpp %s (%s)\n", sd_version(), sd_commit());
    std::set_terminate(on_terminate);
    load_settings();
    load_api_key();
    // The images made with "Don't keep on the console" last until the title starts again.
    if (const int dropped = drop_temporary(kOutputs); dropped > 0) {
        std::printf("ps5sd: deleted %d image(s) that were not to be kept\n", dropped);
    }
    for (const std::string & id : update_catalog_presets(kPresets)) {
        std::printf("ps5sd: preset %s updated to the catalog's current settings\n", id.c_str());
    }
    g_console.start(g_status);
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
    Dashboard          view;
    const bool         ui_ok = renderer.init() && view.load(renderer, kFont);
    std::printf("ps5sd: screen %s\n", ui_ok ? "ready" : "failed");
    // The model and the port the server will use, for the screen before the
    // server has started (the same files server_args() reads), and the
    // console's address (watch_address() keeps it current).
    {
        const ServerConfig config = read_server_config(kModel, kServerArgs, kPort);
        g_status.set_model(config.model);
        g_status.set_port(config.port);
    }
    watch_address();
    auto last_frame = std::chrono::steady_clock::now();
    const auto render = [&] {
        const auto now = std::chrono::steady_clock::now();
        view.update(g_status.snapshot(), std::chrono::duration<float>(now - last_frame).count());
        last_frame = now;
        view.compose(renderer, PS5SD_VERSION);
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

    // The server, or one image with PS5SD_ONESHOT=1 (env.txt). The worker is
    // never joined or detached: the platform's pthread wraps (radv-link.sh)
    // own its stack, and the title never ends on its own.
    const char *          oneshot = std::getenv("PS5SD_ONESHOT");
    const bool            once    = oneshot != nullptr && std::string(oneshot) == "1";
    std::function<void()> work    = once ? generate_once : run_server;
    g_worker_busy                 = !once;  // before the first quit check
    if (!once) {
        // The model downloader, on its own threads, apart from the server:
        // the queue in downloads.json carries on.
        DownloaderConfig dl;
        dl.models_dir         = kModels;
        dl.loras_dir          = kLoras;
        dl.upscalers_dir      = kUpscalers;
        dl.presets_dir        = kPresets;
        dl.queue_file         = std::string(kDir) + "/downloads.json";
        dl.hf_token_file      = std::string(kDir) + "/hf-token.txt";
        dl.civitai_token_file = std::string(kDir) + "/civitai-token.txt";
        // For measuring (env.txt): PS5SD_DL_CONNECTIONS (16), PS5SD_DL_PIECE_MIB (8).
        if (const char * v = std::getenv("PS5SD_DL_CONNECTIONS"); v != nullptr && std::atoi(v) > 0) {
            dl.connections = std::atoi(v);
        }
        if (const char * v = std::getenv("PS5SD_DL_PIECE_MIB"); v != nullptr && std::atoi(v) > 0) {
            dl.piece_mib = static_cast<unsigned>(std::atoi(v));
        }
        downloader_start(dl);
    }
    start_thread(work);
    watch_quit_file();

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
            watch_address();
            quit_if_asked();
        }
    }
#else
    // The quit request until the system closes the title: a title may not
    // return from main or exit.
    for (;;) {
        sceKernelUsleep(500 * 1000);
        watch_address();
        quit_if_asked();
    }
#endif
}
