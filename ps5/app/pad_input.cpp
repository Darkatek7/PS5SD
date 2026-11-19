// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad_input.hpp"

#include "core/input.hpp"
#include "dashboard.hpp"
#include "slideshow.hpp"

#include <cstdio>

namespace ps5sd {

// slideshow.hpp keeps its own copy of the scePad bits so the host tests need no kit; they must be the kit's.
static_assert(slide_pad::kOptions == hui::pad_bits::kOptions);
static_assert(slide_pad::kRight == hui::pad_bits::kRight);
static_assert(slide_pad::kLeft == hui::pad_bits::kLeft);
static_assert(slide_pad::kTriangle == hui::pad_bits::kTriangle);
static_assert(slide_pad::kCircle == hui::pad_bits::kCircle);
static_assert(slide_pad::kIntercepted == hui::pad_bits::kIntercepted);

void PadInput::poll(Dashboard & dashboard, float dt) {
    if (!pad_.is_open()) {
        retry_ -= dt;
        if (tried_ && retry_ > 0) {
            return;
        }
        tried_ = true;
        retry_ = 30.0f;
        if (!pad_.open()) {
            std::printf("ps5sd: no controller input (the slideshow cannot be used)\n");
            dashboard.set_pad_open(false);
            return;
        }
        std::printf("ps5sd: controller input open\n");
        dashboard.set_pad_open(true);
    }
    // Every sample since the last frame: scePadRead batches up to 64, so a short tap survives a long frame.
    hui::PadSample samples[64];
    const std::size_t count = pad_.read(samples);
    for (std::size_t i = 0; i < count; ++i) {
        dashboard.pad_sample(samples[i].buttons, samples[i].left_x, samples[i].connected);
    }
}

}  // namespace ps5sd
