// The network a model or LoRA file is made for, told from its tensors.
//
// A LoRA works only on the network it was trained for. Its name and its
// metadata often do not say which (or say the wrong one: FLUX.1 LoRAs with
// "sd_1.5" in ss_base_model_version), but its tensor names and shapes do:
//
//   FLUX.1           double/single blocks (19/38), img_mlp up 12288, single linear1 21504
//   FLUX.2 Klein 4B  the same names, 5/20 blocks, img_mlp up 18432 (gated), single linear1 27648
//   FLUX.2 Klein 9B, FLUX.2 Dev  the FLUX.2 layout at width 4096 or 6144 (by width, not measured)
//   FLUX.3 Klein     content_mode_blocks / txt_mode_blocks, single_blocks.N.mlp_in
//   SDXL, SD 2.1, SD 1.5  a UNet with a cross attention 2048, 1024 or 768 wide
//   SD 3             joint_blocks
//   Wan 1.3B/5B/14B  blocks.N.self_attn, width 1536, 3072 or 5120
//   Z-Image          layers.N with noise_refiner / context_refiner, or layers.N.attention
//
// A checkpoint and a LoRA give the same label, so "this LoRA fits this model"
// is the two labels being equal. "" when it cannot be told (a LoRA for the
// text encoder only, LoHa/LoKr, .ckpt): the web UI then goes by the name.
//
// No console code in here, so ps5/app/tests builds it on the PC.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5sd {

struct NetTensor {
    std::string          name;
    std::vector<int64_t> shape;  // torch order: [out, in] for a linear weight
};

// The tensors of a .gguf or .safetensors file (by its first bytes); false for
// anything else or a damaged header.
bool read_net_tensors(const std::string & path, std::vector<NetTensor> & out);

// The family of a tensor list, as above.
std::string net_family_of(const std::vector<NetTensor> & tensors);

// The family of a file. Read once per path, size and modification time, then
// from a cache; safe from several threads.
std::string net_family(const std::string & path);

// What part of a model a file is, when it cannot run on its own: "diffusion
// model" (a network without its text encoder, as FLUX, Z-Image and Wan come),
// "text encoder" (CLIP, T5, an LLM such as Qwen3) or "VAE". "" for a whole
// checkpoint (network and text encoder in one file) and for a file that
// cannot be told (.ckpt).
std::string net_part_of(const std::vector<NetTensor> & tensors);

// The part of a file, cached as net_family is.
std::string net_part(const std::string & path);

}  // namespace ps5sd
