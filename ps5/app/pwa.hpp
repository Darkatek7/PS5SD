// The title's icon and the web manifest for the web UI (GET
// /ps5sd/v1/app/icon.png and manifest.webmanifest; open without the API key,
// patches/sdcpp/0088): a browser can then install the page as an app with
// the title's own icon.
//
// Header only (build-app.sh compiles each .cpp by name).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "json.hpp"

namespace ps5sd {

// The colours of the web UI's light theme (ps5/webui/src/app.css): --bg and --accent.
constexpr const char * kManifestBackground = "#f2f2f7";
constexpr const char * kManifestTheme      = "#007aff";

// The pixel size from a PNG's IHDR; false when the bytes are not a PNG.
inline bool png_dimensions(const std::string & png, uint32_t & width, uint32_t & height) {
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (png.size() < 24 || png.compare(0, 8, std::string(reinterpret_cast<const char *>(sig), 8)) != 0 ||
        png.compare(12, 4, "IHDR") != 0) {
        return false;
    }
    auto be32 = [&](size_t at) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(png[at])) << 24) |
               (static_cast<uint32_t>(static_cast<unsigned char>(png[at + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(png[at + 2])) << 8) |
               static_cast<uint32_t>(static_cast<unsigned char>(png[at + 3]));
    };
    width  = be32(16);
    height = be32(20);
    return width > 0 && height > 0;
}

// The manifest; sizes is "512x512" (the icon's real size), or "" when it is not known.
inline std::string manifest_json(const std::string & sizes) {
    nlohmann::json icon = { { "src", "/ps5sd/v1/app/icon.png" }, { "type", "image/png" } };
    if (!sizes.empty()) {
        icon["sizes"] = sizes;
    }
    const nlohmann::json out = { { "name", "PS5SD" },
                                 { "short_name", "PS5SD" },
                                 { "start_url", "/" },
                                 { "display", "standalone" },
                                 { "background_color", kManifestBackground },
                                 { "theme_color", kManifestTheme },
                                 { "icons", nlohmann::json::array({ icon }) } };
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace ps5sd
