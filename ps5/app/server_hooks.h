// PS5SD: activity hooks.
//
// The server calls these around every generation. They are weak no-ops in
// server_hooks.cpp; an embedding application (the PS5 app) defines the real
// ones in an object file that is linked into the executable directly, and they
// replace the no-ops. Keep this file identical to ps5/app/server_hooks.h.
//
// Threads: every hook is called from the thread that runs the generation
// (the async job worker, or the HTTP thread of a synchronous request), one job
// at a time (the generation context is locked), never concurrently with each
// other. The pointers are only valid during the call.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A generation starts. kind: "txt2img", "img2img", "openai_generations",
// "openai_edits", "img_gen" or "vid_gen". prompt is UTF-8 and may be empty.
void sd_activity_job_begin(const char* kind, const char* prompt, int w, int h, int steps);

// A sampling step finished: step of steps, secs is the duration of that step
// (seconds per iteration). Also called for the other passes of a job (hires,
// upscale), so step may restart from 1.
void sd_activity_progress(int step, int steps, float secs);

// A preview of the image being denoised: w * h * 3 bytes, RGB, row after row.
// Only called when the request asked for previews (the web UI does).
void sd_activity_preview(const uint8_t* rgb, int w, int h);

// The job ended. ok is 0 or 1. output_path is the first image saved to
// --output-dir ("" when there is no --output-dir or nothing was saved; never NULL).
void sd_activity_job_end(int ok, const char* output_path);

#ifdef __cplusplus
}
#endif
