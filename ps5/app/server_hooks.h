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

#include <stddef.h>
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

// The job ended. ok is 0 or 1. On success (ok 1) output_path lists the images
// saved to --output-dir, one full path a line in the order of the results
// (several for a batch; "" when there is no --output-dir or nothing was saved).
// On failure (ok 0) it is the reason the job failed, as a person should read
// it, or "" when that is not known. Never NULL.
void sd_activity_job_end(int ok, const char* output_path);

// The application's own routes: GET, POST and DELETE on /ps5sd/v1/app/<path>
// come here with <path>, the request body and its length. Returns the HTTP
// status, or 0 when the application has no such route (the server answers
// 404). *out_body is a malloc'd body of *out_len bytes or NULL (the server
// frees it), *content_type a static string or NULL (application/json).
// Called on the server's HTTP threads, possibly several at once.
int sd_app_request(const char* method, const char* path, const char* body, size_t body_len,
                   char** out_body, size_t* out_len, const char** content_type);

// PUT /ps5sd/v1/app/<path>: an upload. The body is handed over as it
// arrives instead of whole, since a model file can be several GB. length is
// the Content-Length (0 when not given). The application calls read at most
// once: read(read_ctx, sink, sink_ctx) calls sink(sink_ctx, data, n) for each
// piece of the body in order, and returns 1 once all of it went through, 0
// when the connection failed or sink returned 0 to stop. The return value and
// the out_ arguments are as for sd_app_request. Called on the server's HTTP
// threads, possibly several at once.
typedef int (*sd_app_sink)(void* sink_ctx, const char* data, size_t n);
typedef int (*sd_app_reader)(void* read_ctx, sd_app_sink sink, void* sink_ctx);
int sd_app_upload(const char* path, uint64_t length, sd_app_reader read, void* read_ctx, char** out_body,
                  size_t* out_len, const char** content_type);

#ifdef __cplusplus
}
#endif
