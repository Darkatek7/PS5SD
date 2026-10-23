// SPDX-License-Identifier: GPL-3.0-or-later
#include "download_core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "catalog.hpp"
#include "json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace ps5sd {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with(const std::string & s, const std::string & p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::vector<std::string> split(const std::string & s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t next = s.find(sep, at);
        out.push_back(s.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) {
            return out;
        }
        at = next + 1;
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode(const std::string & s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0 && hex_value(s[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// The value of name=... in a query string, "" when it is not there.
std::string query_param(const std::string & query, const std::string & name) {
    for (const std::string & kv : split(query, '&')) {
        const size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == name) {
            return percent_decode(kv.substr(eq + 1));
        }
    }
    return {};
}

bool all_digits(const std::string & s) {
    return !s.empty() && s.size() < 20 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// Strings and numbers out of parsed JSON without throwing on a wrong type.
std::string str_of(const json & j, const char * key) {
    if (j.is_object()) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }
    return {};
}

uint64_t u64_of(const json & j, const char * key) {
    if (j.is_object()) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_number()) {
            const double v = it->get<double>();
            return v > 0 ? static_cast<uint64_t>(v + 0.5) : 0;
        }
    }
    return 0;
}

std::string dump(const json & j) {
    // A file name that is not UTF-8 must not throw on an HTTP thread.
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool is_file_of_size(const fs::path & p, uint64_t bytes) {
    std::error_code ec;
    if (p.empty() || !fs::is_regular_file(p, ec) || ec) {
        return false;
    }
    const auto n = fs::file_size(p, ec);
    return !ec && static_cast<uint64_t>(n) == bytes;
}

bool is_file(const fs::path & p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec) && !ec;
}

}  // namespace

// ---- URLs ------------------------------------------------------------------------

bool parse_url(const std::string & text, Url & out) {
    const size_t sep = text.find("://");
    if (sep == std::string::npos) {
        return false;
    }
    out.scheme = lower(text.substr(0, sep));
    if (out.scheme != "https" && out.scheme != "http") {
        return false;
    }
    const size_t host_at = sep + 3;
    const size_t end     = text.find_first_of("/?#", host_at);
    std::string  hostport = text.substr(host_at, end == std::string::npos ? std::string::npos : end - host_at);
    if (hostport.find('@') != std::string::npos) {
        return false;  // no user:password@
    }
    out.port         = out.scheme == "https" ? 443 : 80;
    const size_t col = hostport.rfind(':');
    if (col != std::string::npos) {
        const std::string p = hostport.substr(col + 1);
        if (!all_digits(p) || std::stoi(p) <= 0 || std::stoi(p) > 65535) {
            return false;
        }
        out.port = std::stoi(p);
        hostport = hostport.substr(0, col);
    }
    out.host = lower(hostport);
    if (out.host.empty()) {
        return false;
    }
    for (unsigned char c : out.host) {
        if (!(std::isalnum(c) || c == '.' || c == '-')) {
            return false;
        }
    }
    std::string target = end == std::string::npos ? "/" : text.substr(end);
    const size_t hash  = target.find('#');
    if (hash != std::string::npos) {
        target = target.substr(0, hash);
    }
    if (target.empty() || target[0] == '?') {
        target = "/" + target;
    }
    for (unsigned char c : target) {
        if (c <= ' ' || c == 0x7f) {
            return false;  // spaces and control characters must be encoded
        }
    }
    out.target = target;
    return true;
}

std::string url_string(const Url & u) {
    const bool default_port = (u.scheme == "https" && u.port == 443) || (u.scheme == "http" && u.port == 80);
    return u.scheme + "://" + u.host + (default_port ? "" : ":" + std::to_string(u.port)) + u.target;
}

std::string resolve_location(const Url & base, const std::string & location) {
    if (location.find("://") != std::string::npos) {
        return location;
    }
    Url u = base;
    if (starts_with(location, "//")) {
        return base.scheme + ":" + location;
    }
    if (!location.empty() && location[0] == '/') {
        u.target = location;
    } else {
        const std::string path = base.target.substr(0, base.target.find('?'));
        u.target = path.substr(0, path.rfind('/') + 1) + location;
    }
    return url_string(u);
}

bool classify_url(const std::string & text, Source & out, std::string & error) {
    Url u;
    if (!parse_url(text, u) || u.scheme != "https") {
        error = "not an https URL: " + text;
        return false;
    }
    const size_t      q     = u.target.find('?');
    const std::string path  = u.target.substr(0, q);
    const std::string query = q == std::string::npos ? "" : u.target.substr(q + 1);
    const auto        parts = split(path.substr(1), '/');
    out = Source{};
    if (u.host == "huggingface.co" || u.host == "www.huggingface.co" || u.host == "hf.co") {
        // /<owner>/<repo>/{resolve,blob}/<rev>/<path...>
        if (parts.size() < 5 || (parts[2] != "resolve" && parts[2] != "blob") || parts[0].empty() ||
            parts[1].empty() || parts[3].empty() || parts.back().empty()) {
            error = "a Hugging Face file URL looks like https://huggingface.co/<owner>/<repo>/resolve/<revision>/<file>";
            return false;
        }
        for (size_t i = 4; i < parts.size(); i++) {
            const std::string seg = percent_decode(parts[i]);
            if (seg.empty() || seg == "." || seg == "..") {
                error = "not a file path: " + path;
                return false;
            }
        }
        out.kind      = "hf";
        out.hf_repo   = parts[0] + "/" + parts[1];
        out.file_name = percent_decode(parts.back());
        std::string target = "/" + parts[0] + "/" + parts[1] + "/resolve";
        for (size_t i = 3; i < parts.size(); i++) {
            target += "/" + parts[i];
        }
        // Query parameters (?download=true) are not needed and not kept.
        out.url = "https://huggingface.co" + target;
        return true;
    }
    if (u.host == "civitai.com" || u.host == "www.civitai.com") {
        std::string version;
        if (parts.size() >= 4 && parts[0] == "api" && parts[1] == "download" && parts[2] == "models") {
            version          = parts[3];
            out.civitai_file = query_param(query, "fileId");
        } else if (parts.size() >= 2 && parts[0] == "models" && all_digits(parts[1])) {
            version = query_param(query, "modelVersionId");
            if (version.empty()) {
                error = "a Civitai model page URL needs ?modelVersionId=<version> (open the version, then copy the URL)";
                return false;
            }
        }
        if (!all_digits(version) || (!out.civitai_file.empty() && !all_digits(out.civitai_file))) {
            error = "a Civitai URL looks like https://civitai.com/models/<id>?modelVersionId=<v> or "
                    "https://civitai.com/api/download/models/<v>";
            return false;
        }
        out.kind            = "civitai";
        out.civitai_version = version;
        out.url = "https://civitai.com/api/download/models/" + version +
                  (out.civitai_file.empty() ? "" : "?fileId=" + out.civitai_file);
        return true;
    }
    error = "only Hugging Face (huggingface.co) and Civitai (civitai.com) URLs are taken";
    return false;
}

bool is_model_file_name(const std::string & name) {
    const std::string n = lower(name);
    for (const char * ext : { ".gguf", ".safetensors", ".ckpt", ".sft" }) {
        const std::string e = ext;
        if (n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

bool is_upscaler_file_name(const std::string & name) {
    const std::string n = lower(name);
    for (const char * ext : { ".pth", ".pt", ".safetensors", ".gguf" }) {
        const std::string e = ext;
        if (n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

bool valid_dest(const std::string & dest, std::string & error) {
    if (dest.empty() || dest.size() > 240) {
        error = "dest must be a file name or a relative path of at most 240 bytes";
        return false;
    }
    if (dest[0] == '/' || dest.find('\\') != std::string::npos || dest.find(':') != std::string::npos) {
        error = "dest must be relative to the models directory: " + dest;
        return false;
    }
    for (unsigned char c : dest) {
        if (c < 0x20 || c == 0x7f) {
            error = "dest has a control character";
            return false;
        }
    }
    const auto segs = split(dest, '/');
    if (segs.size() > 4) {
        error = "dest is nested too deep: " + dest;
        return false;
    }
    for (const std::string & s : segs) {
        if (s.empty() || s[0] == '.' || s == "..") {
            error = "dest may not have empty, hidden or .. parts: " + dest;
            return false;
        }
    }
    if (!is_model_file_name(segs.back())) {
        error = "only .gguf, .safetensors, .ckpt and .sft files go into the models directory: " + dest;
        return false;
    }
    return true;
}

std::string download_id(const std::string & dest) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : dest) {
        h = (h ^ c) * 1099511628211ull;
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(h));
    return text;
}

std::string content_disposition_name(const std::string & header) {
    std::string name;
    for (std::string part : split(header, ';')) {
        while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) {
            part.erase(0, 1);
        }
        const size_t eq = part.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key   = lower(part.substr(0, eq));
        std::string       value = part.substr(eq + 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) {
            value.pop_back();
        }
        if (key == "filename*") {
            const size_t q = value.find("''");  // UTF-8''name
            name = percent_decode(q == std::string::npos ? value : value.substr(q + 2));
            break;  // the extended form wins
        }
        if (key == "filename") {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            name = value;
        }
    }
    // Never a path.
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name == "." || name == "..") {
        name.clear();
    }
    return name;
}

int64_t content_range_total(const std::string & header) {
    const size_t slash = header.rfind('/');
    if (slash == std::string::npos) {
        return -1;
    }
    std::string t = header.substr(slash + 1);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) {
        t.pop_back();
    }
    return all_digits(t) ? static_cast<int64_t>(std::stoll(t)) : -1;
}

std::string url_encode(const std::string & s) {
    std::string out;
    char hex[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

// ---- the catalog -------------------------------------------------------------------

const std::vector<CatalogEntry> & catalog() {
    static const std::vector<CatalogEntry> entries = catalog_data::entries();
    return entries;
}

const CatalogEntry * find_catalog(const std::string & id) {
    for (const CatalogEntry & e : catalog()) {
        if (id == e.id) {
            return &e;
        }
    }
    return nullptr;
}

bool catalog_file_present(const CatalogFile & f, const std::string & models_dir) {
    const fs::path p = fs::u8path(models_dir) / fs::u8path(f.dest);
    return is_file_of_size(p, f.bytes) || (f.alt_bytes != 0 && is_file_of_size(p, f.alt_bytes));
}

std::vector<std::string> update_catalog_presets(const std::string & presets_dir) {
    std::vector<std::string> updated;
    for (const auto & old : catalog_data::old_presets()) {
        const CatalogEntry * e    = find_catalog(old.first);
        const fs::path       path = fs::u8path(presets_dir) / fs::u8path(std::string(old.first) + ".txt");
        std::ifstream        in(path, std::ios::binary);
        if (e == nullptr || !in) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (text != old.second) {
            continue;
        }
        const fs::path tmp = fs::u8path(path.u8string() + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << e->preset;
            if (!out) {
                continue;
            }
        }
        std::error_code ec;
        fs::rename(tmp, path, ec);
        if (!ec) {
            updated.push_back(e->id);
        }
    }
    return updated;
}

bool catalog_files_present(const CatalogEntry & e, const std::string & models_dir) {
    for (const CatalogFile & f : e.files) {
        if (!catalog_file_present(f, models_dir)) {
            return false;
        }
    }
    return true;
}

std::string catalog_json(const std::string & models_dir, const std::string & presets_dir) {
    json list = json::array();
    for (const CatalogEntry & e : catalog()) {
        json     files = json::array();
        uint64_t bytes = 0;
        for (const CatalogFile & f : e.files) {
            files.push_back({ { "url", f.url },
                              { "dest", f.dest },
                              { "bytes", f.bytes },
                              { "installed", catalog_file_present(f, models_dir) } });
            bytes += f.bytes;
        }
        const bool installed = catalog_files_present(e, models_dir) &&
                               is_file(fs::u8path(presets_dir) / fs::u8path(std::string(e.id) + ".txt"));
        list.push_back({ { "id", e.id },
                         { "name", e.name },
                         { "description", e.description },
                         { "bytes", bytes },
                         { "installed", installed },
                         { "featured", e.featured },
                         { "good_for", e.good_for },
                         { "files", files },
                         { "preset", e.preset } });
    }
    return dump(json{ { "entries", list } });
}

// ---- the queue ---------------------------------------------------------------------

namespace {

json record_object(const DownloadRecord & d) {
    return { { "id", d.id },
             { "url", d.url },
             { "dest", d.dest },
             { "kind", d.kind },
             { "bytes", d.bytes },
             { "total", d.total },
             { "state", d.state },
             { "error", d.error },
             { "bytes_per_second", static_cast<uint64_t>(d.bytes_per_second) },
             { "catalog", d.catalog } };
}

}  // namespace

std::string download_json(const DownloadRecord & d) {
    return dump(record_object(d));
}

std::string downloads_json(const std::vector<DownloadRecord> & list) {
    json arr = json::array();
    for (const auto & d : list) {
        arr.push_back(record_object(d));
    }
    return dump(json{ { "downloads", arr } });
}

std::string queue_to_json(const std::vector<DownloadRecord> & list) {
    json arr = json::array();
    for (const auto & d : list) {
        json o     = record_object(d);
        o.erase("bytes_per_second");
        o["piece"]  = d.piece;
        o["pieces"] = d.pieces;
        arr.push_back(o);
    }
    return json{ { "version", 1 }, { "downloads", arr } }.dump(1, ' ', false, json::error_handler_t::replace) + "\n";
}

uint64_t pieces_bytes(const std::string & pieces, uint64_t piece, uint64_t total) {
    if (piece == 0 || total == 0) {
        return 0;
    }
    uint64_t n = 0;
    for (size_t i = 0; i < pieces.size(); i++) {
        if (pieces[i] == '1') {
            const uint64_t start = static_cast<uint64_t>(i) * piece;
            if (start < total) {
                n += std::min(piece, total - start);
            }
        }
    }
    return n;
}

bool pieces_within(const std::string & pieces, uint64_t piece, uint64_t total, uint64_t file_size) {
    for (size_t i = 0; i < pieces.size(); i++) {
        if (pieces[i] == '1') {
            const uint64_t start = static_cast<uint64_t>(i) * piece;
            const uint64_t end   = start < total ? start + std::min(piece, total - start) : total;
            if (piece == 0 || end > file_size) {
                return false;
            }
        }
    }
    return true;
}

bool upload_target(const std::string & path, std::string & kind, std::string & name, std::string & error) {
    const size_t slash = path.find('/');
    kind               = path.substr(0, slash);
    name               = slash == std::string::npos ? std::string() : path.substr(slash + 1);
    if (kind != "models" && kind != "loras" && kind != "embeddings" && kind != "presets" && kind != "upscalers" &&
        kind != "tags") {
        error = "uploads go to models, loras, embeddings, presets, upscalers or tags, not '" + kind + "'";
        return false;
    }
    if (kind == "models") {
        return valid_dest(name, error);
    }
    if (name.find('/') != std::string::npos) {
        error = "no folders in " + kind + ": " + name;
        return false;
    }
    if (kind == "upscalers") {
        // sd-server lists the top of the folder only; ESRGAN models are often .pth
        std::string ignored;
        const size_t dot = name.rfind('.');
        if (!is_upscaler_file_name(name) || dot == std::string::npos ||
            !valid_dest(name.substr(0, dot) + ".gguf", ignored)) {
            error = "an upscaler is a .pth, .pt, .safetensors or .gguf file name: " + name;
            return false;
        }
        return true;
    }
    if (kind == "tags") {
        // tag files for the prompt's completion (prompt_tags.hpp)
        const std::string n = lower(name);
        std::string       ignored;
        if (n.size() <= 4 || (n.compare(n.size() - 4, 4, ".csv") != 0 && n.compare(n.size() - 4, 4, ".txt") != 0) ||
            !valid_dest(name.substr(0, name.size() - 4) + ".gguf", ignored)) {
            error = "a tag file is a .csv or .txt file name: " + name;
            return false;
        }
        return true;
    }
    if (kind != "presets") {
        return valid_dest(name, error);
    }
    const std::string n = lower(name);
    std::string       ignored;
    if (n.size() <= 4 || n.compare(n.size() - 4, 4, ".txt") != 0 || !valid_dest(name.substr(0, name.size() - 4) + ".gguf", ignored)) {
        error = "a preset is a file name ending in .txt: " + name;
        return false;
    }
    return true;
}

bool is_lora_type(const std::string & civitai_type) {
    const std::string t = lower(civitai_type);
    return t == "lora" || t == "locon" || t == "dora";
}

bool valid_target(const std::string & kind, const std::string & dest, std::string & error) {
    if (kind == "models") {
        return valid_dest(dest, error);
    }
    if (kind != "loras" && kind != "upscalers") {
        error = "kind is models, loras or upscalers, not '" + kind + "'";
        return false;
    }
    std::string ignored_kind, ignored_name;
    return upload_target(kind + "/" + dest, ignored_kind, ignored_name, error);
}

std::string download_key(const std::string & kind, const std::string & dest) {
    return kind == "models" ? dest : kind + "/" + dest;
}

bool gallery_name_ok(const std::string & name) {
    if (name.size() <= 4 || name.size() > 200 || name[0] == '.') {
        return false;
    }
    // a PNG or a video (vid_gen): the same extensions gallery_type serves
    const std::string n  = lower(name);
    const bool        ext = n.compare(n.size() - 4, 4, ".png") == 0 || n.compare(n.size() - 4, 4, ".avi") == 0 ||
                     n.compare(n.size() - 5, 5, ".webm") == 0 || n.compare(n.size() - 5, 5, ".webp") == 0;
    if (!ext) {
        return false;
    }
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.')) {
            return false;
        }
    }
    return true;
}

std::string pieces_in_order(const std::string & pieces, uint64_t piece, uint64_t total, uint64_t file_size) {
    std::string out(pieces.size(), '0');
    for (size_t i = 0; i < pieces.size() && pieces[i] == '1' && piece > 0; i++) {
        const uint64_t start = static_cast<uint64_t>(i) * piece;
        if (start >= total || start + std::min(piece, total - start) > file_size) {
            break;
        }
        out[i] = '1';
    }
    return out;
}

std::vector<DownloadRecord> queue_from_json(const std::string & text) {
    std::vector<DownloadRecord> out;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("downloads") || !j["downloads"].is_array()) {
        return out;
    }
    for (const json & e : j["downloads"]) {
        DownloadRecord d;
        d.url     = str_of(e, "url");
        d.dest    = str_of(e, "dest");
        d.kind    = str_of(e, "kind");
        if (d.kind != "loras" && d.kind != "upscalers") {
            d.kind = "models";
        }
        d.catalog = str_of(e, "catalog");
        d.state   = str_of(e, "state");
        d.error   = str_of(e, "error");
        d.total   = u64_of(e, "total");
        d.piece   = u64_of(e, "piece");
        d.pieces  = str_of(e, "pieces");
        std::string ignored;
        Source      src;
        if (!valid_target(d.kind, d.dest, ignored) || !classify_url(d.url, src, ignored)) {
            continue;
        }
        d.id = download_id(download_key(d.kind, d.dest));
        if (std::any_of(out.begin(), out.end(), [&](const DownloadRecord & o) { return o.id == d.id; })) {
            continue;
        }
        const size_t expect = d.piece > 0 && d.total > 0 ? static_cast<size_t>((d.total + d.piece - 1) / d.piece) : 0;
        if (d.pieces.size() != expect ||
            d.pieces.find_first_not_of("01") != std::string::npos) {
            d.pieces.assign(expect, '0');
        }
        if (d.state == "running") {
            d.state = "queued";  // the app stopped while it ran: carry on
        } else if (d.state != "queued" && d.state != "done" && d.state != "failed" && d.state != "cancelled") {
            d.state = "queued";
        }
        d.bytes = d.state == "done" ? d.total : pieces_bytes(d.pieces, d.piece, d.total);
        out.push_back(d);
    }
    return out;
}

// ---- request bodies and Hugging Face ---------------------------------------------------

bool parse_download_request(const std::string & body, DownloadRequest & out, std::string & error) {
    out = DownloadRequest{};
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object()) {
        error = "the body must be {\"url\": \"...\", \"dest\": \"optional\", \"kind\": \"optional\", \"catalog\": \"optional\"}";
        return false;
    }
    out.url     = str_of(j, "url");
    out.dest    = str_of(j, "dest");
    out.catalog = str_of(j, "catalog");
    out.kind    = str_of(j, "kind");
    if (!out.kind.empty() && out.kind != "models" && out.kind != "loras" && out.kind != "upscalers") {
        error = "kind is models, loras or upscalers, not '" + out.kind + "'";
        return false;
    }
    if (!out.kind.empty() && out.kind != "models" && !out.catalog.empty()) {
        error = "a catalog entry goes to models, not " + out.kind;
        return false;
    }
    if (out.url.empty() && out.catalog.empty()) {
        error = "give a \"url\", or a \"catalog\" id to install a starter model";
        return false;
    }
    return true;
}

bool parse_search_request(const std::string & body, SearchRequest & out, std::string & error) {
    out = SearchRequest{};
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object()) {
        error = "the body must be {\"q\": \"...\", \"limit\": 20}";
        return false;
    }
    out.q = str_of(j, "q");
    if (j.contains("limit") && j["limit"].is_number()) {
        const double l = j["limit"].get<double>();
        out.limit      = static_cast<int>(std::max(1.0, std::min(50.0, l)));
    }
    if (out.q.size() > 200) {
        error = "the query is too long";
        return false;
    }
    return true;
}

std::vector<SearchResult> parse_hf_search(const std::string & text) {
    std::vector<SearchResult> out;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_array()) {
        return out;
    }
    for (const json & m : j) {
        SearchResult r;
        r.repo      = str_of(m, "id");
        r.downloads = u64_of(m, "downloads");
        r.likes     = u64_of(m, "likes");
        bool has_model = false;
        if (m.is_object() && m.contains("siblings") && m["siblings"].is_array()) {
            for (const json & s : m["siblings"]) {
                const std::string name = str_of(s, "rfilename");
                if (is_model_file_name(name)) {
                    has_model = true;
                    r.files.push_back({ name, u64_of(s, "size") });
                }
            }
        }
        if (!r.repo.empty() && has_model) {
            out.push_back(r);
        }
    }
    return out;
}

void parse_hf_model_files(const std::string & text, SearchResult & r) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("siblings") || !j["siblings"].is_array()) {
        return;
    }
    std::vector<SearchFile> files;
    for (const json & s : j["siblings"]) {
        const std::string name = str_of(s, "rfilename");
        if (!is_model_file_name(name)) {
            continue;
        }
        uint64_t bytes = u64_of(s, "size");
        if (s.contains("lfs") && s["lfs"].is_object()) {
            bytes = std::max(bytes, u64_of(s["lfs"], "size"));
        }
        files.push_back({ name, bytes });
    }
    r.files = files;
    if (u64_of(j, "downloads") > 0) {
        r.downloads = u64_of(j, "downloads");
    }
    if (u64_of(j, "likes") > 0) {
        r.likes = u64_of(j, "likes");
    }
}

std::string search_json(const std::vector<SearchResult> & results) {
    json arr = json::array();
    for (const auto & r : results) {
        json files = json::array();
        for (const auto & f : r.files) {
            std::string url = "https://huggingface.co/" + r.repo + "/resolve/main";
            for (const std::string & seg : split(f.path, '/')) {
                url += "/" + url_encode(seg);
            }
            files.push_back({ { "path", f.path }, { "bytes", f.bytes }, { "url", url } });
        }
        arr.push_back({ { "repo", r.repo }, { "downloads", r.downloads }, { "likes", r.likes }, { "files", files } });
    }
    return dump(json{ { "results", arr } });
}

bool parse_civitai_version(const std::string & text, const std::string & file_id, std::string & name,
                           std::string & url, uint64_t & bytes, std::string & error, std::string * model_type) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("files") || !j["files"].is_array()) {
        const std::string message = str_of(j, "error");
        error = message.empty() ? "Civitai did not describe this model version" : "Civitai: " + message;
        return false;
    }
    const json * pick = nullptr;
    for (const json & f : j["files"]) {
        if (!f.is_object()) {
            continue;
        }
        if (!file_id.empty()) {
            const auto it = f.find("id");
            if (it != f.end() && it->is_number() && std::to_string(it->get<uint64_t>()) == file_id) {
                pick = &f;
                break;
            }
            continue;
        }
        const bool primary = f.contains("primary") && f["primary"].is_boolean() && f["primary"].get<bool>();
        if (str_of(f, "type") == "Model" && (pick == nullptr || primary)) {
            pick = &f;
        }
    }
    if (pick == nullptr) {
        error = file_id.empty() ? "this model version has no model file" : "this model version has no file " + file_id;
        return false;
    }
    name = content_disposition_name("filename=\"" + str_of(*pick, "name") + "\"");
    url  = str_of(*pick, "downloadUrl");
    if (pick->contains("sizeKB") && (*pick)["sizeKB"].is_number()) {
        bytes = static_cast<uint64_t>(std::llround((*pick)["sizeKB"].get<double>() * 1024.0));
    }
    const std::string type = j.contains("model") && j["model"].is_object() ? str_of(j["model"], "type") : "";
    if (name.empty() || !(is_model_file_name(name) || (type == "Upscaler" && is_upscaler_file_name(name)))) {
        error = "the file of this model version is not a model file: " + name;
        return false;
    }
    if (model_type != nullptr) {
        *model_type = type;
    }
    return true;
}

}  // namespace ps5sd
