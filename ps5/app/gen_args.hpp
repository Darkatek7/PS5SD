// The one-shot generation's settings: defaults for SD-Turbo, overridden by
// /data/PS5SD/model.txt (the model's path) and /data/PS5SD/app-args.txt (one
// argument a line, in sd-cli's spelling). A small subset of sd-cli's options;
// the HTTP server takes its own arguments.
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "stable-diffusion.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ps5sd {

struct GenOptions {
    // Model files; empty ones are left out.
    std::string model;            // --model / -m: a whole checkpoint (SD-Turbo's safetensors or gguf)
    std::string diffusion_model;  // --diffusion-model
    std::string vae;              // --vae
    std::string taesd;            // --taesd
    std::string clip_l;           // --clip_l
    std::string t5xxl;            // --t5xxl
    std::string llm;              // --llm

    std::string prompt = "a photograph of an astronaut riding a horse";  // -p / --prompt
    std::string negative_prompt;                                         // -n / --negative-prompt
    int         width  = 512;                                            // -W / --width
    int         height = 512;                                            // -H / --height
    int         steps  = 1;                                              // --steps: SD-Turbo takes 1 to 4
    float       cfg_scale = 1.0f;                                        // --cfg-scale: SD-Turbo has no CFG
    enum sample_method_t sample_method = EULER_A_SAMPLE_METHOD;          // --sampling-method
    enum scheduler_t     scheduler     = SCHEDULER_COUNT;                // --scheduler; COUNT: the model's default
    int64_t     seed       = 42;                                         // -s / --seed; -1 picks one at random
    bool        vae_tiling = false;                                      // --vae-tiling
    std::string flash_attn;  // --flash-attn on|off|auto[,module=...]; empty: sd.cpp's default
    std::string backend = "Vulkan";  // --backend: ggml's Vulkan registry (the console's GPU); "CPU" to compare
    int         threads = 0;         // -t / --threads; 0: sd.cpp's default (patches/sdcpp/0002 on the PS5)

    // The parameter structs for new_sd_ctx and generate_image. Their strings
    // point into this object, which must outlive them.
    void to_params(sd_ctx_params_t & ctx, sd_img_gen_params_t & gen) const;
};

// Applies args (one argument per element) to options; false with a message in
// error for an unknown option, a missing value or a value out of range.
bool parse_gen_args(const std::vector<std::string> & args, GenOptions & options, std::string & error);

}  // namespace ps5sd
