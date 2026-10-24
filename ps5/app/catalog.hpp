// The starter catalog: models a user can install with one click. Every URL
// and size was checked from a PC with `curl -I -L` (2026-10-19; the
// inpainting and Wan entries 2026-10-21, the Wan TAEHV entry 2026-10-22); the files
// are public, none needs a token. Installing an entry downloads its files
// into /data/PS5SD/models (dest) and then writes its preset,
// /data/PS5SD/presets/<id>.txt, so it shows in GET /ps5sd/v1/app/models.
// Files that are there already at their size (a VAE two entries share) are
// not fetched again. Featured entries are the ones the web UI shows first.
//
// Sizes against the console's 11.44 GiB pool (docs/CONSOLE.md): SDXL with its
// VAE is 5.4 GB in Q8_0 (the full f16 checkpoint is converted to Q8_0 while
// it loads, --type q8_0: in f16 the model manager streams weights during
// sampling and a step takes 2.5 times as long); FLUX.3 Klein Q4_K, Qwen3-VL-4B
// Q4_K_M and the FLUX.2 VAE are 6.3 GB and leave 4 GiB free after a request;
// Z-Image Turbo Q4_K with Qwen3-4B Q4_K_M and the FLUX.1 VAE is 6.7 GB.
// FLUX.2 Klein 4B Q8_0 with Qwen3-4B Q4_K_M and the FLUX.2 VAE is 7.1 GB and leaves 3.9 GiB free
// after a 1024x1024 image. FLUX.2 Klein 9B Q4_0 with Qwen3-8B Q4_K_M and the same VAE is 11.0 GB and
// left 0.9 GiB in one block after a 1024x1024 image (2026-10-24).
// Wan 2.1 T2V 1.3B (f16) with umt5-xxl Q4_K_M and the Wan 2.1 VAE is 6.7 GB;
// the VAE decodes in 256 px tiles at an overlap of 0.25 (since 0.9.1; 128 px at 0.5 before, which
// took twice as long, and 256 px ran out of memory before patches/mesa/0010).
// The SDXL presets sample with dpm++2m and the ays schedule at 10 steps and decode in 640 px tiles
// (since 0.9.2; euler_a at 25 steps and 512 px before, about 70 s an image instead of 30).
// SDXL Q8_0 with the Canny ControlNet (2.5 GB more) left 2.7 GiB free after a 1024x1024 image.
//
// Included by download_core.cpp only.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "download_core.hpp"

namespace ps5sd {
namespace catalog_data {

// The SDXL VAE with the fp16 fix: madebyollin's sdxl_vae.safetensors, under
// the name the presets use.
#define PS5SD_SDXL_VAE                                                                                    \
    { "https://huggingface.co/madebyollin/sdxl-vae-fp16-fix/resolve/main/sdxl_vae.safetensors",           \
      "vae/sdxl_vae_fp16_fix.safetensors", 334641162ull, 0 }
// FLUX.3 Klein's text encoder and the FLUX.2 VAE (Comfy-Org's copy: the one of
// black-forest-labs/FLUX.2-dev is gated). Black Forest Labs' own
// ae.safetensors (336,211,292 bytes, the same weights with another header)
// counts as installed too.
#define PS5SD_KLEIN_LLM                                                                                   \
    { "https://huggingface.co/Qwen/Qwen3-VL-4B-Instruct-GGUF/resolve/main/Qwen3VL-4B-Instruct-Q4_K_M.gguf", \
      "text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf", 2497281664ull, 0 }
#define PS5SD_KLEIN_VAE                                                                                   \
    { "https://huggingface.co/Comfy-Org/flux2-dev/resolve/main/split_files/vae/flux2-vae.safetensors",     \
      "vae/flux2-vae.safetensors", 336213556ull, 336211292ull }

// Qwen3-4B Instruct 2507, the text encoder of Z-Image Turbo and FLUX.2 Klein 4B (which was trained
// with Qwen3-4B; the Instruct 2507 weights work as well, measured 2026-10-22).
#define PS5SD_QWEN3_4B                                                                                    \
    { "https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/"                          \
      "Qwen3-4B-Instruct-2507-Q4_K_M.gguf",                                                               \
      "text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf", 2497281120ull, 0 }

// Presets that an older PS5SD wrote for a catalog entry, byte for byte as it wrote them.
// update_catalog_presets gives such a preset the entry's current text; one that someone changed
// stays as it is.
inline std::vector<std::pair<const char *, const char *>> old_presets() {
    return {
        // up to 0.9.0: 128 px tiles at sd.cpp's overlap of 0.5, which decodes each pixel about three
        // times (docs/CONSOLE.md, "Faster video decoding")
        { "wan21-t2v-1.3b",
          "# name: Wan 2.1 T2V 1.3B (video)\n"
          "# defaults: steps=20 cfg=6 width=832 height=480 sampler=euler\n"
          "--diffusion-model\n"
          "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors\n"
          "--t5xxl\n"
          "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf\n"
          "--vae\n"
          "vae/wan_2.1_vae.safetensors\n"
          "--vae-tiling\n"
          "--vae-tile-size\n"
          "128\n"
          "--flow-shift\n"
          "3\n"
          "--video-frames\n"
          "33\n"
          "--fps\n"
          "16\n" },
        // up to 0.9.1: euler_a at 25 steps (dpm++2m with the ays schedule needs 10 for the same picture), 512 px
        // tiles (640 px decode a 1024x1024 image in 4 tiles instead of 9), and the weights loaded at the first
        // image instead of at the start (docs/CONSOLE.md, "Faster SDXL")
        { "sdxl-full",
          "# name: SDXL Base 1.0\n"
          "# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n"
          "-m\n"
          "sd_xl_base_1.0.safetensors\n"
          "--vae\n"
          "vae/sdxl_vae_fp16_fix.safetensors\n"
          "--type\n"
          "q8_0\n"
          "--vae-tiling\n"
          "--vae-tile-size\n"
          "512\n" },
        { "sdxl-base",
          "# name: SDXL Base 1.0 (Q8_0)\n"
          "# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n"
          "-m\n"
          "stable-diffusion-xl-base-1.0-Q8_0.gguf\n"
          "--vae\n"
          "vae/sdxl_vae_fp16_fix.safetensors\n"
          "--vae-tiling\n"
          "--vae-tile-size\n"
          "512\n" },
    };
}

inline std::vector<CatalogEntry> entries() {
    return {
        {
            "sdxl-full",
            "SDXL Base 1.0",
            "The classic, works with LoRAs and finetunes. About 30 s an image, 11 s with the DMD2 LoRA.",
            {
                { "https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/resolve/main/"
                  "sd_xl_base_1.0.safetensors",
                  "sd_xl_base_1.0.safetensors", 6938078334ull, 0 },
                PS5SD_SDXL_VAE,
            },
            "# name: SDXL Base 1.0\n"
            "# defaults: steps=10 cfg=6 width=1024 height=1024 sampler=dpm++2m scheduler=ays\n"
            "-m\n"
            "sd_xl_base_1.0.safetensors\n"
            "--vae\n"
            "vae/sdxl_vae_fp16_fix.safetensors\n"
            "--type\n"
            "q8_0\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "640\n"
            "--eager-load\n",
            true,
            "LoRAs, finetunes, classic SDXL look",
        },
        {
            "flux3-klein-q4",
            "FLUX.3 Image Klein (Q4_K)",
            "Best quality, writes text, slower: about 2 min an image.",
            {
                { "https://huggingface.co/leejet/flux-3-image-klein-GGUF/resolve/main/flux3-image-klein-Q4_K.gguf",
                  "diffusion_models/flux3-image-klein-Q4_K.gguf", 3514023104ull, 0 },
                PS5SD_KLEIN_LLM,
                PS5SD_KLEIN_VAE,
            },
            "# name: FLUX.3 Image Klein (Q4_K)\n"
            "# defaults: steps=16 cfg=1 width=1024 height=1024 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/flux3-image-klein-Q4_K.gguf\n"
            "--llm\n"
            "text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/flux2-vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            true,
            "best detail and text in images",
        },
        {
            "z-image-turbo",
            "Z-Image Turbo (Q4_K)",
            "Photoreal in 8 steps, about 75 s an image.",
            {
                { "https://huggingface.co/leejet/Z-Image-Turbo-GGUF/resolve/main/z_image_turbo-Q4_K.gguf",
                  "diffusion_models/z_image_turbo-Q4_K.gguf", 3864250304ull, 0 },
                PS5SD_QWEN3_4B,
                // The FLUX.1 VAE: Comfy-Org's copy (black-forest-labs/FLUX.1-schnell answers 401).
                { "https://huggingface.co/Comfy-Org/z_image_turbo/resolve/main/split_files/vae/ae.safetensors",
                  "vae/flux1-ae.safetensors", 335304388ull, 0 },
            },
            "# name: Z-Image Turbo (Q4_K)\n"
            "# defaults: steps=8 cfg=1 width=1024 height=1024 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/z_image_turbo-Q4_K.gguf\n"
            "--llm\n"
            "text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/flux1-ae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            true,
            "fast photoreal images",
        },
        {
            "flux2-klein-4b",
            "FLUX.2 Klein 4B (Q8_0)",
            "Fast and detailed: 1024x1024 in 4 steps, about 30 s an image. Apache 2.0. 7.1 GB.",
            {
                { "https://huggingface.co/leejet/FLUX.2-klein-4B-GGUF/resolve/main/flux-2-klein-4b-Q8_0.gguf",
                  "diffusion_models/flux-2-klein-4b-Q8_0.gguf", 4300629440ull, 0 },
                PS5SD_QWEN3_4B,
                PS5SD_KLEIN_VAE,
            },
            "# name: FLUX.2 Klein 4B (Q8_0)\n"
            "# defaults: steps=4 cfg=1 width=1024 height=1024 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/flux-2-klein-4b-Q8_0.gguf\n"
            "--llm\n"
            "text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/flux2-vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            true,
            "fast, detailed images",
        },
        {
            "flux2-klein-9b",
            "FLUX.2 Klein 9B (Q4_0)",
            "The larger Klein: more detail than the 4B, 1024x1024 in 4 steps, about 50 s an image (100 s the first "
            "after a start). Works with FLUX.2 Klein 9B LoRAs. FLUX Non-Commercial License. 11.0 GB.",
            {
                { "https://huggingface.co/leejet/FLUX.2-klein-9B-GGUF/resolve/main/flux-2-klein-9b-Q4_0.gguf",
                  "diffusion_models/flux-2-klein-9b-Q4_0.gguf", 5616208032ull, 0 },
                // Qwen3-8B, the text encoder Klein 9B was trained with.
                { "https://huggingface.co/unsloth/Qwen3-8B-GGUF/resolve/main/Qwen3-8B-Q4_K_M.gguf",
                  "text_encoders/Qwen3-8B-Q4_K_M.gguf", 5027784512ull, 0 },
                PS5SD_KLEIN_VAE,
            },
            "# name: FLUX.2 Klein 9B (Q4_0)\n"
            "# defaults: steps=4 cfg=1 width=1024 height=1024 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/flux-2-klein-9b-Q4_0.gguf\n"
            "--llm\n"
            "text_encoders/Qwen3-8B-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/flux2-vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            true,
            "most detailed fast images",
        },
        {
            "flux3-klein-q6",
            "FLUX.3 Image Klein (Q6_K)",
            "FLUX.3 Klein in Q6_K: closer to the original than Q4_K, with the same text encoder and VAE. 896x896 in about 2 min; 1024x1024 fits right after a start of PS5SD, 1536x1536 does not. 7.7 GB.",
            {
                { "https://huggingface.co/leejet/flux-3-image-klein-GGUF/resolve/main/flux3-image-klein-Q6_K.gguf",
                  "diffusion_models/flux3-image-klein-Q6_K.gguf", 4874083520ull, 0 },
                PS5SD_KLEIN_LLM,
                PS5SD_KLEIN_VAE,
            },
            "# name: FLUX.3 Image Klein (Q6_K)\n"
            "# defaults: steps=16 cfg=1 width=896 height=896 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/flux3-image-klein-Q6_K.gguf\n"
            "--llm\n"
            "text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/flux2-vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            false,
            "",
        },
        {
            "sdxl-base",
            "SDXL Base 1.0 (Q8_0)",
            "SDXL already in Q8_0 (gpustack's GGUF), with the fp16-fix VAE; loads faster than the full one. About 30 s an image. 5.4 GB.",
            {
                { "https://huggingface.co/gpustack/stable-diffusion-xl-base-1.0-GGUF/resolve/main/"
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf",
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf", 5040957760ull, 0 },
                PS5SD_SDXL_VAE,
            },
            "# name: SDXL Base 1.0 (Q8_0)\n"
            "# defaults: steps=10 cfg=6 width=1024 height=1024 sampler=dpm++2m scheduler=ays\n"
            "-m\n"
            "stable-diffusion-xl-base-1.0-Q8_0.gguf\n"
            "--vae\n"
            "vae/sdxl_vae_fp16_fix.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "640\n"
            "--eager-load\n",
            false,
            "",
        },
        {
            "sdxl-canny",
            "SDXL Base 1.0 (Q8_0) + Canny ControlNet",
            "SDXL with a ControlNet: the edges of a guide image (Canny) set the layout of the new one. 7.9 GB.",
            {
                { "https://huggingface.co/gpustack/stable-diffusion-xl-base-1.0-GGUF/resolve/main/"
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf",
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf", 5040957760ull, 0 },
                PS5SD_SDXL_VAE,
                // xinsir's Canny ControlNet for SDXL, V2 (Apache 2.0), in the diffusers layout sd.cpp reads.
                { "https://huggingface.co/xinsir/controlnet-canny-sdxl-1.0/resolve/main/"
                  "diffusion_pytorch_model_V2.safetensors",
                  "controlnet/xinsir-canny-sdxl-v2.safetensors", 2502139104ull, 0 },
            },
            "# name: SDXL Base 1.0 (Q8_0) + Canny ControlNet\n"
            "# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n"
            "-m\n"
            "stable-diffusion-xl-base-1.0-Q8_0.gguf\n"
            "--vae\n"
            "vae/sdxl_vae_fp16_fix.safetensors\n"
            "--control-net\n"
            "controlnet/xinsir-canny-sdxl-v2.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            false,
            "",
        },
        {
            "sdxl-inpainting",
            "SDXL Inpainting 0.1 (Q8_0)",
            "For inpainting: blends a painted-over part into SDXL images better than a normal model. 5.2 GB.",
            {
                { "https://huggingface.co/gpustack/stable-diffusion-xl-inpainting-1.0-GGUF/resolve/main/"
                  "stable-diffusion-xl-inpainting-1.0-Q8_0.gguf",
                  "stable-diffusion-xl-inpainting-1.0-Q8_0.gguf", 4873718560ull, 0 },
                PS5SD_SDXL_VAE,
            },
            "# name: SDXL Inpainting 0.1 (Q8_0)\n"
            "# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n"
            "-m\n"
            "stable-diffusion-xl-inpainting-1.0-Q8_0.gguf\n"
            "--vae\n"
            "vae/sdxl_vae_fp16_fix.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
            false,
            "",
        },
        {
            "sd15-inpainting",
            "SD 1.5 Inpainting (Q8_0)",
            "For inpainting at 512x512: Stable Diffusion 1.5's inpainting model. 1.9 GB.",
            {
                { "https://huggingface.co/gpustack/stable-diffusion-v1-5-inpainting-GGUF/resolve/main/"
                  "stable-diffusion-v1-5-inpainting-Q8_0.gguf",
                  "stable-diffusion-v1-5-inpainting-Q8_0.gguf", 1881270304ull, 0 },
            },
            "# name: SD 1.5 Inpainting (Q8_0)\n"
            "# defaults: steps=25 cfg=7 width=512 height=512 sampler=euler_a\n"
            "-m\n"
            "stable-diffusion-v1-5-inpainting-Q8_0.gguf\n",
            false,
            "",
        },
        {
            "sdxs-512",
            "SDXS-512 (Q8_0)",
            "Tiny and fast: 512x512 in one step, about 0.3 s an image once loaded. 683 MB.",
            {
                { "https://huggingface.co/concedo/sdxs-512-tinySDdistilled-GGUF/resolve/main/"
                  "sdxs-512-tinySDdistilled_Q8_0.gguf",
                  "sdxs-512-tinySDdistilled_Q8_0.gguf", 682847200ull, 0 },
            },
            "# name: SDXS-512 (Q8_0)\n"
            "# defaults: steps=1 cfg=1 width=512 height=512 sampler=euler_a\n"
            "-m\n"
            "sdxs-512-tinySDdistilled_Q8_0.gguf\n",
            false,
            "",
        },
        {
            "sd-turbo",
            "SD-Turbo (Q8_0)",
            "Stable Diffusion 2.1 distilled for 1 to 4 steps at 512x512, about 2 s an image. 2.0 GB.",
            {
                { "https://huggingface.co/Green-Sky/SD-Turbo-GGUF/resolve/main/sd_turbo-f16-q8_0.gguf",
                  "sd_turbo-f16-q8_0.gguf", 2023745376ull, 0 },
            },
            "# name: SD-Turbo (Q8_0)\n"
            "# defaults: steps=1 cfg=1 width=512 height=512 sampler=euler_a\n"
            "-m\n"
            "sd_turbo-f16-q8_0.gguf\n",
            false,
            "",
        },
        {
            "wan21-t2v-1.3b",
            "Wan 2.1 T2V 1.3B (video)",
            "Short videos from a prompt: 33 frames (2 s) at 832x480 in about 2 min 15 s with the CausVid LoRA, 81 frames (5 s) in about 8 min. 6.7 GB.",
            {
                { "https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged/resolve/main/split_files/"
                  "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors",
                  "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors", 2838303560ull, 0 },
                { "https://huggingface.co/city96/umt5-xxl-encoder-gguf/resolve/main/umt5-xxl-encoder-Q4_K_M.gguf",
                  "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf", 3655145312ull, 0 },
                { "https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged/resolve/main/split_files/vae/"
                  "wan_2.1_vae.safetensors",
                  "vae/wan_2.1_vae.safetensors", 253815318ull, 0 },
            },
            "# name: Wan 2.1 T2V 1.3B (video)\n"
            "# defaults: steps=20 cfg=6 width=832 height=480 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors\n"
            "--t5xxl\n"
            "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/wan_2.1_vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "256\n"
            "--vae-tile-overlap\n"
            "0.25\n"
            "--flow-shift\n"
            "3\n"
            "--video-frames\n"
            "33\n"
            "--fps\n"
            "16\n",
            true,
            "short videos",
        },
        // The same model with TAEHV (madebyollin, MIT; lightx2v's copy on Hugging Face, the same file):
        // the decode of 33 frames in 3.6 s instead of 55 s, a little softer (docs/CONSOLE.md, "Faster video
        // decoding"). Tiled from the start: whole, it asks for 20 GB and sd.cpp falls back to tiles only
        // after two failed allocations. lightx2v's lighttaew2_1 came out washed out and striped in sd.cpp.
        {
            "wan21-t2v-1.3b-tae",
            "Wan 2.1 T2V 1.3B, fast decoder (video)",
            "Wan 2.1 with a small decoder: 33 frames (2 s) in about 1 min 30 s with the CausVid LoRA, 81 frames in about 6 min; a little softer. Shares the model files, 23 MB more.",
            {
                { "https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged/resolve/main/split_files/"
                  "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors",
                  "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors", 2838303560ull, 0 },
                { "https://huggingface.co/city96/umt5-xxl-encoder-gguf/resolve/main/umt5-xxl-encoder-Q4_K_M.gguf",
                  "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf", 3655145312ull, 0 },
                { "https://huggingface.co/lightx2v/Autoencoders/resolve/main/taew2_1.safetensors",
                  "vae/taew2_1.safetensors", 22642902ull, 0 },
            },
            "# name: Wan 2.1 T2V 1.3B, fast decoder (video)\n"
            "# defaults: steps=20 cfg=6 width=832 height=480 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors\n"
            "--t5xxl\n"
            "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf\n"
            "--taesd\n"
            "vae/taew2_1.safetensors\n"
            "--vae-tiling\n"
            "--temporal-tiling\n"
            "--vae-tile-size\n"
            "256\n"
            "--vae-tile-overlap\n"
            "0.25\n"
            "--flow-shift\n"
            "3\n"
            "--video-frames\n"
            "33\n"
            "--fps\n"
            "16\n",
            false,
            "",
        },
        // Wan 2.2 TI2V 5B (QuantStack's Q8_0) with its own VAE: a start image as the first frame. Measured on
        // the console (docs/CONSOLE.md, "Image to video"): 1280x704 with 33 frames 36 s a step and 196 s to
        // decode, 832x480 11.7 s and 88 s. From a prompt alone it needs 1280x704; smaller comes out washed out.
        {
            "wan22-ti2v-5b",
            "Wan 2.2 TI2V 5B (video from a picture)",
            "Video from a picture, which is the first frame, or from a prompt: 33 frames at 1280x704 in about 16 min, at 832x480 from a picture in about 6 min. 10.5 GB.",
            {
                { "https://huggingface.co/QuantStack/Wan2.2-TI2V-5B-GGUF/resolve/main/Wan2.2-TI2V-5B-Q8_0.gguf",
                  "diffusion_models/Wan2.2-TI2V-5B-Q8_0.gguf", 5400179040ull, 0 },
                { "https://huggingface.co/city96/umt5-xxl-encoder-gguf/resolve/main/umt5-xxl-encoder-Q4_K_M.gguf",
                  "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf", 3655145312ull, 0 },
                { "https://huggingface.co/Comfy-Org/Wan_2.2_ComfyUI_Repackaged/resolve/main/split_files/vae/"
                  "wan2.2_vae.safetensors",
                  "vae/wan2.2_vae.safetensors", 1409400960ull, 0 },
            },
            "# name: Wan 2.2 TI2V 5B (video from a picture)\n"
            "# defaults: steps=20 cfg=5 width=1280 height=704 sampler=euler\n"
            "--diffusion-model\n"
            "diffusion_models/Wan2.2-TI2V-5B-Q8_0.gguf\n"
            "--t5xxl\n"
            "text_encoders/umt5-xxl-encoder-Q4_K_M.gguf\n"
            "--vae\n"
            "vae/wan2.2_vae.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "256\n"
            "--vae-tile-overlap\n"
            "0.25\n"
            "--flow-shift\n"
            "5\n"
            "--video-frames\n"
            "33\n"
            "--fps\n"
            "24\n",
            true,
            "video from a picture",
        },
    };
}

#undef PS5SD_SDXL_VAE
#undef PS5SD_KLEIN_LLM
#undef PS5SD_KLEIN_VAE

}  // namespace catalog_data
}  // namespace ps5sd
