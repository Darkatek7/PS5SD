// Whether a run will fit in the console's GPU memory, said before it runs
// (POST /ps5sd/v1/app/fit, the hint under Generate).
//
// Not a computation. sd.cpp decides at run time whether a graph runs in one
// piece or in segments, and evicts weights block by block, so what a run
// needs is not known in advance (docs/CONSOLE.md, "Memory"). What is known:
//
//   - the largest sizes measured on the console per model family, right after
//     a start (kLimits in mem_fit.cpp);
//   - GPU memory that earlier runs freed goes back to the pool in pieces.
//     Before 0.9.0 each GPU buffer needed one block, so a run larger than
//     every run since the model loaded could fail where it worked right after
//     a start. A buffer can now be made of several (patches/mesa/0010); until
//     that is measured for a family too (safe in kLimits), the warning stays.
//
// So the answer is "fits", "tight" (larger than anything measured, or than
// any run so far: it may fail, and a restart of PS5SD gives the most room)
// or "too_big" (measured not to fit). It warns; it never blocks a run.
//
// No console code in here, so ps5/app/tests builds it on the PC.
#pragma once

#include <cstdint>
#include <string>

namespace ps5sd {

struct FitQuery {
    std::string family;            // net_family of the loaded model: "SDXL", "FLUX.2 Klein 4B", ...
    int         width  = 0;        // the request's size
    int         height = 0;
    int         pass_width  = 0;   // the largest pass: the hires fix's target, else the size
    int         pass_height = 0;
    int         frames = 1;        // a video's frames
    int64_t     largest_ok = 0;    // width x height of the largest run that worked since the model loaded
    bool        other_model_before = false;  // another model ran in this start of PS5SD
};

struct FitAnswer {
    std::string verdict;   // "fits", "tight", "too_big"
    std::string message;   // for the page: what it means and what helps; "" when it fits
};

// The query from a POST /sdcpp/v1/img_gen or vid_gen body (width, height,
// hires {enabled, scale}, video_frames); false with a message for a body that
// is not a JSON object or has no size.
bool fit_query_from_body(const std::string & body, FitQuery & q, std::string & error);

FitAnswer check_fit(const FitQuery & q);

// {"verdict", "message", "family", "width", "height", "frames"}
std::string fit_json(const FitQuery & q, const FitAnswer & a);

}  // namespace ps5sd
