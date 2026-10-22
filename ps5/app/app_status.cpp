// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_status.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <utility>

namespace ps5sd {

namespace {

double now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// One value a line, '#' lines and blanks skipped: model.txt and
// server-args.txt, as main.cpp reads them.
std::vector<std::string> read_lines(const char * path) {
    std::vector<std::string> out;
    if (path == nullptr) {
        return out;
    }
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

}  // namespace

AppStatus::State AppStatus::classify(const std::string & phase) {
    const auto has = [&](const char * text) { return phase.find(text) != std::string::npos; };
    const auto starts = [&](const char * text) { return phase.rfind(text, 0) == 0; };
    if (has("fail") || has("did not") || has("rror") || has("could not") || starts("Nothing to do")) {
        return State::error;
    }
    if (starts("Generating")) {
        return State::generating;
    }
    if (starts("Ready") || starts("Done")) {
        return State::ready;
    }
    if (has("top")) {  // Stopping the server, Server stopped, The server stopped
        return State::stopped;
    }
    if (starts("Opening") || phase.empty()) {
        return State::starting;
    }
    return State::loading;
}

std::string AppStatus::display_name(const std::string & path) {
    std::string name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && name.size() - dot <= 12) {
        name.resize(dot);
    }
    return name;
}

void AppStatus::set_phase(std::string phase, std::string detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    const State next = classify(phase);
    const double now = now_seconds();
    if (s_.state == State::generating && next != State::generating && job_started_ >= 0) {
        s_.last_job_s = now - job_started_;
        jobs_done_s_ += s_.last_job_s;
        job_started_  = -1;
    }
    if (next == State::generating) {
        if (job_started_ < 0) {
            job_started_ = now;
        }
        // A new job: its prompt, and no preview of the last one. A model that
        // has just loaded reported its tensors as steps; sd-server's job_begin
        // sets the denoiser steps before it comes here, and that is the only
        // time the steps are kept.
        s_.prompt = detail;
        s_.preview.reset();
        if (s_.state == State::loading || s_.state == State::starting) {
            s_.step = s_.steps = 0;
            s_.step_s          = 0;
        }
    } else {
        s_.step = s_.steps = 0;
        s_.step_s          = 0;
    }
    if (phase == "Loading the model" && !detail.empty() && detail.rfind("then", 0) != 0) {
        s_.model = display_name(detail);  // the one-shot passes the model's path
    }
    s_.state  = next;
    s_.phase  = std::move(phase);
    s_.detail = std::move(detail);
}

void AppStatus::set_progress(int step, int steps, float step_s) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.step   = step;
    s_.steps  = steps;
    s_.step_s = step_s;
}

void AppStatus::set_memory(double free_gib, double pool_gib) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.free_gib = free_gib;
    s_.pool_gib = pool_gib;
}

void AppStatus::set_result(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.recent.erase(std::remove_if(s_.recent.begin(), s_.recent.end(),
                                   [&](const Result & r) { return r.path == path; }),
                    s_.recent.end());
    s_.recent.insert(s_.recent.begin(), Result{ path, s_.prompt });
    if (s_.recent.size() > kResults) {
        s_.recent.resize(kResults);
    }
    s_.images++;
    s_.result = std::move(path);
}

void AppStatus::set_model(std::string name) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.model = std::move(name);
}

void AppStatus::set_api_key(std::string key) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.api_key = std::move(key);
}

void AppStatus::set_tv_images(bool shown) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.tv_images = shown;
}

void AppStatus::set_address(std::string address) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.address = std::move(address);
}

void AppStatus::set_port(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port > 0 && port < 65536) {
        s_.port = port;
    }
}

void AppStatus::set_preview(const uint8_t * rgb, int width, int height) {
    // 8192 * 8192 * 3 bytes is what a frame can be at most; a bad call is ignored.
    if (rgb == nullptr || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        return;
    }
    auto frame    = std::make_shared<Frame>();
    frame->width  = width;
    frame->height = height;
    frame->rgb.assign(rgb, rgb + (std::size_t) width * (std::size_t) height * 3);
    std::lock_guard<std::mutex> lock(mutex_);
    s_.preview = std::move(frame);
    s_.preview_serial++;
}

void AppStatus::set_console(const Console & console) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.console = console;
}

double AppStatus::job_seconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_done_s_ + (job_started_ >= 0 ? std::max(0.0, now_seconds() - job_started_) : 0.0);
}

void AppStatus::log(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(std::move(line));
    while (lines_.size() > kLines) {
        lines_.pop_front();
    }
}

AppStatus::Snapshot AppStatus::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s = s_;
    s.lines.assign(lines_.begin(), lines_.end());
    if (s.state == State::generating && job_started_ >= 0) {
        s.job_s = std::max(0.0, now_seconds() - job_started_);
    }
    return s;
}

ServerConfig read_server_config(const char * model_file, const char * server_args_file, int default_port) {
    ServerConfig config;
    config.port = default_port;
    if (const auto model = read_lines(model_file); !model.empty()) {
        config.model = AppStatus::display_name(model[0]);
    }
    const auto args = read_lines(server_args_file);
    for (std::size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "-m" || args[i] == "--model" || args[i] == "--diffusion-model") {
            config.model = AppStatus::display_name(args[i + 1]);
        } else if (args[i] == "--listen-port") {
            if (const int port = std::atoi(args[i + 1].c_str()); port > 0 && port < 65536) {
                config.port = port;
            }
        }
    }
    return config;
}

}  // namespace ps5sd
