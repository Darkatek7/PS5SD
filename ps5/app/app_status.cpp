// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_status.hpp"

#include <utility>

namespace ps5sd {

void AppStatus::set_phase(std::string phase, std::string detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    s_.phase  = std::move(phase);
    s_.detail = std::move(detail);
    s_.step = s_.steps = 0;
    s_.step_s          = 0;
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
    s_.result = std::move(path);
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
    return s;
}

}  // namespace ps5sd
