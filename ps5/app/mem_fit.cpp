// What mem_fit.hpp says.
#include "mem_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "json.hpp"

namespace ps5sd {

namespace {

// Measured on the console (FW 13.00, 0.8.1, docs/CONSOLE.md "Memory"), each right after a start, one step.
// The work of a run is width x height of its largest pass, times the frames of a video.
struct Limit {
    const char * family;   // net_family's label; "Wan" also matches "Wan 1.3B"
    int64_t      ok;       // the largest work that ran
    int64_t      too_big;  // the smallest work that did not run even after a start; 0: none found
    int64_t      safe;     // work that runs after other runs too (the size of most images)
    const char * ok_text;  // ok, for people
};

constexpr int64_t px(int w, int h, int frames = 1) { return (int64_t) w * h * frames; }

const Limit kLimits[] = {
    { "SDXL", px(2048, 2048), 0, px(2048, 2048), "2048x2048" },
    { "SD 2.1", px(1024, 1024), 0, px(1024, 1024), "1024x1024" },
    { "SD 1.5", px(1024, 1024), 0, px(1024, 1024), "1024x1024" },
    // 0.9.0: buffers of several blocks (patches/mesa/0010); 2048 also ran after 1024, 1536 and 1280
    { "FLUX.2 Klein 4B", px(2048, 2048), 0, px(2048, 2048), "2048x2048" },
    // 0.9.7: Q4_0 with Qwen3-8B, 1024x1024 right after a start and warm (0.9 GiB left in one block)
    { "FLUX.2 Klein 9B", px(1024, 1024), 0, px(1024, 1024), "1024x1024" },
    { "FLUX.3 Klein", px(1024, 1024), px(1536, 1536), px(896, 896), "1024x1024" },
    { "Z-Image", px(1536, 1536), 0, px(1024, 1024), "1536x1536" },
    // 0.9.1: 81 frames after 33 and 49, with CausVid
    { "Wan 1.3B", px(832, 480, 81), 0, px(832, 480, 81), "832x480, 81 frames" },
    // 0.9.6: Wan 2.2 TI2V 5B, 1280x704 with 33 frames right after a start (2.48 GiB left in one block);
    // 832x480 with 33 frames after smaller runs
    { "Wan 5B", px(1280, 704, 33), 0, px(832, 480, 33), "1280x704, 33 frames" },
};

const Limit * limit_for(const std::string & family) {
    for (const Limit & l : kLimits) {
        if (family == l.family) {
            return &l;
        }
    }
    return nullptr;
}

std::string size_text(const FitQuery & q) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%dx%d", q.pass_width, q.pass_height);
    return q.frames > 1 ? std::string(buf) + ", " + std::to_string(q.frames) + " frames" : std::string(buf);
}

}  // namespace

bool fit_query_from_body(const std::string & body, FitQuery & q, std::string & error) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object()) {
        error = "the body must be the JSON object of an img_gen or vid_gen request";
        return false;
    }
    auto integer = [&](const char * key, int fallback) {
        const auto it = j.find(key);
        return it != j.end() && it->is_number() ? (int) it->get<double>() : fallback;
    };
    q.width  = integer("width", 0);
    q.height = integer("height", 0);
    if (q.width <= 0 || q.height <= 0) {
        error = "the body needs \"width\" and \"height\"";
        return false;
    }
    q.frames      = std::max(1, integer("video_frames", 1));
    q.pass_width  = q.width;
    q.pass_height = q.height;
    const auto hr = j.find("hires");
    if (hr != j.end() && hr->is_object() && hr->value("enabled", false)) {
        const double scale = hr->contains("scale") && (*hr)["scale"].is_number() ? (*hr)["scale"].get<double>() : 1.0;
        if (scale > 1.0) {
            // the server's rounding of the hires target (a multiple of 8)
            q.pass_width  = (int) std::lround(q.width * scale / 8.0) * 8;
            q.pass_height = (int) std::lround(q.height * scale / 8.0) * 8;
        }
    }
    return true;
}

FitAnswer check_fit(const FitQuery & q) {
    const int64_t work = px(q.pass_width, q.pass_height, q.frames);
    const int64_t size = px(q.width, q.height);
    const Limit * l    = limit_for(q.family);
    if (l != nullptr && l->too_big > 0 && work >= l->too_big) {
        const char * lower = q.frames > 1 ? "Lower the size or the frames."
                             : q.pass_width != q.width || q.pass_height != q.height ? "Lower the size or the hires scale."
                                                                                    : "Lower the size.";
        return { "too_big", "Too large for the console's memory: " + q.family + " runs up to about " + l->ok_text +
                                ", measured, and " + size_text(q) + " does not. " + lower };
    }
    if (l != nullptr && work > l->ok) {
        return { "tight", "Larger than anything measured with " + q.family + " on the console (up to " + l->ok_text +
                              "). It may run out of memory." };
    }
    // Memory that earlier runs freed is not all usable again: a run larger than any before it may not
    // find room, unless it is a size that runs after others too.
    const bool    history = q.largest_ok > 0 || q.other_model_before;
    const int64_t safe    = l != nullptr ? l->safe : px(1024, 1024);
    if (history && work > safe && size > q.largest_ok) {
        return { "tight", std::string("Larger than every run since ") +
                              (q.other_model_before ? "PS5SD started" : "the model loaded") +
                              ". It may run out of memory now; right after a restart of PS5SD it has the most room." };
    }
    return { "fits", "" };
}

std::string fit_json(const FitQuery & q, const FitAnswer & a) {
    return nlohmann::json{ { "verdict", a.verdict },
                           { "message", a.message },
                           { "family", q.family },
                           { "width", q.pass_width },
                           { "height", q.pass_height },
                           { "frames", q.frames } }
        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace ps5sd
