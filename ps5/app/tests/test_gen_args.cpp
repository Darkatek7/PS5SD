// Host test for gen_args.cpp: the app-args.txt parser and the parameter
// structs it fills.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gen_args.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char * what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool parse(const std::vector<std::string> & args, ps5sd::GenOptions & o, std::string & error) {
    o = ps5sd::GenOptions{};
    error.clear();
    return ps5sd::parse_gen_args(args, o, error);
}

}  // namespace

int main() {
    ps5sd::GenOptions o;
    std::string       error;

    // The SD-Turbo defaults with only a model.
    check(parse({ "--model", "/data/PS5SD/models/sd_turbo.safetensors" }, o, error), "model only parses");
    sd_ctx_params_t     ctx;
    sd_img_gen_params_t gen;
    o.to_params(ctx, gen);
    check(std::strcmp(ctx.model_path, "/data/PS5SD/models/sd_turbo.safetensors") == 0, "model path");
    check(ctx.vae_path == nullptr && ctx.taesd_path == nullptr, "unset paths are null");
    check(!ctx.auto_fit && !ctx.enable_mmap, "auto-fit and mmap off");
    check(std::strcmp(ctx.backend, "Vulkan") == 0, "Vulkan backend by default");
    check(ctx.flash_attn_mode == nullptr, "flash attention left to sd.cpp");
    check(ctx.n_threads > 0, "default thread count kept");
    check(gen.width == 512 && gen.height == 512, "512x512");
    check(gen.sample_params.sample_steps == 1, "one step");
    check(gen.sample_params.guidance.txt_cfg == 1.0f, "cfg 1");
    check(gen.sample_params.sample_method == EULER_A_SAMPLE_METHOD, "euler_a");
    check(gen.sample_params.scheduler == SCHEDULER_COUNT, "model's scheduler");
    check(gen.seed == 42, "seed 42");
    check(!gen.vae_tiling_params.enabled, "no vae tiling");

    // Every option, one argument a line, as app-args.txt has them.
    check(parse({ "--diffusion-model", "d.gguf", "--vae", "v.safetensors", "--taesd", "t.safetensors", "--clip_l",
                  "c.safetensors", "--t5xxl", "t5.gguf", "--llm", "l.gguf", "-p", "a red fox in the snow", "-n",
                  "blurry", "-W", "768", "-H", "512", "--steps", "4", "--cfg-scale", "2.5", "--sampling-method",
                  "euler", "--scheduler", "karras", "--seed", "-1", "--vae-tiling", "--flash-attn", "on",
                  "--backend", "CPU", "-t", "3" },
                o, error),
          "all options parse");
    o.to_params(ctx, gen);
    check(ctx.model_path == nullptr && std::strcmp(ctx.diffusion_model_path, "d.gguf") == 0, "diffusion model");
    check(std::strcmp(ctx.llm_path, "l.gguf") == 0 && std::strcmp(ctx.t5xxl_path, "t5.gguf") == 0, "encoders");
    check(std::strcmp(gen.prompt, "a red fox in the snow") == 0, "prompt with spaces");
    check(std::strcmp(gen.negative_prompt, "blurry") == 0, "negative prompt");
    check(gen.width == 768 && gen.height == 512, "size");
    check(gen.sample_params.sample_steps == 4 && gen.sample_params.guidance.txt_cfg == 2.5f, "steps and cfg");
    check(gen.sample_params.sample_method == EULER_SAMPLE_METHOD, "euler");
    check(gen.sample_params.scheduler == KARRAS_SCHEDULER, "karras");
    check(gen.seed == -1, "random seed");
    check(gen.vae_tiling_params.enabled, "vae tiling");
    check(std::strcmp(ctx.flash_attn_mode, "on") == 0, "flash attention on");
    check(std::strcmp(ctx.backend, "CPU") == 0 && ctx.n_threads == 3, "backend and threads");

    // Errors.
    check(!parse({}, o, error) && error.find("no model") != std::string::npos, "no model is an error");
    check(!parse({ "-m", "x", "--steps" }, o, error), "missing value");
    check(!parse({ "-m", "x", "--bogus", "1" }, o, error), "unknown option");
    check(!parse({ "-m", "x", "-W", "500" }, o, error), "width not a multiple of 8");
    check(!parse({ "-m", "x", "--steps", "0" }, o, error), "zero steps");
    check(!parse({ "-m", "x", "--sampling-method", "nope" }, o, error), "unknown sampler");
    check(!parse({ "-m", "x", "--scheduler", "nope" }, o, error), "unknown scheduler");
    check(!parse({ "-m", "x", "--cfg-scale", "abc" }, o, error), "cfg not a number");

    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
