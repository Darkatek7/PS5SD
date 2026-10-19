// PS5SD's screen for now: what the app is doing, the generation's progress,
// the memory free and the last lines of the log, in one monospaced font.
//
// StatusView draws a snapshot of AppStatus (app_status.hpp) into a DrawList,
// the same way PS5LM's dashboard draws its frame; a dashboard built from
// ps5-homebrew-ui's components takes StatusView's place in main.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_status.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "ui/fonts.hpp"

#include <string>

namespace hui::gfx {
class Renderer;
}

namespace ps5sd {

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
