// What net_family.hpp says.
#include "net_family.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "json.hpp"

namespace fs = std::filesystem;

namespace ps5sd {

namespace {

// ---------- reading the tensor list ----------

struct Reader {
    std::ifstream & in;
    bool            bad = false;

    template <typename T> T get() {
        T v{};
        if (!in.read(reinterpret_cast<char *>(&v), sizeof v)) {
            bad = true;
        }
        return v;
    }
    std::string str() {
        const uint64_t n = get<uint64_t>();
        if (bad || n > (1u << 20)) {
            bad = true;
            return {};
        }
        std::string s(n, '\0');
        if (n && !in.read(&s[0], (std::streamsize) n)) {
            bad = true;
        }
        return s;
    }
    void skip(uint64_t n) {
        in.seekg((std::streamoff) n, std::ios::cur);
        bad = bad || !in;
    }
    // One metadata value of GGUF type t (the tokenizer's arrays hold 100k+ strings: skipped, not kept).
    void skip_value(uint32_t t, int depth = 0) {
        static const int size[] = { 1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8 };
        if (t == 8) {
            const uint64_t n = get<uint64_t>();
            skip(n);
        } else if (t == 9 && depth == 0) {
            const uint32_t et = get<uint32_t>();
            const uint64_t n  = get<uint64_t>();
            if (et < 13 && et != 8 && et != 9) {
                skip(n * size[et]);
            } else {
                for (uint64_t i = 0; i < n && !bad; i++) {
                    skip_value(et, depth + 1);
                }
            }
        } else if (t < 13 && t != 8 && t != 9) {
            skip(size[t]);
        } else {
            bad = true;
        }
    }
};

bool read_gguf(std::ifstream & in, std::vector<NetTensor> & out) {
    Reader         r{ in };
    const uint32_t version = r.get<uint32_t>();
    const uint64_t n_t     = r.get<uint64_t>();
    const uint64_t n_kv    = r.get<uint64_t>();
    if (r.bad || version < 2 || n_t > 200000 || n_kv > 100000) {
        return false;
    }
    for (uint64_t i = 0; i < n_kv && !r.bad; i++) {
        r.str();
        r.skip_value(r.get<uint32_t>());
    }
    for (uint64_t i = 0; i < n_t && !r.bad; i++) {
        NetTensor      t;
        t.name             = r.str();
        const uint32_t nd  = r.get<uint32_t>();
        if (nd > 8) {
            return false;
        }
        for (uint32_t d = 0; d < nd; d++) {
            t.shape.push_back((int64_t) r.get<uint64_t>());
        }
        std::reverse(t.shape.begin(), t.shape.end());  // ggml's ne[0] is torch's last dimension
        r.get<uint32_t>();                             // type
        r.get<uint64_t>();                             // offset
        out.push_back(std::move(t));
    }
    return !r.bad;
}

bool read_safetensors(std::ifstream & in, std::vector<NetTensor> & out) {
    uint64_t n = 0;
    in.seekg(0);
    if (!in.read(reinterpret_cast<char *>(&n), sizeof n) || n < 2 || n > (100u << 20)) {
        return false;
    }
    std::string head(n, '\0');
    if (!in.read(&head[0], (std::streamsize) n)) {
        return false;
    }
    const nlohmann::json j = nlohmann::json::parse(head, nullptr, false);
    if (!j.is_object()) {
        return false;
    }
    for (const auto & kv : j.items()) {
        if (kv.key() == "__metadata__" || !kv.value().is_object()) {
            continue;
        }
        const auto it = kv.value().find("shape");
        if (it == kv.value().end() || !it->is_array()) {
            continue;
        }
        NetTensor t;
        t.name = kv.key();
        for (const auto & d : *it) {
            t.shape.push_back(d.is_number_integer() ? d.get<int64_t>() : 0);
        }
        out.push_back(std::move(t));
    }
    return true;
}

// ---------- telling the network ----------

// A tensor name as the network holds it, lower case with '_' as '.' (kohya
// writes double_blocks_3_img_mlp_0 for double_blocks.3.img_mlp.0), and what
// the tensor is: a weight, a LoRA's down (A) or up (B) half, or other.
enum class Role { weight, down, up, other };

struct Key {
    std::string k;
    Role        role = Role::other;
};

bool starts(const std::string & s, const std::string & p) { return s.compare(0, p.size(), p) == 0; }

bool ends(const std::string & s, const std::string & p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

Key key_of(const std::string & name) {
    std::string s = name;
    // the wrappers of checkpoints and LoRAs (diffusers, kohya, PEFT)
    for (bool again = true; again;) {
        again = false;
        for (const char * p : { "model.diffusion_model.", "diffusion_model.", "transformer.", "lora_unet_",
                                "lora_transformer_", "base_model.model.", "unet." }) {
            if (starts(s, p)) {
                s     = s.substr(std::strlen(p));
                again = true;
            }
        }
    }
    for (char & c : s) {
        c = c == '_' ? '.' : (char) std::tolower((unsigned char) c);
    }
    Key out;
    for (const auto & r : std::initializer_list<std::pair<const char *, Role>>{
             { ".lora.down.weight", Role::down }, { ".lora.a.weight", Role::down }, { ".lora.up.weight", Role::up },
             { ".lora.b.weight", Role::up },
             // PEFT names the adapter: "...lora_A.default.weight"
             { ".lora.a.default.weight", Role::down }, { ".lora.b.default.weight", Role::up },
             { ".weight", Role::weight } }) {
        if (ends(s, r.first)) {
            out.k    = s.substr(0, s.size() - std::strlen(r.first));
            out.role = r.second;
            return out;
        }
    }
    out.k = s;
    return out;
}

// The number after a prefix ("double.blocks.12.img..." -> 12), or -1.
int index_after(const std::string & k, const std::string & prefix) {
    if (!starts(k, prefix) || k.size() <= prefix.size() || !std::isdigit((unsigned char) k[prefix.size()])) {
        return -1;
    }
    return std::atoi(k.c_str() + prefix.size());
}

// The rest after "<prefix><number>.", or "" when k does not start so.
std::string after_index(const std::string & k, const std::string & prefix) {
    if (index_after(k, prefix) < 0) {
        return "";
    }
    size_t i = prefix.size();
    while (i < k.size() && std::isdigit((unsigned char) k[i])) {
        i++;
    }
    return i < k.size() && k[i] == '.' ? k.substr(i + 1) : "";
}

// A weight's or a LoRA down half's input width; a weight's or an up half's output width.
int64_t in_dim(const Key & key, const NetTensor & t) {
    return (key.role == Role::weight || key.role == Role::down) && t.shape.size() >= 2 ? t.shape[1] : 0;
}

int64_t out_dim(const Key & key, const NetTensor & t) {
    return (key.role == Role::weight || key.role == Role::up) && t.shape.size() >= 2 ? t.shape[0] : 0;
}

}  // namespace

bool read_net_tensors(const std::string & path, std::vector<NetTensor> & out) {
    out.clear();
    std::ifstream in(fs::u8path(path), std::ios::binary);
    char          magic[4] = {};
    if (!in.read(magic, 4)) {
        return false;
    }
    if (std::memcmp(magic, "GGUF", 4) == 0) {
        return read_gguf(in, out);
    }
    return read_safetensors(in, out);
}

std::string net_family_of(const std::vector<NetTensor> & tensors) {
    int     flux_double = 0, flux_single = 0;  // the highest block index + 1
    bool    flux3 = false, bfl_double = false, sd3 = false, wan = false, zimage = false, unet = false;
    int64_t hidden = 0, mlp = 0, linear1 = 0, wan_dim = 0, context = 0;
    for (const NetTensor & t : tensors) {
        const Key key = key_of(t.name);
        if (key.role == Role::other) {
            continue;
        }
        const std::string & k = key.k;
        std::string         rest;
        if (k.find("content.mode.blocks") != std::string::npos || k.find("txt.mode.blocks") != std::string::npos ||
            ends(after_index(k, "single.blocks."), "mlp.in")) {
            flux3 = true;
        }
        if (!(rest = after_index(k, "double.blocks.")).empty() || !(rest = after_index(k, "transformer.blocks.")).empty()) {
            bfl_double  = bfl_double || index_after(k, "double.blocks.") >= 0;
            flux_double = std::max(flux_double, std::max(index_after(k, "double.blocks."), index_after(k, "transformer.blocks.")) + 1);
            if (rest == "img.attn.qkv" || rest == "attn.to.q") {
                hidden = std::max(hidden, in_dim(key, t));
            }
            // diffusers names FLUX.2's gated MLP ff.linear_in
            if (rest == "img.mlp.0" || rest == "ff.net.0.proj" || rest == "ff.linear.in") {
                mlp = std::max(mlp, out_dim(key, t));
            }
        }
        if (!(rest = after_index(k, "single.blocks.")).empty() || !(rest = after_index(k, "single.transformer.blocks.")).empty()) {
            flux_single = std::max(flux_single, std::max(index_after(k, "single.blocks."), index_after(k, "single.transformer.blocks.")) + 1);
            if (rest == "linear1" || rest == "attn.to.qkv.mlp.proj") {  // the second: diffusers' FLUX.2
                linear1 = std::max(linear1, out_dim(key, t));
                hidden  = std::max(hidden, in_dim(key, t));
            }
        }
        sd3 = sd3 || index_after(k, "joint.blocks.") >= 0;
        rest = after_index(k, "blocks.");
        if (starts(rest, "self.attn") || starts(rest, "cross.attn") || starts(rest, "ffn")) {
            wan = true;
            if (rest == "self.attn.q") {
                wan_dim = std::max(wan_dim, std::max(in_dim(key, t), out_dim(key, t)));
            }
        }
        rest = after_index(k, "layers.");
        if (starts(rest, "attention") || starts(rest, "feed.forward") || starts(rest, "adaln") ||
            starts(k, "noise.refiner") || starts(k, "context.refiner")) {
            zimage = true;
        }
        if (ends(k, ".attn2.to.k") || ends(k, ".attn2.to.v")) {
            unet    = true;
            context = std::max(context, in_dim(key, t));
        }
    }

    if (flux3) {
        return "FLUX.3 Klein";
    }
    // diffusers' transformer_blocks alone are SD 3 as well: FLUX only with single blocks or FLUX.1's width
    if (flux_single || bfl_double || (flux_double && hidden == 3072)) {
        // FLUX.1 and FLUX.2 share the names; the MLP says which (4x wide, or gated 3x: 6x),
        // else the block count (FLUX.2 Klein 4B has 5 double and 20 single blocks).
        int gen = 0;
        if (hidden && mlp) {
            gen = mlp == 4 * hidden ? 1 : mlp == 6 * hidden ? 2 : 0;
        } else if (hidden && linear1) {
            gen = linear1 == 7 * hidden ? 1 : linear1 == 9 * hidden ? 2 : 0;
        }
        if (!gen && hidden && hidden != 3072) {
            gen = 2;  // FLUX.1 is 3072 wide
        }
        // FLUX.2 has at most 8 double blocks (XLabs LoRAs name no layer this reads, only blocks)
        if (!gen && (flux_double > 8 || (hidden == 3072 && (flux_double > 5 || flux_single > 20)))) {
            gen = 1;
        }
        if (gen == 1) {
            return "FLUX.1";
        }
        if (gen == 2) {
            return hidden == 3072 ? "FLUX.2 Klein 4B" : hidden == 4096 ? "FLUX.2 Klein 9B" : hidden == 6144 ? "FLUX.2 Dev" : "FLUX.2";
        }
        return "FLUX";
    }
    if (sd3) {
        return "SD 3";
    }
    if (wan) {
        return wan_dim == 1536 ? "Wan 1.3B" : wan_dim == 3072 ? "Wan 5B" : wan_dim == 5120 ? "Wan 14B" : "Wan";
    }
    if (zimage) {
        return "Z-Image";
    }
    if (unet) {
        return context == 2048 ? "SDXL" : context == 1024 ? "SD 2.1" : context == 768 ? "SD 1.5" : "";
    }
    return "";
}

std::string net_part_of(const std::vector<NetTensor> & tensors) {
    bool text_encoder = false, vae = false;
    for (const NetTensor & t : tensors) {
        const std::string k = key_of(t.name).k;
        // CLIP (text_model, OpenCLIP's resblocks), T5 (encoder.block), an LLM
        // (llama.cpp's blk.N and token_embd, or Hugging Face's layers.N.self_attn),
        // and the text encoders inside a checkpoint (cond_stage_model, conditioner)
        if (k.find("text.model.") != std::string::npos || k.find("resblocks.") != std::string::npos ||
            k.find("encoder.block.") != std::string::npos || starts(k, "blk.") || starts(k, "enc.blk.") ||
            starts(k, "token.embd") || (k.find("layers.") != std::string::npos && k.find(".self.attn.") != std::string::npos) ||
            starts(k, "cond.stage.model.") || starts(k, "conditioner.embedders.")) {
            text_encoder = true;
        }
        // a VAE's decoder (ldm and diffusers: conv_in; Wan: conv1 and upsamples)
        if (k.find("decoder.conv.in") != std::string::npos || k.find("decoder.conv1") != std::string::npos ||
            k.find("decoder.upsamples.") != std::string::npos) {
            vae = true;
        }
    }
    const bool net = !net_family_of(tensors).empty();
    if (net) {
        return text_encoder ? "" : "diffusion model";
    }
    return text_encoder ? "text encoder" : vae ? "VAE" : "";
}

namespace {

struct Seen {
    uint64_t           size;
    fs::file_time_type time;
    std::string        family, part;
};

// A file's family and part, read once per path, size and modification time.
bool seen(const std::string & path, Seen & out) {
    static std::mutex                   mu;
    static std::map<std::string, Seen> cache;

    std::error_code ec;
    const fs::path  p    = fs::u8path(path);
    const uint64_t  size = fs::file_size(p, ec);
    if (ec) {
        return false;
    }
    const fs::file_time_type time = fs::last_write_time(p, ec);
    {
        std::lock_guard<std::mutex> lock(mu);
        const auto                  it = cache.find(path);
        if (it != cache.end() && it->second.size == size && it->second.time == time) {
            out = it->second;
            return true;
        }
    }
    std::vector<NetTensor> tensors;
    out = Seen{ size, time, "", "" };
    if (read_net_tensors(path, tensors)) {
        out.family = net_family_of(tensors);
        out.part   = net_part_of(tensors);
    }
    std::lock_guard<std::mutex> lock(mu);
    cache[path] = out;
    return true;
}

}  // namespace

std::string net_part(const std::string & path) {
    Seen s;
    return seen(path, s) ? s.part : "";
}

std::string net_family(const std::string & path) {
    Seen s;
    return seen(path, s) ? s.family : "";
}

}  // namespace ps5sd
