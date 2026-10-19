// PS5SD's screen for now: what the app is doing, the generation's progress,
// the memory free and the last lines of the log, in one monospaced font.
//
// AppStatus is written by the generation thread and the log callback and read
// by the main thread once a frame. StatusView draws a snapshot of it into a
// DrawList, the same way PS5LM's dashboard draws its frame; a dashboard built
// from ps5-homebrew-ui's components takes StatusView's place in main.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "ui/fonts.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace hui::gfx {
class Renderer;
}

namespace ps5sd {

class AppStatus {
  public:
    struct Snapshot {
        std::string              phase;         // one line: what the app is doing
        std::string              detail;        // a second line, or empty
        int                      step     = 0;  // the progress callback's step and steps
        int                      steps    = 0;
        float                    step_s   = 0;  // seconds the last step took
        double                   free_gib = -1;  // direct memory free, or -1 before it is read
        double                   pool_gib = -1;
        std::string              result;  // the last image written
        std::vector<std::string> lines;   // the log's last lines, oldest first
    };

    void set_phase(std::string phase, std::string detail = {});
    void set_progress(int step, int steps, float step_s);
    void set_memory(double free_gib, double pool_gib);
    void set_result(std::string path);
    void log(std::string line);
    Snapshot snapshot() const;

  private:
    static constexpr std::size_t kLines = 24;
    mutable std::mutex           mutex_;
    Snapshot                     s_;
    std::deque<std::string>      lines_;
};

class StatusView {
  public:
    // The font, from the title's assets (scripts/build-app.sh copies
    // ps5-homebrew-ui's DejaVu Sans Mono there).
    bool load(hui::gfx::Renderer & renderer, const std::string & font_path);
    void draw(const AppStatus::Snapshot & s, const char * version, hui::gfx::DrawList & list) const;

  private:
    hui::gfx::Font    mono_;
    hui::ui::FontRef  font_;
};

}  // namespace ps5sd
