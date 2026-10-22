// Host test for net_family.cpp: the network of a model or LoRA from tensor
// names and shapes (as the real files have them), and both file formats.
//
//   test_net_family                 the checks
//   test_net_family FILE...         prints the family of each file (for a look at real files)
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
    part({}, "", "nothing to tell");
}

}  // namespace

int main(int argc, char ** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            const std::string f = net_family(argv[i]), p = net_part(argv[i]);
            std::printf("%-18s %-16s %s\n", f.empty() ? "?" : f.c_str(), p.empty() ? "-" : p.c_str(), argv[i]);
        }
        return 0;
    }
    test_flux();
    test_unets();
    test_others();
    test_files();
    test_parts();
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
