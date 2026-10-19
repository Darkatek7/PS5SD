// Decoding a finished image (PNG or JPEG) for the dashboard.
//
// stb_image, as the kit has no decoder. Kept in a file of its own so stb's
// implementation stays private to it (sd-server's own copy is private too).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5sd {

// RGBA8 pixels, top row first (what GlBatch::create_texture takes).
struct Rgba {
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> pixels;
};

// Decodes the file. An image larger than max_side on its longer side is
// shrunk by whole-number box filters (never below max_side / 2), so a 1024
// px picture becomes a 512 px texture rather than a 2 MiB one with aliasing.
// Returns false when the file is missing or not an image.
bool load_image_rgba(const std::string & path, int max_side, Rgba & out);

// A copy of `in` whose longer side is at most max_side, by box filter.
// Ratios between whole numbers only; an image already small enough is copied.
Rgba shrink_to(const Rgba & in, int max_side);

}  // namespace ps5sd
