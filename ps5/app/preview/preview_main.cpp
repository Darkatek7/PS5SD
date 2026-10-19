// The dashboard on the PC: renders a few states of the screen to PNG files
// through Mesa's surfaceless EGL (llvmpipe), with the same calls main.cpp makes
// (AppStatus setters, Dashboard::update, Dashboard::compose). Built and run by
// scripts/preview-app-ui.sh.
//
//   preview_app <font dir> <output dir> <work dir> [result png] [width height]
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_status.hpp"
#include "dashboard.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "image_file.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

bool open_context() {
    auto get_platform_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = get_platform_display != nullptr
                             ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API)) {
        return false;
    }
    const EGLint attributes[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                  EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

float clamp01(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

// A picture made of a sky, a sun and two hills; the seed moves the hues. Stands
// in for a generated image where no real one is at hand.
ps5sd::Rgba fake_picture(int seed, int w, int h) {
    ps5sd::Rgba img;
    img.width  = w;
    img.height = h;
    img.pixels.resize((size_t) w * h * 4);
    const float hue = 0.15f * (float) seed;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const float u = (float) x / (float) w, v = (float) y / (float) h;
            float r = 0.15f + 0.6f * v + 0.2f * std::sin(hue * 6.0f + u * 2.0f);
            float g = 0.1f + 0.35f * (1.0f - v) + 0.2f * std::sin(hue * 4.0f + 1.0f);
            float b = 0.4f + 0.45f * (1.0f - v) + 0.2f * std::cos(hue * 5.0f + u);
            const float dx = u - (0.3f + 0.1f * (float) (seed % 4)), dy = v - 0.3f;
            if (dx * dx + dy * dy < 0.012f) {
                r = 1.0f;
                g = 0.9f;
                b = 0.6f;
            }
            const float hill1 = 0.68f + 0.07f * std::sin(u * 7.0f + (float) seed);
            const float hill2 = 0.8f + 0.05f * std::sin(u * 11.0f + (float) seed * 2.0f);
            if (v > hill1) {
                r *= 0.45f;
                g *= 0.55f;
                b *= 0.6f;
            }
            if (v > hill2) {
                r *= 0.4f;
                g *= 0.5f;
                b *= 0.55f;
            }
            uint8_t * p = &img.pixels[((size_t) y * w + x) * 4];
            p[0]        = (uint8_t) (clamp01(r) * 255);
            p[1]        = (uint8_t) (clamp01(g) * 255);
            p[2]        = (uint8_t) (clamp01(b) * 255);
            p[3]        = 255;
        }
    }
    return img;
}

bool save_png(const std::string & path, const ps5sd::Rgba & img) {
    return stbi_write_png(path.c_str(), img.width, img.height, 4, img.pixels.data(), img.width * 4) != 0;
}

// What the denoiser would have at `progress` (0..1): the picture under noise.
std::vector<uint8_t> noisy_rgb(const ps5sd::Rgba & img, float progress) {
    std::vector<uint8_t> rgb((size_t) img.width * img.height * 3);
    uint32_t             seed = 12345;
    for (size_t i = 0; i < (size_t) img.width * img.height; i++) {
        seed             = seed * 1664525u + 1013904223u;
        const float n    = (float) ((seed >> 8) & 0xffff) / 65535.0f;
        const float keep = progress * progress;  // the picture comes out of the noise late
        for (int c = 0; c < 3; c++) {
            const float v  = (float) img.pixels[i * 4 + c] / 255.0f;
            rgb[i * 3 + c] = (uint8_t) (clamp01(v * keep + (0.5f + 0.5f * (n - 0.5f)) * (1.0f - keep) * 0.9f) * 255);
        }
    }
    return rgb;
}

}  // namespace

int main(int argc, char ** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <font dir> <output dir> <work dir> [result png] [width height]\n", argv[0]);
        return 2;
    }
    const std::string fonts  = argv[1];
    const std::string output = argv[2];
    const std::string work   = argv[3];
    const std::string result = argc > 4 ? argv[4] : "";
    const int         width  = argc > 6 ? std::atoi(argv[5]) : 1920;
    const int         height = argc > 6 ? std::atoi(argv[6]) : 1080;
    mkdir(output.c_str(), 0777);
    mkdir(work.c_str(), 0777);

    if (!open_context()) {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    hui::gfx::set_glsl_prefix("#version 450 core\n");
    hui::gfx::Renderer renderer;
    ps5sd::Dashboard   dashboard;
    if (!renderer.init() || !dashboard.load(renderer, fonts + "/dejavu-sans-mono.huifont")) {
        std::fprintf(stderr, "the dashboard did not load\n");
        return 1;
    }

    GLuint framebuffer = 0, color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return 1;
    }

    // The pictures a session has made: the real one if given, then made-up ones
    // (one of them a portrait), written where the dashboard reads them from.
    std::vector<std::string> paths;
    ps5sd::Rgba              first;
    if (!result.empty() && ps5sd::load_image_rgba(result, 4096, first)) {
        paths.push_back(result);
    } else {
        first = fake_picture(0, 512, 512);
        paths.push_back(work + "/result-0.png");
        save_png(paths.back(), first);
    }
    for (int i = 1; i <= 6; i++) {
        const bool portrait = i == 3;
        paths.push_back(work + "/result-" + std::to_string(i) + ".png");
        save_png(paths.back(), fake_picture(i, portrait ? 384 : 512, 512));
    }

    ps5sd::AppStatus           status;
    std::vector<unsigned char> pixels((size_t) width * height * 4);
    const auto                 frames = [&](int n) {
        for (int i = 0; i < n; i++) {
            dashboard.update(status.snapshot(), 1.0f / 60.0f);
        }
    };
    // job_s and the last job's time come from the clock; the PC runs the whole
    // script in a blink, so they are filled in for the picture.
    const auto capture = [&](const std::string & name, double job_s, double last_job_s) {
        ps5sd::AppStatus::Snapshot s = status.snapshot();
        s.job_s                      = job_s;
        s.last_job_s                 = last_job_s >= 0 ? last_job_s : s.last_job_s;
        dashboard.update(s, 1.0f / 60.0f);
        dashboard.compose(renderer, "0.1.0");
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255;  // opaque whatever the blending left
        }
        const std::string path = output + "/" + name + ".png";
        stbi_flip_vertically_on_write(1);
        const bool ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
        std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n", path.c_str(),
                     renderer.last_instances(), renderer.last_draw_calls(), glGetError());
        return ok;
    };
    bool ok = true;

    // The log, as sd.cpp writes it.
    const char * const log[] = {
        "[INFO ] stable-diffusion.cpp:2140 - Version: master-975-95b3090",
        "[INFO ] stable-diffusion.cpp:2142 - System Info: Vulkan: AMD Radeon Graphics (RADV)",
        "[INFO ] model.cpp:1050 - loading tensors from /data/PS5SD/models/sdxs-512-0.9_Q8_0.gguf",
        "[INFO ] stable-diffusion.cpp:812 - total params memory size = 1331.23MB (VRAM 1331.23MB, RAM 0.00MB)",
        "[INFO ] stable-diffusion.cpp:821 - loading model completed, taking 3.81s",
        "ps5sd: listening on port 7860",
    };

    // 1. Loading the model: the server is not up yet, the console's address unknown.
    status.set_port(7860);
    status.set_model("sdxs-512-0.9_Q8_0");
    status.set_memory(11.1, 11.44);
    status.set_phase("Opening the GPU");
    for (int i = 0; i < 3; i++) {
        status.log(log[i]);
    }
    status.set_phase("Loading the model", "then the API on port 7860");
    status.set_progress(640, 1200, 0.0f);
    status.set_memory(9.9, 11.44);
    frames(100);
    ok = capture("01-loading", 0, -1) && ok;

    // 2. Ready, nothing generated yet; the address is known.
    status.set_address("192.168.1.50");
    status.set_memory(9.6, 11.44);
    status.log(log[3]);
    status.log(log[4]);
    status.log(log[5]);
    status.set_phase("Ready", "API and web UI on port 7860");
    frames(120);
    ok = capture("02-ready", 0, -1) && ok;

    // 3. A job with previews, at 40%.
    const char * prompt = "a photograph of an astronaut riding a horse above the Earth, golden hour, dramatic clouds, "
                          "35mm film grain, highly detailed";
    status.set_progress(0, 20, 0.0f);  // sd_activity_job_begin
    status.set_phase("Generating", prompt);
    status.log("[INFO ] stable-diffusion.cpp:3401 - generating image: 1/1 - seed 42");
    status.log("[INFO ] stable-diffusion.cpp:3447 - sampling using Euler A method");
    status.set_progress(8, 20, 1.34f);
    status.set_memory(8.7, 11.44);
    const auto frame = noisy_rgb(first, 0.4f);
    status.set_preview(frame.data(), first.width, first.height);  // sd_activity_preview
    frames(80);
    ok = capture("03-generating-preview", 11.2, -1) && ok;

    // 4. Done: the picture, the first of the session.
    status.set_progress(20, 20, 1.34f);
    status.set_result(paths[0]);  // sd_activity_job_end
    status.set_phase("Ready", "saved " + paths[0]);
    status.log("[INFO ] stable-diffusion.cpp:3560 - generate_image completed in 27.1s");
    frames(150);
    ok = capture("04-done", 0, 27.1) && ok;

    // 5. Later, with a request that asked for no preview: the old picture steps back.
    for (int i = 6; i >= 1; i--) {
        status.set_phase("Generating", i == 1 ? "a lighthouse on a cliff in a storm, oil painting" : prompt);
        status.set_phase("Ready", "saved");
        status.set_result(paths[(size_t) i]);
        frames(8);
    }
    status.set_progress(0, 30, 0.0f);
    status.set_phase("Generating", "a lighthouse on a cliff in a storm, oil painting, thick brush strokes");
    status.set_progress(21, 30, 0.97f);
    frames(60);
    ok = capture("05-generating-no-preview", 20.4, -1) && ok;

    // 6. Ready with a full strip of recent images.
    status.set_result(paths[0]);
    status.set_phase("Ready", "saved");
    frames(200);
    ok = capture("06-ready-recent", 0, 18.6) && ok;

    // 7. An error.
    status.set_phase("Generation failed", "Out of device memory while sampling. See /data/PS5SD/app.log");
    status.log("[ERROR] ggml_vulkan.cpp:5120 - vk::Device::allocateMemory: ErrorOutOfDeviceMemory");
    frames(60);
    ok = capture("07-error", 0, -1) && ok;

    // 8. The log, in place of the recent images.
    status.set_phase("Ready", "API and web UI on port 7860");
    dashboard.toggle_log();
    frames(30);
    ok = capture("08-log", 0, 18.6) && ok;
    return ok ? 0 : 1;
}
