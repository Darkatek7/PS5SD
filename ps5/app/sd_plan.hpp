// Generation plans for the PS5SD app: what model a file holds, what it takes
// to run it on the console's GPU, and the settings that suit it.
//
// A plan is made from the files' headers alone (GGUF metadata and tensor
// infos; the safetensors length word and JSON header), no weights are read.
// From the tensor names it finds the model family the way stable-diffusion.cpp
// does (ModelLoader::get_sd_version, src/model_loader.cpp), adds what the file
// name says that the tensors cannot (Turbo, schnell, LCM: same network, other
// weights), sums the weight bytes per component, estimates the compute
// buffers at the chosen size, and picks where the weights live (all on the
// GPU, the text encoders on disk, everything on disk, or segmented) so that
// it fits the memory left. The result is the settings, a warning when it
// does not fit, and the arguments for gen_args.cpp's parser.
//
// The memory constants (sd_plan.cpp, "kMeasure...") are estimates from
// sd.cpp's logs on PCs and from the model shapes. They are marked "to be
// measured on the console"; Phase 3 calibrates them.
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5sd {

constexpr uint64_t kGiB = 1ull << 30;

// The unified device heap RADV reports with radv_enable_unified_heap_on_apu
// (docs/PORTING.md), and what the display (ps5-opengl's buffers) and the
// driver keep of it.
constexpr uint64_t kDeviceHeapBytes     = 11 * kGiB + (kGiB * 44) / 100;  // 11.44 GiB
constexpr uint64_t kDisplayReserveBytes = (kGiB * 6) / 10;                // ~0.6 GiB
constexpr uint64_t kDefaultBudgetBytes  = kDeviceHeapBytes - kDisplayReserveBytes;

enum class Family {
    Unknown,
    SD1,             // SD 1.x, SD 1.x Inpaint / Instruct-Pix2Pix / Tiny
    SD2,             // SD 2.x, SD-Turbo
    SDXS,            // SDXS (512-DS: SD1 network, 09: SD2 network)
    SDXL,            // SDXL, SDXL-Turbo, Vega, SSD-1B, inpaint, pix2pix
    SD3,             // SD3.x medium / large
    Flux1,           // FLUX.1 dev / schnell / fill / control / Flex.2 / Kontext
    Flux2,           // FLUX.2 dev / klein
    Chroma,          // Chroma, Chroma Radiance
    ZImage,          // Z-Image, Z-Image-Turbo
    QwenImage,       // Qwen-Image, Layered, 2.1
    PixArt,          // PixArt
    OtherImage,      // an image DiT sd.cpp knows (Anima, Ovis, Lens, ...): generic settings
    Wan,             // Wan 2.x: video, not made by the app's image path
    OtherVideo,      // other video models sd.cpp knows (SVD, Hunyuan, LTX, ...)
};

const char * family_name(Family f);   // "SD 2.x", "FLUX.1", ...

// Where the weights live; the step of the ladder a plan took.
enum class Fit {
    TooBig,          // does not fit even at the smallest size with everything offloaded
    AllOnGpu,        // every weight resident on the GPU (the default placement)
    TextOnDisk,      // --params-backend te=disk: the text encoders are read in while they run
    AllOnDisk,       // --params-backend disk: each module's weights are read in while it runs
    Segmented,       // disk, and the diffusion model runs in segments (docs/performance.md); slow
};

const char * fit_name(Fit f);

// One tensor of a model file. ne is in ggml order (ne[0] the fastest), as
// sd.cpp holds it; a safetensors shape is reversed to match.
struct TensorInfo {
    std::string name;
    std::string dtype;        // "F16", "BF16", "Q4_0", "F8_E4M3", ...
    int64_t     ne[5] = { 1, 1, 1, 1, 1 };
    int         n_dims = 0;
    uint64_t    bytes  = 0;   // in the file
};

// The header of one model file.
struct FileHeader {
    std::string path;
    std::string format;       // "gguf", "safetensors", or "" when unreadable
    bool        ok = false;
    std::string error;
    uint64_t    file_bytes = 0;
    std::vector<TensorInfo> tensors;
    std::string hint;         // lower-case file name plus a GGUF's general.name / .architecture, for variants
};

// Reads the header of a .gguf or .safetensors file (by its first bytes, not
// its extension). Other formats (.ckpt, .pt) give ok = false with the file's
// size and name hint, so that a plan can still go by the name.
FileHeader read_file_header(const std::string & path);

// What goes into a plan: the model file and the separate components, as the
// options of gen_args.hpp name them. Empty paths are left out.
struct PlanInput {
    std::string model;            // --model: a whole checkpoint (or, with diffusion_model empty, a diffusion-only file)
    std::string diffusion_model;  // --diffusion-model
    std::string vae;
    std::string taesd;
    std::string clip_l;
    std::string clip_g;
    std::string t5xxl;
    std::string llm;
    uint64_t    budget_bytes = kDefaultBudgetBytes;  // free GPU memory to plan for
    int         width  = 0;       // 0: the recommended size
    int         height = 0;
};

struct Settings {
    int         width  = 512;
    int         height = 512;
    int         steps  = 20;
    float       cfg_scale = 7.0f;
    std::string sampler;          // sd.cpp's name ("euler_a", "euler", "lcm")
    std::string scheduler;        // empty: the model's default
    bool        flash_attn = true;
    bool        vae_tiling = false;
    std::string params_backend;   // "", "te=disk", "disk"
};

// Bytes of GPU memory by component, and the estimated compute buffers at the
// planned size.
struct Memory {
    uint64_t diffusion_weights = 0;
    uint64_t text_weights      = 0;   // clip_l, clip_g, t5xxl, llm
    uint64_t vae_weights       = 0;
    uint64_t compute_text      = 0;
    uint64_t compute_diffusion = 0;
    uint64_t compute_vae       = 0;   // with tiling when the plan tiles
    uint64_t overhead          = 0;   // allocator, staging, descriptor sets
    uint64_t peak              = 0;   // the most that is resident at once, with params_backend as planned
    uint64_t weights() const { return diffusion_weights + text_weights + vae_weights; }
};

// What the compute-buffer estimate depends on, found with the family.
struct ComputeModel {
    bool   unet = true;                 // a UNet (bytes per latent pixel) or a DiT (bytes per token)
    double unet_kib_per_latent_px = 150;
    int    dit_hidden = 3072;
    int    text_tokens = 512;           // text tokens a DiT attends over
    int    frames = 1;                  // latent frames (video)
    bool   big_text_encoder = false;    // T5-XXL or an LLM: a larger text compute buffer
    bool   taesd = false;               // the tiny VAE decodes: no big VAE buffer, no tiling
};

struct Plan {
    bool        ok = false;       // the files could be read and the family is known to sd.cpp
    std::string error;            // when !ok

    // What the model is.
    Family      family = Family::Unknown;
    std::string name;             // for the UI: "SD-Turbo", "FLUX.1 schnell", "Z-Image-Turbo (Q4_0)"
    std::string sd_version;       // sd.cpp's own name for it: "SD 2.x", "Flux", "Z-Image"
    std::string detected_by;      // "tensors", "tensors+name" or "name"
    bool        distilled   = false;   // few steps, no CFG: Turbo, schnell, LCM, Lightning, klein, ...
    bool        is_video    = false;
    bool        needs_input = false;   // inpaint, edit, fill, control: wants an init or reference image
    uint64_t    weight_bytes = 0;      // all given files, as they sit on the GPU
    std::string dtype_summary;         // "Q4_0 71%, F16 28%, F32 1%" (the diffusion model's weights)
    std::string quant;                 // the dominant dtype of the diffusion model

    // What it takes.
    Settings    settings;
    Memory      memory;
    ComputeModel compute;
    Fit         fit = Fit::TooBig;
    int         max_side = 0;     // the largest square side (multiple of 64) that fits with the same placement rules; 0: none
    std::vector<std::string> missing;   // components the family needs that no file provides: "vae", "t5xxl", ...
    std::vector<std::string> warnings;
    std::string warning;          // the warnings in one line; empty when all is well

    // The files, as options.
    PlanInput   input;
    bool        model_is_checkpoint = true;   // --model, else --diffusion-model

    // gen_args.cpp's arguments (one argument per element): the files, size,
    // steps, cfg, sampler, scheduler, flash attention, VAE tiling, params
    // backend. "--clip_g" and "--params-backend" are not yet options of
    // gen_args.cpp; the coordinator adds them there.
    std::vector<std::string> to_args() const;

    // One line for the screen and the log.
    std::string summary() const;
};

// Plans from the files named in `in`.
Plan plan_generation(const PlanInput & in);

// The slots of the headers that plan_from_headers takes, in PlanInput's order.
enum Slot { kSlotModel, kSlotDiffusion, kSlotVae, kSlotTaesd, kSlotClipL, kSlotClipG, kSlotT5, kSlotLlm, kSlots };

// The same from headers already read: `headers` has kSlots entries, an unused
// slot with an empty path. For tests.
Plan plan_from_headers(const PlanInput & in, const std::vector<FileHeader> & headers);

// The estimate on its own: peak GPU bytes for a size and placement, from the
// plan's weights. `fit` picks the placement; tiling the VAE's.
uint64_t estimate_peak(const Plan & plan, int width, int height, Fit fit, bool vae_tiling);

}  // namespace ps5sd
