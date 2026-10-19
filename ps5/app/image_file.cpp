// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_file.hpp"

#include <algorithm>
#include <cstdio>

// Only PNG and JPEG (the app writes PNG), no HDR (which would pull in pow and
// ldexp) and no thread-local error string. Static, so these definitions
// cannot meet the ones in sd-server's media_io.
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ps5sd {

Rgba shrink_to(const Rgba & in, int max_side) {
    const int longer = std::max(in.width, in.height);
    if (max_side <= 0 || longer <= max_side) {
        return in;
    }
    const int factor = (longer + max_side - 1) / max_side;  // smallest whole ratio that fits
    Rgba      out;
    out.width  = std::max(1, in.width / factor);
    out.height = std::max(1, in.height / factor);
    out.pixels.resize((std::size_t) out.width * out.height * 4);
    for (int y = 0; y < out.height; y++) {
        for (int x = 0; x < out.width; x++) {
            unsigned sum[4] = {};
            for (int dy = 0; dy < factor; dy++) {
                for (int dx = 0; dx < factor; dx++) {
                    const uint8_t * p = &in.pixels[((std::size_t) (y * factor + dy) * in.width + (x * factor + dx)) * 4];
                    for (int c = 0; c < 4; c++) {
                        sum[c] += p[c];
                    }
                }
            }
            uint8_t * q = &out.pixels[((std::size_t) y * out.width + x) * 4];
            for (int c = 0; c < 4; c++) {
                q[c] = (uint8_t) (sum[c] / (unsigned) (factor * factor));
            }
        }
    }
    return out;
}

bool load_image_rgba(const std::string & path, int max_side, Rgba & out) {
    int         w = 0, h = 0, channels = 0;
    stbi_uc *   data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (data == nullptr || w <= 0 || h <= 0) {
        if (data != nullptr) {
            stbi_image_free(data);
        }
        return false;
    }
    Rgba full;
    full.width  = w;
    full.height = h;
    full.pixels.assign(data, data + (std::size_t) w * h * 4);
    stbi_image_free(data);
    out = shrink_to(full, max_side);
    return true;
}

}  // namespace ps5sd
