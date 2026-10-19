// What the app is doing, for the screen (status_view.hpp) or, in a build
// without one (PS5SD_NO_DISPLAY), for the log and the notifications.
//
// Written by the generation thread and the log callback, read by the main
// thread once a frame. No ps5-homebrew-ui or ps5-opengl in here, so the
// no-display build compiles it on its own.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

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

}  // namespace ps5sd
