// Host test for net_family.cpp: the network of a model or LoRA from tensor
// names and shapes (as the real files have them), both file formats, and
// ComfyUI's quantized weights.
//
//   test_net_family                 the checks
//   test_net_family FILE...         prints the family, part and format of each file (for a look at real files)
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_family.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "json.hpp"

using namespace ps5sd;
using T = std::vector<NetTensor>;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void is(const T & t, const std::string & want, const std::string & what) {
    const std::string got = net_family_of(t);
    check(got == want, what + ": \"" + got + "\", not \"" + want + "\"");
}

// A LoRA pair for one layer: down [rank, in], up [out, rank].
void lora(T & t, const std::string & base, int64_t in, int64_t out, const char * down = ".lora_down.weight",
          const char * up = ".lora_up.weight") {
    t.push_back({ base + down, { 16, in } });
    t.push_back({ base + up, { out, 16 } });
}

void test_flux() {
    T flux1;  // kohya FLUX.1 LoRA, all 19/38 blocks
    for (int i = 0; i < 19; i++) {
        lora(flux1, "lora_unet_double_blocks_" + std::to_string(i) + "_img_attn_qkv", 3072, 9216);
        lora(flux1, "lora_unet_double_blocks_" + std::to_string(i) + "_img_mlp_0", 3072, 12288);
    }
    for (int i = 0; i < 38; i++) {
        lora(flux1, "lora_unet_single_blocks_" + std::to_string(i) + "_linear1", 3072, 21504);
    }
    is(flux1, "FLUX.1", "kohya FLUX.1 LoRA");

    T realism;  // double blocks only, qkv only (flux_realism)
    for (int i = 0; i < 19; i++) {
        lora(realism, "diffusion_model.double_blocks." + std::to_string(i) + ".img_attn.qkv", 3072, 9216);
    }
    is(realism, "FLUX.1", "double blocks only, by the count");

    T klein;  // FLUX.2 Klein 4B LoRA: 5/20 blocks, the gated MLP
    for (int i = 0; i < 5; i++) {
        lora(klein, "diffusion_model.double_blocks." + std::to_string(i) + ".img_attn.qkv", 3072, 9216);
        lora(klein, "diffusion_model.double_blocks." + std::to_string(i) + ".img_mlp.0", 3072, 18432);
    }
    for (int i = 0; i < 20; i++) {
        lora(klein, "diffusion_model.single_blocks." + std::to_string(i) + ".linear1", 3072, 27648);
    }
    is(klein, "FLUX.2 Klein 4B", "FLUX.2 Klein 4B LoRA");

    T klein_model;  // the model itself (GGUF names, weights)
    klein_model.push_back({ "double_blocks.0.img_attn.qkv.weight", { 9216, 3072 } });
    klein_model.push_back({ "double_blocks.4.img_mlp.0.weight", { 18432, 3072 } });
    klein_model.push_back({ "single_blocks.19.linear1.weight", { 27648, 3072 } });
    is(klein_model, "FLUX.2 Klein 4B", "FLUX.2 Klein 4B model");

    T singles;  // single blocks only: linear1 says which
    lora(singles, "lora_unet_single_blocks_3_linear1", 3072, 27648);
    is(singles, "FLUX.2 Klein 4B", "single blocks only, by linear1");

    T diffusers;  // diffusers FLUX.1 LoRA (iphone15)
    for (int i = 0; i < 19; i++) {
        lora(diffusers, "transformer.transformer_blocks." + std::to_string(i) + ".attn.to_q", 3072, 3072, ".lora_A.weight", ".lora_B.weight");
        lora(diffusers, "transformer.transformer_blocks." + std::to_string(i) + ".ff.net.0.proj", 3072, 12288, ".lora_A.weight", ".lora_B.weight");
    }
    lora(diffusers, "transformer.single_transformer_blocks.0.proj_mlp", 3072, 12288, ".lora_A.weight", ".lora_B.weight");
    is(diffusers, "FLUX.1", "diffusers FLUX.1 LoRA");

    T klein9;  // PEFT FLUX.2 Klein 9B LoRA in diffusers names, no prefix (lenovo_flux_klein9b): 8/24 blocks
    for (int i = 0; i < 8; i++) {
        lora(klein9, "transformer_blocks." + std::to_string(i) + ".attn.to_q", 4096, 4096, ".lora_A.default.weight",
             ".lora_B.default.weight");
        lora(klein9, "transformer_blocks." + std::to_string(i) + ".ff.linear_in", 4096, 24576, ".lora_A.default.weight",
             ".lora_B.default.weight");
    }
    for (int i = 0; i < 24; i++) {
        lora(klein9, "single_transformer_blocks." + std::to_string(i) + ".attn.to_qkv_mlp_proj", 4096, 36864,
             ".lora_A.default.weight", ".lora_B.default.weight");
    }
    is(klein9, "FLUX.2 Klein 9B", "PEFT diffusers FLUX.2 Klein 9B LoRA");

    T klein9_singles;  // its single blocks alone: the fused projection is 9x wide
    lora(klein9_singles, "single_transformer_blocks.0.attn.to_qkv_mlp_proj", 4096, 36864, ".lora_A.default.weight",
         ".lora_B.default.weight");
    is(klein9_singles, "FLUX.2 Klein 9B", "diffusers FLUX.2 single blocks only");

    T sd3;  // diffusers transformer_blocks without single blocks, another width: not FLUX
    lora(sd3, "transformer.transformer_blocks.0.attn.to_q", 1536, 1536, ".lora_A.weight", ".lora_B.weight");
    lora(sd3, "transformer.transformer_blocks.0.ff.net.0.proj", 1536, 6144, ".lora_A.weight", ".lora_B.weight");
    check(net_family_of(sd3).compare(0, 4, "FLUX") != 0, "diffusers SD 3 blocks are not FLUX");

    T flux3;
    flux3.push_back({ "content_mode_blocks.0.attn.qkv.weight", { 9216, 3072 } });
    flux3.push_back({ "single_blocks.27.mlp_in.weight", { 18432, 3072 } });
    is(flux3, "FLUX.3 Klein", "FLUX.3 Klein model");
}

void test_unets() {
    T sdxl;  // kohya SDXL LoRA
    lora(sdxl, "lora_unet_input_blocks_4_1_transformer_blocks_0_attn2_to_k", 2048, 640);
    lora(sdxl, "lora_unet_input_blocks_4_1_transformer_blocks_0_attn1_to_q", 640, 640);
    is(sdxl, "SDXL", "kohya SDXL LoRA");

    T sdxl_ckpt;  // a whole checkpoint, its text encoders included
    sdxl_ckpt.push_back({ "model.diffusion_model.input_blocks.4.1.transformer_blocks.0.attn2.to_k.weight", { 640, 2048 } });
    sdxl_ckpt.push_back({ "conditioner.embedders.1.model.transformer.resblocks.0.attn.in_proj_weight", { 3840, 1280 } });
    is(sdxl_ckpt, "SDXL", "SDXL checkpoint");

    T sd15;  // diffusers SD 1.5 LoRA (PEFT)
    lora(sd15, "unet.down_blocks.0.attentions.0.transformer_blocks.0.attn2.to_k", 768, 320, ".lora_A.weight", ".lora_B.weight");
    is(sd15, "SD 1.5", "diffusers SD 1.5 LoRA");

    T sd21;
    sd21.push_back({ "model.diffusion_model.output_blocks.3.1.transformer_blocks.0.attn2.to_v.weight", { 1280, 1024 } });
    is(sd21, "SD 2.1", "SD 2.1 checkpoint");

    T te_only;  // a LoRA for the text encoder alone cannot be told
    lora(te_only, "lora_te1_text_model_encoder_layers_0_self_attn_q_proj", 768, 768);
    is(te_only, "", "text encoder only");
}

void test_others() {
    T causvid;  // Wan 2.1 1.3B LoRA
    for (int i = 0; i < 30; i++) {
        lora(causvid, "diffusion_model.blocks." + std::to_string(i) + ".self_attn.q", 1536, 1536);
        lora(causvid, "diffusion_model.blocks." + std::to_string(i) + ".cross_attn.k", 1536, 1536);
    }
    is(causvid, "Wan 1.3B", "Wan 1.3B LoRA");

    T wan14;
    wan14.push_back({ "blocks.39.self_attn.q.weight", { 5120, 5120 } });
    is(wan14, "Wan 14B", "Wan 14B model");

    T zimage;
    zimage.push_back({ "layers.29.attention.qkv.weight", { 11520, 3840 } });
    zimage.push_back({ "noise_refiner.0.attention.qkv.weight", { 11520, 3840 } });
    is(zimage, "Z-Image", "Z-Image model");

    T sd3;
    sd3.push_back({ "model.diffusion_model.joint_blocks.0.x_block.attn.qkv.weight", { 4608, 1536 } });
    is(sd3, "SD 3", "SD 3 model");

    // MOSS-Image: lodestones' 8-step DMD LoRA (kohya names) and the model (sd.cpp's GGUF names)
    T moss_lora;
    for (int i = 0; i < 2; i++) {
        lora(moss_lora, "diffusion_model.blocks." + std::to_string(i) + ".proj.q", 3072, 3072);
        lora(moss_lora, "diffusion_model.blocks." + std::to_string(i) + ".proj.ffn_up_gate", 3072, 16384);
        lora(moss_lora, "diffusion_model.blocks." + std::to_string(i) + ".proj.ffn_down", 8192, 3072);
    }
    is(moss_lora, "MOSS-Image", "MOSS-Image LoRA");
    T moss_kohya;
    lora(moss_kohya, "lora_unet_blocks_3_proj_ffn_up_gate", 3072, 16384);
    is(moss_kohya, "MOSS-Image", "MOSS-Image LoRA, kohya underscores");
    T moss_model;
    moss_model.push_back({ "video_embedder.weight", { 3072, 128 } });
    moss_model.push_back({ "shared_block_modulation.weight", { 18432, 3072 } });
    moss_model.push_back({ "blocks.0.proj.qkv.weight", { 9216, 3072 } });
    is(moss_model, "MOSS-Image", "MOSS-Image model");

    // Krea 2: the model (realrebelai's GGUF and Comfy-Org's safetensors have the same names) and a kohya LoRA
    T krea2;
    krea2.push_back({ "first.weight", { 3072, 64 } });
    krea2.push_back({ "txtfusion.layerwise_blocks.0.attn.wq.weight", { 2560, 2560 } });
    krea2.push_back({ "txtfusion.projector.weight", { 3072, 2560 } });
    krea2.push_back({ "blocks.0.attn.wq.weight", { 3072, 3072 } });
    krea2.push_back({ "blocks.0.mlp.gate.weight", { 8192, 3072 } });
    krea2.push_back({ "blocks.0.mod.lin.weight", { 18432, 3072 } });
    krea2.push_back({ "tmlp.0.weight", { 3072, 256 } });
    is(krea2, "Krea 2", "Krea 2 model");
    T krea2_lora;
    lora(krea2_lora, "lora_unet_blocks_7_attn_wq", 3072, 3072);
    lora(krea2_lora, "lora_unet_blocks_7_mlp_gate", 3072, 8192);
    is(krea2_lora, "Krea 2", "Krea 2 LoRA, kohya underscores");

    is(T{}, "", "no tensors");
}

void test_files() {
    namespace fs        = std::filesystem;
    const fs::path dir  = fs::temp_directory_path() / "ps5sd-net-family";
    fs::create_directories(dir);

    // safetensors: u64 header length, JSON header (with __metadata__ that says the wrong family), data
    const nlohmann::json head = {
        { "__metadata__", { { "ss_base_model_version", "sd_v1" } } },
        { "lora_unet_double_blocks_0_img_attn_qkv.lora_down.weight", { { "dtype", "F16" }, { "shape", { 16, 3072 } }, { "data_offsets", { 0, 0 } } } },
        { "lora_unet_double_blocks_0_img_mlp_0.lora_up.weight", { { "dtype", "F16" }, { "shape", { 12288, 16 } }, { "data_offsets", { 0, 0 } } } },
    };
    const std::string h  = head.dump();
    const uint64_t    hn = h.size();
    const fs::path    st = dir / "flux1.safetensors";
    {
        std::ofstream f(st, std::ios::binary);
        f.write(reinterpret_cast<const char *>(&hn), 8);
        f << h;
    }
    check(net_family(st.u8string()) == "FLUX.1", "safetensors file, its metadata ignored");
    check(net_family(st.u8string()) == "FLUX.1", "the second look (cached)");

    // GGUF v3: two metadata values (a string and a string array), then the tensor infos (ggml order)
    const fs::path gg = dir / "klein.gguf";
    {
        std::ofstream f(gg, std::ios::binary);
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
        auto u64 = [&](uint64_t v) { f.write(reinterpret_cast<const char *>(&v), 8); };
        auto str = [&](const std::string & s) { u64(s.size()); f << s; };
        f << "GGUF";
        u32(3);
        u64(2);  // tensors
        u64(2);  // metadata
        str("general.architecture");
        u32(8);
        str("flux");
        str("tokenizer.list");
        u32(9);
        u32(8);
        u64(2);
        str("a");
        str("bc");
        str("double_blocks.0.img_mlp.0.weight");
        u32(2);
        u64(3072);
        u64(18432);
        u32(8);
        u64(0);
        str("double_blocks.0.img_attn.qkv.weight");
        u32(2);
        u64(3072);
        u64(9216);
        u32(8);
        u64(0);
    }
    check(net_family(gg.u8string()) == "FLUX.2 Klein 4B", "GGUF file");

    const fs::path junk = dir / "junk.ckpt";
    std::ofstream(junk, std::ios::binary) << "PK\x03\x04 not a header";
    check(net_family(junk.u8string()).empty(), "a .ckpt cannot be told");
    check(net_family((dir / "missing.gguf").u8string()).empty(), "a missing file");
    fs::remove_all(dir);
}

// A .safetensors file from a header and the data of its .comfy_quant tensors (name -> JSON); the other
// tensors get no bytes. cut: leave the data out, as a download that stopped would.
void write_safetensors(const std::filesystem::path & path, nlohmann::json head,
                       const std::vector<std::pair<std::string, std::string>> & quant = {}, bool cut = false) {
    std::string data;
    for (const auto & q : quant) {
        head[q.first] = { { "dtype", "U8" }, { "shape", { q.second.size() } },
                          { "data_offsets", { data.size(), data.size() + q.second.size() } } };
        data += q.second;
    }
    const std::string h  = head.dump();
    const uint64_t    hn = h.size();
    std::ofstream     f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(&hn), 8);
    f << h;
    if (!cut) {
        f << data;
    }
}

nlohmann::json tensor(const char * dtype, std::vector<int64_t> shape) {
    return { { "dtype", dtype }, { "shape", shape }, { "data_offsets", { 0, 0 } } };
}

// The weight format (net_format), with the layouts of the files people bring from ComfyUI (issue 25).
void test_formats() {
    namespace fs       = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ps5sd-net-format";
    fs::create_directories(dir);
    const auto want = [&](const char * file, const std::string & label, bool loads, const std::string & what) {
        const NetFormat f = net_format((dir / file).u8string());
        check(f.label == label, what + ": \"" + f.label + "\", not \"" + label + "\"");
        check(f.loads == loads, what + (loads ? ": does not load" : ": loads"));
        check(label.empty() == f.note.empty(), what + ": a note when there is a format, none else");
    };
    const std::string int8 = R"({"format": "int8_tensorwise", "convrot": true, "convrot_groupsize": 256})";

    // RunningHub's MOSS-Image INT8 convrot: I8 weights, a scale and a .comfy_quant for each layer
    write_safetensors(dir / "moss_int8.safetensors",
                      { { "blocks.0.proj.ffn_up_gate.weight", tensor("I8", { 16384, 3072 }) },
                        { "blocks.0.proj.ffn_up_gate.weight_scale", tensor("F32", { 16384, 1 }) },
                        { "blocks.0.proj.qkv.weight", tensor("I8", { 9216, 3072 }) },
                        { "blocks.0.proj.qkv.weight_scale", tensor("F32", { 9216, 1 }) },
                        { "shared_block_modulation.weight", tensor("BF16", { 18432, 3072 }) } },
                      { { "blocks.0.proj.ffn_up_gate.comfy_quant", int8 }, { "blocks.0.proj.qkv.comfy_quant", int8 } });
    want("moss_int8.safetensors", "ComfyUI INT8", false, ".comfy_quant INT8 (ends the title on the PS5)");
    check(net_family((dir / "moss_int8.safetensors").u8string()) == "MOSS-Image", "INT8 MOSS-Image: its family");

    // the same weights with the format only in __metadata__ (toxicdog's MOSS-Image SFT Turbo INT8 ConvRot)
    const std::string layers = R"({"layers": {"blocks.0.proj.qkv": {"format": "int8_tensorwise", "convrot": true}}})";
    write_safetensors(dir / "meta_int8.safetensors",
                      { { "__metadata__", { { "format", "pt" }, { "_quantization_metadata", layers } } },
                        { "blocks.0.proj.qkv.weight", tensor("I8", { 9216, 3072 }) },
                        { "blocks.0.proj.qkv.weight_scale", tensor("F32", { 9216, 1 }) } });
    want("meta_int8.safetensors", "ComfyUI INT8", false, "INT8 named in the metadata only");

    // Comfy-Org's Krea 2 Turbo NVFP4: U8 weights with an F8 block scale and an F32 second scale
    const std::string nvfp4 = R"({"layers": {"blocks.0.attn.wq": {"format": "nvfp4"}}})";
    write_safetensors(dir / "krea2_nvfp4.safetensors",
                      { { "__metadata__", { { "_quantization_metadata", nvfp4 } } },
                        { "blocks.0.attn.wq.weight", tensor("U8", { 3072, 1536 }) },
                        { "blocks.0.attn.wq.weight_scale", tensor("F8_E4M3", { 3072, 192 }) },
                        { "blocks.0.attn.wq.weight_scale_2", tensor("F32", {}) } });
    want("krea2_nvfp4.safetensors", "ComfyUI NVFP4", false, "NVFP4");
    check(net_part((dir / "krea2_nvfp4.safetensors").u8string()) == "diffusion model", "NVFP4 Krea 2: a diffusion model");

    // Comfy-Org's Krea 2 Turbo FP8 scaled
    const std::string fp8 = R"({"layers": {"blocks.0.attn.wq": {"format": "float8_e4m3fn"}}})";
    write_safetensors(dir / "krea2_fp8.safetensors",
                      { { "__metadata__", { { "_quantization_metadata", fp8 } } },
                        { "blocks.0.attn.wq.weight", tensor("F8_E4M3", { 3072, 3072 }) },
                        { "blocks.0.attn.wq.weight_scale", tensor("F32", {}) } });
    want("krea2_fp8.safetensors", "FP8 (scaled)", true, "FP8 scaled");

    // an FP8 file as older tools wrote it: F8 weights, nothing else
    write_safetensors(dir / "fp8.safetensors", { { "double_blocks.0.img_attn.qkv.weight", tensor("F8_E4M3", { 9216, 3072 }) } });
    want("fp8.safetensors", "FP8", true, "plain FP8");

    // Qwen3.5 9B W4A8 GPTQ: packed I8 weights with a codebook and F8 group scales (no FP8 weight)
    write_safetensors(dir / "qwen_w4a8.safetensors",
                      { { "model.layers.0.mlp.up_proj.weight", tensor("I8", { 12288, 2048 }) },
                        { "model.layers.0.mlp.up_proj.weight_codebook", tensor("F32", { 16 }) },
                        { "model.layers.0.mlp.up_proj.weight_s_channel", tensor("F32", { 12288 }) },
                        { "model.layers.0.mlp.up_proj.weight_s_rel", tensor("F8_E4M3", { 12288, 256 }) } },
                      { { "model.layers.0.mlp.up_proj.comfy_quant", R"({"format": "asym_w4a8_int8", "group_size": 16})" } });
    want("qwen_w4a8.safetensors", "ComfyUI W4A8", false, "W4A8 (the same INT8 path)");

    // unsloth's FLUX.3 Klein FP8 and INT8: torchao layers (_weight_qdata, _weight_scale, _weight_zero_point)
    write_safetensors(dir / "torchao_fp8.safetensors",
                      { { "mode_blocks.image.0.attn.to_k._weight_qdata", tensor("F8_E4M3", { 3072, 3072 }) },
                        { "mode_blocks.image.0.attn.to_k._weight_scale", tensor("F32", { 3072, 1 }) },
                        { "mode_blocks.image.0.attn.norm_q.weight", tensor("BF16", { 128 }) } });
    want("torchao_fp8.safetensors", "torchao FP8", false, "torchao FP8");
    write_safetensors(dir / "torchao_int8.safetensors",
                      { { "mode_blocks.image.0.attn.to_k._weight_qdata", tensor("I8", { 3072, 3072 }) },
                        { "mode_blocks.image.0.attn.to_k._weight_scale", tensor("F32", { 3072, 1 }) },
                        { "mode_blocks.image.0.attn.to_k._weight_zero_point", tensor("I8", { 3072, 1 }) } });
    want("torchao_int8.safetensors", "torchao INT8", false, "torchao INT8");

    // I8 weights and nothing that says what they are (a diffusers INT8 export)
    write_safetensors(dir / "bare_int8.safetensors", { { "transformer_blocks.0.attn.to_q.weight", tensor("I8", { 3072, 3072 }) },
                                                       { "transformer_blocks.0.attn.norm_q.weight", tensor("BF16", { 128 }) } });
    want("bare_int8.safetensors", "INT8", false, "I8 weights without a format");

    // BFL's FLUX.2 Klein 4B FP8: scalar scales, an input scale (sd.cpp does not need it)
    const std::string bfl = R"({"format_version": "1.0", "layers": {"double_blocks.0.img_attn.proj": {"format": "float8_e4m3fn"}}})";
    write_safetensors(dir / "bfl_fp8.safetensors",
                      { { "__metadata__", { { "_quantization_metadata", bfl } } },
                        { "double_blocks.0.img_attn.proj.weight", tensor("F8_E4M3", { 3072, 3072 }) },
                        { "double_blocks.0.img_attn.proj.weight_scale", tensor("F32", {}) },
                        { "double_blocks.0.img_attn.proj.input_scale", tensor("F32", {}) } });
    want("bfl_fp8.safetensors", "FP8 (scaled)", true, "BFL FP8");

    // a .comfy_quant whose bytes are missing
    write_safetensors(dir / "cut.safetensors", { { "blocks.0.proj.qkv.weight", tensor("I8", { 9216, 3072 }) } },
                      { { "blocks.0.proj.qkv.comfy_quant", int8 } }, true);
    want("cut.safetensors", "ComfyUI quantized", false, "a cut .comfy_quant");

    // ordinary files
    write_safetensors(dir / "bf16.safetensors", { { "blocks.0.attn.wq.weight", tensor("BF16", { 3072, 3072 }) },
                                                  { "blocks.0.norm.scale", tensor("F32", { 3072 }) } });
    want("bf16.safetensors", "", true, "BF16");
    want("missing.safetensors", "", true, "a missing file");
    fs::remove_all(dir);
}

// What part of a model a file is (net_part_of), with the names the real files use.
void test_parts() {
    const auto part = [](const T & t, const std::string & want, const std::string & what) {
        const std::string got = net_part_of(t);
        check(got == want, what + ": part \"" + got + "\", not \"" + want + "\"");
    };
    part({ { "model.diffusion_model.input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight", { 320, 768 } },
           { "cond_stage_model.transformer.text_model.encoder.layers.0.self_attn.q_proj.weight", { 768, 768 } },
           { "first_stage_model.decoder.conv_in.weight", { 512, 4, 3, 3 } } },
         "", "SD 1.5 checkpoint");
    part({ { "model.diffusion_model.input_blocks.4.1.transformer_blocks.0.attn2.to_k.weight", { 640, 2048 } },
           { "conditioner.embedders.1.model.transformer.resblocks.0.attn.in_proj_weight", { 3840, 1280 } } },
         "", "SDXL checkpoint");
    part({ { "model.diffusion_model.double_blocks.0.img_attn.qkv.weight", { 9216, 3072 } },
           { "text_encoders.t5xxl.transformer.encoder.block.0.layer.0.SelfAttention.q.weight", { 4096, 4096 } } },
         "", "FLUX.1 all in one");
    part({ { "double_blocks.0.img_attn.qkv.weight", { 9216, 3072 } }, { "double_blocks.0.img_mlp.0.weight", { 18432, 3072 } } },
         "diffusion model", "FLUX.2 Klein diffusion model");
    part({ { "layers.0.attention.to_q.weight", { 3840, 3840 } }, { "noise_refiner.0.attention.to_q.weight", { 3840, 3840 } } },
         "diffusion model", "Z-Image diffusion model");
    part({ { "blocks.0.self_attn.q.weight", { 1536, 1536 } }, { "blocks.0.cross_attn.k.weight", { 1536, 4096 } } },
         "diffusion model", "Wan diffusion model");
    part({ { "token_embd.weight", { 151936, 2560 } }, { "blk.0.attn_q.weight", { 4096, 2560 } } }, "text encoder",
         "Qwen3 GGUF");
    part({ { "model.layers.0.self_attn.q_proj.weight", { 4096, 2560 } } }, "text encoder", "Qwen3 safetensors");
    part({ { "encoder.block.0.layer.0.SelfAttention.q.weight", { 4096, 4096 } } }, "text encoder", "T5");
    part({ { "text_model.encoder.layers.0.self_attn.q_proj.weight", { 768, 768 } } }, "text encoder", "CLIP-L");
    part({ { "decoder.conv_in.weight", { 512, 16, 3, 3 } }, { "encoder.conv_in.weight", { 128, 3, 3, 3 } } }, "VAE",
         "FLUX VAE");
    part({ { "decoder.conv1.weight", { 384, 16, 3, 3, 3 } }, { "decoder.upsamples.0.residual.0.gamma", { 384 } } }, "VAE",
         "Wan VAE");
    part({ { "txtfusion.projector.weight", { 3072, 2560 } }, { "blocks.0.attn.wq.weight", { 3072, 3072 } } },
         "diffusion model", "Krea 2 diffusion model");
    part({}, "", "nothing to tell");
}

}  // namespace

int main(int argc, char ** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            const std::string f = net_family(argv[i]), p = net_part(argv[i]);
            const NetFormat   m = net_format(argv[i]);
            std::printf("%-18s %-16s %-20s %s\n", f.empty() ? "?" : f.c_str(), p.empty() ? "-" : p.c_str(),
                        m.label.empty() ? "-" : (m.label + (m.loads ? "" : " (no)")).c_str(), argv[i]);
        }
        return 0;
    }
    test_flux();
    test_unets();
    test_others();
    test_files();
    test_formats();
    test_parts();
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
