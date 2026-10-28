// Host test for mem_fit.cpp: the request body read as a fit query, and the
// verdicts against the sizes measured on the console.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mem_fit.hpp"

#include <cstdio>
#include <string>

using namespace ps5sd;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

FitQuery query(const std::string & family, int w, int h, int frames = 1) {
    FitQuery q;
    q.family = family;
    q.width = q.pass_width = w;
    q.height = q.pass_height = h;
    q.frames = frames;
    return q;
}

void is(const FitQuery & q, const std::string & want, const std::string & what) {
    const FitAnswer a = check_fit(q);
    check(a.verdict == want, what + ": " + a.verdict + ", not " + want);
    check(a.verdict == "fits" ? a.message.empty() : !a.message.empty(), what + ": a message when it does not fit");
}

void test_body() {
    FitQuery    q;
    std::string error;
    check(fit_query_from_body(R"({"prompt":"x","width":832,"height":1216})", q, error), "an img_gen body");
    check(q.pass_width == 832 && q.pass_height == 1216 && q.frames == 1, "the size is the pass");
    check(fit_query_from_body(R"({"width":1024,"height":1024,"hires":{"enabled":true,"scale":1.5}})", q, error),
          "with the hires fix");
    check(q.width == 1024 && q.pass_width == 1536 && q.pass_height == 1536, "the hires target is the largest pass");
    check(fit_query_from_body(R"({"width":1024,"height":1024,"hires":{"enabled":false,"scale":2}})", q, error) &&
              q.pass_width == 1024,
          "the hires fix off");
    check(fit_query_from_body(R"({"width":1024,"height":1024,"control_image":"iVBOR..."})", q, error) && q.control,
          "a guide image");
    check(fit_query_from_body(R"({"width":1024,"height":1024})", q, error) && !q.control, "no guide image");
    check(fit_query_from_body(R"({"width":1024,"height":1024,"control":true})", q, error) && q.control,
          "a guide image named by the web UI");
    check(fit_query_from_body(R"({"width":832,"height":480,"video_frames":33})", q, error) && q.frames == 33,
          "a vid_gen body");
    check(!fit_query_from_body(R"({"prompt":"x"})", q, error) && !error.empty(), "no size");
    check(!fit_query_from_body("not json", q, error), "not JSON");
}

void test_verdicts() {
    // right after a start
    is(query("SDXL", 1024, 1024), "fits", "SDXL 1024");
    is(query("SDXL", 2048, 2048), "fits", "SDXL 2048, measured");
    is(query("SDXL", 2560, 2560), "fits", "SDXL 2560, measured (0.9.18)");
    is(query("SDXL", 3072, 3072), "fits", "SDXL 3072, measured (0.9.18)");
    is(query("SDXL", 4096, 4096), "tight", "SDXL over the measured size");
    {
        FitQuery g = query("SDXL", 1536, 1536);
        g.control  = true;
        is(g, "fits", "SDXL with a guide image, 1536 measured (0.9.18)");
        g = query("SDXL", 2048, 2048);
        g.control = true;
        is(g, "tight", "SDXL with a guide image, 2048 ran out (0.9.18)");
        check(check_fit(g).message.find("SDXL + ControlNet") != std::string::npos, "the guide image's limit is named");
        g.family = "FLUX.2 Klein 4B";
        is(g, "fits", "a guide image with a family without its own limit: the family's");
    }
    is(query("FLUX.2 Klein 4B", 1536, 1536), "fits", "Klein 4B 1536");
    is(query("FLUX.2 Klein 4B", 2048, 2048), "fits", "Klein 4B 2048, measured (patches/mesa/0010)");
    is(query("FLUX.2 Klein 4B", 2560, 2560), "tight", "Klein 4B over the measured size");
    is(query("FLUX.2 Klein 9B", 1024, 1024), "fits", "Klein 9B 1024, measured");
    is(query("FLUX.2 Klein 9B", 1536, 1536), "tight", "Klein 9B over the measured size");
    is(query("FLUX.3 Klein", 1024, 1024), "fits", "FLUX.3 Klein 1024");
    is(query("FLUX.3 Klein", 1536, 1536), "too_big", "FLUX.3 Klein 1536");
    is(query("Wan 1.3B", 832, 480, 33), "fits", "Wan 33 frames");
    is(query("Wan 1.3B", 832, 480, 81), "fits", "Wan 81 frames, measured");
    is(query("Wan 1.3B", 832, 480, 97), "tight", "Wan 97 frames, not measured");
    is(query("Wan 5B", 1280, 704, 33), "fits", "Wan 2.2 TI2V 1280x704, 33 frames, measured");
    is(query("Wan 5B", 1280, 704, 49), "tight", "Wan 2.2 TI2V 49 frames at 1280x704, not measured");
    is(query("", 768, 768), "fits", "an unknown family at a usual size");
    is(query("FLUX.1", 1024, 1024), "fits", "an unmeasured family at 1024");
    is(query("FLUX.1", 1536, 1536), "tight", "an unmeasured family above 1024");
    check(check_fit(query("SD 3", 2048, 2048)).message.find("Not measured") != std::string::npos,
          "an unmeasured family says so");
    check(check_fit(query("SDXL", 4096, 4096)).message.find("Lower the size.") != std::string::npos,
          "a tight run says what to lower");
    {
        FitQuery v = query("Wan 1.3B", 832, 480, 97);
        check(check_fit(v).message.find("Lower the size or the frames.") != std::string::npos, "a video: the frames");
    }

    // after other runs
    FitQuery q = query("SDXL", 2048, 2048);
    q.largest_ok = 1024 * 1024;
    is(q, "fits", "SDXL 2048 after 1024 (it runs in segments, patches/sdcpp/0067)");
    q = query("FLUX.2 Klein 4B", 1536, 1536);
    q.largest_ok = 1024 * 1024;
    is(q, "fits", "Klein 4B 1536 after 1024 (buffers of several blocks, patches/mesa/0010)");
    q = query("Z-Image", 1536, 1536);
    q.largest_ok = 1024 * 1024;
    is(q, "tight", "Z-Image 1536 after 1024, not measured");
    q.largest_ok = 1536 * 1536;
    is(q, "fits", "Z-Image 1536 again");
    q = query("SDXL", 1024, 1024);
    q.other_model_before = true;
    is(q, "fits", "SDXL 1024 after another model");
    q = query("FLUX.3 Klein", 1024, 1024);
    q.other_model_before = true;
    is(q, "tight", "FLUX.3 Klein 1024 after another model (it has no segments)");
    q = query("SDXL", 832, 1216);
    q.pass_width  = 1248;
    q.pass_height = 1824;
    q.largest_ok  = 832 * 1216;
    is(q, "fits", "the same size with the hires fix again (the history counts the request's size)");
}

}  // namespace

int main() {
    test_body();
    test_verdicts();
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
