// The DualSense for the dashboard's slideshow: ps5-homebrew-ui's pad code (scePad through the
// initial user), read once a frame and handed to the dashboard.
//
// Console code only (the preview and the host tests make their own samples); main.cpp calls
// poll() where it draws a frame. Needs libScePad and libSceUserService, which a title loads
// like any other system module (the kit's own titles use them).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "platform/ps5/pad.hpp"

namespace ps5sd {

class Dashboard;

class PadInput {
  public:
    // Reads the samples since the last frame into the dashboard. Opens the pad on the first call,
    // and again every 30 seconds while it is not open (a console that has no user yet, or a pad
    // service that is late); the attempt takes up to a second when it fails.
    void poll(Dashboard & dashboard, float dt);

  private:
    hui::ps5::Pad pad_;
    float         retry_ = 0;
    bool          tried_ = false;
};

}  // namespace ps5sd
