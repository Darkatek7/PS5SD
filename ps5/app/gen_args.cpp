// SPDX-License-Identifier: GPL-3.0-or-later
#include "gen_args.hpp"

#include <cerrno>
#include <cstdlib>

namespace ps5sd {

namespace {

const char * or_null(const std::string & s) {
    return s.empty() ? nullptr : s.c_str();
}

bool to_int(const std::string & s, long long lo, long long hi, long long & out) {
    if (s.empty()) {
        return false;
    }
    char * end = nullptr;
    errno = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || v < lo || v > hi) {
        return false;
    }
    out = v;
    return true;
}

bool to_float(const std::string & s, float lo, float hi, float & out) {
    if (s.empty()) {
        return false;
    }
    char * end = nullptr;
    errno = 0;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || *end != '\0' || !(v >= lo && v <= hi)) {
        return false;
    }
    out = v;
    return true;
}

}  // namespace

void GenOptions::to_params(sd_ctx_params_t & ctx, sd_img_gen_params_t & gen) const {
    sd_ctx_params_init(&ctx);
    ctx.model_path           = or_null(model);
    ctx.diffusion_model_path = or_null(diffusion_model);
    ctx.vae_path             = or_null(vae);
    ctx.taesd_path           = or_null(taesd);
    ctx.clip_l_path          = or_null(clip_l);
    ctx.t5xxl_path           = or_null(t5xxl);
    ctx.llm_path             = or_null(llm);
    ctx.backend              = or_null(backend);
    ctx.flash_attn_mode      = or_null(flash_attn);
    // The plan is the one backend asked for: auto-fit would size the weights by
    // the "free RAM" a title does not have in the usual sense (patches/sdcpp/0001
    // reports direct memory, which the GPU heap is cut from too).
    ctx.auto_fit    = false;
    // A title cannot map large files (PS5SX2: mmap in the app sandbox fails);
    // sd.cpp's default, kept explicit.
    ctx.enable_mmap = false;
    if (threads > 0) {
        ctx.n_threads = threads;
    }

    sd_img_gen_params_init(&gen);
    gen.prompt                           = prompt.c_str();
    gen.negative_prompt                  = negative_prompt.c_str();
    gen.width                            = width;
    gen.height                           = height;
    gen.seed                             = seed;
    gen.sample_params.sample_steps       = steps;
    gen.sample_params.guidance.txt_cfg   = cfg_scale;
    gen.sample_params.sample_method      = sample_method;
    gen.sample_params.scheduler          = scheduler;
    gen.vae_tiling_params.enabled        = vae_tiling;
}

bool parse_gen_args(const std::vector<std::string> & args, GenOptions & o, std::string & error) {
    struct Path {
        const char *  long_name;
        const char *  short_name;
        std::string * value;
    };
    const Path paths[] = {
        { "--model", "-m", &o.model },
        { "--diffusion-model", nullptr, &o.diffusion_model },
        { "--vae", nullptr, &o.vae },
        { "--taesd", nullptr, &o.taesd },
        { "--clip_l", nullptr, &o.clip_l },
        { "--t5xxl", nullptr, &o.t5xxl },
        { "--llm", nullptr, &o.llm },
        { "--prompt", "-p", &o.prompt },
        { "--negative-prompt", "-n", &o.negative_prompt },
        { "--flash-attn", nullptr, &o.flash_attn },
        { "--backend", nullptr, &o.backend },
    };
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string & a = args[i];
        if (a == "--vae-tiling") {
            o.vae_tiling = true;
            continue;
        }
        if (i + 1 >= args.size()) {
            error = a + ": no value (each argument goes on a line of its own)";
            return false;
        }
        const std::string & v = args[++i];
        bool known = false;
        for (const auto & p : paths) {
            if (a == p.long_name || (p.short_name && a == p.short_name)) {
                *p.value = v;
                known = true;
            }
        }
        if (known) {
            continue;
        }
        long long n = 0;
        if (a == "-W" || a == "--width" || a == "-H" || a == "--height") {
            // sd.cpp works in multiples of 8 pixels (the VAE's factor); 64 fits every model.
            if (!to_int(v, 64, 4096, n) || n % 8 != 0) {
                error = a + ": " + v + " is not a multiple of 8 from 64 to 4096";
                return false;
            }
            (a == "-W" || a == "--width" ? o.width : o.height) = (int) n;
        } else if (a == "--steps") {
            if (!to_int(v, 1, 1000, n)) {
                error = "--steps: " + v + " is not 1 to 1000";
                return false;
            }
            o.steps = (int) n;
        } else if (a == "-t" || a == "--threads") {
            if (!to_int(v, 1, 64, n)) {
                error = a + ": " + v + " is not 1 to 64";
                return false;
            }
            o.threads = (int) n;
        } else if (a == "-s" || a == "--seed") {
            if (!to_int(v, -1, INT64_MAX, n)) {
                error = a + ": " + v + " is not -1 or a seed";
                return false;
            }
            o.seed = n;
        } else if (a == "--cfg-scale") {
            if (!to_float(v, 0.0f, 100.0f, o.cfg_scale)) {
                error = "--cfg-scale: " + v + " is not 0 to 100";
                return false;
            }
        } else if (a == "--sampling-method") {
            o.sample_method = str_to_sample_method(v.c_str());
            if (o.sample_method == SAMPLE_METHOD_COUNT) {
                error = "--sampling-method: unknown method " + v;
                return false;
            }
        } else if (a == "--scheduler") {
            o.scheduler = str_to_scheduler(v.c_str());
            if (o.scheduler == SCHEDULER_COUNT) {
                error = "--scheduler: unknown scheduler " + v;
                return false;
            }
        } else {
            error = "unknown option " + a;
            return false;
        }
    }
    if (o.model.empty() && o.diffusion_model.empty()) {
        error = "no model: put its path in /data/PS5SD/model.txt or --model in app-args.txt";
        return false;
    }
    return true;
}

}  // namespace ps5sd
