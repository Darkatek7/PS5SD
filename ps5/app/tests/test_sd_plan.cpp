// Host test for sd_plan.cpp: family detection from headers, the settings per
// family, the memory ladder, and the arguments that go through gen_args.cpp's
// parser. The model files are made here: a GGUF or safetensors header with
// tensor infos and no weights (sizes are declared by shapes and offsets).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gen_args.hpp"
#include "sd_plan.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace ps5sd;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

#define CHECK(cond) check((cond), std::string(#cond) + " (line " + std::to_string(__LINE__) + ")")

// ---------------------------------------------------------------------------
// Fake model files
// ---------------------------------------------------------------------------

struct T {
    std::string          name;
    std::vector<int64_t> ne;      // ggml order
    std::string          dtype = "F16";
};

// A tensor of about `bytes` bytes, as one row.
T fat(const std::string & name, uint64_t bytes, const std::string & dtype = "F16") {
    const uint64_t elem = dtype == "F32" ? 4 : dtype == "F16" || dtype == "BF16" ? 2 : 1;
    return { name, { (int64_t) (bytes / elem) }, dtype };
}

T fat_gib(const std::string & name, double gib, const std::string & dtype = "F16") {
    return fat(name, (uint64_t) (gib * (double) kGiB), dtype);
}

uint64_t elem_size(const std::string & dtype) {
    if (dtype == "F32") return 4;
    if (dtype == "F16" || dtype == "BF16") return 2;
    return 1;
}

std::string g_dir;
std::vector<std::string> g_files;

std::string path_of(const std::string & name) {
    return g_dir + "/" + name;
}

void put(const std::string & name, const std::string & bytes) {
    const std::string path = path_of(name);
    FILE * f = std::fopen(path.c_str(), "wb");
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    g_files.push_back(path);
}

template <class V> std::string raw(V v) {
    return std::string((const char *) &v, sizeof(v));
}

std::string json_str(const std::string & s) {
    return "\"" + s + "\"";
}

// A safetensors file: length word, JSON header, no data.
std::string write_safetensors(const std::string & name, const std::vector<T> & tensors) {
    std::string json = "{\"__metadata__\":{\"format\":\"pt\",\"note\":\"a \\\"quoted\\\" {brace}\"}";
    uint64_t offset = 0;
    for (const T & t : tensors) {
        uint64_t n = elem_size(t.dtype);
        std::string shape;
        for (size_t i = t.ne.size(); i-- > 0;) {
            n *= (uint64_t) t.ne[i];
            shape += (shape.empty() ? "" : ",") + std::to_string(t.ne[i]);
        }
        json += ",\n " + json_str(t.name) + ":{\"dtype\":" + json_str(t.dtype) + ",\"shape\":[" + shape +
                "],\"data_offsets\":[" + std::to_string(offset) + "," + std::to_string(offset + n) + "]}";
        offset += n;
    }
    json += "}";
    put(name, raw<uint64_t>(json.size()) + json);
    return path_of(name);
}

struct Type { const char * name; uint32_t id; uint32_t block; uint32_t bytes; };
const Type kTypes[] = { { "F32", 0, 1, 4 }, { "F16", 1, 1, 2 }, { "Q4_0", 2, 32, 18 }, { "Q8_0", 8, 32, 34 }, { "BF16", 30, 1, 2 } };

const Type & type_of(const std::string & dtype) {
    for (const Type & t : kTypes) {
        if (dtype == t.name) return t;
    }
    std::abort();
}

std::string gstr(const std::string & s) {
    return raw<uint64_t>(s.size()) + s;
}

// A GGUF file: metadata (strings, and arrays to skip), tensor infos, no data.
std::string write_gguf(const std::string & name, const std::vector<T> & tensors,
                       const std::vector<std::pair<std::string, std::string>> & strings = {}) {
    std::string kv;
    uint64_t n_kv = 0;
    for (const auto & [k, v] : strings) {
        kv += gstr(k) + raw<uint32_t>(8) + gstr(v);
        n_kv++;
    }
    // A string array and a numeric array, which the reader has to step over.
    kv += gstr("tokenizer.ggml.tokens") + raw<uint32_t>(9) + raw<uint32_t>(8) + raw<uint64_t>(3) + gstr("a") + gstr("bb") + gstr("ccc");
    kv += gstr("tokenizer.ggml.scores") + raw<uint32_t>(9) + raw<uint32_t>(6) + raw<uint64_t>(3) + raw<float>(1) + raw<float>(2) + raw<float>(3);
    kv += gstr("general.file_type") + raw<uint32_t>(4) + raw<uint32_t>(7);
    kv += gstr("some.long.text") + raw<uint32_t>(8) + gstr(std::string(100000, 'x'));
    n_kv += 4;
    std::string infos;
    uint64_t offset = 0;
    for (const T & t : tensors) {
        const Type & ty = type_of(t.dtype);
        infos += gstr(t.name) + raw<uint32_t>((uint32_t) t.ne.size());
        uint64_t n = 1;
        for (int64_t d : t.ne) {
            infos += raw<uint64_t>((uint64_t) d);
            n *= (uint64_t) d;
        }
        infos += raw<uint32_t>(ty.id) + raw<uint64_t>(offset);
        offset += n / ty.block * ty.bytes;
    }
    put(name, std::string("GGUF") + raw<uint32_t>(3) + raw<uint64_t>(tensors.size()) + raw<uint64_t>(n_kv) + kv + infos);
    return path_of(name);
}

// ---------------------------------------------------------------------------
// The tensors of each family (just the names and shapes sd.cpp looks at)
// ---------------------------------------------------------------------------

const std::string kDm = "model.diffusion_model.";

std::vector<T> unet(int context_dim, bool tiny_mid = false, bool sdxl = false) {
    std::vector<T> t = {
        { kDm + "input_blocks.0.0.weight", { 3, 3, 4, 320 } },
        { kDm + "input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight", { context_dim, 320 } },
        { kDm + "output_blocks.7.1.transformer_blocks.0.attn1.to_k.weight", { 640, 640 } },
        { kDm + "output_blocks.3.1.transformer_blocks.1.attn1.to_k.weight", { 640, 640 } },
    };
    if (!tiny_mid) {
        t.push_back({ kDm + "middle_block.1.proj_in.weight", { 1280, 1280 } });
    }
    if (sdxl) {
        t.push_back({ kDm + "label_emb.0.0.weight", { 2816, 1280 } });
    }
    return t;
}

std::vector<T> vae_tensors(double gib = 0.16) {
    return { fat_gib("first_stage_model.decoder.conv_in.weight", gib) };
}

std::vector<T> sd1_checkpoint(double unet_gib = 1.7) {
    auto t = unet(768);
    t.push_back(fat_gib(kDm + "out.2.weight", unet_gib));
    t.push_back({ "cond_stage_model.transformer.text_model.embeddings.token_embedding.weight", { 768, 49408 } });
    t.push_back(fat_gib("cond_stage_model.transformer.text_model.encoder.layers.0.weight", 0.25));
    for (const T & v : vae_tensors()) t.push_back(v);
    return t;
}

std::vector<T> sd2_checkpoint(double unet_gib = 1.7) {
    auto t = unet(1024);
    t.push_back(fat_gib(kDm + "out.2.weight", unet_gib));
    t.push_back({ "cond_stage_model.model.token_embedding.weight", { 1024, 49408 } });
    t.push_back(fat_gib("cond_stage_model.model.transformer.resblocks.0.weight", 0.7));
    for (const T & v : vae_tensors()) t.push_back(v);
    return t;
}

std::vector<T> sdxl_checkpoint(double unet_gib = 5.1) {
    auto t = unet(2048, false, true);
    t.push_back(fat_gib(kDm + "out.2.weight", unet_gib));
    t.push_back({ "conditioner.embedders.0.transformer.text_model.embeddings.token_embedding.weight", { 768, 49408 } });
    t.push_back({ "conditioner.embedders.1.model.token_embedding.weight", { 1280, 49408 } });
    t.push_back(fat_gib("conditioner.embedders.1.model.transformer.resblocks.0.weight", 1.2));
    for (const T & v : vae_tensors()) t.push_back(v);
    return t;
}

std::vector<T> flux_network(double gib, bool guidance, const std::string & prefix = "", const std::string & dtype = "F16") {
    std::vector<T> t = {
        { prefix + "double_blocks.0.img_attn.qkv.weight", { 3072, 9216 }, dtype },
        { prefix + "img_in.weight", { 64, 3072 }, dtype },
        { prefix + "txt_in.weight", { 4096, 3072 }, dtype },
    };
    if (guidance) {
        t.push_back({ prefix + "guidance_in.in_layer.weight", { 256, 3072 }, dtype });
    }
    t.push_back(fat_gib(prefix + "single_blocks.0.linear1.weight", gib, dtype));
    return t;
}

// The components a Flux/SD3-style model gets as separate files.
PlanInput flux_input(const std::string & model, double t5_gib = 9.5) {
    PlanInput in;
    in.diffusion_model = model;
    in.vae = write_safetensors("ae.safetensors", { fat_gib("decoder.conv_in.weight", 0.16) });
    in.clip_l = write_safetensors("clip_l.safetensors", { fat_gib("text_model.encoder.layers.0.weight", 0.25) });
    in.t5xxl = write_safetensors("t5xxl_" + std::to_string((int) (t5_gib * 10)) + ".safetensors", { fat_gib("encoder.block.0.weight", t5_gib) });
    return in;
}

PlanInput llm_input(const std::string & model, double llm_gib = 2.4) {
    PlanInput in;
    in.diffusion_model = model;
    in.vae = write_safetensors("ae.safetensors", { fat_gib("decoder.conv_in.weight", 0.16) });
    in.llm = write_safetensors("llm.safetensors", { fat_gib("model.layers.0.weight", llm_gib) });
    return in;
}

// SD_PLAN_VERBOSE=1 prints every plan the tests make.
Plan plan_of(const PlanInput & in) {
    Plan p = plan_generation(in);
    if (std::getenv("SD_PLAN_VERBOSE")) {
        std::printf("plan (budget %.2f GiB): %s\n", (double) in.budget_bytes / (double) kGiB, p.summary().c_str());
    }
    return p;
}

PlanInput model_only(const std::string & path) {
    PlanInput in;
    in.model = path;
    return in;
}

bool has(const std::vector<std::string> & args, const std::string & flag, const std::string & value) {
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == flag && args[i + 1] == value) return true;
    }
    return false;
}

bool has_flag(const std::vector<std::string> & args, const std::string & flag) {
    for (const auto & a : args) {
        if (a == flag) return true;
    }
    return false;
}

bool warns(const Plan & p, const char * what) {
    return p.warning.find(what) != std::string::npos;
}

// ---------------------------------------------------------------------------

void test_readers() {
    // safetensors: the metadata block with quotes and braces, shapes reversed, sizes from offsets.
    const std::string st = write_safetensors("reader.safetensors", { { "a.weight", { 768, 49408 } }, { "b", { 3, 3, 4, 320 }, "F32" } });
    FileHeader h = read_file_header(st);
    CHECK(h.ok && h.format == "safetensors" && h.tensors.size() == 2);
    CHECK(h.tensors[0].name == "a.weight" && h.tensors[0].ne[0] == 768 && h.tensors[0].ne[1] == 49408);
    CHECK(h.tensors[0].bytes == 768ull * 49408 * 2 && h.tensors[0].dtype == "F16");
    CHECK(h.tensors[1].ne[2] == 4 && h.tensors[1].bytes == 3ull * 3 * 4 * 320 * 4 && h.tensors[1].dtype == "F32");
    CHECK(h.hint == "reader.safetensors");

    // GGUF: arrays and a long string skipped, types and sizes from ggml.
    const std::string gg = write_gguf("reader.gguf",
                                      { { "double_blocks.0.x", { 3072, 64 }, "Q4_0" }, { "b.bias", { 10 }, "F32" } },
                                      { { "general.name", "FLUX.1 Schnell" }, { "general.architecture", "flux" } });
    h = read_file_header(gg);
    CHECK(h.ok && h.format == "gguf" && h.tensors.size() == 2);
    CHECK(h.tensors[0].dtype == "Q4_0" && h.tensors[0].bytes == 3072 / 32 * 18 * 64);
    CHECK(h.tensors[1].dtype == "F32" && h.tensors[1].bytes == 40);
    CHECK(h.hint.find("flux.1 schnell") != std::string::npos && h.hint.find("flux") != std::string::npos);

    // Not a model, not there, a zip.
    put("garbage.bin", std::string(64, 'z'));
    CHECK(!read_file_header(path_of("garbage.bin")).ok);
    CHECK(!read_file_header(path_of("missing.safetensors")).ok && read_file_header(path_of("missing.safetensors")).file_bytes == 0);
    put("old.ckpt", "PK\x03\x04" + std::string(60, '\0'));
    h = read_file_header(path_of("old.ckpt"));
    CHECK(!h.ok && h.file_bytes == 64 && h.tensors.empty());
    put("trunc.safetensors", raw<uint64_t>(1000) + "{\"a\":");
    CHECK(!read_file_header(path_of("trunc.safetensors")).ok);
}

void test_families() {
    // SD 1.x
    Plan p = plan_of(model_only(write_safetensors("v1-5-pruned-emaonly.safetensors", sd1_checkpoint())));
    CHECK(p.ok && p.family == Family::SD1 && p.sd_version == "SD 1.x" && p.detected_by == "tensors");
    CHECK(p.settings.width == 512 && p.settings.height == 512 && p.settings.steps == 20);
    CHECK(p.settings.cfg_scale == 7.0f && p.settings.sampler == "euler_a" && p.settings.flash_attn);
    CHECK(!p.distilled && !p.settings.vae_tiling && p.settings.params_backend.empty() && p.fit == Fit::AllOnGpu);
    CHECK(p.warning.empty() && p.missing.empty() && p.model_is_checkpoint);
    CHECK(p.quant == "F16" && p.dtype_summary.find("F16 100%") == 0);
    // 1.7 GiB unet + 0.25 text + 0.16 vae, all F16 (the tiny tensors are rounding)
    CHECK(p.weight_bytes > 2.1 * kGiB && p.weight_bytes < 2.2 * kGiB);

    // The same as a GGUF, with the EMA copy that the loader never reads left out of the weights.
    auto with_ema = sd1_checkpoint();
    with_ema.push_back(fat_gib("model_ema.diffusion_model.out.2.weight", 3.0));
    p = plan_of(model_only(write_gguf("sd15-q.gguf", with_ema, { { "general.architecture", "sd1" } })));
    CHECK(p.ok && p.family == Family::SD1 && p.weight_bytes > 2.1 * kGiB && p.weight_bytes < 2.2 * kGiB);

    // SD 1.x pieces: inpaint (9 input channels), tiny UNet, SDXS-512-DS.
    auto inpaint = sd1_checkpoint();
    inpaint[0] = { kDm + "input_blocks.0.0.weight", { 3, 3, 9, 320 } };
    p = plan_of(model_only(write_safetensors("sd15-inpainting.safetensors", inpaint)));
    CHECK(p.ok && p.sd_version == "SD 1.x Inpaint" && p.needs_input && warns(p, "input image"));
    auto sdxs = sd1_checkpoint(0.5);
    sdxs = unet(768, true);
    sdxs.erase(sdxs.begin() + 2);  // no output_blocks.7.1: not the tiny UNet
    sdxs.push_back({ "cond_stage_model.transformer.text_model.embeddings.token_embedding.weight", { 768, 49408 } });
    sdxs.push_back(fat_gib("first_stage_model.decoder.conv_in.weight", 0.1));
    p = plan_of(model_only(write_safetensors("sdxs-512-dreamshaper.safetensors", sdxs)));
    CHECK(p.ok && p.family == Family::SDXS && p.sd_version == "SDXS (512-DS)");
    CHECK(p.settings.steps == 1 && p.settings.cfg_scale == 1.0f && p.settings.width == 512 && p.distilled);

    // SD 2.x: 768 by default, 512 for a base model.
    p = plan_of(model_only(write_safetensors("v2-1_768-ema-pruned.safetensors", sd2_checkpoint())));
    CHECK(p.ok && p.family == Family::SD2 && p.sd_version == "SD 2.x" && p.name == "SD 2.x" && !p.distilled);
    CHECK(p.settings.width == 768 && p.settings.steps == 20 && p.settings.cfg_scale == 7.0f);
    p = plan_of(model_only(write_safetensors("v2-1_512-base-ema.safetensors", sd2_checkpoint())));
    CHECK(p.settings.width == 512);

    // SD-Turbo is SD 2.x's network: the name says Turbo.
    p = plan_of(model_only(write_safetensors("sd_turbo.safetensors", sd2_checkpoint())));
    CHECK(p.ok && p.family == Family::SD2 && p.name == "SD-Turbo" && p.detected_by == "tensors+name");
    CHECK(p.distilled && p.settings.steps == 1 && p.settings.cfg_scale == 1.0f && p.settings.width == 512);
    CHECK(p.settings.sampler == "euler_a");

    // SDXL, and SDXL-Turbo by name.
    p = plan_of(model_only(write_safetensors("sd_xl_base_1.0.safetensors", sdxl_checkpoint())));
    CHECK(p.ok && p.family == Family::SDXL && p.sd_version == "SDXL");
    CHECK(p.settings.width == 1024 && p.settings.height == 1024 && p.settings.steps == 25 && p.settings.cfg_scale == 7.0f);
    CHECK(p.settings.vae_tiling);   // above 768
    p = plan_of(model_only(write_safetensors("sdxl_turbo_1.0_fp16.safetensors", sdxl_checkpoint())));
    CHECK(p.name == "SDXL-Turbo" && p.distilled && p.settings.steps == 1 && p.settings.cfg_scale == 1.0f);
    CHECK(p.settings.width == 512 && !p.settings.vae_tiling);
    // Variants of the XL network: inpaint, Vega (no middle block 1), SSD-1B.
    auto xl_inpaint = sdxl_checkpoint();
    xl_inpaint[0] = { kDm + "input_blocks.0.0.weight", { 3, 3, 9, 320 } };
    CHECK(plan_of(model_only(write_safetensors("xl_inp.safetensors", xl_inpaint))).sd_version == "SDXL Inpaint");
    auto vega = sdxl_checkpoint();
    vega.erase(vega.begin() + 4);   // middle_block.1
    vega.erase(vega.begin() + 3);   // output_blocks.3.1.transformer_blocks.1
    CHECK(plan_of(model_only(write_safetensors("vega.safetensors", vega))).sd_version == "SDXL (Vega)");
    auto ssd = sdxl_checkpoint();
    ssd.erase(ssd.begin() + 4);
    CHECK(plan_of(model_only(write_safetensors("ssd1b.safetensors", ssd))).sd_version == "SDXL (SSD1B)");

    // Few-step LoRAs merged in, by name.
    p = plan_of(model_only(write_safetensors("dreamshaper_lcm_v7.safetensors", sd1_checkpoint())));
    CHECK(p.distilled && p.settings.sampler == "lcm" && p.settings.steps == 4 && p.settings.cfg_scale == 1.0f);
    p = plan_of(model_only(write_safetensors("sdxl_lightning_4step.safetensors", sdxl_checkpoint())));
    CHECK(p.distilled && p.settings.steps == 4 && p.settings.cfg_scale == 1.0f && p.settings.sampler == "euler" &&
          p.settings.scheduler == "sgm_uniform");
    // A name that only contains "hyper" is not Hyper-SD.
    p = plan_of(model_only(write_safetensors("hyperrealism_v3.safetensors", sd1_checkpoint())));
    CHECK(!p.distilled && p.settings.steps == 20);

    // A UNet file without text encoders (--diffusion-model): told by the cross-attention width.
    auto bare = unet(768);
    bare.push_back(fat_gib(kDm + "out.2.weight", 1.7));
    PlanInput in;
    in.diffusion_model = write_safetensors("unet_only.safetensors", bare);
    p = plan_of(in);
    CHECK(p.ok && p.family == Family::SD1 && !p.model_is_checkpoint);
    CHECK(p.missing.size() == 2);
}

void test_missing_and_components() {
    // SD 1.x as a diffusion-only file: with the components given, nothing is missing.
    auto bare = unet(768);
    bare.push_back(fat_gib(kDm + "out.2.weight", 1.7));
    PlanInput in;
    in.diffusion_model = write_safetensors("unet_only2.safetensors", bare);
    Plan p = plan_of(in);
    CHECK(p.ok && p.missing.size() == 2 && p.missing[0] == "vae" && p.missing[1] == "clip_l");
    CHECK(warns(p, "needs vae, clip_l"));
    in.vae = write_safetensors("sd_vae.safetensors", { fat_gib("decoder.conv_in.weight", 0.16) });
    in.clip_l = write_safetensors("sd_clip_l.safetensors", { { "text_model.embeddings.token_embedding.weight", { 768, 49408 } }, fat_gib("text_model.encoder.layers.0.weight", 0.25) });
    p = plan_of(in);
    CHECK(p.ok && p.missing.empty() && p.warning.empty());
    CHECK(p.memory.vae_weights > 0.15 * kGiB && p.memory.text_weights > 0.25 * kGiB);
    auto args = p.to_args();
    CHECK(has(args, "--diffusion-model", in.diffusion_model) && has(args, "--vae", in.vae) && has(args, "--clip_l", in.clip_l));
    CHECK(!has_flag(args, "--model"));

    // Its own VAE file replaces the checkpoint's.
    PlanInput two;
    two.model = write_safetensors("sd15_ckpt.safetensors", sd1_checkpoint());
    const Plan plain = plan_of(two);
    two.vae = write_safetensors("fp16_vae.safetensors", { fat_gib("decoder.conv_in.weight", 0.3) });
    p = plan_of(two);
    CHECK(p.memory.vae_weights > 0.29 * kGiB && p.memory.vae_weights < 0.31 * kGiB && p.missing.empty());
    CHECK(plain.memory.vae_weights < 0.17 * kGiB);

    // TAESD: a tiny decode buffer, no tiling.
    PlanInput tae = model_only(write_safetensors("sdxl_tae.safetensors", sdxl_checkpoint()));
    tae.taesd = write_safetensors("taesdxl.safetensors", { fat_gib("decoder.conv_in.weight", 0.005) });
    p = plan_of(tae);
    CHECK(p.ok && !p.settings.vae_tiling && p.memory.compute_vae < 0.25 * kGiB && has(p.to_args(), "--taesd", tae.taesd));
}

void test_dit_families() {
    // FLUX.1: dev has the guidance embedder, schnell has none; GGUF with city96's bare names works too.
    PlanInput in = flux_input(write_safetensors("flux1-dev-fp8.safetensors", flux_network(5.0, true, "", "F16")));
    Plan p = plan_of(in);
    CHECK(p.ok && p.family == Family::Flux1 && p.name == "FLUX.1 dev" && p.sd_version == "Flux");
    CHECK(p.settings.steps == 20 && p.settings.cfg_scale == 1.0f && p.settings.sampler == "euler" && !p.distilled);
    CHECK(p.settings.width == 1024 && p.settings.vae_tiling && p.missing.empty());
    CHECK(p.memory.text_weights > 9.5 * kGiB && p.memory.diffusion_weights > 5.0 * kGiB);

    in = flux_input(write_gguf("flux1-schnell-Q4_0.gguf", flux_network(0.0, false, "", "F16"), { { "general.architecture", "flux" } }));
    p = plan_of(in);
    CHECK(p.ok && p.name == "FLUX.1 schnell" && p.distilled && p.settings.steps == 4 && p.settings.cfg_scale == 1.0f);
    CHECK(p.detected_by == "tensors+name");

    // Without the name, the tensors say it: no guidance_in is schnell, guidance_in is dev.
    p = plan_of(flux_input(write_gguf("flux-model-a.gguf", flux_network(0.0, false))));
    CHECK(p.name == "FLUX.1 schnell" && p.detected_by == "tensors");
    p = plan_of(flux_input(write_gguf("flux-model-b.gguf", flux_network(0.0, true))));
    CHECK(p.name == "FLUX.1 dev" && p.settings.steps == 20 && p.detected_by == "tensors");
    // sd.cpp-converted names (with the prefix) and a GGUF's Q4_0 weights in the summary.
    auto q4 = flux_network(0.0, true, kDm);
    q4.push_back({ kDm + "single_blocks.0.linear1.weight", { 32 * 100000, 100 }, "Q4_0" });
    p = plan_of(flux_input(write_gguf("flux1-dev-q4_0.gguf", q4)));
    CHECK(p.ok && p.name == "FLUX.1 dev" && p.quant == "Q4_0" && p.dtype_summary.find("Q4_0 ") == 0);

    // Fill, Control, Flex.2, Kontext: the dev network with other inputs.
    auto fill = flux_network(0.5, true);
    fill[1] = { "img_in.weight", { 384, 3072 } };
    p = plan_of(flux_input(write_safetensors("flux1-fill-dev.safetensors", fill)));
    CHECK(p.sd_version == "Flux Fill" && p.needs_input && p.name == "Flux Fill");
    p = plan_of(flux_input(write_safetensors("flux1-kontext-dev.safetensors", flux_network(0.5, true))));
    CHECK(p.name == "FLUX.1 Kontext" && p.needs_input);

    // Chroma: Flux's network with the distilled guidance layer.
    auto chroma = flux_network(0.5, false);
    chroma.push_back({ "distilled_guidance_layer.in_proj.weight", { 64, 5120 } });
    in = flux_input(write_safetensors("chroma-unlocked-v40.safetensors", chroma));
    p = plan_of(in);
    CHECK(p.ok && p.family == Family::Chroma && p.settings.steps == 20 && p.settings.cfg_scale == 4.0f && !p.distilled);
    CHECK(p.missing.empty());  // wants the T5 and the VAE: given
    PlanInput no_t5 = in;
    no_t5.t5xxl.clear();
    CHECK(plan_of(no_t5).missing.size() == 1 && plan_of(no_t5).missing[0] == "t5xxl");
    std::vector<T> radiance = { { kDm + "nerf_final_layer_conv.weight", { 3, 3, 64, 3 } }, fat_gib("blocks.0.weight", 0.5) };
    p = plan_of(model_only(write_safetensors("chroma-radiance.safetensors", radiance)));
    CHECK(p.family == Family::Chroma && p.sd_version == "Chroma Radiance" && p.missing.size() == 1 && p.missing[0] == "t5xxl");

    // SD3: a checkpoint with its encoders inside.
    std::vector<T> sd3 = { { kDm + "joint_blocks.0.x_block.attn.qkv.weight", { 1536, 4608 } }, fat_gib(kDm + "x_embedder.proj.weight", 4.0),
                           fat_gib("text_encoders.clip_l.transformer.text_model.encoder.layers.0.weight", 0.25),
                           fat_gib("text_encoders.clip_g.transformer.text_model.encoder.layers.0.weight", 1.3),
                           fat_gib("text_encoders.t5xxl.transformer.encoder.block.0.weight", 4.7),
                           fat_gib("first_stage_model.decoder.conv_in.weight", 0.16) };
    p = plan_of(model_only(write_safetensors("sd3_medium_incl_clips_t5xxlfp16.safetensors", sd3)));
    CHECK(p.ok && p.family == Family::SD3 && p.sd_version == "SD3.x" && p.settings.steps == 28 && p.settings.cfg_scale == 4.5f);
    CHECK(p.missing.empty() && p.model_is_checkpoint && p.memory.text_weights > 6.0 * kGiB);

    // FLUX.2: dev (48 single blocks) and klein.
    std::vector<T> f2 = { { kDm + "double_stream_modulation_img.lin.weight", { 6144, 6144 } }, fat_gib(kDm + "x.weight", 1.0) };
    p = plan_of(llm_input(write_safetensors("flux-2-klein-4b.safetensors", f2)));
    CHECK(p.family == Family::Flux2 && p.sd_version == "Flux.2 klein" && p.distilled && p.settings.steps == 4 && p.settings.cfg_scale == 1.0f);
    p = plan_of(llm_input(write_safetensors("flux-2-klein-base-4b.safetensors", f2)));
    CHECK(!p.distilled && p.settings.steps == 20 && p.settings.cfg_scale == 4.0f);
    f2.push_back({ kDm + "single_blocks.47.linear1.weight", { 6144, 100 } });
    p = plan_of(llm_input(write_safetensors("flux2-dev.safetensors", f2)));
    CHECK(p.sd_version == "Flux.2" && p.missing.empty());

    // Z-Image: Turbo by name; the Comfy Base name; a name that says nothing assumes Turbo with a note.
    std::vector<T> z = { { kDm + "cap_embedder.0.weight", { 2560, 3840 } }, fat_gib(kDm + "layers.0.weight", 3.0) };
    p = plan_of(llm_input(write_safetensors("z_image_turbo_bf16.safetensors", z)));
    CHECK(p.ok && p.family == Family::ZImage && p.name == "Z-Image-Turbo" && p.distilled && p.settings.steps == 8 &&
          p.settings.cfg_scale == 1.0f && p.warning.empty());
    p = plan_of(llm_input(write_safetensors("z_image_bf16.safetensors", z)));
    CHECK(p.name == "Z-Image Base" && !p.distilled && p.settings.steps == 20 && p.settings.cfg_scale == 5.0f);
    p = plan_of(llm_input(write_safetensors("model.safetensors", z)));
    CHECK(p.name == "Z-Image-Turbo" && p.settings.steps == 8 && warns(p, "assuming Z-Image-Turbo"));
    p = plan_of(llm_input(write_gguf("z_image_turbo-Q3_K.gguf", z, { { "general.name", "Z-Image-Turbo" } })));
    CHECK(p.name == "Z-Image-Turbo" && p.settings.steps == 8);

    // Qwen-Image (and Layered).
    std::vector<T> q = { { kDm + "transformer_blocks.0.img_mod.1.weight", { 3072, 18432 } }, { kDm + "img_in.weight", { 64, 3072 } },
                         fat_gib(kDm + "transformer_blocks.0.x.weight", 6.0) };
    p = plan_of(llm_input(write_safetensors("qwen-image-Q4_0.safetensors", q), 4.5));
    CHECK(p.ok && p.family == Family::QwenImage && p.sd_version == "Qwen Image" && p.settings.steps == 20 && p.settings.cfg_scale == 2.5f);
    q.push_back({ kDm + "time_text_embed.addition_t_embedding.weight", { 3072, 2 } });
    CHECK(plan_of(llm_input(write_safetensors("qwen-image-layered.safetensors", q))).sd_version == "Qwen Image Layered");

    // Wan: video; the app's image path does not make it.
    std::vector<T> wan = { { kDm + "blocks.0.cross_attn.norm_k.weight", { 1536 } }, { kDm + "patch_embedding.weight", { 1, 2, 2, 16, 1536 } },
                           fat_gib(kDm + "blocks.0.x.weight", 2.6) };
    p = plan_of(model_only(write_safetensors("wan2.1_t2v_1.3B_fp16.safetensors", wan)));
    CHECK(p.ok && p.family == Family::Wan && p.is_video && warns(p, "video model") && p.settings.width == 832 && p.settings.height == 480);
    CHECK(p.sd_version == "Wan 2.x");

    // Another image DiT sd.cpp knows: generic settings.
    std::vector<T> ovis = { { kDm + "double_blocks.0.img_mlp.gate_proj.weight", { 3072, 3072 } }, fat_gib("x", 1.0) };
    p = plan_of(llm_input(write_safetensors("ovis_image.safetensors", ovis)));
    CHECK(p.ok && p.family == Family::OtherImage && p.sd_version == "Ovis Image" && p.settings.cfg_scale == 5.0f);
}

void test_name_fallback() {
    // A PyTorch checkpoint: no header to read, so the name; the file's size is the weights.
    put("v1-5-pruned-emaonly.ckpt", "PK\x03\x04" + std::string(4000, '\0'));
    Plan p = plan_of(model_only(path_of("v1-5-pruned-emaonly.ckpt")));
    CHECK(p.ok && p.family == Family::SD1 && p.detected_by == "name" && p.weight_bytes == 4004);
    CHECK(warns(p, "PyTorch checkpoint"));
    put("something.ckpt", "PK\x03\x04" + std::string(40, '\0'));
    p = plan_of(model_only(path_of("something.ckpt")));
    CHECK(!p.ok && p.error.find("cannot tell") != std::string::npos);
    // A safetensors file whose tensors match nothing known.
    p = plan_of(model_only(write_safetensors("lora_thing.safetensors", { fat_gib("lora_unet_down.weight", 0.1) })));
    CHECK(!p.ok && p.error.find("not a model") != std::string::npos);
    // No model, a missing file.
    CHECK(!plan_of(PlanInput{}).ok);
    CHECK(!plan_of(model_only(path_of("nonexistent.safetensors"))).ok);
    CHECK(plan_of(model_only(path_of("nonexistent.safetensors"))).error.find("cannot open") != std::string::npos);
}

void test_budget() {
    const uint64_t big = 64 * kGiB;

    // The ladder: all on the GPU, text encoders on disk, everything on disk.
    PlanInput in = flux_input(write_safetensors("flux1-dev-q.safetensors", flux_network(6.4, true)));
    in.budget_bytes = big;
    const Plan ref = plan_of(in);
    CHECK(ref.ok && ref.fit == Fit::AllOnGpu && ref.settings.params_backend.empty() && ref.max_side == 2048);
    const int w = ref.settings.width, h = ref.settings.height;
    const bool tiled = ref.settings.vae_tiling;
    const uint64_t gpu  = estimate_peak(ref, w, h, Fit::AllOnGpu, tiled);
    const uint64_t te   = estimate_peak(ref, w, h, Fit::TextOnDisk, tiled);
    const uint64_t disk = estimate_peak(ref, w, h, Fit::AllOnDisk, tiled);
    CHECK(gpu > te && te > disk);
    CHECK(ref.memory.peak == gpu);

    in.budget_bytes = gpu;
    Plan p = plan_of(in);
    CHECK(p.fit == Fit::AllOnGpu && p.settings.params_backend.empty() && p.settings.width == w);
    in.budget_bytes = gpu - 1;
    p = plan_of(in);
    CHECK(p.fit == Fit::TextOnDisk && p.settings.params_backend == "te=disk" && p.settings.width == w);
    CHECK(has(p.to_args(), "--params-backend", "te=disk"));
    in.budget_bytes = te - 1;
    p = plan_of(in);
    CHECK(p.fit == Fit::AllOnDisk && p.settings.params_backend == "disk" && p.settings.width == w);
    CHECK(p.memory.peak == disk && p.warning.empty());
    CHECK(has(p.to_args(), "--params-backend", "disk"));

    // Less than that: the text encoders alone (T5 and CLIP, 9.7 GiB) need more
    // than the budget however small the picture, so nothing helps.
    in.budget_bytes = disk - 1;
    p = plan_of(in);
    CHECK(p.ok && p.fit == Fit::TooBig && warns(p, "won't fit") && p.max_side == 0);

    // With a small text encoder the diffusion model is what limits the size: a
    // smaller picture, the size said, and the VAE tiled.
    PlanInput small_te = flux_input(write_safetensors("flux1-dev-q.safetensors", flux_network(6.4, true)), 1.5);
    small_te.budget_bytes = big;
    const Plan sref = plan_of(small_te);
    CHECK(sref.fit == Fit::AllOnGpu && sref.settings.width == 1024);
    const uint64_t sdisk = estimate_peak(sref, 1024, 1024, Fit::AllOnDisk, true);
    small_te.budget_bytes = sdisk - 1;
    p = plan_of(small_te);
    CHECK(p.ok && p.fit != Fit::TooBig && p.fit != Fit::Segmented);
    CHECK(p.settings.width < 1024 && warns(p, "size reduced from 1024x1024") && p.max_side < 1024 && p.max_side >= 256);
    CHECK(p.memory.peak <= small_te.budget_bytes);
    CHECK(p.settings.width == p.settings.height && p.settings.width % 64 == 0);
    CHECK(p.max_side >= p.settings.width);
    // max_side grows with the budget.
    int previous = 0;
    for (uint64_t gib_budget : { 9, 10, 11, 12, 16, 24, 64 }) {
        small_te.budget_bytes = gib_budget * kGiB;
        const Plan q = plan_of(small_te);
        CHECK(q.max_side >= previous);
        previous = q.max_side;
    }

    // The default budget is 11.44 GiB less the display's share.
    CHECK(kDefaultBudgetBytes > 10.8 * kGiB && kDefaultBudgetBytes < 10.9 * kGiB);
    in.budget_bytes = kDefaultBudgetBytes;
    p = plan_of(in);
    CHECK(p.fit == Fit::AllOnDisk && p.settings.params_backend == "disk");   // 16 GiB of weights, T5 and all
    CHECK(p.warning.empty() && p.settings.width == 1024);

    // A budget that cannot hold even the text encoder: too big, said so.
    in.budget_bytes = 1 * kGiB;
    p = plan_of(in);
    CHECK(p.ok && p.fit == Fit::TooBig && p.max_side == 0 && warns(p, "won't fit") && p.memory.peak > in.budget_bytes);
    CHECK(p.settings.params_backend.empty());

    // A diffusion model bigger than the GPU: segmented, with a warning, at the native size.
    PlanInput heavy = flux_input(write_safetensors("flux1-dev-q8.safetensors", flux_network(12.0, true)), 4.9);
    heavy.budget_bytes = kDefaultBudgetBytes;
    p = plan_of(heavy);
    CHECK(p.ok && p.fit == Fit::Segmented && p.settings.params_backend == "disk" && warns(p, "segments") && p.settings.width == 1024);
    CHECK(p.max_side > 0);

    // SDXL on a small budget: tiling, a reduced size, then too big.
    PlanInput xl = model_only(write_safetensors("sdxl_budget.safetensors", sdxl_checkpoint()));
    xl.budget_bytes = big;
    const Plan xl_ref = plan_of(xl);
    CHECK(xl_ref.fit == Fit::AllOnGpu && xl_ref.settings.vae_tiling && xl_ref.max_side == 1536);
    // Memory is tight enough to tile a small picture, which would not otherwise tile.
    xl.width = xl.height = 512;
    xl.budget_bytes = big;
    Plan roomy = plan_of(xl);
    CHECK(!roomy.settings.vae_tiling && roomy.settings.width == 512);
    const uint64_t untiled = estimate_peak(roomy, 512, 512, Fit::AllOnGpu, false);
    CHECK(estimate_peak(roomy, 512, 512, Fit::AllOnGpu, true) < untiled);
    xl.budget_bytes = (uint64_t) ((double) untiled / 0.85);   // 15% to spare: no need
    CHECK(!plan_of(xl).settings.vae_tiling);
    xl.budget_bytes = untiled + (untiled / 100);              // 1% to spare: tile
    CHECK(plan_of(xl).settings.vae_tiling && plan_of(xl).fit == Fit::AllOnGpu);
    // Only tiling makes it fit.
    xl.budget_bytes = untiled - 1;
    CHECK(plan_of(xl).settings.vae_tiling && plan_of(xl).fit == Fit::AllOnGpu && plan_of(xl).warning.empty());
    xl.width = xl.height = 0;
    // The UNet is 5.1 GiB: the budget for 768x768 with the weights on disk.
    xl.budget_bytes = estimate_peak(xl_ref, 768, 768, Fit::AllOnDisk, false);
    p = plan_of(xl);
    CHECK(p.ok && p.fit == Fit::AllOnDisk && p.settings.width == 768);   // the UNet sets the peak: tiling would not help
    CHECK(p.max_side >= p.settings.width && p.max_side < 1024 && warns(p, "size reduced"));
    // Less than the UNet and its smallest buffers: segmented, or too big.
    xl.budget_bytes = 4 * kGiB;
    p = plan_of(xl);
    CHECK(p.ok && (p.fit == Fit::TooBig || p.fit == Fit::Segmented));
    xl.budget_bytes = 200ull << 20;
    p = plan_of(xl);
    CHECK(p.fit == Fit::TooBig && p.max_side == 0 && warns(p, "won't fit"));

    // A size asked for that does not fit is kept, with a warning.
    xl.budget_bytes = estimate_peak(xl_ref, 768, 768, Fit::AllOnDisk, false);
    xl.width = xl.height = 1536;
    p = plan_of(xl);
    CHECK(p.settings.width == 1536 && p.fit == Fit::TooBig && p.max_side == 768 && warns(p, "won't fit at 1536x1536") && warns(p, "768 px"));
}

void test_args() {
    // The arguments go through gen_args.cpp's parser into the options sd.cpp is given.
    PlanInput in = flux_input(write_safetensors("flux1-schnell.safetensors", flux_network(3.0, false)));
    in.budget_bytes = 40 * kGiB;
    Plan p = plan_of(in);
    std::vector<std::string> args = p.to_args();
    CHECK(has(args, "--diffusion-model", in.diffusion_model) && has(args, "--t5xxl", in.t5xxl) && has(args, "--vae", in.vae));
    CHECK(has(args, "-W", "1024") && has(args, "-H", "1024") && has(args, "--steps", "4") && has(args, "--cfg-scale", "1"));
    CHECK(has(args, "--sampling-method", "euler") && has(args, "--flash-attn", "on") && has_flag(args, "--vae-tiling"));
    CHECK(!has_flag(args, "--scheduler") && !has_flag(args, "--params-backend"));

    // gen_args.cpp does not know --clip_g and --params-backend yet: drop those pairs and the rest must parse.
    auto parse = [](std::vector<std::string> a, GenOptions & o, std::string & error) {
        std::vector<std::string> known;
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i] == "--clip_g" || a[i] == "--params-backend") {
                i++;
            } else {
                known.push_back(a[i]);
            }
        }
        return parse_gen_args(known, o, error);
    };
    GenOptions o;
    std::string error;
    CHECK(parse(args, o, error));
    CHECK(error.empty() && o.diffusion_model == in.diffusion_model && o.width == 1024 && o.steps == 4 &&
          o.cfg_scale == 1.0f && o.sample_method == EULER_SAMPLE_METHOD && o.vae_tiling && o.flash_attn == "on");

    // Every sampler and scheduler a plan names is one sd.cpp knows.
    for (const char * name : { "sd_turbo.safetensors", "dreamshaper_lcm.safetensors", "sdxl_lightning.safetensors", "sd15.safetensors" }) {
        const Plan q = plan_of(model_only(write_safetensors(name, std::string(name).find("sdxl") == 0 ? sdxl_checkpoint() : std::string(name).find("sd_turbo") == 0 ? sd2_checkpoint() : sd1_checkpoint())));
        GenOptions qo;
        std::string qe;
        CHECK(parse(q.to_args(), qo, qe));
        CHECK(qe.empty());
        CHECK(qo.sample_method == str_to_sample_method(q.settings.sampler.c_str()));
        if (!q.settings.scheduler.empty()) {
            CHECK(qo.scheduler == str_to_scheduler(q.settings.scheduler.c_str()) && qo.scheduler != SCHEDULER_COUNT);
        }
        CHECK(has(q.to_args(), "--model", path_of(name)));
    }

    // An SD-Turbo checkpoint plans what gen_args.hpp's defaults are (512, 1 step, cfg 1, euler_a).
    p = plan_of(model_only(write_safetensors("sd_turbo.safetensors", sd2_checkpoint())));
    GenOptions def, turbo;
    CHECK(parse(p.to_args(), turbo, error));
    CHECK(turbo.width == def.width && turbo.height == def.height && turbo.steps == def.steps &&
          turbo.cfg_scale == def.cfg_scale && turbo.sample_method == def.sample_method && !turbo.vae_tiling);

    // SDXL-sized: a tiled VAE and a params backend ride along.
    PlanInput xl = model_only(write_safetensors("sdxl_args.safetensors", sdxl_checkpoint()));
    p = plan_of(xl);   // 1024x1024: the VAE is tiled
    args = p.to_args();
    CHECK(has_flag(args, "--vae-tiling") && p.settings.width == 1024);
    CHECK(!p.summary().empty() && p.summary().find("SDXL") != std::string::npos);
    CHECK(plan_of(PlanInput{}).summary().find("no plan") == 0);

    // clip_g and a checkpoint plus a separate diffusion model.
    PlanInput both;
    both.model = write_safetensors("sd3_ckpt.safetensors", sdxl_checkpoint(0.5));
    both.diffusion_model = write_safetensors("sdxl_unet.safetensors", unet(2048, false, true));
    both.clip_g = write_safetensors("clip_g.safetensors", { fat_gib("text_model.encoder.layers.0.weight", 1.3) });
    args = plan_of(both).to_args();
    CHECK(has(args, "--model", both.model) && has(args, "--diffusion-model", both.diffusion_model) && has(args, "--clip_g", both.clip_g));
}

void test_fp8_and_names() {
    // 8-bit float weights are counted at F16 size.
    std::vector<T> fp8 = flux_network(0.0, true);
    fp8.push_back({ "single_blocks.0.linear1.weight", { 1000000000 }, "F8_E4M3" });
    const Plan p = plan_of(flux_input(write_safetensors("flux1-dev-fp8.safetensors", fp8)));
    CHECK(p.ok && p.memory.diffusion_weights >= 2000000000ull && p.memory.diffusion_weights < 2100000000ull);
    CHECK(warns(p, "expanded to F16") && p.quant == "F8_E4M3");
    CHECK(family_name(Family::Flux1) == std::string("FLUX.1") && std::string(fit_name(Fit::AllOnDisk)) == "weights on disk");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/ps5sd_plan_testXXXXXX";
    const char * dir = mkdtemp(tmpl);
    if (!dir) {
        std::printf("FAIL: no temp dir\n");
        return 1;
    }
    g_dir = dir;

    test_readers();
    test_families();
    test_missing_and_components();
    test_dit_families();
    test_name_fallback();
    test_budget();
    test_args();
    test_fp8_and_names();

    for (const auto & f : g_files) {
        unlink(f.c_str());
    }
    rmdir(g_dir.c_str());
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
