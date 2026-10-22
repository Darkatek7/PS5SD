// What the app is doing, for the screen (dashboard.hpp) or, in a build
// without one (PS5SD_NO_DISPLAY), for the log and the notifications.
//
// Written by the generation thread and the log callback, read by the main
// thread once a frame. No ps5-homebrew-ui or ps5-opengl in here, so the
// no-display build compiles it on its own.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ps5sd {

class AppStatus {
  public:
    // What the dashboard calls the app's state. set_phase() works it out from
    // the phase text main.cpp reports (classify()), so main.cpp needs no
    // second vocabulary.
    enum class State {
        starting,    // opening the GPU
        loading,     // loading the model
        ready,       // waiting for a request, or the last one is done
        generating,  // a job is running
        error,       // the model did not load, the server did not start, a job failed
        stopped,     // the server has been stopped
    };

    // An RGB frame (w * h * 3 bytes, row after row): a preview of the image
    // being denoised. Immutable once published, so the screen holds it by
    // pointer and the snapshot copies a pointer, not megabytes.
    struct Frame {
        int                  width  = 0;
        int                  height = 0;
        std::vector<uint8_t> rgb;
    };

    // A finished image: where it was saved and the prompt that made it.
    struct Result {
        std::string path;
        std::string prompt;
    };

    // The console's readings for the screen (console_stats.hpp), NaN for what
    // is not read.
    struct Console {
        double soc_c = NAN;  // the SoC's hottest sensor, degrees C
        double watts = NAN;  // the SoC's power
        double fan   = NAN;  // the fan's duty, 0..1
        double cpu   = NAN;  // PS5SD's share of the CPUs, 0..1
        double job   = NAN;  // the share of the last second a job ran, 0..1
    };

    struct Snapshot {
        State                    state = State::starting;
        std::string              phase;         // one line: what the app is doing
        std::string              detail;        // a second line, or empty
        std::string              model;         // the model's file name, or empty while unknown
        std::string              address;       // the console's IP address; empty until the app knows it
        int                      port     = 7860;  // the server's port
        std::string              prompt;        // the running job's prompt, then the last job's
        int                      step     = 0;  // the progress callback's step and steps
        int                      steps    = 0;  // (tensors while a model loads, denoiser steps while sampling)
        float                    step_s   = 0;  // seconds the last step took
        double                   job_s    = 0;  // seconds since the running job began (generating only)
        double                   last_job_s = -1;  // how long the last finished job took, or -1
        int                      images   = 0;  // finished images since the app started
        double                   free_gib = -1;  // direct memory free, or -1 before it is read
        double                   pool_gib = -1;
        std::string              result;        // the last image written
        std::vector<Result>      recent;        // finished images, newest first
        std::shared_ptr<const Frame> preview;   // the job's newest preview, or null
        uint64_t                 preview_serial = 0;  // grows with every new preview
        std::vector<std::string> lines;         // the log's last lines, oldest first
        bool                     tv_images = true;  // false: the screen shows no images and no prompts
        std::string              api_key;       // the API key to show, or empty (none, or hidden)
        Console                  console;
    };

    void set_phase(std::string phase, std::string detail = {});
    void set_progress(int step, int steps, float step_s);
    void set_memory(double free_gib, double pool_gib);
    void set_result(std::string path);
    void set_model(std::string name);
    void set_tv_images(bool shown);
    // The API key the screen shows; empty for none.
    void set_api_key(std::string key);
    // Where a browser reaches the server. The address is whatever should be
    // shown in "http://<address>:<port>": an IPv4 string, or empty while the
    // app does not know its own (the screen then shows a placeholder).
    void set_address(std::string address);
    void set_port(int port);
    // Copies the frame; the pointer is only valid during the call.
    void set_preview(const uint8_t * rgb, int width, int height);
    void set_console(const Console & console);
    void log(std::string line);
    Snapshot snapshot() const;
    // The seconds jobs have run since the app started, the running one's so far.
    double job_seconds() const;

    // The state a phase text stands for (see State).
    static State classify(const std::string & phase);
    // A path's file name without directory and extension, for the screen.
    static std::string display_name(const std::string & path);

  private:
    static constexpr std::size_t kLines   = 24;
    static constexpr std::size_t kResults = 12;
    mutable std::mutex           mutex_;
    Snapshot                     s_;
    std::deque<std::string>      lines_;
    double                       job_started_ = -1;  // monotonic seconds, or -1
    double                       jobs_done_s_ = 0;   // the seconds of the finished jobs
};

// The model and the port the server will use, read from the same files
// main.cpp's server_args() reads (model.txt, server-args.txt: -m, --model or
// --diffusion-model, --listen-port), so the screen can name them before the
// server has started. Anything not found keeps its default (empty model,
// default_port).
struct ServerConfig {
    std::string model;
    int         port = 7860;
};
ServerConfig read_server_config(const char * model_file, const char * server_args_file, int default_port);

}  // namespace ps5sd
