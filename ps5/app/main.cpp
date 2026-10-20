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
// which runs on threads of its own. The server runs on its own thread with a
// 16 MiB stack and is started again for a model switch; the main thread
// draws the screen and waits for /data/PS5SD/quit, which stops the server
// before the title ends.
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

#include <atomic>
#include <chrono>
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
#include "gen_args.hpp"
#include "ggml-backend.h"
#include "download_core.hpp"
#include "downloader.hpp"
#include "upload.hpp"
#include "models.hpp"
#include "server_hooks.h"
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
// sd-server-lib (patches/sdcpp/0050): serves until sd_server_terminate().
int  sd_server(int argc, char ** argv);
void sd_server_terminate(void);
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
constexpr const char * kDocs    = "/app0/docs";  // the Help tab's docs (scripts/build-app.sh)
constexpr const char * kOutputs = "/data/PS5SD/outputs";
constexpr const char * kLoras   = "/data/PS5SD/loras";
constexpr const char * kEmbeddings = "/data/PS5SD/embeddings";
constexpr const char * kModels  = "/data/PS5SD/models";
constexpr const char * kPresets = "/data/PS5SD/presets";
#ifndef PS5SD_NO_DISPLAY
constexpr const char * kFont    = "/app0/assets/fonts/dejavu-sans-mono.huifont";
#endif
constexpr double       kGiB     = 1073741824.0;
constexpr int          kPort    = 7860;  // sd-server's listen port unless server-args.txt names one
constexpr int          kStopWaitSeconds = 120;  // how long a quit waits for the server to stop

AppStatus g_status;

const ModelLibrary g_library{ kModels, kPresets };

// The server's life, for the quit request and the "ready" announcement:
// started just before sd_server() is called, done once it has returned (its
// HTTP threads and its job worker have ended, the model is freed). Both are
// set again for every start; the worker is busy from the first start to the
// last stop, model switches between them included.
std::atomic<bool> g_server_started{false};
std::atomic<bool> g_server_done{false};
std::atomic<bool> g_worker_busy{false};
std::atomic<int>  g_server_port{kPort};
std::atomic<int>  g_generation{0};  // counts the server's starts
std::atomic<bool> g_quit_requested{false};
std::atomic<bool> g_quit_file{false};  // kQuit seen by watch_quit_file()

// A model switch: asked for by POST /ps5sd/v1/app/models/active (pending),
// taken up by the main loop, which stops the server (restart), and over once
// the new model is ready or has failed (switching).
std::atomic<bool> g_switch_pending{false};
std::atomic<bool> g_restart{false};
std::atomic<bool> g_switching{false};
// The server runs without a model (sd-server --standby, patches/sdcpp/0056).
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
// without model.txt, server-args.txt must name the model. sd-server appends
// to a string option given twice (--backend Vulkan --backend CPU would be
// "Vulkan,CPU"), so a default is only added when no argument names that
// option itself.
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
    if (!names({ "--embd-dir" })) {
        args.insert(args.end(), { "--embd-dir", kEmbeddings });
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
// address and port as server-args.txt gives them, if it does.
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
    plan.args = { "sd-server", "--standby", "--listen-ip", ip, "--listen-port", port, "--output-dir", kOutputs };
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

// The server, on the worker thread, until a quit request stops it; started
// again with the new model after a switch, and with the last model that
// worked when a switched-to one does not load.
void run_server() {
    struct NotBusy {
        ~NotBusy() { g_worker_busy = false; }
    } not_busy;
    log_devices();
    log_memory("before loading");
    bool force_standby = false;  // a model failed to load with none to go back to
    for (;;) {
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
        g_standby     = standby;
        std::printf("ps5sd: sd-server");
        std::vector<char *> argv;
        for (auto & a : plan.args) {
            argv.push_back(a.data());
            if (&a != &plan.args.front()) {
                std::printf(" %s", a.c_str());
            }
        }
        std::printf("\n");
        argv.push_back(nullptr);

        g_status.set_model(plan.name);
        g_status.set_port(g_server_port.load());
        if (standby) {
            set_phase("Starting without a model", "the web UI on port " + std::to_string(g_server_port.load()));
        } else {
            set_phase("Loading the model", plan.name + ", then the API on port " + std::to_string(g_server_port.load()));
        }
        int rc = 0;
        {
            // Done however sd_server() ends, an exception included
            // (start_thread reports it), so a quit request does not wait out
            // its time for nothing.
            struct MarkDone {
                ~MarkDone() {
                    g_server_done = true;
                    std::lock_guard<std::mutex> lock(g_model_mutex);
                    g_loaded_id.clear();
                }
            } mark_done;
            g_server_done    = false;
            g_server_started = true;
            if (g_quit_requested) {
                return;  // the quit came between two starts
            }
            ++g_generation;
            rc = sd_server((int) plan.args.size(), argv.data());
        }
        std::printf("ps5sd: sd_server returned %d\n", rc);
        log_memory("after the server");
        if (g_quit_requested) {
            set_phase("Server stopped");
            return;
        }
        if (g_restart.exchange(false)) {
            std::printf("ps5sd: switching the model\n");
            continue;
        }
        std::string good;
        {
            std::lock_guard<std::mutex> lock(g_model_mutex);
            good = g_good_id;
        }
        if (rc != 0 && !good.empty() && good != plan.id && write_active_model(good)) {
            // A switch to a model that does not load (not enough memory, a
            // broken file): back to the one that worked, so the web UI stays.
            std::printf("ps5sd: %s did not load, back to %s\n", plan.id.c_str(), good.c_str());
            notify_outcome(("PS5SD: " + plan.name + " did not load, back to the last model").c_str());
            continue;
        }
        if (rc != 0 && !standby) {
            // Nothing that worked to go back to: the web UI stays up anyway.
            std::printf("ps5sd: %s did not load\n", plan.id.c_str());
            notify_outcome(("PS5SD: " + plan.name + " did not load, see /data/PS5SD/app.log").c_str());
            force_standby = true;
            continue;
        }
        g_switching = false;
        if (rc != 0) {
            // The model did not load, the port could not be bound, or an
            // argument was wrong (the usage is in the log).
            set_phase("The server did not start", "See /data/PS5SD/app.log");
            notify_outcome("PS5SD: the server did not start, see /data/PS5SD/app.log");
        } else {
            set_phase("The server stopped", "See /data/PS5SD/app.log");
            notify_outcome("PS5SD: the server stopped, see /data/PS5SD/app.log");
        }
        return;
    }
}

// A switch the web UI asked for: the main loop stops the server, which the
// worker then starts again with the model model.txt now names.
void switch_if_asked() {
    if (!g_switch_pending.exchange(false) || g_quit_requested) {
        return;
    }
    g_restart = true;
    if (g_server_started && !g_server_done) {
        set_phase("Switching the model");
        sd_server_terminate();
    }
}

// Once the server accepts connections, says so: the phase, and a
// notification. Checked from the main loop, by connecting to it on loopback.
void announce_when_listening() {
    static int announced  = 0;  // the server start announced last
    const int  generation = g_generation.load();
    if (announced == generation || !g_server_started || g_server_done || g_quit_requested || g_restart) {
        return;
    }
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return;
    }
    sockaddr_in addr     = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t) g_server_port.load());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool up        = connect(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
    close(s);
    if (!up) {
        return;
    }
    announced = generation;
    g_switching = false;
    const std::string port = std::to_string(g_server_port.load());
    if (g_standby) {
        std::printf("ps5sd: listening on port %s, without a model\n", port.c_str());
        set_phase("No model loaded", "Web UI on port " + port + ": download or pick one under Models");
        notify_outcome(("PS5SD: no model loaded; web UI on port " + port + ", tab Models").c_str());
        return;
    }
    {
        // The id of the model this start loaded: model.txt has not changed
        // since, as a switch is only asked for through this server.
        const auto  active = read_lines(kModel);
        ModelEntry  entry;
        std::string ignored;
        std::lock_guard<std::mutex> lock(g_model_mutex);
        g_loaded_id = !active.empty() && g_library.find(active[0], entry, ignored) ? entry.id : std::string();
        g_good_id   = g_loaded_id;
    }
    log_memory("after loading");
    std::printf("ps5sd: listening on port %s\n", port.c_str());
    set_phase("Ready", "API and web UI on port " + port);
    notify_outcome(("PS5SD: ready, API and web UI on port " + port).c_str());
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
        out = error_json(error);
        return 404;
    }
    if (g_switching || g_quit_requested) {
        out = error_json("the server is switching the model or stopping");
        return 409;
    }
    if (entry.id == loaded_id()) {
        out = models_json(g_library, entry.id, entry.id, false);
        return 200;
    }
    if (!write_active_model(entry.id)) {
        out = error_json("could not write /data/PS5SD/model.txt");
        return 500;
    }
    std::printf("ps5sd: switch to %s asked for\n", entry.id.c_str());
    g_switching      = true;
    g_switch_pending = true;
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
        out    = error_json("the app is stopping");
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
        status = upload_file(UploadDirs{ kModels, kLoras, kEmbeddings, kPresets }, p.substr(8), length, reader, out);
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
            out    = error_json("no model " + checkpoint + "; GET /sdapi/v1/sd-models lists them");
            status = 404;
        } else {
            status = ask_switch(id, out);
            if (status == 200 || status == 202) {
                out    = "{}";
                status = 200;
            }
        }
    } else if (m == "DELETE" && p.rfind("gallery/", 0) == 0) {
        // One image of the gallery (the PNGs in outputs); the gallery lists
        // that folder on each request, so the file is all there is.
        const std::string name = p.substr(8);
        if (!gallery_name_ok(name)) {
            out    = error_json("not a gallery image: " + name);
            status = 400;
        } else if (::unlink((std::string(kOutputs) + "/" + name).c_str()) == 0) {
            out    = "{\"deleted\": \"" + name + "\"}";
            status = 200;
        } else if (errno == ENOENT) {
            out    = error_json("no image " + name);
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
        for (const std::string & in_use : { active.empty() ? std::string() : active[0], loaded }) {
            ModelEntry  entry;
            std::string ignored;
            if (!in_use.empty() && g_library.find(in_use, entry, ignored)) {
                keep.insert(keep.end(), entry.files.begin(), entry.files.end());
            }
        }
        std::string error;
        if (g_switching || g_quit_requested) {
            out    = error_json("the server is switching the model or stopping");
            status = 409;
        } else if ((!active.empty() && id == active[0]) || id == loaded) {
            out    = error_json(id + " is the model in use; switch to another one first");
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

extern "C" void sd_activity_job_end(int ok, const char * output_path) {
    log_memory("after a request");
    if (!ok) {
        set_phase("Generation failed", "See /data/PS5SD/app.log");
        return;
    }
    if (*output_path != '\0') {
        g_status.set_result(output_path);
    }
    // A stop asked for during the job goes on once it ends; not ready then.
    if (g_quit_requested || g_restart) {
        return;
    }
    set_phase("Ready", *output_path != '\0' ? std::string("saved ") + output_path : "image sent");
}

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
    Dashboard          view;
    const bool         ui_ok = renderer.init() && view.load(renderer, kFont);
    std::printf("ps5sd: screen %s\n", ui_ok ? "ready" : "failed");
    // The model and the port the server will use, for the screen before the
    // server has started (the same files server_args() reads). The console's
    // own address is not known yet: g_status.set_address("a.b.c.d") fills the
    // dashboard's "http://<address>:<port>" once something finds it.
    {
        const ServerConfig config = read_server_config(kModel, kServerArgs, kPort);
        g_status.set_model(config.model);
        g_status.set_port(config.port);
    }
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
        // The model downloader, on its own threads, apart from the server
        // (which is stopped and started again for a model switch): the queue
        // in downloads.json carries on.
        DownloaderConfig dl;
        dl.models_dir         = kModels;
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
            announce_when_listening();
            switch_if_asked();
            quit_if_asked();
        }
    }
#else
    // The quit request until the system closes the title: a title may not
    // return from main or exit.
    for (;;) {
        sceKernelUsleep(500 * 1000);
        announce_when_listening();
        switch_if_asked();
        quit_if_asked();
    }
#endif
}
