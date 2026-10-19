// SPDX-License-Identifier: GPL-3.0-or-later
#include "sd_plan.hpp"

#include "ggml.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace ps5sd {

namespace {

// ---------------------------------------------------------------------------
// Constants of the memory model. Every kMeasure* is an estimate to be measured
// on the console; Phase 3 calibrates them.
// ---------------------------------------------------------------------------

// Compute buffer of a UNet with flash attention, per latent pixel (W/8 * H/8).
// sd.cpp's logs on PCs: SD 1.5 at 512x512 ~0.6 GiB (4096 latent pixels),
// SDXL at 1024x1024 ~2 GiB (16384).
constexpr double kMeasureUnetKiBPerLatentPxSd1  = 150.0;
constexpr double kMeasureUnetKiBPerLatentPxSdxl = 130.0;
constexpr double kMeasureUnetKiBPerLatentPxSdxs = 100.0;
// A DiT's compute buffer: tokens (image tokens + text tokens) x hidden size x
// 4 bytes (f32 activations) x this many live tensors (qkv, the 4x MLP, the
// residual and modulation copies). Flux at 1024x1024: 4608 x 3072 x 4 x 24
// = 1.27 GiB. Without flash attention the N x N score matrix would come on
// top, which is why the plan always asks for flash attention.
constexpr double kMeasureDitLiveTensors = 24.0;
// VAE decode, untiled: the 128-channel full-resolution maps, ~9 live f32
// copies: 4400 bytes a pixel (512x512 ~1.1 GiB, 1024x1024 ~4.3 GiB).
constexpr double   kMeasureVaeBytesPerPx = 4400.0;
// A tiled decode runs one tile at a time (sd.cpp's default tile is 256 px).
constexpr uint64_t kVaeTilePx            = 256ull * 256;
// TAESD decodes in a small fixed buffer.
constexpr uint64_t kMeasureTaesdCompute  = 128ull << 20;
// Text encoders: 77 CLIP tokens, or 512 T5 / LLM tokens.
constexpr uint64_t kMeasureTextComputeClip = 96ull << 20;
constexpr uint64_t kMeasureTextComputeBig  = 384ull << 20;
// ggml's graph allocator rounds up and fragments.
constexpr double   kMeasureComputeMargin = 1.25;
// Allocator, staging buffer for weight uploads, descriptor sets, the tensors
// that stay resident next to a graph (latents, conditioning).
constexpr uint64_t kMeasureOverhead      = 384ull << 20;
// Segmented execution (docs/performance.md) keeps a window of the diffusion
// model's weights on the GPU: this fraction of them, at least this much.
constexpr double   kMeasureSegmentFraction = 0.35;
constexpr uint64_t kMeasureSegmentMin      = 1ull << 30;
// 8-bit float weights (safetensors F8_E4M3 / F8_E5M2) have no matmul in
// ggml-vulkan; sd.cpp expands them to F16 on load.
constexpr uint64_t kMeasureF8Expand        = 2;
// The VAE's tiling is wanted above this side, or when memory is tight.
constexpr int      kTileAboveSide          = 768;
constexpr double   kTightFraction          = 0.90;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string lower(std::string s) {
    for (auto & c : s) {
        c = (char) std::tolower((unsigned char) c);
    }
    return s;
}

std::string upper(std::string s) {
    for (auto & c : s) {
        c = (char) std::toupper((unsigned char) c);
    }
    return s;
}

bool starts_with(const std::string & s, const char * p) {
    return s.compare(0, std::strlen(p), p) == 0;
}

bool contains(const std::string & s, const char * p) {
    return s.find(p) != std::string::npos;
}

std::string base_name(const std::string & path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string fmt(const char * f, double a, double b = 0, double c = 0, double d = 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b, c, d);
    return buf;
}

double gib(uint64_t bytes) {
    return (double) bytes / (double) kGiB;
}

// ---------------------------------------------------------------------------
// File readers
// ---------------------------------------------------------------------------

struct Reader {
    FILE * f   = nullptr;
    bool   bad = false;

    bool read(void * out, size_t n) {
        if (bad || (n && std::fread(out, 1, n, f) != n)) {
            bad = true;
        }
        return !bad;
    }
    template <typename T> T get() {
        T v{};
        read(&v, sizeof(v));
        return v;
    }
    // A length-prefixed string; longer than `keep` is skipped and returned empty.
    std::string str(uint64_t keep = 1u << 20) {
        const uint64_t n = get<uint64_t>();
        if (bad || n > (1ull << 32)) {
            bad = true;
            return {};
        }
        if (n > keep) {
            skip(n);
            return {};
        }
        std::string s(n, '\0');
        if (n) {
            read(&s[0], n);
        }
        return s;
    }
    void skip(uint64_t n) {
        if (!bad && n && fseeko(f, (off_t) n, SEEK_CUR) != 0) {
            bad = true;
        }
    }
};

// GGUF metadata value types.
enum : uint32_t { G_U8, G_I8, G_U16, G_I16, G_U32, G_I32, G_F32, G_BOOL, G_STRING, G_ARRAY, G_U64, G_I64, G_F64 };

uint64_t gguf_scalar_size(uint32_t t) {
    switch (t) {
        case G_U8: case G_I8: case G_BOOL: return 1;
        case G_U16: case G_I16: return 2;
        case G_U32: case G_I32: case G_F32: return 4;
        case G_U64: case G_I64: case G_F64: return 8;
        default: return 0;
    }
}

// The bytes of a tensor in a GGUF file, from its ggml type and shape.
uint64_t ggml_tensor_bytes(uint32_t type, const int64_t * ne, int n_dims) {
    if (type >= (uint32_t) GGML_TYPE_COUNT || ggml_blck_size((ggml_type) type) <= 0) {
        return 0;
    }
    int64_t rows = 1;
    for (int i = 1; i < n_dims; i++) {
        rows *= ne[i];
    }
    return (uint64_t) ggml_row_size((ggml_type) type, ne[0]) * (uint64_t) rows;
}

bool read_gguf(Reader & r, FileHeader & h) {
    r.get<uint32_t>();  // magic, checked by the caller
    const uint32_t version = r.get<uint32_t>();
    if (version < 2 || version > 3) {
        h.error = "unsupported GGUF version";
        return false;
    }
    const uint64_t n_tensors = r.get<uint64_t>();
    const uint64_t n_kv      = r.get<uint64_t>();
    if (r.bad || n_tensors > 1000000 || n_kv > 1000000) {
        h.error = "not a GGUF header";
        return false;
    }
    std::string name_hint;
    for (uint64_t i = 0; i < n_kv && !r.bad; i++) {
        const std::string key = r.str();
        const uint32_t    t   = r.get<uint32_t>();
        if (t == G_STRING) {
            const bool general = starts_with(key, "general.");
            const std::string v = r.str(general ? 4096 : 0);
            if (general && (key == "general.name" || key == "general.basename" || key == "general.finetune" ||
                            key == "general.architecture")) {
                name_hint += " " + lower(v);
            }
        } else if (t == G_ARRAY) {
            const uint32_t et = r.get<uint32_t>();
            const uint64_t n  = r.get<uint64_t>();
            if (et == G_STRING) {
                for (uint64_t j = 0; j < n && !r.bad; j++) {
                    r.skip(r.get<uint64_t>());
                }
            } else if (gguf_scalar_size(et)) {
                r.skip(n * gguf_scalar_size(et));
            } else {
                r.bad = true;
            }
        } else if (gguf_scalar_size(t)) {
            r.skip(gguf_scalar_size(t));
        } else {
            r.bad = true;
        }
    }
    h.tensors.reserve(n_tensors);
    for (uint64_t i = 0; i < n_tensors && !r.bad; i++) {
        TensorInfo ti;
        ti.name        = r.str();
        const uint32_t n_dims = r.get<uint32_t>();
        if (n_dims > 5) {
            r.bad = true;
            break;
        }
        ti.n_dims = (int) n_dims;
        for (uint32_t d = 0; d < n_dims; d++) {
            ti.ne[d] = (int64_t) r.get<uint64_t>();
        }
        const uint32_t type = r.get<uint32_t>();
        r.get<uint64_t>();  // offset in the data
        ti.dtype = type < (uint32_t) GGML_TYPE_COUNT && ggml_type_name((ggml_type) type)
                       ? upper(ggml_type_name((ggml_type) type))
                       : "TYPE" + std::to_string(type);
        ti.bytes = ggml_tensor_bytes(type, ti.ne, ti.n_dims);
        h.tensors.push_back(std::move(ti));
    }
    if (r.bad) {
        h.error = "GGUF header could not be read";
        return false;
    }
    h.hint += name_hint;
    h.format = "gguf";
    return true;
}

// A cursor over the safetensors JSON header: objects of objects, with
// strings, integers and integer arrays. Just enough for that.
struct Json {
    const std::string & s;
    size_t i   = 0;
    bool   bad = false;

    explicit Json(const std::string & text) : s(text) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) {
            i++;
        }
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        return false;
    }
    void need(char c) {
        if (!eat(c)) {
            bad = true;
        }
    }
    std::string str() {
        std::string out;
        ws();
        if (i >= s.size() || s[i] != '"') {
            bad = true;
            return out;
        }
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                switch (s[i]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': out += '?'; i += 4; break;  // tensor names are ASCII
                    default: out += s[i];
                }
                i++;
            } else {
                out += s[i++];
            }
        }
        if (i >= s.size()) {
            bad = true;
        } else {
            i++;
        }
        return out;
    }
    int64_t num() {
        ws();
        size_t k = i;
        if (k < s.size() && s[k] == '-') {
            k++;
        }
        while (k < s.size() && std::isdigit((unsigned char) s[k])) {
            k++;
        }
        if (k == i) {
            bad = true;
            return 0;
        }
        const int64_t v = std::strtoll(s.c_str() + i, nullptr, 10);
        i = k;
        return v;
    }
    void skip_value() {
        ws();
        if (i >= s.size()) {
            bad = true;
        } else if (s[i] == '"') {
            str();
        } else if (s[i] == '{') {
            i++;
            if (!eat('}')) {
                do {
                    str();
                    need(':');
                    skip_value();
                } while (!bad && eat(','));
                need('}');
            }
        } else if (s[i] == '[') {
            i++;
            if (!eat(']')) {
                do {
                    skip_value();
                } while (!bad && eat(','));
                need(']');
            }
        } else {
            while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') {
                i++;
            }
        }
    }
};

bool read_safetensors(Reader & r, uint64_t file_bytes, FileHeader & h) {
    const uint64_t n = r.get<uint64_t>();
    if (r.bad || file_bytes < 8 || n < 2 || n > (128ull << 20) || n > file_bytes - 8) {
        h.error = "not a safetensors header";
        return false;
    }
    std::string text(n, '\0');
    if (!r.read(&text[0], n) || text[0] != '{') {
        h.error = "not a safetensors header";
        return false;
    }
    Json j(text);
    j.need('{');
    if (!j.eat('}')) {
        do {
            const std::string name = j.str();
            j.need(':');
            if (name == "__metadata__") {
                j.skip_value();
                continue;
            }
            TensorInfo ti;
            ti.name = name;
            std::vector<int64_t> shape;
            uint64_t begin = 0, end = 0;
            j.need('{');
            if (!j.eat('}')) {
                do {
                    const std::string key = j.str();
                    j.need(':');
                    if (key == "dtype") {
                        ti.dtype = j.str();
                    } else if (key == "shape") {
                        j.need('[');
                        if (!j.eat(']')) {
                            do {
                                shape.push_back(j.num());
                            } while (!j.bad && j.eat(','));
                            j.need(']');
                        }
                    } else if (key == "data_offsets") {
                        j.need('[');
                        begin = (uint64_t) j.num();
                        j.need(',');
                        end = (uint64_t) j.num();
                        j.need(']');
                    } else {
                        j.skip_value();
                    }
                } while (!j.bad && j.eat(','));
                j.need('}');
            }
            if (j.bad || shape.size() > 5 || end < begin) {
                break;
            }
            ti.n_dims = (int) shape.size();
            for (size_t d = 0; d < shape.size(); d++) {
                ti.ne[d] = shape[shape.size() - 1 - d];  // torch order to ggml order
            }
            ti.bytes = end - begin;
            h.tensors.push_back(std::move(ti));
        } while (!j.bad && j.eat(','));
        j.need('}');
    }
    if (j.bad) {
        h.error = "safetensors header could not be parsed";
        h.tensors.clear();
        return false;
    }
    h.format = "safetensors";
    return true;
}

}  // namespace

FileHeader read_file_header(const std::string & path) {
    FileHeader h;
    h.path = path;
    h.hint = lower(base_name(path));
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0) {
        h.error = "cannot open " + path;
        return h;
    }
    h.file_bytes = (uint64_t) st.st_size;
    Reader r;
    r.f = std::fopen(path.c_str(), "rb");
    if (!r.f) {
        h.error = "cannot open " + path;
        return h;
    }
    char magic[4] = {};
    const bool have_magic = std::fread(magic, 1, 4, r.f) == 4;
    std::rewind(r.f);
    if (have_magic && std::memcmp(magic, "GGUF", 4) == 0) {
        h.ok = read_gguf(r, h);
    } else if (have_magic && std::memcmp(magic, "PK", 2) != 0) {
        h.ok = read_safetensors(r, h.file_bytes, h);
    } else {
        h.error = "a PyTorch checkpoint: the header cannot be read here; the plan goes by the file name";
    }
    std::fclose(r.f);
    if (!h.ok) {
        h.tensors.clear();
    }
    return h;
}

namespace {

// ---------------------------------------------------------------------------
// The version, from the tensor names. Mirrors ModelLoader::get_sd_version
// (third_party/stable-diffusion.cpp/src/model_loader.cpp, pinned b13a482), which
// is internal to the library and works on the loader's converted names; here
// the names are those of the file, with the prefix every checkpoint family
// writes ("model.diffusion_model.", "unet.") taken off, so one rule reads
// original, Comfy, diffusers-UNet and sd.cpp-converted GGUF files alike.
// ---------------------------------------------------------------------------

// SDVersion, in the order of src/model.h; names as src/pipeline/diffusion_engine.cpp's
// model_version_to_str[].
enum V {
    V_SD1, V_SD1_INPAINT, V_SD1_PIX2PIX, V_SD1_TINY_UNET, V_SD2, V_SD2_INPAINT, V_SD2_TINY_UNET, V_SDXS_512_DS,
    V_SDXS_09, V_SDXL, V_SDXL_INPAINT, V_SDXL_PIX2PIX, V_SDXL_VEGA, V_SDXL_SSD1B, V_SVD, V_SD3, V_FLUX,
    V_FLUX_FILL, V_FLUX_CONTROLS, V_FLEX_2, V_CHROMA_RADIANCE, V_WAN2, V_WAN2_2_I2V, V_WAN2_2_TI2V, V_WAN2_2_S2V,
    V_LINGBOT_VIDEO, V_QWEN_IMAGE, V_QWEN_IMAGE_LAYERED, V_QWEN_IMAGE_2_1, V_HUNYUAN_VIDEO, V_ANIMA, V_FLUX2,
    V_FLUX2_KLEIN, V_LTXAV, V_MINIMAX_H3, V_HIDREAM_O1, V_Z_IMAGE, V_BOOGU_IMAGE, V_OVIS_IMAGE, V_ERNIE_IMAGE,
    V_LENS, V_MINIT2I, V_LONGCAT, V_PID, V_IDEOGRAM4, V_SEFI_IMAGE, V_KREA2, V_MAGE_FLOW, V_SENSENOVA_U1_5,
    V_LLADA_IMAGE, V_ESRGAN, V_PIXART, V_MING_IMAGE, V_Z_IMAGE_L2P, V_IRIS, V_FLUX3, V_MOSS_IMAGE, V_COUNT,
};

const char * const kVersionNames[] = {
    "SD 1.x", "SD 1.x Inpaint", "Instruct-Pix2Pix", "SD 1.x Tiny UNet", "SD 2.x", "SD 2.x Inpaint",
    "SD 2.x Tiny UNet", "SDXS (512-DS)", "SDXS (09)", "SDXL", "SDXL Inpaint", "SDXL Instruct-Pix2Pix",
    "SDXL (Vega)", "SDXL (SSD1B)", "SVD", "SD3.x", "Flux", "Flux Fill", "Flux Control", "Flex.2",
    "Chroma Radiance", "Wan 2.x", "Wan 2.2 I2V", "Wan 2.2 TI2V", "Wan 2.2 S2V", "LingBot Video", "Qwen Image",
    "Qwen Image Layered", "Qwen Image 2.1", "Hunyuan Video", "Anima", "Flux.2", "Flux.2 klein", "LTXAV",
    "MiniMax-H3", "HiDream O1", "Z-Image", "Boogu Image", "Ovis Image", "Ernie Image", "Lens", "MiniT2I",
    "Longcat-Image", "PiD", "Ideogram 4", "SeFi-Image", "Krea2", "Mage Flow", "SenseNova U1.5", "LLaDA-Image",
    "ESRGAN", "PixArt", "Ming-Image", "Z-Image L2P", "Iris-3B", "Flux.3 Image Klein", "MOSS-Image",
};
static_assert(sizeof(kVersionNames) / sizeof(kVersionNames[0]) == V_COUNT, "version names out of step with V");

// Tensors of every file given, by the name sd.cpp's loader would use.
struct Pool {
    struct Entry {
        std::string        raw;      // as the loader sees it, with the role's prefix ("te." for clip_l)
        std::string        canon;    // raw without "model.diffusion_model." / "diffusion_model." / "unet."
        const TensorInfo * info = nullptr;
    };
    std::vector<Entry> entries;
    std::unordered_map<std::string, size_t> by_raw, by_canon;

    void add(const std::string & prefix, const TensorInfo & t) {
        Entry e;
        e.raw = prefix + t.name;
        e.canon = e.raw;
        for (const char * p : { "model.diffusion_model.", "diffusion_model.", "unet." }) {
            if (starts_with(e.canon, p)) {
                e.canon = e.canon.substr(std::strlen(p));
                break;
            }
        }
        e.info = &t;
        by_raw.emplace(e.raw, entries.size());
        by_canon.emplace(e.canon, entries.size());
        entries.push_back(std::move(e));
    }
    // The canonical name starts with p: the loader's "model.diffusion_model.<p>".
    bool dm(const char * p) const {
        for (const auto & e : entries) {
            if (starts_with(e.canon, p)) {
                return true;
            }
        }
        return false;
    }
    // The raw name contains p anywhere: the loader's plain find().
    bool sub(const char * p) const {
        for (const auto & e : entries) {
            if (contains(e.raw, p)) {
                return true;
            }
        }
        return false;
    }
    const TensorInfo * canon(const char * name) const {
        auto it = by_canon.find(name);
        return it == by_canon.end() ? nullptr : entries[it->second].info;
    }
    const TensorInfo * raw(const char * name) const {
        auto it = by_raw.find(name);
        return it == by_raw.end() ? nullptr : entries[it->second].info;
    }
};

struct Detected {
    V    version = V_COUNT;
    bool chroma = false;           // Flux's network with Chroma's distilled guidance layer
    bool guidance_in = false;      // Flux dev (the guidance embedder); schnell has none
    bool has_te_tensors = false;   // the pool has a text encoder (for the fallback to the context dim)
};

V unet_version(bool is_xl, const TensorInfo * input_block, int64_t token_dim, bool has_mid1, bool has_out311,
               bool has_out71, bool has_attn_1024) {
    const bool inpaint = input_block && input_block->ne[2] == 9;
    const bool ip2p    = input_block && input_block->ne[2] == 8;
    if (is_xl) {
        if (inpaint) return V_SDXL_INPAINT;
        if (ip2p) return V_SDXL_PIX2PIX;
        if (!has_mid1) return has_out311 ? V_SDXL_SSD1B : V_SDXL_VEGA;
        return V_SDXL;
    }
    if (token_dim == 768) {
        if (inpaint) return V_SD1_INPAINT;
        if (ip2p) return V_SD1_PIX2PIX;
        if (!has_mid1) return has_out71 ? V_SD1_TINY_UNET : V_SDXS_512_DS;
        return V_SD1;
    }
    if (token_dim == 1024) {
        if (inpaint) return V_SD2_INPAINT;
        if (!has_mid1) return has_attn_1024 ? V_SDXS_09 : V_SD2_TINY_UNET;
        return V_SD2;
    }
    return V_COUNT;
}

Detected detect(const Pool & p) {
    Detected d;
    auto done = [&](V v) { d.version = v; return d; };

    if (p.canon("emb_in.image.weight") && p.canon("txt_mode_blocks.0.q_proj.weight")) return done(V_FLUX3);
    if (p.canon("video_embedder.weight") && p.canon("shared_block_modulation.weight")) return done(V_MOSS_IMAGE);

    // Markers that name a family on their own (get_sd_version returns at the first).
    if (p.dm("net.lq_proj.latent_proj.0.weight")) return done(V_PID);
    if (p.sub("embed_image_indicator.weight")) return done(V_IDEOGRAM4);
    if (p.dm("txtfusion.projector.weight") || p.dm("text_fusion.projector.weight")) return done(V_KREA2);
    if (p.dm("nerf_final_layer_conv.")) return done(V_CHROMA_RADIANCE);
    if (p.dm("joint_blocks.")) return done(V_SD3);
    if (p.sub("model.x_embedder.proj1.weight") && p.raw("model.language_model.layers.0.self_attn.q_proj.weight")) {
        return done(V_HIDREAM_O1);
    }
    if (p.dm("transformer_blocks.0.attn.norm_added_q.weight") && p.canon("transformer_blocks.0.img_mlp.w1.weight")) {
        return done(V_LENS);
    }
    if (p.sub("net.img_embedder.proj1.weight")) return done(V_MINIT2I);
    if (p.sub("language_model.model.layers.0.self_attn.q_proj_mot_gen.weight")) return done(V_SENSENOVA_U1_5);
    if (p.canon("txt_in.text_norm.weight")) return done(V_QWEN_IMAGE_2_1);
    if (p.dm("transformer_blocks.0.img_mod.1.weight")) {
        const TensorInfo * img_in = p.canon("img_in.weight");
        if (img_in && img_in->ne[0] == 128) return done(V_MAGE_FLOW);
        if (p.canon("time_text_embed.addition_t_embedding.weight")) return done(V_QWEN_IMAGE_LAYERED);
        return done(V_QWEN_IMAGE);
    }
    if (p.dm("txt_in.individual_token_refiner.blocks.0.adaLN_modulation.1.weight")) return done(V_HUNYUAN_VIDEO);
    if (p.sub("llm_adapter.blocks.0.cross_attn.q_proj.weight")) return done(V_ANIMA);
    const bool is_flux2 = p.dm("double_stream_modulation_img.lin.weight");
    if (p.sub("dual_time_embed.semantic_embedder.linear_1.weight")) return done(V_SEFI_IMAGE);
    const bool single_47 = p.sub("single_blocks.47.linear1.weight");
    if (p.dm("double_blocks.0.img_mlp.gate_proj.weight")) return done(V_OVIS_IMAGE);
    if (p.dm("sigvq_embedder.1.weight")) return done(V_LLADA_IMAGE);
    if (p.dm("cap_embedder.0.weight")) {
        if (p.raw("text_encoders.llm.connector.layers.0.self_attn.q_proj.weight")) return done(V_MING_IMAGE);
        if (p.canon("local_decoder.out_conv.weight")) return done(V_Z_IMAGE_L2P);
        return done(V_Z_IMAGE);
    }
    if (p.sub("double_stream_layers.0.img_instruct_attn.processor.img_to_q.weight")) return done(V_BOOGU_IMAGE);
    if (p.dm("layers.0.adaLN_sa_ln.weight")) return done(V_ERNIE_IMAGE);
    const bool audio_patchify = p.canon("audio_patchify_proj.weight") != nullptr;
    if (p.dm("t_block.1.weight") && p.canon("x_embedder.proj.weight") && !audio_patchify) return done(V_PIXART);
    if (p.dm("adaln_single.emb.timestep_embedder.linear_1.bias")) {
        // PixArt shares this timestep embedding with LTX-AV.
        if (p.canon("pos_embed.proj.weight") && p.canon("adaln_single.linear.weight") && !audio_patchify) {
            return done(V_PIXART);
        }
        return done(V_LTXAV);
    }
    if (p.dm("video_patch_proj.weight") && p.canon("audio_patch_proj.weight")) return done(V_MINIMAX_H3);
    if (p.canon("y_embedder.layer_pool.weight") && p.canon("modulation_cores.adaln_img.weight")) return done(V_IRIS);

    const bool is_wan = p.dm("blocks.0.cross_attn.norm_k.weight");
    const bool is_s2v = p.sub("casual_audio_encoder.weights") || p.sub("audio_injector.injector.0.q.weight");
    if (p.dm("patch_embedder.weight")) return done(V_LINGBOT_VIDEO);
    const TensorInfo * patch_embedding = p.canon("patch_embedding.weight");
    const bool has_img_emb = p.dm("img_emb");
    if (p.dm("input_blocks.8.0.time_mixer.mix_factor")) return done(V_SVD);

    const bool is_unet = p.dm("input_blocks.") || p.dm("down_blocks.");
    const bool is_flux = p.dm("double_blocks.") || p.dm("single_transformer_blocks.");
    const bool multi_encoders = p.sub("conditioner.embedders.1") || p.sub("cond_stage_model.1") || p.sub("te.1");
    const bool has_mid1  = p.dm("middle_block.1.") || p.dm("mid_block.resnets.1.");
    const bool has_out311 = p.dm("output_blocks.3.1.transformer_blocks.1") ||
                            p.dm("up_blocks.1.attentions.0.transformer_blocks.1");
    const bool has_out71  = p.dm("output_blocks.7.1") || p.dm("up_blocks.2.attentions.1");
    const TensorInfo * attn1_k = p.canon("output_blocks.7.1.transformer_blocks.0.attn1.to_k.weight");
    const bool has_attn_1024 = attn1_k && attn1_k->ne[0] == 1024;

    const TensorInfo * token_embedding = nullptr;
    for (const char * n : { "cond_stage_model.transformer.text_model.embeddings.token_embedding.weight",
                            "cond_stage_model.model.token_embedding.weight",
                            "text_model.embeddings.token_embedding.weight",
                            "te.text_model.embeddings.token_embedding.weight",
                            "conditioner.embedders.0.model.token_embedding.weight",
                            "conditioner.embedders.0.transformer.text_model.embeddings.token_embedding.weight" }) {
        if (const TensorInfo * t = p.raw(n)) {
            token_embedding = t;
        }
    }
    const TensorInfo * input_block = p.canon("input_blocks.0.0.weight");
    if (!input_block) input_block = p.canon("img_in.weight");
    if (!input_block) input_block = p.canon("conv_in.weight");
    const TensorInfo * context_embedding = p.canon("txt_in.weight");
    if (!context_embedding) context_embedding = p.canon("context_embedder.weight");

    if (is_wan) {
        const int64_t channels = patch_embedding ? patch_embedding->ne[3] : 0;
        if (is_s2v) return done(V_WAN2_2_S2V);
        if (channels == 184320 && !has_img_emb) return done(V_WAN2_2_I2V);
        if (channels == 147456 && !has_img_emb) return done(V_WAN2_2_TI2V);
        return done(V_WAN2);
    }
    d.has_te_tensors = token_embedding != nullptr;
    const bool is_xl = is_unet && multi_encoders;
    if (is_xl) {
        return done(unet_version(true, input_block, 0, has_mid1, has_out311, has_out71, has_attn_1024));
    }
    if (is_flux && !is_flux2) {
        d.chroma      = p.sub("distilled_guidance_layer.in_proj.weight");
        d.guidance_in = p.dm("guidance_in.");
        if (context_embedding && context_embedding->ne[0] == 3584) return done(V_LONGCAT);
        if (input_block && input_block->ne[0] == 384) return done(V_FLUX_FILL);
        if (input_block && input_block->ne[0] == 128) return done(V_FLUX_CONTROLS);
        if (input_block && input_block->ne[0] == 196) return done(V_FLEX_2);
        return done(V_FLUX);
    }
    if (is_flux2) return done(single_47 ? V_FLUX2 : V_FLUX2_KLEIN);

    if (token_embedding) {
        return done(unet_version(false, input_block, token_embedding->ne[0], has_mid1, has_out311, has_out71,
                                 has_attn_1024));
    }
    // Not in sd.cpp: a UNet file without its text encoder (a --diffusion-model
    // file, the encoders given separately, or not at all) is told by the
    // width of its cross-attention. SDXL's "label_emb." (the size and crop
    // conditioning) says it is XL.
    if (is_unet) {
        if (p.dm("label_emb.")) {
            return done(unet_version(true, input_block, 0, has_mid1, has_out311, has_out71, has_attn_1024));
        }
        const TensorInfo * ctx = p.canon("input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight");
        if (!ctx) ctx = p.canon("down_blocks.0.attentions.0.transformer_blocks.0.attn2.to_k.weight");
        if (ctx) {
            return done(unet_version(false, input_block, ctx->ne[0], has_mid1, has_out311, has_out71, has_attn_1024));
        }
    }
    return d;
}

// What the file name says when the tensors cannot (a .ckpt, or a header of a
// kind that this does not know). Only the families the app is for.
V version_from_name(const std::string & hint) {
    auto has = [&](const char * s) { return contains(hint, s); };
    if (has("z_image") || has("z-image") || has("zimage")) return V_Z_IMAGE;
    if (has("qwen_image") || has("qwen-image")) return V_QWEN_IMAGE;
    if (has("chroma")) return has("radiance") ? V_CHROMA_RADIANCE : V_FLUX;
    if (has("flux.2") || has("flux2") || has("flux-2") || has("flux_2")) return has("klein") ? V_FLUX2_KLEIN : V_FLUX2;
    if (has("flux")) return V_FLUX;
    if (has("sd3") || has("sd_3") || has("sd-3") || has("stable-diffusion-3")) return V_SD3;
    if (has("sdxs")) return V_SDXS_512_DS;
    if (has("sdxl") || has("sd_xl") || has("sd-xl") || has("xl_base") || has("xl-base")) return V_SDXL;
    if (has("pixart")) return V_PIXART;
    if (has("wan2") || has("wan_2") || has("wan-2")) return V_WAN2;
    if (has("turbo") && (has("sd_") || has("sd-") || has("sd2") || has("sdturbo"))) return V_SD2;
    if (has("v2-1") || has("v2_1") || has("sd2") || has("sd_2") || has("768-v")) return V_SD2;
    if (has("v1-5") || has("v1_5") || has("sd15") || has("sd-v1") || has("sd_v1") || has("sd1") ||
        has("v1-4") || has("1.5")) {
        return V_SD1;
    }
    return V_COUNT;
}

// ---------------------------------------------------------------------------
// Settings and the compute model, per family
// ---------------------------------------------------------------------------

struct Variant {
    bool turbo = false, schnell = false, lcm = false, lightning = false, hyper = false, dmd = false;
    bool base = false, kontext = false, dev = false, flash = false;
};

Variant variant_of(const std::string & hint) {
    Variant v;
    v.turbo     = contains(hint, "turbo");
    v.schnell   = contains(hint, "schnell");
    v.lcm       = contains(hint, "lcm");
    v.lightning = contains(hint, "lightning");
    v.hyper     = contains(hint, "hyper-sd") || contains(hint, "hyper_sd") || contains(hint, "hypersd");
    v.dmd       = contains(hint, "dmd");
    v.base      = contains(hint, "base");
    v.kontext   = contains(hint, "kontext");
    v.dev       = contains(hint, "dev");
    v.flash     = contains(hint, "flash");
    return v;
}

struct Spec {
    Family      family = Family::OtherImage;
    std::string label;             // the family's name for the UI
    int         width = 1024, height = 1024;
    int         max_side = 2048;
    int         steps = 20;
    float       cfg = 4.5f;
    std::string sampler = "euler";
    std::string scheduler;
    int         turbo_steps = 8;   // when the name says Turbo / Lightning / Flash
    ComputeModel compute;
    bool        distilled = false;
    bool        video = false;
    bool        needs_input = false;
    std::string note;              // a warning that goes with this family
};

ComputeModel unet_model(double kib) {
    ComputeModel c;
    c.unet = true;
    c.unet_kib_per_latent_px = kib;
    c.text_tokens = 77;
    return c;
}

ComputeModel dit_model(int hidden, int text_tokens, bool big_text_encoder = true) {
    ComputeModel c;
    c.unet = false;
    c.dit_hidden = hidden;
    c.text_tokens = text_tokens;
    c.big_text_encoder = big_text_encoder;
    return c;
}

// The other image DiTs sd.cpp knows: generic settings from each one's doc page.
Spec other_image(V v) {
    Spec s;
    s.family = Family::OtherImage;
    s.compute = dit_model(3072, 512);
    s.cfg = 4.5f;
    switch (v) {
        case V_ANIMA: s.cfg = 6.0f; break;
        case V_OVIS_IMAGE: s.cfg = 5.0f; break;
        case V_LENS: s.cfg = 5.0f; s.turbo_steps = 4; break;
        case V_ERNIE_IMAGE: s.cfg = 5.0f; break;
        case V_LONGCAT: s.cfg = 5.0f; break;
        case V_IRIS: s.cfg = 3.0f; break;
        case V_HIDREAM_O1: s.cfg = 1.0f; break;
        case V_LLADA_IMAGE: s.turbo_steps = 4; break;
        default: break;
    }
    return s;
}

Spec spec_for(V v, const Detected & d, const Variant & var) {
    Spec s;
    switch (v) {
        case V_SD1: case V_SD1_INPAINT: case V_SD1_PIX2PIX: case V_SD1_TINY_UNET:
            s.family = Family::SD1;
            s.label = "SD 1.x";
            s.width = s.height = 512;
            s.max_side = 1024;
            s.steps = 20;
            s.cfg = 7.0f;
            s.sampler = "euler_a";
            s.turbo_steps = 1;
            s.compute = unet_model(kMeasureUnetKiBPerLatentPxSd1);
            s.needs_input = v == V_SD1_INPAINT || v == V_SD1_PIX2PIX;
            break;
        case V_SD2: case V_SD2_INPAINT: case V_SD2_TINY_UNET:
            s.family = Family::SD2;
            s.label = "SD 2.x";
            // 768-v is the common SD 2.x; the base model and Turbo are 512.
            s.width = s.height = (var.base || var.turbo) ? 512 : 768;
            s.max_side = 1024;
            s.steps = 20;
            s.cfg = 7.0f;
            s.sampler = "euler_a";
            s.turbo_steps = 1;
            s.compute = unet_model(kMeasureUnetKiBPerLatentPxSd1);
            s.needs_input = v == V_SD2_INPAINT;
            break;
        case V_SDXS_512_DS: case V_SDXS_09:
            s.family = Family::SDXS;
            s.label = "SDXS";
            s.width = s.height = 512;
            s.max_side = 768;
            s.steps = 1;
            s.cfg = 1.0f;
            s.sampler = "euler_a";
            s.distilled = true;
            s.compute = unet_model(kMeasureUnetKiBPerLatentPxSdxs);
            break;
        case V_SDXL: case V_SDXL_INPAINT: case V_SDXL_PIX2PIX: case V_SDXL_VEGA: case V_SDXL_SSD1B:
            s.family = Family::SDXL;
            s.label = "SDXL";
            s.width = s.height = var.turbo ? 512 : 1024;
            s.max_side = 1536;
            s.steps = 25;
            s.cfg = 7.0f;
            s.sampler = "euler_a";
            s.turbo_steps = 1;
            s.compute = unet_model(kMeasureUnetKiBPerLatentPxSdxl);
            s.needs_input = v == V_SDXL_INPAINT || v == V_SDXL_PIX2PIX;
            break;
        case V_SD3:
            s.family = Family::SD3;
            s.label = "SD3.x";
            s.steps = 28;
            s.cfg = 4.5f;
            s.turbo_steps = 4;
            s.compute = dit_model(2048, 333);
            break;
        case V_FLUX: case V_FLUX_FILL: case V_FLUX_CONTROLS: case V_FLEX_2:
            if (d.chroma) {
                s.family = Family::Chroma;
                s.label = "Chroma";
                s.steps = 20;
                s.cfg = 4.0f;
                s.compute = dit_model(3072, 512);
            } else {
                s.family = Family::Flux1;
                s.label = "FLUX.1";
                s.steps = 20;
                // guidance is a distilled input in dev; cfg stays 1 (docs/flux.md)
                s.cfg = 1.0f;
                s.compute = dit_model(3072, 512);
                s.needs_input = v != V_FLUX || var.kontext;
            }
            break;
        case V_CHROMA_RADIANCE:
            s.family = Family::Chroma;
            s.label = "Chroma Radiance";
            s.steps = 20;
            s.cfg = 4.0f;
            s.compute = dit_model(3072, 512);
            break;
        case V_FLUX2: case V_FLUX2_KLEIN:
            s.family = Family::Flux2;
            s.label = v == V_FLUX2 ? "FLUX.2" : "FLUX.2 klein";
            s.steps = 20;
            s.cfg = 1.0f;
            s.compute = dit_model(v == V_FLUX2 ? 6144 : 4096, 512);
            break;
        case V_FLUX3:
            s.family = Family::Flux2;
            s.label = "FLUX.3 Image Klein";
            s.steps = 16;
            s.cfg = 1.0f;
            s.compute = dit_model(3072, 512);
            break;
        case V_Z_IMAGE:
            s.family = Family::ZImage;
            s.label = "Z-Image";
            s.steps = 20;
            s.cfg = 5.0f;
            s.turbo_steps = 8;
            s.compute = dit_model(3840, 512);
            break;
        case V_QWEN_IMAGE: case V_QWEN_IMAGE_LAYERED: case V_QWEN_IMAGE_2_1:
            s.family = Family::QwenImage;
            s.label = v == V_QWEN_IMAGE_2_1 ? "Qwen-Image 2.1" : v == V_QWEN_IMAGE ? "Qwen-Image" : "Qwen-Image Layered";
            s.steps = 20;
            s.cfg = v == V_QWEN_IMAGE_2_1 ? 6.0f : 2.5f;
            s.turbo_steps = 4;
            s.max_side = 1664;
            s.compute = dit_model(3072, 512);
            break;
        case V_PIXART:
            s.family = Family::PixArt;
            s.label = "PixArt";
            s.steps = 20;
            s.cfg = 4.5f;
            s.compute = dit_model(1152, 300);
            break;
        case V_WAN2: case V_WAN2_2_I2V: case V_WAN2_2_TI2V: case V_WAN2_2_S2V:
            s.family = Family::Wan;
            s.label = v == V_WAN2 ? "Wan 2.x" : kVersionNames[v];
            s.width = 832;
            s.height = 480;
            s.max_side = 1280;
            s.steps = 20;
            s.cfg = 6.0f;
            s.video = true;
            s.needs_input = v == V_WAN2_2_I2V || v == V_WAN2_2_S2V;
            s.compute = dit_model(1536, 512);
            s.compute.frames = 9;   // 33 video frames
            break;
        case V_SVD: case V_HUNYUAN_VIDEO: case V_LINGBOT_VIDEO: case V_LTXAV: case V_MINIMAX_H3:
            s = other_image(v);
            s.family = Family::OtherVideo;
            s.video = true;
            s.compute.frames = 9;
            break;
        default:
            s = other_image(v);
            s.label = kVersionNames[v];
            break;
    }
    if (s.label.empty()) {
        s.label = kVersionNames[v];
    }
    return s;
}

// ---------------------------------------------------------------------------
// The memory model
// ---------------------------------------------------------------------------

struct Compute {
    uint64_t text = 0, diffusion = 0, vae = 0;
};

Compute compute_buffers(const ComputeModel & m, int width, int height, bool vae_tiling) {
    Compute c;
    c.text = m.big_text_encoder ? kMeasureTextComputeBig : kMeasureTextComputeClip;
    const double w = width, h = height;
    if (m.unet) {
        c.diffusion = (uint64_t) ((w / 8) * (h / 8) * m.unet_kib_per_latent_px * 1024.0);
    } else {
        const double tokens = (w / 16) * (h / 16) * m.frames + m.text_tokens;
        c.diffusion = (uint64_t) (tokens * m.dit_hidden * 4.0 * kMeasureDitLiveTensors);
    }
    if (m.taesd) {
        c.vae = kMeasureTaesdCompute;
    } else {
        const double px = vae_tiling ? (double) std::min<uint64_t>(kVaeTilePx, (uint64_t) (w * h)) : w * h;
        c.vae = (uint64_t) (px * kMeasureVaeBytesPerPx);
    }
    auto pad = [](uint64_t b) { return (uint64_t) ((double) b * kMeasureComputeMargin); };
    c.text = pad(c.text);
    c.diffusion = pad(c.diffusion);
    c.vae = pad(c.vae);
    return c;
}

// The most GPU memory resident at once under a placement. With AllOnGpu
// every weight is resident; with the text encoders on disk they are read in
// while they run and dropped; with everything on disk each module's weights
// are resident only while it runs; segmented, the diffusion model holds a
// window of its weights.
uint64_t peak_of(const Memory & m, Fit fit) {
    const uint64_t T = m.text_weights, D = m.diffusion_weights, V = m.vae_weights;
    const uint64_t ct = m.compute_text, cd = m.compute_diffusion, cv = m.compute_vae;
    uint64_t peak = 0;
    switch (fit) {
        case Fit::AllOnGpu:
            peak = T + D + V + std::max({ ct, cd, cv });
            break;
        case Fit::TextOnDisk:
            peak = D + V + std::max({ T + ct, cd, cv });
            break;
        case Fit::AllOnDisk:
            peak = std::max({ T + ct, D + cd, V + cv });
            break;
        case Fit::Segmented: {
            const uint64_t window = std::max<uint64_t>(kMeasureSegmentMin, (uint64_t) ((double) D * kMeasureSegmentFraction));
            peak = std::max({ T + ct, std::min(D, window) + cd, V + cv });
            break;
        }
        case Fit::TooBig:
            return UINT64_MAX;
    }
    return peak + m.overhead;
}

const char * params_backend_for(Fit f) {
    switch (f) {
        case Fit::TextOnDisk: return "te=disk";
        case Fit::AllOnDisk: case Fit::Segmented: return "disk";
        default: return "";
    }
}

struct Candidate {
    Fit    fit = Fit::TooBig;
    bool   tiling = false;
    Memory mem;
};

// The placement of a plan at a size: the first rung of the ladder that fits,
// with the VAE tiled above 768 px, when it is needed to fit, or when memory is
// tight. `deepest` is the last rung allowed.
Candidate place(const Plan & plan, int width, int height, uint64_t budget, Fit deepest) {
    static const Fit ladder[] = { Fit::AllOnGpu, Fit::TextOnDisk, Fit::AllOnDisk, Fit::Segmented };
    Candidate best;
    best.mem = plan.memory;
    const bool tile_first = !plan.compute.taesd && std::max(width, height) > kTileAboveSide;
    for (Fit f : ladder) {
        if (f > deepest) {
            break;
        }
        // The text encoders only move to disk when there are any to move.
        if (f == Fit::TextOnDisk && plan.memory.text_weights == 0) {
            continue;
        }
        // At each rung, the VAE untiled (tiled above 768 px), then tiled: tiling
        // costs little time, going to disk costs a great deal.
        for (int pass = 0; pass < 2; pass++) {
            const bool tiling = pass == 0 ? tile_first : true;
            if (pass == 1 && (tile_first || plan.compute.taesd)) {
                break;
            }
            Memory m = plan.memory;
            const Compute c = compute_buffers(plan.compute, width, height, tiling);
            m.compute_text = c.text;
            m.compute_diffusion = c.diffusion;
            m.compute_vae = c.vae;
            m.overhead = kMeasureOverhead;
            m.peak = peak_of(m, f);
            best.mem = m;
            if (m.peak > budget) {
                continue;
            }
            best.fit = f;
            best.tiling = tiling;
            // Memory is tight: a tiled VAE costs little time and buys room, when
            // the VAE is what sets the peak.
            if (!tiling && !plan.compute.taesd && (double) m.peak > kTightFraction * (double) budget) {
                const Compute ct = compute_buffers(plan.compute, width, height, true);
                Memory mt = m;
                mt.compute_vae = ct.vae;
                mt.peak = peak_of(mt, f);
                if (mt.peak < m.peak) {
                    best.tiling = true;
                    best.mem = mt;
                }
            }
            return best;
        }
    }
    return best;
}

int largest_side(const Plan & plan, int cap, uint64_t budget, Fit deepest) {
    int best = 0;
    for (int side = 256; side <= cap; side += 64) {
        if (place(plan, side, side, budget, deepest).fit != Fit::TooBig) {
            best = side;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Reading what a component is
// ---------------------------------------------------------------------------

enum class Role { Diffusion, Text, Vae };

// Which module a tensor of a whole checkpoint belongs to.
Role classify(const std::string & name) {
    for (const char * p : { "first_stage_model.", "vae.", "post_quant_conv.", "quant_conv." }) {
        if (starts_with(name, p)) {
            return Role::Vae;
        }
    }
    for (const char * p : { "cond_stage_model.", "conditioner.", "text_encoders.", "te.", "text_model.", "t5xxl.",
                            "llm.", "language_model.", "model.language_model." }) {
        if (starts_with(name, p)) {
            return Role::Text;
        }
    }
    return Role::Diffusion;
}

// Tensors a checkpoint carries that the loader never reads: the EMA copy, the
// diffusion schedule's constants.
bool unused_tensor(const std::string & name) {
    return starts_with(name, "model_ema.") || starts_with(name, "model.ema.") || starts_with(name, "ema.") ||
           contains(name, "alphas_cumprod") || starts_with(name, "optimizer");
}

uint64_t weight_bytes_of(const TensorInfo & t) {
    return starts_with(t.dtype, "F8") ? t.bytes * kMeasureF8Expand : t.bytes;
}

std::string dtype_summary_of(const std::map<std::string, uint64_t> & by_type, std::string & dominant) {
    uint64_t total = 0;
    for (const auto & [t, b] : by_type) {
        total += b;
    }
    std::vector<std::pair<uint64_t, std::string>> sorted;
    for (const auto & [t, b] : by_type) {
        sorted.emplace_back(b, t);
    }
    std::sort(sorted.begin(), sorted.end(), [](auto & a, auto & b) { return a.first > b.first; });
    std::string out;
    dominant = sorted.empty() ? "" : sorted[0].second;
    int shown = 0;
    for (const auto & [b, t] : sorted) {
        const int pct = total ? (int) (100.0 * (double) b / (double) total + 0.5) : 0;
        if (pct < 1 || shown == 4) {
            break;
        }
        out += (out.empty() ? "" : ", ") + t + " " + std::to_string(pct) + "%";
        shown++;
    }
    return out;
}

void warn(Plan & p, const std::string & w) {
    p.warnings.push_back(w);
}

}  // namespace

const char * family_name(Family f) {
    switch (f) {
        case Family::SD1: return "SD 1.x";
        case Family::SD2: return "SD 2.x";
        case Family::SDXS: return "SDXS";
        case Family::SDXL: return "SDXL";
        case Family::SD3: return "SD3.x";
        case Family::Flux1: return "FLUX.1";
        case Family::Flux2: return "FLUX.2";
        case Family::Chroma: return "Chroma";
        case Family::ZImage: return "Z-Image";
        case Family::QwenImage: return "Qwen-Image";
        case Family::PixArt: return "PixArt";
        case Family::OtherImage: return "other image model";
        case Family::Wan: return "Wan";
        case Family::OtherVideo: return "other video model";
        default: return "unknown";
    }
}

const char * fit_name(Fit f) {
    switch (f) {
        case Fit::AllOnGpu: return "all on GPU";
        case Fit::TextOnDisk: return "text encoders on disk";
        case Fit::AllOnDisk: return "weights on disk";
        case Fit::Segmented: return "segmented";
        default: return "too big";
    }
}

Plan plan_from_headers(const PlanInput & in, const std::vector<FileHeader> & headers) {
    Plan plan;
    plan.input = in;
    auto slot = [&](int i) -> const FileHeader * {
        return i < (int) headers.size() && !headers[i].path.empty() ? &headers[i] : nullptr;
    };
    const FileHeader * model = slot(kSlotModel);
    const FileHeader * diff  = slot(kSlotDiffusion);
    const FileHeader * main_file = diff ? diff : model;
    if (!main_file) {
        plan.error = "no model: put its path in /data/PS5SD/model.txt or --model in app-args.txt";
        return plan;
    }
    for (const FileHeader * h : { model, diff }) {
        if (h && !h->ok && h->format.empty() && h->file_bytes == 0) {
            plan.error = h->error;   // the file is not there
            return plan;
        }
    }

    // The tensors of every readable file, as the loader would name them.
    Pool pool;
    static const char * const kPrefix[kSlots] = { "", "", "", "", "te.", "te.1.", "", "" };
    for (int i = 0; i < kSlots; i++) {
        if (const FileHeader * h = slot(i)) {
            for (const TensorInfo & t : h->tensors) {
                pool.add(kPrefix[i], t);
            }
        }
    }

    // The file name, for what the tensors do not say.
    std::string hint = main_file->hint;
    if (model && diff) {
        hint += " " + model->hint;
    }
    const Variant var = variant_of(hint);

    const Detected det = detect(pool);
    V version = det.version;
    plan.detected_by = "tensors";
    const bool readable = main_file->ok;
    if (version == V_COUNT) {
        version = version_from_name(hint);
        plan.detected_by = "name";
        if (version == V_COUNT) {
            plan.error = readable ? "not a model stable-diffusion.cpp knows (no tensor names match a family)"
                                  : "cannot tell what " + base_name(main_file->path) + " is: " + main_file->error;
            return plan;
        }
        if (readable) {
            warn(plan, "the tensor names did not name a family; going by the file name");
        } else {
            warn(plan, main_file->error);
        }
    }
    if (version == V_ESRGAN) {
        plan.error = "an upscaler, not a generator";
        return plan;
    }

    // Family, settings and compute model.
    Spec spec = spec_for(version, det, var);
    plan.family = spec.family;
    plan.sd_version = kVersionNames[version];
    plan.is_video = spec.video;
    plan.needs_input = spec.needs_input;
    plan.compute = spec.compute;

    // The variants. Turbo, LCM and friends share their base's network, so the
    // tensors cannot tell them from it; the file name does.
    std::string label = spec.label;
    bool few_step = spec.distilled;
    bool name_used = false;
    const bool unet_family = spec.family == Family::SD1 || spec.family == Family::SD2 || spec.family == Family::SDXL;
    switch (spec.family) {
        case Family::SD2:
            if (var.turbo) { label = "SD-Turbo"; few_step = true; name_used = true; }
            break;
        case Family::SDXL:
            if (var.turbo) { label = "SDXL-Turbo"; few_step = true; name_used = true; }
            break;
        case Family::SD3:
            if (var.turbo) { label = "SD3.5 Large Turbo"; few_step = true; name_used = true; }
            break;
        case Family::Flux1:
            if (version != V_FLUX) {
                label = kVersionNames[version];   // Fill, Control, Flex.2: dev's network
            } else if (var.kontext) {
                label = "FLUX.1 Kontext";
                name_used = true;
            } else if (var.schnell || (!var.dev && !pool.entries.empty() && !det.guidance_in)) {
                // dev has the guidance embedder in the network; schnell has none.
                label = "FLUX.1 schnell";
                few_step = true;
                name_used = var.schnell;
                spec.steps = 4;
            } else {
                label = "FLUX.1 dev";
                name_used = var.dev;
            }
            break;
        case Family::Flux2:
            if (version == V_FLUX2_KLEIN || version == V_FLUX3) {
                if (var.base) {
                    label += " base";
                    spec.steps = 20;
                    spec.cfg = version == V_FLUX3 ? 3.5f : 4.0f;
                } else {
                    few_step = true;
                    spec.steps = version == V_FLUX3 ? 16 : 4;
                }
                name_used = var.base;
            }
            break;
        case Family::ZImage: {
            // Turbo and Base are one network; the file name tells (Comfy's
            // "z_image_bf16" is Base, "z_image_turbo_bf16" Turbo).
            const bool named_z = contains(hint, "z_image") || contains(hint, "z-image") || contains(hint, "zimage");
            if (var.turbo || (!var.base && !named_z)) {
                label = "Z-Image-Turbo";
                few_step = true;
                spec.steps = 8;
                name_used = var.turbo;
                if (!var.turbo) {
                    warn(plan, "assuming Z-Image-Turbo (8 steps, cfg 1); the file name says neither Turbo nor Base");
                }
            } else {
                label = "Z-Image Base";
                name_used = true;
            }
            break;
        }
        case Family::QwenImage:
        case Family::OtherImage:
            if (var.turbo || var.lightning || var.flash) { few_step = true; name_used = true; }
            break;
        default:
            break;
    }
    if (unet_family && var.lcm) {
        label += " (LCM)";
        spec.sampler = "lcm";
        spec.steps = 4;
        few_step = true;
        name_used = true;
    } else if (unet_family && var.dmd) {
        label += " (DMD)";
        spec.sampler = "lcm";
        spec.steps = 4;
        few_step = true;
        name_used = true;
    } else if (var.lightning || var.hyper) {
        label += var.lightning ? " (Lightning)" : " (Hyper-SD)";
        spec.steps = 4;
        few_step = true;
        name_used = true;
        if (unet_family) {
            spec.sampler = "euler";
            spec.scheduler = "sgm_uniform";
        }
    } else if (few_step && (unet_family || spec.family == Family::SD3 || spec.family == Family::QwenImage ||
                            spec.family == Family::OtherImage)) {
        spec.steps = spec.turbo_steps;   // Turbo-style: the family's few-step count
    }
    if (few_step) {
        spec.cfg = 1.0f;   // distilled models take no classifier-free guidance
    }
    if (name_used && plan.detected_by == "tensors") {
        plan.detected_by = "tensors+name";
    }
    plan.name = label;
    plan.distilled = few_step;

    // Where the weights are: per module, by what each file is.
    uint64_t total = 0;
    std::map<std::string, uint64_t> diffusion_types;
    bool has_vae = false, has_clip_l = false, has_clip_g = false, has_t5 = false, has_llm = false;
    const bool own_diffusion = diff != nullptr, own_vae = slot(kSlotVae) || slot(kSlotTaesd);
    auto account = [&](const FileHeader & h, int s) {
        if (!h.ok) {
            // A checkpoint whose header cannot be read: all of it, by its size, to the module of its role.
            const uint64_t b = h.file_bytes;
            (s == kSlotVae || s == kSlotTaesd ? plan.memory.vae_weights
             : s >= kSlotClipL                ? plan.memory.text_weights
                                              : plan.memory.diffusion_weights) += b;
            total += b;
            return;
        }
        for (const TensorInfo & t : h.tensors) {
            if (unused_tensor(t.name)) {
                continue;
            }
            Role role = s == kSlotDiffusion ? Role::Diffusion
                      : s == kSlotVae || s == kSlotTaesd ? Role::Vae
                      : s >= kSlotClipL ? Role::Text
                      : classify(t.name);
            if (s == kSlotModel && role == Role::Diffusion && own_diffusion) {
                continue;   // --diffusion-model replaces the checkpoint's
            }
            if (s == kSlotModel && role == Role::Vae && own_vae) {
                continue;   // so does --vae
            }
            const uint64_t b = weight_bytes_of(t);
            total += b;
            switch (role) {
                case Role::Diffusion:
                    plan.memory.diffusion_weights += b;
                    diffusion_types[t.dtype] += b;
                    break;
                case Role::Text: plan.memory.text_weights += b; break;
                case Role::Vae: plan.memory.vae_weights += b; break;
            }
            if (s == kSlotModel) {
                if (role == Role::Vae) has_vae = true;
                if (role == Role::Text) {
                    if (contains(t.name, "conditioner.embedders.1") || contains(t.name, "clip_g") || contains(t.name, "cond_stage_model.1")) has_clip_g = true;
                    else if (contains(t.name, "t5xxl") || contains(t.name, ".t5")) has_t5 = true;
                    else if (contains(t.name, "text_encoders.llm") || contains(t.name, "language_model")) has_llm = true;
                    else has_clip_l = true;
                }
            }
        }
    };
    for (int i = 0; i < kSlots; i++) {
        if (const FileHeader * h = slot(i)) {
            account(*h, i);
        }
    }
    has_vae |= own_vae;
    has_clip_l |= slot(kSlotClipL) != nullptr;
    has_clip_g |= slot(kSlotClipG) != nullptr;
    has_t5 |= slot(kSlotT5) != nullptr;
    has_llm |= slot(kSlotLlm) != nullptr;
    plan.weight_bytes = total;
    plan.dtype_summary = dtype_summary_of(diffusion_types, plan.quant);
    plan.compute.taesd = slot(kSlotTaesd) != nullptr && !slot(kSlotVae);
    if (plan.compute.big_text_encoder) {
        plan.compute.big_text_encoder = has_t5 || has_llm || spec.family != Family::SD3;
    }

    // --model for a whole checkpoint, --diffusion-model for a file of the network alone.
    plan.model_is_checkpoint = model != nullptr;
    if (model && !diff && model->ok) {
        bool any_other = false;
        for (const TensorInfo & t : model->tensors) {
            if (classify(t.name) != Role::Diffusion) {
                any_other = true;
                break;
            }
        }
        plan.model_is_checkpoint = any_other;
    }

    // Components the family needs and no file gives.
    auto need = [&](bool have, const char * what) {
        if (!have) {
            plan.missing.push_back(what);
        }
    };
    switch (spec.family) {
        case Family::SD1: case Family::SD2: case Family::SDXS:
            need(has_vae, "vae"); need(has_clip_l, "clip_l");
            break;
        case Family::SDXL:
            need(has_vae, "vae"); need(has_clip_l, "clip_l"); need(has_clip_g, "clip_g");
            break;
        case Family::SD3:
            need(has_vae, "vae"); need(has_clip_l, "clip_l"); need(has_clip_g, "clip_g");
            break;
        case Family::Flux1:
            need(has_vae, "vae"); need(has_clip_l, "clip_l"); need(has_t5, "t5xxl");
            break;
        case Family::Chroma:
            if (version != V_CHROMA_RADIANCE) need(has_vae, "vae");
            need(has_t5, "t5xxl");
            break;
        case Family::Flux2: case Family::ZImage: case Family::QwenImage:
            need(has_vae, "vae"); need(has_llm, "llm");
            break;
        case Family::PixArt: case Family::Wan:
            need(has_vae, "vae"); need(has_t5, "t5xxl");
            break;
        default:
            break;
    }
    if (!plan.missing.empty()) {
        std::string list;
        for (const auto & m : plan.missing) {
            list += (list.empty() ? "" : ", ") + m;
        }
        warn(plan, "needs " + list + ": not in the model file and not given");
    }
    if (spec.needs_input) {
        warn(plan, "an inpaint/edit/control model: it wants an input image, which the app's text-to-image path does not give");
    }
    if (spec.video) {
        warn(plan, "a video model: the app makes still images only");
    }
    if (!pool.entries.empty()) {
        for (const auto & [t, b] : diffusion_types) {
            if (starts_with(t, "F8")) {
                warn(plan, "8-bit float weights are expanded to F16 on load (counted at twice their size)");
                break;
            }
        }
    }

    // Size, placement and the largest size.
    const uint64_t budget = in.budget_bytes;
    plan.settings.steps = spec.steps;
    plan.settings.cfg_scale = spec.cfg;
    plan.settings.sampler = spec.sampler;
    plan.settings.scheduler = spec.scheduler;
    plan.settings.flash_attn = true;

    // The default size: the native one, or the largest smaller one (down to
    // two thirds of the native side) that fits with the weights at most on
    // disk; failing that, the native size run in segments, which is slow.
    const bool user_size = in.width > 0 && in.height > 0;
    int width = user_size ? in.width : spec.width;
    int height = user_size ? in.height : spec.height;
    const int native_w = width, native_h = height;
    Candidate c = place(plan, width, height, budget, Fit::AllOnDisk);
    if (c.fit == Fit::TooBig && !user_size) {
        const int shortest = std::min(native_w, native_h);
        const int floor_side = std::max(256, shortest * 2 / 3 / 64 * 64);
        for (int side = shortest - 64; side >= floor_side; side -= 64) {
            const int w = std::max(256, (int) ((long long) native_w * side / shortest) / 64 * 64);
            const int h = std::max(256, (int) ((long long) native_h * side / shortest) / 64 * 64);
            Candidate s = place(plan, w, h, budget, Fit::AllOnDisk);
            if (s.fit != Fit::TooBig) {
                warn(plan, "size reduced from " + std::to_string(native_w) + "x" + std::to_string(native_h) + " to " +
                               std::to_string(w) + "x" + std::to_string(h) + " to fit " +
                               fmt("%.1f", gib(budget)) + " GiB");
                width = w;
                height = h;
                c = s;
                break;
            }
        }
    }
    if (c.fit == Fit::TooBig) {
        // Segmented, at the native size or else the largest that fits that way.
        c = place(plan, width, height, budget, Fit::Segmented);
        for (int side = std::min(native_w, native_h) - 64; c.fit == Fit::TooBig && !user_size && side >= 256; side -= 64) {
            const int shortest = std::min(native_w, native_h);
            const int w = std::max(256, (int) ((long long) native_w * side / shortest) / 64 * 64);
            const int h = std::max(256, (int) ((long long) native_h * side / shortest) / 64 * 64);
            Candidate s = place(plan, w, h, budget, Fit::Segmented);
            if (s.fit != Fit::TooBig) {
                warn(plan, "size reduced from " + std::to_string(native_w) + "x" + std::to_string(native_h) + " to " +
                               std::to_string(w) + "x" + std::to_string(h) + " to fit " +
                               fmt("%.1f", gib(budget)) + " GiB");
                width = w;
                height = h;
                c = s;
            }
        }
        if (c.fit == Fit::Segmented) {
            warn(plan, "does not fit the GPU at once: the diffusion model runs in segments from disk (slow; not measured on the console)");
        }
    }
    plan.fit = c.fit;
    plan.memory = c.mem;
    plan.settings.width = width;
    plan.settings.height = height;
    plan.settings.vae_tiling = c.tiling;
    plan.settings.params_backend = params_backend_for(c.fit);
    plan.max_side = largest_side(plan, spec.max_side, budget, Fit::AllOnDisk);
    if (plan.max_side == 0) {
        plan.max_side = largest_side(plan, spec.max_side, budget, Fit::Segmented);
    }
    if (c.fit == Fit::TooBig) {
        const Compute small = compute_buffers(plan.compute, 256, 256, true);
        Memory m = plan.memory;
        m.compute_text = small.text;
        m.compute_diffusion = small.diffusion;
        m.compute_vae = small.vae;
        m.overhead = kMeasureOverhead;
        const uint64_t least = std::min(peak_of(m, Fit::Segmented), peak_of(m, Fit::AllOnDisk));
        plan.memory.peak = least;
        if (plan.max_side > 0) {
            warn(plan, "won't fit at " + std::to_string(width) + "x" + std::to_string(height) +
                           ": the largest size that fits " + fmt("%.1f", gib(budget)) + " GiB is " +
                           std::to_string(plan.max_side) + " px");
        } else {
            warn(plan, "won't fit: needs at least " + fmt("%.1f", gib(least)) + " GiB at 256x256, " +
                           fmt("%.1f", gib(budget)) + " GiB is free");
        }
    } else if (user_size && std::max(width, height) > plan.max_side && plan.max_side > 0) {
        warn(plan, "the size asked for is above the largest that fits (" + std::to_string(plan.max_side) + " px)");
    }

    for (const auto & w : plan.warnings) {
        plan.warning += (plan.warning.empty() ? "" : "; ") + w;
    }
    plan.ok = true;
    return plan;
}

uint64_t estimate_peak(const Plan & plan, int width, int height, Fit fit, bool vae_tiling) {
    Memory m = plan.memory;
    const Compute c = compute_buffers(plan.compute, width, height, vae_tiling);
    m.compute_text = c.text;
    m.compute_diffusion = c.diffusion;
    m.compute_vae = c.vae;
    m.overhead = kMeasureOverhead;
    return peak_of(m, fit);
}

Plan plan_generation(const PlanInput & in) {
    std::vector<FileHeader> headers(kSlots);
    const std::string * paths[kSlots] = { &in.model, &in.diffusion_model, &in.vae, &in.taesd,
                                          &in.clip_l, &in.clip_g, &in.t5xxl, &in.llm };
    for (int i = 0; i < kSlots; i++) {
        if (!paths[i]->empty()) {
            headers[i] = read_file_header(*paths[i]);
            if (!headers[i].ok && headers[i].file_bytes == 0) {
                Plan p;
                p.input = in;
                p.error = headers[i].error;
                return p;
            }
        }
    }
    return plan_from_headers(in, headers);
}

std::vector<std::string> Plan::to_args() const {
    std::vector<std::string> a;
    auto add = [&](const char * flag, const std::string & value) {
        if (!value.empty()) {
            a.push_back(flag);
            a.push_back(value);
        }
    };
    if (!input.diffusion_model.empty()) {
        add("--diffusion-model", input.diffusion_model);
        add("--model", input.model);
    } else {
        add(model_is_checkpoint ? "--model" : "--diffusion-model", input.model);
    }
    add("--vae", input.vae);
    add("--taesd", input.taesd);
    add("--clip_l", input.clip_l);
    add("--clip_g", input.clip_g);
    add("--t5xxl", input.t5xxl);
    add("--llm", input.llm);
    add("-W", std::to_string(settings.width));
    add("-H", std::to_string(settings.height));
    add("--steps", std::to_string(settings.steps));
    char cfg[32];
    std::snprintf(cfg, sizeof(cfg), "%g", (double) settings.cfg_scale);
    add("--cfg-scale", cfg);
    add("--sampling-method", settings.sampler);
    add("--scheduler", settings.scheduler);
    add("--flash-attn", settings.flash_attn ? "on" : "off");
    if (settings.vae_tiling) {
        a.push_back("--vae-tiling");
    }
    add("--params-backend", settings.params_backend);
    return a;
}

std::string Plan::summary() const {
    if (!ok) {
        return "no plan: " + error;
    }
    std::string s = name + " (" + sd_version + ", " + detected_by + "), " + fmt("%.1f", gib(weight_bytes)) + " GiB";
    if (!quant.empty()) {
        s += " " + quant;
    }
    s += ": " + std::to_string(settings.width) + "x" + std::to_string(settings.height) + ", " +
         std::to_string(settings.steps) + " steps, cfg " + fmt("%g", (double) settings.cfg_scale) + ", " +
         settings.sampler;
    s += std::string(", ") + fit_name(fit);
    if (settings.vae_tiling) {
        s += ", VAE tiled";
    }
    s += ", peak " + fmt("%.1f", gib(memory.peak)) + " GiB, up to " + std::to_string(max_side) + " px";
    if (!warning.empty()) {
        s += " [" + warning + "]";
    }
    return s;
}

}  // namespace ps5sd
