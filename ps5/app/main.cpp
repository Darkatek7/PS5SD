// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
//
// PS5SD native app: stable-diffusion.cpp on the PS5's GPU through RADV
// (ggml-vulkan), with its state on the TV drawn by ps5-homebrew-ui through
// ps5-opengl.
//
// A title gets the console's direct memory (12 GiB) where a payload gets none,
// and RADV turns it into Vulkan device memory, so the whole model sits on the
// GPU. The app runs stable-diffusion.cpp's HTTP server (sd_server() from
// sd-server-lib, patches/sdcpp/0050-0053) on port 7860: the OpenAI and A1111
// image APIs, sd.cpp's own, the web UI (ps5/webui) at / and the gallery of
// /data/PS5SD/outputs. The model's path comes from /data/PS5SD/model.txt,
// more of sd-server's arguments from /data/PS5SD/server-args.txt (one a
// line). The server runs on its own thread with a 16 MiB stack; the main
// thread draws the screen and waits for /data/PS5SD/quit, which stops the
// server before the title ends.
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
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "app_status.hpp"
#include "gen_args.hpp"
#include "ggml-backend.h"
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
constexpr const char * kOutputs = "/data/PS5SD/outputs";
constexpr const char * kLoras   = "/data/PS5SD/loras";
constexpr const char * kEmbeddings = "/data/PS5SD/embeddings";
#ifndef PS5SD_NO_DISPLAY
constexpr const char * kFont    = "/app0/assets/fonts/dejavu-sans-mono.huifont";
#endif
constexpr double       kGiB     = 1073741824.0;
constexpr int          kPort    = 7860;  // sd-server's listen port unless server-args.txt names one
constexpr int          kStopWaitSeconds = 120;  // how long a quit waits for the server to stop

AppStatus g_status;

// The server's life, for the quit request and the "ready" announcement:
// started just before sd_server() is called, done once it has returned (its
// HTTP threads and its job worker have ended, the model is freed).
std::atomic<bool> g_server_started{false};
std::atomic<bool> g_server_done{false};
std::atomic<int>  g_server_port{kPort};
std::atomic<bool> g_quit_requested{false};

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
void quit_if_asked() {
    using clock = std::chrono::steady_clock;
    static clock::time_point asked;
    if (!g_quit_requested) {
        if (access(kQuit, F_OK) != 0) {
            return;
        }
        g_quit_requested = true;
        asked            = clock::now();
        std::printf("ps5sd: quit requested\n");
        if (g_server_started && !g_server_done) {
            set_phase("Stopping the server");
            sd_server_terminate();
        }
    }
    if (g_server_started && !g_server_done) {
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

// sd-server's command line: the app's defaults, then server-args.txt (one
// argument a line). sd-server appends to a string option given twice
// (--backend Vulkan --backend CPU would be "Vulkan,CPU"), so a default is
// only added when the file does not name that option itself.
std::vector<std::string> server_args(std::string & error) {
    const std::vector<std::string> extra = read_lines(kServerArgs);
    const auto names = [&](std::initializer_list<const char *> options) {
        for (const auto & a : extra) {
            for (const char * o : options) {
                if (a == o) {
                    return true;
                }
            }
        }
        return false;
    };
    std::vector<std::string> args = { "sd-server" };
    if (!names({ "-m", "--model", "--diffusion-model" })) {
        const auto model = read_lines(kModel);
        if (model.empty()) {
            error = "no model: put its path in /data/PS5SD/model.txt";
            return {};
        }
        args.insert(args.end(), { "-m", model[0] });
    }
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
    return args;
}

// The server, on the worker thread, until a quit request stops it.
void run_server() {
    log_devices();
    log_memory("before loading");
    std::string              error;
    std::vector<std::string> args = server_args(error);
    if (args.empty()) {
        std::printf("ps5sd: %s\n", error.c_str());
        set_phase("Nothing to do", error);
        notify_outcome(("PS5SD: " + error).c_str());
        return;
    }
    std::printf("ps5sd: sd-server");
    std::vector<char *> argv;
    for (auto & a : args) {
        argv.push_back(a.data());
        if (&a != &args.front()) {
            std::printf(" %s", a.c_str());
        }
    }
    std::printf("\n");
    argv.push_back(nullptr);

    set_phase("Loading the model", "then the API on port " + std::to_string(g_server_port.load()));
    // Done however sd_server() ends, an exception included (start_thread
    // reports it), so a quit request does not wait out its time for nothing.
    struct MarkDone {
        ~MarkDone() { g_server_done = true; }
    } mark_done;
    g_server_started = true;
    const int rc     = sd_server((int) args.size(), argv.data());
    std::printf("ps5sd: sd_server returned %d\n", rc);
    log_memory("after the server");
    if (g_quit_requested) {
        set_phase("Server stopped");
    } else if (rc != 0) {
        // The model did not load, the port could not be bound, or an argument
        // was wrong (the usage is in the log).
        set_phase("The server did not start", "See /data/PS5SD/app.log");
        notify_outcome("PS5SD: the server did not start, see /data/PS5SD/app.log");
    } else {
        set_phase("The server stopped", "See /data/PS5SD/app.log");
        notify_outcome("PS5SD: the server stopped, see /data/PS5SD/app.log");
    }
}

// Once the server accepts connections, says so: the phase, and a
// notification. Checked from the main loop, by connecting to it on loopback.
void announce_when_listening() {
    static bool announced = false;
    if (announced || !g_server_started || g_server_done || g_quit_requested) {
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
    announced = true;
    log_memory("after loading");
    const std::string port = std::to_string(g_server_port.load());
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

// TODO(dashboard): show the image being denoised.
extern "C" void sd_activity_preview(const uint8_t *, int, int) {}

extern "C" void sd_activity_job_end(int ok, const char * output_path) {
    log_memory("after a request");
    if (!ok) {
        set_phase("Generation failed", "See /data/PS5SD/app.log");
        return;
    }
    if (*output_path != '\0') {
        g_status.set_result(output_path);
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

    // The server, or one image with PS5SD_ONESHOT=1 (env.txt). The worker is
    // never joined or detached: the platform's pthread wraps (radv-link.sh)
    // own its stack, and the title never ends on its own.
    const char *          oneshot = std::getenv("PS5SD_ONESHOT");
    std::function<void()> work    = oneshot != nullptr && std::string(oneshot) == "1" ? generate_once : run_server;
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
            announce_when_listening();
            quit_if_asked();
        }
    }
#else
    // The quit request until the system closes the title: a title may not
    // return from main or exit.
    for (;;) {
        sceKernelUsleep(500 * 1000);
        announce_when_listening();
        quit_if_asked();
    }
#endif
}
