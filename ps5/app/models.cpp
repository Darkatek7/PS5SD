// SPDX-License-Identifier: GPL-3.0-or-later
#include "models.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;

namespace ps5sd {
namespace {

// Only the error_code overloads: on the console stat("") fails with EINVAL,
// which the throwing ones turn into an exception.
bool is_file(const fs::path & p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec) && !ec;
}

uint64_t size_of(const fs::path & p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_checkpoint_name(const std::string & name) {
    const std::string n = lower(name);
    for (const char * ext : { ".gguf", ".safetensors", ".ckpt", ".sft" }) {
        const std::string e = ext;
        if (n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

// A safe id: a file or preset name, never a path.
bool is_plain_name(const std::string & id) {
    return !id.empty() && id.find('/') == std::string::npos && id.find('\\') == std::string::npos && id != "." &&
           id != ".." && id.find("..") == std::string::npos;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

// "steps=25 cfg=6" -> {{"steps","25"},{"cfg","6"}}
std::vector<std::pair<std::string, std::string>> parse_defaults(const std::string & text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(text);
    for (std::string word; in >> word;) {
        const size_t eq = word.find('=');
        if (eq != std::string::npos && eq > 0) {
            out.emplace_back(word.substr(0, eq), word.substr(eq + 1));
        }
    }
    return out;
}

void add_files(ModelEntry & e) {
    for (size_t i = 0; i + 1 < e.args.size(); i++) {
        if (is_model_path_option(e.args[i])) {
            const std::string & f = e.args[i + 1];
            e.files.push_back(f);
            if (is_file(fs::u8path(f))) {
                e.bytes += size_of(fs::u8path(f));
            } else {
                e.missing.push_back(f);
            }
        }
    }
}

ModelEntry file_entry(const std::string & id, const std::string & path) {
    ModelEntry e;
    e.id   = id;
    e.name = fs::u8path(path).stem().u8string();
    e.kind = "file";
    e.args = { "-m", path };
    add_files(e);
    return e;
}

}  // namespace

bool is_model_path_option(const std::string & o) {
    static const char * const kOptions[] = {
        "-m",           "--model",       "--diffusion-model", "--high-noise-diffusion-model", "--vae",
        "--taesd",      "--clip_l",      "--clip_g",          "--clip_vision",                "--t5xxl",
        "--llm",        "--llm_vision",  "--control-net",     "--upscale-model",              "--photo-maker",
        "--audio-vae",  "--tokenizer",
    };
    for (const char * k : kOptions) {
        if (o == k) {
            return true;
        }
    }
    return false;
}

ModelEntry ModelLibrary::parse_preset(const std::string & id, const std::string & text) const {
    ModelEntry e;
    e.id   = id;
    e.name = id;
    e.kind = "preset";
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            const std::string body  = trim(line.substr(1));
            const size_t      colon = body.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            const std::string key   = lower(trim(body.substr(0, colon)));
            const std::string value = trim(body.substr(colon + 1));
            if (key == "name" && !value.empty()) {
                e.name = value;
            } else if (key == "defaults") {
                e.defaults = parse_defaults(value);
            }
            continue;
        }
        // A relative path after a model option comes from the models directory.
        if (!e.args.empty() && is_model_path_option(e.args.back()) && line[0] != '/' && line[0] != '-') {
            line = (fs::u8path(models_dir) / fs::u8path(line)).lexically_normal().u8string();
        }
        e.args.push_back(line);
    }
    add_files(e);
    return e;
}

std::vector<ModelEntry> ModelLibrary::list() const {
    std::vector<ModelEntry> presets, files;
    std::error_code         ec;
    for (fs::directory_iterator it(fs::u8path(presets_dir), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        if (lower(p.extension().u8string()) != ".txt" || !is_file(p)) {
            continue;
        }
        std::ifstream      f(p);
        std::ostringstream text;
        text << f.rdbuf();
        presets.push_back(parse_preset(p.stem().u8string(), text.str()));
    }
    ec.clear();
    for (fs::directory_iterator it(fs::u8path(models_dir), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path    p    = it->path();
        const std::string name = p.filename().u8string();
        if (is_checkpoint_name(name) && is_file(p)) {
            files.push_back(file_entry(name, p.u8string()));
        }
    }
    const auto by_id = [](const ModelEntry & a, const ModelEntry & b) { return a.id < b.id; };
    std::sort(presets.begin(), presets.end(), by_id);
    std::sort(files.begin(), files.end(), by_id);
    presets.insert(presets.end(), files.begin(), files.end());
    return presets;
}

bool ModelLibrary::find(const std::string & id_in, ModelEntry & out, std::string & error) const {
    const std::string id = trim(id_in);
    if (id.empty()) {
        error = "no model given";
        return false;
    }
    if (id[0] == '/') {  // model.txt from before presets: a checkpoint's path
        if (!is_file(fs::u8path(id))) {
            error = id + " is not there";
            return false;
        }
        const fs::path    p      = fs::u8path(id);
        const bool        inside = p.parent_path().lexically_normal() == fs::u8path(models_dir).lexically_normal();
        out                      = file_entry(inside ? p.filename().u8string() : id, id);
        return true;
    }
    if (!is_plain_name(id)) {
        error = "not a model name: " + id;
        return false;
    }
    const fs::path preset = fs::u8path(presets_dir) / fs::u8path(id + ".txt");
    if (is_file(preset)) {
        std::ifstream      f(preset);
        std::ostringstream text;
        text << f.rdbuf();
        out = parse_preset(id, text.str());
        if (!out.missing.empty()) {
            error = "preset " + id + " names a file that is not there: " + out.missing[0];
            return false;
        }
        return true;
    }
    const fs::path file = fs::u8path(models_dir) / fs::u8path(id);
    if (is_checkpoint_name(id) && is_file(file)) {
        out = file_entry(id, file.u8string());
        return true;
    }
    error = "no model " + id + " in " + models_dir + " or " + presets_dir;
    return false;
}

bool ModelLibrary::remove(const std::string & id_in, const std::vector<std::string> & keep,
                          std::vector<std::string> & deleted, std::vector<std::string> & kept,
                          std::string & error) const {
    const std::string id = trim(id_in);
    if (!is_plain_name(id)) {
        error = "not a model name: " + id;
        return false;
    }
    const fs::path root   = fs::u8path(models_dir).lexically_normal();
    const auto     inside = [&](const std::string & f) {
        const fs::path rel = fs::u8path(f).lexically_normal().lexically_relative(root);
        return !rel.empty() && rel.is_relative() && *rel.begin() != "..";
    };
    const auto     norm      = [](const std::string & f) { return fs::u8path(f).lexically_normal().u8string(); };
    const fs::path preset    = fs::u8path(presets_dir) / fs::u8path(id + ".txt");
    const bool     is_preset = is_file(preset);
    const fs::path file      = fs::u8path(models_dir) / fs::u8path(id);
    if (!is_preset && !(is_checkpoint_name(id) && is_file(file))) {
        error = "no model " + id + " in " + models_dir + " or " + presets_dir;
        return false;
    }
    // The files every other preset names, with that preset's id.
    std::vector<std::pair<std::string, std::string>> used;
    for (const ModelEntry & e : list()) {
        if (e.kind == "preset" && !(is_preset && e.id == id)) {
            for (const auto & f : e.files) {
                used.emplace_back(norm(f), e.id);
            }
        }
    }
    const auto user_of = [&](const std::string & f) -> std::string {
        for (const auto & u : used) {
            if (u.first == norm(f)) {
                return u.second;
            }
        }
        return {};
    };
    const auto kept_for_server = [&](const std::string & f) {
        for (const auto & k : keep) {
            if (norm(k) == norm(f)) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::string> files;
    if (is_preset) {
        std::ifstream      in(preset);
        std::ostringstream text;
        text << in.rdbuf();
        in.close();
        files = parse_preset(id, text.str()).files;
    } else {
        const std::string other = user_of(file.u8string());
        if (!other.empty()) {
            error = id + " is part of preset " + other + "; delete that preset instead";
            return false;
        }
        if (kept_for_server(file.u8string())) {
            error = id + " is the model the server runs";
            return false;
        }
        files = { file.u8string() };
    }
    std::error_code ec;
    for (const auto & f : files) {
        if (!is_file(fs::u8path(f))) {
            continue;  // missing already
        }
        if (!inside(f) || !user_of(f).empty() || kept_for_server(f)) {
            kept.push_back(f);
            continue;
        }
        if (!fs::remove(fs::u8path(f), ec) || ec) {
            error = "could not delete " + f + (ec ? ": " + ec.message() : "");
            return false;
        }
        deleted.push_back(f);
    }
    if (is_preset) {
        if (!fs::remove(preset, ec) || ec) {
            error = "could not delete " + preset.u8string() + (ec ? ": " + ec.message() : "");
            return false;
        }
        deleted.push_back(preset.u8string());
    }
    return true;
}

std::string models_json(const ModelLibrary & library, const std::string & active, const std::string & loaded,
                        bool switching, bool standby) {
    nlohmann::json list = nlohmann::json::array();
    for (const ModelEntry & e : library.list()) {
        nlohmann::json defaults = nlohmann::json::object();
        for (const auto & kv : e.defaults) {
            defaults[kv.first] = kv.second;
        }
        list.push_back({ { "id", e.id },
                         { "name", e.name },
                         { "kind", e.kind },
                         { "bytes", e.bytes },
                         { "files", e.files },
                         { "missing", e.missing },
                         { "defaults", defaults } });
    }
    nlohmann::json out = { { "active", active }, { "loaded", loaded }, { "switching", switching }, { "standby", standby }, { "models", list } };
    // A file name that is not UTF-8 must not throw on an HTTP thread.
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string removed_json(const ModelLibrary & library, const std::vector<std::string> & deleted,
                         const std::vector<std::string> & kept, const std::string & active, const std::string & loaded) {
    nlohmann::json out = nlohmann::json::parse(models_json(library, active, loaded, false));
    out["deleted"]     = deleted;
    out["kept"]        = kept;
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string loras_json(const std::string & dir) {
    std::vector<std::pair<std::string, uint64_t>> files;
    std::error_code                               ec;
    const fs::path                                root = fs::u8path(dir);
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && is_checkpoint_name(it->path().filename().u8string())) {
            const uint64_t size = it->file_size(e2);
            files.emplace_back(it->path().lexically_relative(root).generic_u8string(), e2 ? 0 : size);
        }
    }
    std::sort(files.begin(), files.end());
    nlohmann::json list = nlohmann::json::array();
    for (const auto & f : files) {
        list.push_back({ { "name", fs::u8path(f.first).stem().u8string() }, { "path", f.first }, { "bytes", f.second } });
    }
    return nlohmann::json{ { "loras", list } }.dump();
}

std::string sd_models_json(const ModelLibrary & library) {
    nlohmann::json list = nlohmann::json::array();
    for (const ModelEntry & e : library.list()) {
        if (!e.missing.empty() || e.files.empty()) {
            continue;
        }
        // No hashes: hashing gigabytes on the console for a label is not worth it.
        list.push_back({ { "title", e.id },
                         { "model_name", e.name },
                         { "filename", e.files.front() },
                         { "hash", nullptr },
                         { "sha256", nullptr },
                         { "config", nullptr } });
    }
    return list.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string openai_models_json(const ModelLibrary & library) {
    nlohmann::json list = nlohmann::json::array();
    for (const ModelEntry & e : library.list()) {
        if (e.missing.empty() && !e.files.empty()) {
            list.push_back({ { "id", e.id }, { "object", "model" }, { "owned_by", "ps5sd" } });
        }
    }
    const nlohmann::json out = { { "object", "list" }, { "data", list } };
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string checkpoint_id(const ModelLibrary & library, const std::string & checkpoint) {
    std::string name = trim(checkpoint);
    if (name.size() > 2 && name.back() == ']') {
        const size_t open = name.rfind(" [");
        if (open != std::string::npos) {
            name = trim(name.substr(0, open));
        }
    }
    if (name.empty()) {
        return {};
    }
    const std::vector<ModelEntry> entries = library.list();
    for (const ModelEntry & e : entries) {
        if (e.id == name) {
            return e.id;
        }
    }
    for (const ModelEntry & e : entries) {
        if (e.name == name) {
            return e.id;
        }
    }
    return {};
}

std::string options_json(const std::string & checkpoint) {
    const nlohmann::json out = { { "samples_format", "png" }, { "sd_model_checkpoint", checkpoint } };
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string checkpoint_from_options(const std::string & body) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_object() && j.contains("sd_model_checkpoint") && j["sd_model_checkpoint"].is_string()) {
        return j["sd_model_checkpoint"].get<std::string>();
    }
    return {};
}

std::string info_json(const std::string & version, const std::string & sd_version, const std::string & sd_commit) {
    const nlohmann::json out = { { "name", "PS5SD" },
                                 { "version", version },
                                 { "sd_cpp", { { "version", sd_version }, { "commit", sd_commit } } } };
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string error_json(const std::string & message) {
    return nlohmann::json{ { "error", message } }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string id_from_json(const std::string & body) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_object() && j.contains("id") && j["id"].is_string()) {
        return j["id"].get<std::string>();
    }
    return {};
}

std::string settings_json(const AppSettings & settings) {
    return nlohmann::json{ { "tv_images", settings.tv_images } }.dump();
}

bool apply_settings(const std::string & body, AppSettings & settings, std::string & error) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_object()) {
        error = "the body must be a JSON object, like {\"tv_images\": false}";
        return false;
    }
    AppSettings next = settings;
    if (j.contains("tv_images")) {
        if (!j["tv_images"].is_boolean()) {
            error = "tv_images must be true or false";
            return false;
        }
        next.tv_images = j["tv_images"].get<bool>();
    }
    settings = next;
    return true;
}

}  // namespace ps5sd
