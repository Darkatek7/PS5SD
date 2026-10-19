// The starter catalog: models a user can install with one click. Every URL
// and size was checked from a PC with `curl -I -L` (2026-10-19); the files
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

inline std::vector<CatalogEntry> entries() {
    return {
        {
            "sdxl-full",
            "SDXL Base 1.0",
            "The classic, works with LoRAs and finetunes.",
            {
                { "https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/resolve/main/"
                  "sd_xl_base_1.0.safetensors",
                  "sd_xl_base_1.0.safetensors", 6938078334ull, 0 },
                PS5SD_SDXL_VAE,
            },
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
            "512\n",
            true,
            "LoRAs, finetunes, classic SDXL look",
        },
        {
            "flux3-klein-q4",
            "FLUX.3 Image Klein (Q4_K)",
            "Best quality, writes text, slower.",
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
            "Fast photoreal, 8 steps.",
            {
                { "https://huggingface.co/leejet/Z-Image-Turbo-GGUF/resolve/main/z_image_turbo-Q4_K.gguf",
                  "diffusion_models/z_image_turbo-Q4_K.gguf", 3864250304ull, 0 },
                { "https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/main/"
                  "Qwen3-4B-Instruct-2507-Q4_K_M.gguf",
                  "text_encoders/Qwen3-4B-Instruct-2507-Q4_K_M.gguf", 2497281120ull, 0 },
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
            "flux3-klein-q6",
            "FLUX.3 Image Klein (Q6_K)",
            "FLUX.3 Klein in Q6_K: closer to the original than Q4_K, with the same text encoder and VAE. 7.7 GB.",
            {
                { "https://huggingface.co/leejet/flux-3-image-klein-GGUF/resolve/main/flux3-image-klein-Q6_K.gguf",
                  "diffusion_models/flux3-image-klein-Q6_K.gguf", 4874083520ull, 0 },
                PS5SD_KLEIN_LLM,
                PS5SD_KLEIN_VAE,
            },
            "# name: FLUX.3 Image Klein (Q6_K)\n"
            "# defaults: steps=16 cfg=1 width=1024 height=1024 sampler=euler\n"
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
            "SDXL already in Q8_0 (gpustack's GGUF), with the fp16-fix VAE; loads faster than the full one. 5.4 GB.",
            {
                { "https://huggingface.co/gpustack/stable-diffusion-xl-base-1.0-GGUF/resolve/main/"
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf",
                  "stable-diffusion-xl-base-1.0-Q8_0.gguf", 5040957760ull, 0 },
                PS5SD_SDXL_VAE,
            },
            "# name: SDXL Base 1.0 (Q8_0)\n"
            "# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n"
            "-m\n"
            "stable-diffusion-xl-base-1.0-Q8_0.gguf\n"
            "--vae\n"
            "vae/sdxl_vae_fp16_fix.safetensors\n"
            "--vae-tiling\n"
            "--vae-tile-size\n"
            "512\n",
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
            "Stable Diffusion 2.1 distilled for 1 to 4 steps at 512x512. 2.0 GB.",
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
    };
}

#undef PS5SD_SDXL_VAE
#undef PS5SD_KLEIN_LLM
#undef PS5SD_KLEIN_VAE

}  // namespace catalog_data
}  // namespace ps5sd
