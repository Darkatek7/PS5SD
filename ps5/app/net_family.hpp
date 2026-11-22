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
//   MOSS-Image       blocks.N.proj.ffn_up_gate, shared_block_modulation
//   Krea 2           txtfusion.*, blocks.N.attn.wq
//
// A checkpoint and a LoRA give the same label, so "this LoRA fits this model"
// is the two labels being equal. "" when it cannot be told (a LoRA for the
// text encoder only, LoHa/LoKr, .ckpt): the web UI then goes by the name.
//
// No console code in here, so ps5/app/tests builds it on the PC.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ps5sd {

struct NetTensor {
    std::string          name;
    std::vector<int64_t> shape;  // torch order: [out, in] for a linear weight
    std::string          dtype;  // a .safetensors file's ("BF16", "I8", "F8_E4M3", ...); "" in a GGUF
};

// What ComfyUI's quantizers wrote into a .safetensors file: the formats
// ("int8_tensorwise", "nvfp4", "float8_e4m3fn", "convrot_w4a4",
// "asym_w4a8_int8", ...) that "_quantization_metadata" in its "__metadata__"
// names, and those its ".comfy_quant" tensors (a JSON each) name.
struct ComfyQuant {
    std::set<std::string> in_metadata;
    std::set<std::string> in_tensors;
};

// The tensors of a .gguf or .safetensors file (by its first bytes); false for
// anything else or a damaged header. With quant, also ComfyUI's formats.
bool read_net_tensors(const std::string & path, std::vector<NetTensor> & out, ComfyQuant * quant = nullptr);

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

// The weight format of a file when stable-diffusion.cpp on the PS5 runs it
// otherwise than a GGUF or an F16/BF16/F32 file, or not at all. ComfyUI's
// quantized safetensors (sd.cpp 32d516c, src/model_io/safetensors_io.cpp):
//
//   int8_tensorwise, convrot_w4a4, asym_w4a8_int8 in .comfy_quant tensors:
//       sd.cpp reads them; the PS5's GPU has no INT8 dot product, and sd.cpp's
//       CPU fallback for these layers ends the title (SIGBUS, 2026-11-22). It
//       cannot run the file here.
//   the same only in "_quantization_metadata": sd.cpp does not read that, and
//       the I8 weights mean nothing without it. It cannot run the file.
//   nvfp4: sd.cpp has no NVFP4. It cannot run the file.
//   torchao (unsloth's files: <layer>._weight_qdata, _weight_scale): sd.cpp
//       does not read it. It cannot run the file.
//   I8 weights without either: nothing says how to use them. It cannot run
//       the file.
//   F8_E4M3 / F8_E5M2 weights: sd.cpp turns them into BF16 as it runs, on the
//       GPU (the PS5's has no FP8): about 1.7 times the time of a Q8_0 GGUF.
struct NetFormat {
    std::string label;          // "ComfyUI INT8", ...; "" for an ordinary file
    std::string note;           // one or two sentences for a person
    bool        loads = true;   // false: this build cannot run the file
};

NetFormat net_format_of(const std::vector<NetTensor> & tensors, const ComfyQuant & quant);

// The format of a file, cached as net_family is.
NetFormat net_format(const std::string & path);

}  // namespace ps5sd
