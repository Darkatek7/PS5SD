// SPDX-License-Identifier: GPL-3.0-or-later
#include "status_view.hpp"

#include "core/save_file.hpp"
#include "gfx/renderer.hpp"

#include <algorithm>
#include <cstdio>
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

bool StatusView::load(hui::gfx::Renderer & renderer, const std::string & font_path) {
    std::string data;
    if (!hui::save::read_file(font_path, &data) || !mono_.load(data)) {
        std::printf("ps5sd: font %s failed\n", font_path.c_str());
        return false;
    }
    font_.font    = &mono_;
    font_.texture = renderer.batch().create_font_texture(mono_);
    return true;
}

void StatusView::draw(const AppStatus::Snapshot & s, const char * version, hui::gfx::DrawList & list) const {
    using hui::gfx::Color;
    using hui::gfx::Rect;
    const Color bg     = Color::rgb(0x101418);
    const Color panel  = Color::rgb(0x1b2229);
    const Color text   = Color::rgb(0xe8edf2);
    const Color dim    = Color::rgb(0x8a96a3);
    const Color accent = Color::rgb(0x4fb3ff);
    const float x      = 96;

    list.rounded_rect({ 0, 0, hui::gfx::kVirtualWidth, hui::gfx::kVirtualHeight }, 0, bg);
    hui::ui::text(list, font_, "PS5SD", x, 120, 56, text);
    hui::ui::text(list, font_, std::string("v") + version + "  stable-diffusion.cpp on the GPU (ggml-vulkan, RADV)",
                  x + 220, 120, 22, dim);

    hui::ui::text(list, font_, s.phase.empty() ? "Starting" : s.phase, x, 210, 34, text);
    if (!s.detail.empty()) {
        hui::ui::text(list, font_, font_.font->fit(s.detail, 24, 1720), x, 252, 24, dim);
    }

    // The progress callback's bar: steps while sampling, bytes while loading.
    const Rect bar{ x, 286, 1728, 18 };
    list.rounded_rect(bar, 9, panel);
    if (s.steps > 0) {
        const float done = std::clamp((float) s.step / (float) s.steps, 0.0f, 1.0f);
        list.rounded_rect({ bar.x, bar.y, std::max(bar.h, bar.w * done), bar.h }, 9, accent);
        char line[96];
        std::snprintf(line, sizeof(line), "%d / %d   %.2f s per step", s.step, s.steps, s.step_s);
        hui::ui::text(list, font_, line, x, 344, 24, dim);
    }

    char memory[128];
    if (s.free_gib >= 0) {
        std::snprintf(memory, sizeof(memory), "Memory free %.2f of %.2f GiB", s.free_gib, s.pool_gib);
    } else {
        std::snprintf(memory, sizeof(memory), "Memory free: not read yet");
    }
    hui::ui::text(list, font_, memory, x, 390, 24, dim);
    if (!s.result.empty()) {
        hui::ui::text(list, font_, "Saved " + s.result, x, 430, 24, accent);
    }

    // The log's last lines.
    const Rect log{ x - 24, 470, 1776, 560 };
    list.rounded_rect(log, 12, panel);
    float baseline = log.y + 36;
    for (const auto & l : s.lines) {
        hui::ui::text(list, font_, font_.font->fit(l, 18, log.w - 48), x, baseline, 18, text);
        baseline += 22;
    }
}

}  // namespace ps5sd
