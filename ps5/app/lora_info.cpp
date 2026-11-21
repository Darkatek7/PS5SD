// SPDX-License-Identifier: GPL-3.0-or-later
#include "lora_info.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <vector>

#include "download_core.hpp"

namespace ps5sd {
namespace {

// ---- small helpers ---------------------------------------------------------------------------

constexpr size_t kMaxSidecar = 4u << 20;
constexpr size_t kMaxText    = 20000;      // a trigger, a note, a description
constexpr size_t kMaxPreview = 16u << 20;  // a preview GET sends
constexpr size_t kMaxImage   = 4u << 20;   // a preview a lookup downloads
constexpr size_t kMaxImages  = 6;

const char * const kPreviewExts[] = { "png", "jpeg", "jpg", "webp" };

std::string dump(const Json & j) {
    return j.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// The one error shape of the app's routes (models.cpp's error_json).
std::string err_json(const std::string & message, const std::string & code) {
    Json e = { { "message", message } };
    if (!code.empty()) {
        e["code"] = code;
    }
    return dump(Json{ { "error", e } });
}

std::string lower_of(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim_of(const std::string & s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        a++;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        b--;
    }
    return s.substr(a, b - a);
}

std::string str_of(const Json & j, const char * key) {
    if (j.is_object()) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_string()) {
            return it->get<std::string>();
        }
    }
    return {};
}

// A number, or a string that is one; 0 when it is neither.
double num_of(const Json & j, const char * key) {
    if (!j.is_object()) {
        return 0;
    }
    const auto it = j.find(key);
    if (it == j.end()) {
        return 0;
    }
    double v = 0;
    if (it->is_number()) {
        v = it->get<double>();
    } else if (it->is_string()) {
        char *            end = nullptr;
        const std::string s   = it->get<std::string>();
        v                     = std::strtod(s.c_str(), &end);
        if (end == s.c_str()) {
            v = 0;
        }
    }
    return std::isfinite(v) ? v : 0;
}

// An id as digits: from a number or a string of digits; "" otherwise.
std::string id_of(const Json & j) {
    if (j.is_number_unsigned() || (j.is_number_integer() && j.get<int64_t>() > 0)) {
        return std::to_string(j.get<uint64_t>());
    }
    if (j.is_string()) {
        const std::string s = j.get<std::string>();
        if (!s.empty() && s.size() < 20 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
            return s;
        }
    }
    return {};
}

uint64_t to_u64(const std::string & digits) {
    return std::strtoull(digits.c_str(), nullptr, 10);
}

// "Unknown" is what A1111 writes for a LoRA whose version it does not know.
bool blank_text(const std::string & s) {
    return trim_of(s).empty();
}
bool blank_version(const std::string & s) {
    const std::string t = lower_of(trim_of(s));
    return t.empty() || t == "unknown";
}

Json & ps5sd_of(Json & sidecar) {
    if (!sidecar.is_object()) {
        sidecar = Json::object();
    }
    if (!sidecar.contains("ps5sd") || !sidecar["ps5sd"].is_object()) {
        sidecar["ps5sd"] = Json::object();
    }
    return sidecar["ps5sd"];
}

const Json & ps5sd_const(const Json & sidecar) {
    static const Json none = Json::object();
    if (sidecar.is_object()) {
        const auto it = sidecar.find("ps5sd");
        if (it != sidecar.end() && it->is_object()) {
            return *it;
        }
    }
    return none;
}

void fill_text(Json & sidecar, const char * key, const std::string & value) {
    if (blank_text(value)) {
        return;
    }
    if (blank_text(str_of(sidecar, key))) {
        sidecar[key] = value;
    }
}

void fill_version(Json & sidecar, const std::string & value) {
    if (!blank_version(value) && blank_version(str_of(sidecar, "sd version"))) {
        sidecar["sd version"] = value;
    }
}

std::string base_name_of(const std::string & path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool is_file(const std::string & path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::mutex & sidecar_mutex() {
    static std::mutex m;
    return m;
}

// text -> path, a .tmp, fsync, rename. mode 0666 (the umask applies) or 0600.
bool write_file_atomic(const std::string & path, const std::string & bytes, mode_t mode, std::string & error) {
    const std::string tmp = path + ".tmp";
    const int         fd  = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        error = "cannot write " + base_name_of(tmp) + ": " + std::strerror(errno);
        return false;
    }
    size_t done = 0;
    bool   ok   = true;
    while (done < bytes.size() && ok) {
        const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0) {
            ok = false;
        } else {
            done += static_cast<size_t>(n);
        }
    }
    ::fsync(fd);
    ::close(fd);
    if (!ok) {
        error = "cannot write " + base_name_of(tmp) + ": " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        error = "cannot rename " + base_name_of(tmp) + ": " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

// ---- names -----------------------------------------------------------------------------------

std::string lora_stem(const std::string & file) {
    const size_t slash = file.rfind('/');
    const size_t dot   = file.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot == 0 ||
        (slash != std::string::npos && dot == slash + 1)) {
        return file;
    }
    return file.substr(0, dot);
}

std::string sidecar_path(const std::string & file) {
    return lora_stem(file) + ".json";
}

std::string preview_path(const std::string & file, const std::string & ext) {
    return lora_stem(file) + ".preview." + ext;
}

std::string find_preview(const std::string & file, const std::function<bool(const std::string &)> & exists) {
    const std::string stem = lora_stem(file);
    for (const char * ext : kPreviewExts) {
        if (exists(stem + ".preview." + ext)) {
            return stem + ".preview." + ext;
        }
    }
    for (const char * ext : { "png", "jpg", "jpeg", "webp" }) {
        if (exists(stem + "." + ext)) {
            return stem + "." + ext;
        }
    }
    return {};
}

std::string find_preview_on_disk(const std::string & file) {
    return find_preview(file, is_file);
}

std::string preview_content_type(const std::string & path) {
    const size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : lower_of(path.substr(dot + 1));
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    if (ext == "webp") {
        return "image/webp";
    }
    return {};
}

std::string preview_extension(const std::string & content_type) {
    std::string t = lower_of(trim_of(content_type.substr(0, content_type.find(';'))));
    if (t == "image/png") {
        return "png";
    }
    if (t == "image/jpeg" || t == "image/jpg") {
        return "jpeg";
    }
    if (t == "image/webp") {
        return "webp";
    }
    return {};
}

void remove_lora_extras(const std::string & file) {
    ::unlink(sidecar_path(file).c_str());
    for (const char * ext : kPreviewExts) {
        ::unlink(preview_path(file, ext).c_str());
    }
}

// ---- the sidecar -------------------------------------------------------------------------------

Json read_sidecar(const std::string & file) {
    const std::string path = sidecar_path(file);
    struct stat       st;
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || static_cast<uint64_t>(st.st_size) > kMaxSidecar) {
        return Json::object();
    }
    FILE * f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return Json::object();
    }
    std::string text(static_cast<size_t>(st.st_size), '\0');
    const size_t got = std::fread(&text[0], 1, text.size(), f);
    std::fclose(f);
    text.resize(got);
    Json j = Json::parse(text, nullptr, false);
    return j.is_object() ? j : Json::object();
}

bool write_sidecar(const std::string & file, const Json & sidecar, std::string & error) {
    return write_file_atomic(sidecar_path(file), sidecar.dump(2, ' ', false, Json::error_handler_t::replace) + "\n",
                             0666, error);
}

bool update_sidecar(const std::string & file, const std::function<void(Json &)> & fn, std::string & error) {
    try {
        std::lock_guard<std::mutex> lock(sidecar_mutex());
        Json                        sidecar = read_sidecar(file);
        fn(sidecar);
        return write_sidecar(file, sidecar, error);
    } catch (const std::exception & e) {
        error = e.what();
        return false;
    }
}

namespace {

std::string base_of(const Json & sidecar) {
    const std::string v = str_of(sidecar, "sd version");
    if (!blank_version(v)) {
        return trim_of(v);
    }
    const std::string c = str_of(ps5sd_const(sidecar).value("civitai", Json::object()), "base_model");
    return blank_version(c) ? std::string() : c;
}

}  // namespace

Json lora_list_extras(const Json & sidecar, bool has_sidecar, bool has_preview) {
    Json o = Json::object();
    if (!blank_text(str_of(sidecar, "activation text"))) {
        o["trigger"] = trim_of(str_of(sidecar, "activation text"));
    }
    const double weight = num_of(sidecar, "preferred weight");
    if (weight != 0) {
        o["weight"] = weight;
    }
    const std::string base = base_of(sidecar);
    if (!base.empty()) {
        o["base"] = base;
    }
    if (has_preview) {
        o["preview"] = true;
        // so the list blurs a mature preview as the info view does
        const auto level = ps5sd_const(sidecar).find("preview_nsfw_level");
        if (level != ps5sd_const(sidecar).end() && level->is_number_integer()) {
            o["preview_nsfw_level"] = level->get<int>();
        }
    }
    if (has_sidecar) {
        o["info"] = true;
    }
    const std::string lookup = str_of(ps5sd_const(sidecar), "lookup");
    if (lookup == "found" || lookup == "not_found") {
        o["lookup"] = lookup;
    }
    return o;
}

Json lora_info_view(const std::string & path, const Json & sidecar, bool has_preview) {
    Json         o  = Json::object();
    const Json & ps = ps5sd_const(sidecar);
    o["path"]       = path;
    const Json extras = lora_list_extras(sidecar, false, false);
    if (extras.contains("trigger")) {
        o["trigger"] = extras["trigger"];
    }
    if (extras.contains("weight")) {
        o["weight"] = extras["weight"];
    }
    if (!blank_text(str_of(sidecar, "description"))) {
        o["description"] = str_of(sidecar, "description");
    }
    if (!blank_text(str_of(sidecar, "notes"))) {
        o["notes"] = str_of(sidecar, "notes");
    }
    if (extras.contains("base")) {
        o["base"] = extras["base"];
    }
    for (const char * key : { "source", "sha256" }) {
        if (!str_of(ps, key).empty()) {
            o[key] = str_of(ps, key);
        }
    }
    for (const char * key : { "civitai", "hf" }) {
        const auto it = ps.find(key);
        if (it != ps.end() && it->is_object() && !it->empty()) {
            o[key] = *it;
        }
    }
    const auto images = ps.find("images");
    if (images != ps.end() && images->is_array() && !images->empty()) {
        o["images"] = *images;
    }
    if (has_preview) {
        o["preview"] = true;
        const auto level = ps.find("preview_nsfw_level");
        if (level != ps.end() && level->is_number_integer()) {
            o["preview_nsfw_level"] = *level;
        }
    }
    if (!str_of(ps, "looked_up").empty()) {
        o["looked_up"] = str_of(ps, "looked_up");
    }
    if (extras.contains("lookup")) {
        o["lookup"] = extras["lookup"];
    }
    return o;
}

bool apply_info_post(Json & sidecar, const Json & body, bool & changed, std::string & error) {
    changed = false;
    if (!body.is_object()) {
        error = "the body must be an object like {\"trigger\": \"...\", \"weight\": 0.8, \"notes\": \"...\"}";
        return false;
    }
    for (const char * key : { "trigger", "notes", "description" }) {
        const auto it = body.find(key);
        if (it == body.end()) {
            continue;
        }
        if (!it->is_string()) {
            error = std::string(key) + " must be a string";
            return false;
        }
        if (it->get<std::string>().size() > kMaxText) {
            error = std::string(key) + " is too long (at most " + std::to_string(kMaxText) + " bytes)";
            return false;
        }
    }
    double weight = 0;
    const auto w  = body.find("weight");
    if (w != body.end()) {
        if (!w->is_null() && !w->is_number()) {
            error = "weight must be a number, or null to clear it";
            return false;
        }
        weight = w->is_null() ? 0 : w->get<double>();
        if (!std::isfinite(weight) || std::fabs(weight) > 100) {
            error = "weight must be a number between -100 and 100";
            return false;
        }
    }
    if (!sidecar.is_object()) {
        sidecar = Json::object();
    }
    if (body.contains("trigger")) {
        sidecar["activation text"] = trim_of(body["trigger"].get<std::string>());
        changed                    = true;
    }
    if (body.contains("notes")) {
        sidecar["notes"] = body["notes"].get<std::string>();
        changed          = true;
    }
    if (body.contains("description")) {
        sidecar["description"] = body["description"].get<std::string>();
        changed                = true;
    }
    if (w != body.end()) {
        if (weight == 0) {
            sidecar["preferred weight"] = 0;
        } else {
            sidecar["preferred weight"] = weight;
        }
        changed = true;
    }
    return true;
}

// ---- HTML to text ---------------------------------------------------------------------------------

namespace {

void put_utf8(std::string & out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
    }
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The entity at html[at] == '&': its text (UTF-8) and the index after it; false when it is none.
bool decode_entity(const std::string & html, size_t at, std::string & text, size_t & next) {
    const size_t semi = html.find(';', at);
    if (semi == std::string::npos || semi - at > 10 || semi - at < 2) {
        return false;
    }
    const std::string name = html.substr(at + 1, semi - at - 1);
    uint32_t          cp   = 0;
    if (name[0] == '#') {
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const std::string digits = name.substr(hex ? 2 : 1);
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [hex](unsigned char c) {
                return hex ? std::isxdigit(c) != 0 : std::isdigit(c) != 0;
            })) {
            return false;
        }
        cp = static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10));
    } else if (name == "amp") {
        cp = '&';
    } else if (name == "lt") {
        cp = '<';
    } else if (name == "gt") {
        cp = '>';
    } else if (name == "quot") {
        cp = '"';
    } else if (name == "apos") {
        cp = '\'';
    } else if (name == "nbsp") {
        cp = ' ';
    } else if (name == "hellip") {
        cp = 0x2026;
    } else if (name == "ndash") {
        cp = 0x2013;
    } else if (name == "mdash") {
        cp = 0x2014;
    } else if (name == "lsquo") {
        cp = 0x2018;
    } else if (name == "rsquo") {
        cp = 0x2019;
    } else if (name == "ldquo") {
        cp = 0x201C;
    } else if (name == "rdquo") {
        cp = 0x201D;
    } else if (name == "copy") {
        cp = 0xA9;
    } else {
        return false;
    }
    text.clear();
    put_utf8(text, cp == 0xA0 ? ' ' : cp);
    next = semi + 1;
    return true;
}

size_t trailing_newlines(const std::string & s) {
    size_t n = 0;
    while (n < s.size() && s[s.size() - 1 - n] == '\n') {
        n++;
    }
    return n;
}

}  // namespace

std::string html_to_text(const std::string & html, size_t max_chars) {
    std::string out;
    bool        pending_space = false;
    bool        bullet        = false;  // a list item has begun and its "- " is not written yet
    int         in_li         = 0;
    const auto  newlines      = [&](size_t want) {  // at least `want` line ends at the end
        if (out.empty()) {
            return;
        }
        for (size_t have = trailing_newlines(out); have < want; have++) {
            out += '\n';
        }
        pending_space = false;
    };
    size_t i = 0;
    while (i < html.size()) {
        const char c = html[i];
        if (c == '<' && i + 1 < html.size() &&
            (std::isalpha(static_cast<unsigned char>(html[i + 1])) || html[i + 1] == '/' || html[i + 1] == '!')) {
            const size_t end = html.find('>', i);
            if (end == std::string::npos) {
                break;
            }
            std::string tag = html.substr(i + 1, end - i - 1);
            i               = end + 1;
            const bool closing = !tag.empty() && tag[0] == '/';
            size_t     s       = closing ? 1 : 0;
            size_t     e       = s;
            while (e < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[e])))) {
                e++;
            }
            const std::string name = lower_of(tag.substr(s, e - s));
            if (!closing && (name == "script" || name == "style")) {
                const size_t close = lower_of(html).find("</" + name, i);
                i                  = close == std::string::npos ? html.size() : close;
            } else if (name == "br") {
                if (!out.empty() && trailing_newlines(out) < 2) {
                    out += '\n';
                }
                pending_space = false;
            } else if ((name == "p" && in_li > 0) || name == "li") {
                // a list item's paragraph is the item's own line
                newlines(1);
                if (name == "li") {
                    bullet = !closing;
                    in_li  = std::max(0, in_li + (closing ? -1 : 1));
                }
            } else if (name == "p" || (name.size() == 2 && name[0] == 'h' && std::isdigit(static_cast<unsigned char>(name[1]))) ||
                       name == "blockquote" || name == "pre" || name == "table" || name == "hr") {
                newlines(2);
            } else if (name == "div" || name == "ul" || name == "ol" || name == "tr") {
                newlines(1);
            }
            continue;
        }
        if (c == '&') {
            std::string text;
            size_t      next = 0;
            if (decode_entity(html, i, text, next)) {
                i = next;
                if (text == " ") {
                    pending_space = true;
                } else {
                    if (bullet) {
                        out += "- ";
                        bullet        = false;
                        pending_space = false;
                    }
                    if (pending_space && !out.empty() && out.back() != '\n' && out.back() != ' ') {
                        out += ' ';
                    }
                    pending_space = false;
                    out += text;
                }
                continue;
            }
        }
        i++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            pending_space = true;
            continue;
        }
        if (bullet) {
            out += "- ";
            bullet        = false;
            pending_space = false;
        }
        if (pending_space && !out.empty() && out.back() != '\n' && out.back() != ' ') {
            out += ' ';
        }
        pending_space = false;
        out += c;
    }
    out = trim_of(out);
    // at most max_chars characters (not bytes), cut at a character's edge
    size_t chars = 0, at = 0;
    while (at < out.size()) {
        if ((static_cast<unsigned char>(out[at]) & 0xC0) != 0x80) {
            if (chars == max_chars) {
                break;
            }
            chars++;
        }
        at++;
    }
    if (at < out.size()) {
        // cut: leave room for the "…" within max_chars
        size_t keep = max_chars > 0 ? max_chars - 1 : 0, n = 0, pos = 0;
        while (pos < out.size()) {
            if ((static_cast<unsigned char>(out[pos]) & 0xC0) != 0x80) {
                if (n == keep) {
                    break;
                }
                n++;
            }
            pos++;
        }
        out = trim_of(out.substr(0, pos)) + "\xE2\x80\xA6";
    }
    return out;
}

std::string civitai_thumb_url(const std::string & url) {
    std::string out = url;
    const std::string from = "/original=true/";
    const size_t      at   = out.find(from);
    if (at != std::string::npos) {
        out.replace(at, from.size(), "/width=450/");
    }
    return out;
}

int pick_preview_image(const Json & images) {
    if (!images.is_array() || images.empty()) {
        return -1;
    }
    for (size_t i = 0; i < images.size(); i++) {
        const Json & im    = images[i];
        const auto   level = im.is_object() ? im.find("nsfw_level") : im.end();
        const double v     = level != im.end() && level->is_number() ? level->get<double>() : 1;
        if (v <= 2) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::string iso_utc(int64_t unix_time) {
    const std::time_t t = static_cast<std::time_t>(unix_time);
    struct tm         tm;
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// ---- Civitai and Hugging Face -------------------------------------------------------------------

namespace {

// Civitai's level of an image: nsfwLevel (1 none, 2 soft, 4 mature, 8 X, 16 XXX), else the older
// "nsfw" word. 4 when it says nothing (treated as mature, so it is not picked as a preview).
int civitai_level(const Json & im) {
    const auto it = im.find("nsfwLevel");
    if (it != im.end() && it->is_number()) {
        return static_cast<int>(it->get<double>());
    }
    const auto old = im.find("nsfw");
    if (old != im.end() && old->is_string()) {
        const std::string w = lower_of(old->get<std::string>());
        return w == "none" ? 1 : w == "soft" ? 2 : w == "mature" ? 4 : 8;
    }
    return 4;
}

Json civitai_images(const Json & version) {
    Json out = Json::array();
    const auto it = version.find("images");
    if (it == version.end() || !it->is_array()) {
        return out;
    }
    for (const Json & im : *it) {
        if (out.size() >= kMaxImages) {
            break;
        }
        if (!im.is_object() || str_of(im, "url").empty()) {
            continue;
        }
        const std::string type = str_of(im, "type");
        if (!type.empty() && type != "image") {
            continue;
        }
        Json o = { { "url", civitai_thumb_url(str_of(im, "url")) }, { "nsfw_level", civitai_level(im) } };
        for (const char * key : { "width", "height" }) {
            const auto d = im.find(key);
            if (d != im.end() && d->is_number_integer()) {
                o[key] = *d;
            }
        }
        out.push_back(o);
    }
    return out;
}

std::string join_words(const Json & words) {
    std::string out;
    if (words.is_array()) {
        for (const Json & w : words) {
            const std::string t = w.is_string() ? trim_of(w.get<std::string>()) : std::string();
            if (!t.empty()) {
                out += (out.empty() ? "" : ", ") + t;
            }
        }
    }
    return out;
}

bool image_name(const std::string & url) {
    std::string path = lower_of(url.substr(0, url.find_first_of("?#")));
    for (const char * ext : { ".png", ".jpg", ".jpeg", ".webp" }) {
        const std::string e = ext;
        if (path.size() > e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

std::string encode_path(const std::string & path) {
    std::string out;
    size_t      at = 0;
    for (;;) {
        const size_t next = path.find('/', at);
        out += url_encode(path.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) {
            return out;
        }
        out += '/';
        at = next + 1;
    }
}

}  // namespace

void civitai_to_sidecar(const Json & version, const Json & model, Json & sidecar, const std::string & looked_up) {
    Json & ps = ps5sd_of(sidecar);
    Json   c  = ps.contains("civitai") && ps["civitai"].is_object() ? ps["civitai"] : Json::object();
    const std::string model_id   = id_of(version.value("modelId", Json()));
    const std::string version_id = id_of(version.value("id", Json()));
    const Json        vmodel     = version.value("model", Json::object());
    if (!model_id.empty()) {
        c["model_id"] = to_u64(model_id);
    }
    if (!version_id.empty()) {
        c["version_id"] = to_u64(version_id);
    }
    if (!str_of(vmodel, "name").empty()) {
        c["model"] = str_of(vmodel, "name");
    }
    if (!str_of(version, "name").empty()) {
        c["version"] = str_of(version, "name");
    }
    if (!str_of(version, "baseModel").empty()) {
        c["base_model"] = str_of(version, "baseModel");
    }
    if (!model_id.empty() && !version_id.empty()) {
        c["url"] = "https://civitai.com/models/" + model_id + "?modelVersionId=" + version_id;
    }
    ps["civitai"] = c;

    fill_text(sidecar, "activation text", join_words(version.value("trainedWords", Json())));
    // the model's description (the page's text), else the version's own
    const std::string html = model.is_object() && !str_of(model, "description").empty() ? str_of(model, "description")
                                                                                       : str_of(version, "description");
    fill_text(sidecar, "description", html_to_text(html));
    fill_version(sidecar, str_of(version, "baseModel"));

    ps["images"]    = civitai_images(version);
    ps["lookup"]    = "found";
    ps["looked_up"] = looked_up;
}

void hf_to_sidecar(const Json & model, const std::string & repo, Json & sidecar, const std::string & looked_up) {
    Json &       ps   = ps5sd_of(sidecar);
    const Json & card = model.is_object() && model.contains("cardData") && model["cardData"].is_object()
                            ? model["cardData"]
                            : Json::object();
    ps["hf"] = { { "repo", repo }, { "url", "https://huggingface.co/" + repo } };

    std::string trigger;
    const auto  ip = card.find("instance_prompt");
    if (ip != card.end()) {
        trigger = ip->is_string() ? ip->get<std::string>() : join_words(*ip);
    }
    fill_text(sidecar, "activation text", trim_of(trigger));
    std::string base;
    const auto  bm = card.find("base_model");
    if (bm != card.end()) {
        if (bm->is_string()) {
            base = bm->get<std::string>();
        } else if (bm->is_array()) {
            for (const Json & b : *bm) {
                if (base.empty() && b.is_string()) {
                    base = b.get<std::string>();
                }
            }
        }
    }
    fill_version(sidecar, base);

    Json images = Json::array();
    const auto widget = card.find("widget");
    if (widget != card.end() && widget->is_array()) {
        for (const Json & w : *widget) {
            if (images.size() >= kMaxImages) {
                break;
            }
            const std::string url = w.is_object() && w.contains("output") ? str_of(w["output"], "url") : std::string();
            if (url.empty() || !image_name(url)) {
                continue;
            }
            std::string full = url;
            if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
                full = "https://huggingface.co/" + encode_path(repo) + "/resolve/main/" +
                       encode_path(url[0] == '/' ? url.substr(1) : url);
            }
            images.push_back({ { "url", full } });
        }
    }
    ps["images"]    = images;
    ps["lookup"]    = "found";
    ps["looked_up"] = looked_up;
}

std::string sidecar_civitai_version(const Json & sidecar) {
    const Json & ps = ps5sd_const(sidecar);
    const auto   c  = ps.find("civitai");
    if (c != ps.end() && c->is_object()) {
        const std::string id = id_of(c->value("version_id", Json()));
        if (!id.empty()) {
            return id;
        }
    }
    Source src;
    std::string error;
    if (classify_url(str_of(ps, "source"), src, error) && src.kind == "civitai") {
        return src.civitai_version;
    }
    return {};
}

std::string sidecar_hf_repo(const Json & sidecar) {
    Url u;
    if (!parse_url(str_of(ps5sd_const(sidecar), "source"), u) || u.scheme != "https" ||
        (u.host != "huggingface.co" && u.host != "www.huggingface.co" && u.host != "hf.co")) {
        return {};
    }
    std::string path = u.target.substr(0, u.target.find_first_of("?#"));
    std::vector<std::string> parts;
    size_t at = 1;
    while (at <= path.size()) {
        const size_t next = path.find('/', at);
        parts.push_back(path.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) {
            break;
        }
        at = next + 1;
    }
    if (parts.size() < 2 || parts[0].empty() || parts[1].empty() || parts[0] == "api" || parts[0] == "spaces" ||
        parts[0] == "datasets" || parts[0] == "docs" || parts[0] == "models") {
        return {};
    }
    return parts[0] + "/" + parts[1];
}

bool note_lora_source(const std::string & file, const std::string & source, const std::string & civitai_version,
                      std::string & error) {
    return update_sidecar(file, [&](Json & sc) {
        Json & ps    = ps5sd_of(sc);
        ps["source"] = source;
        if (!civitai_version.empty()) {
            Json c = ps.contains("civitai") && ps["civitai"].is_object() ? ps["civitai"] : Json::object();
            c["version_id"] = to_u64(civitai_version);
            ps["civitai"]   = c;
        }
    }, error);
}

// ---- SHA-256 -----------------------------------------------------------------------------------

namespace {

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

Sha256::Sha256() {
    static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    std::memcpy(h_, init, sizeof(h_));
    std::memset(buf_, 0, sizeof(buf_));
}

void Sha256::block(const unsigned char * p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) | (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(p[i * 4 + 2]) << 8) | static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i]              = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t s1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch    = (e & f) ^ (~e & g);
        const uint32_t t1    = h + s1 + ch + kSha256K[i] + w[i];
        const uint32_t s0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2    = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void * data, size_t n) {
    const unsigned char * p = static_cast<const unsigned char *>(data);
    total_ += n;
    if (buffered_ > 0) {
        const size_t take = std::min(n, sizeof(buf_) - buffered_);
        std::memcpy(buf_ + buffered_, p, take);
        buffered_ += take;
        p += take;
        n -= take;
        if (buffered_ == sizeof(buf_)) {
            block(buf_);
            buffered_ = 0;
        }
    }
    while (n >= 64) {
        block(p);
        p += 64;
        n -= 64;
    }
    if (n > 0) {
        std::memcpy(buf_, p, n);
        buffered_ = n;
    }
}

std::string Sha256::finish() {
    const uint64_t bits = total_ * 8;
    unsigned char  pad[72];
    size_t         padn = (buffered_ < 56 ? 56 : 120) - buffered_;
    std::memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (int i = 0; i < 8; i++) {
        pad[padn + static_cast<size_t>(i)] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    }
    update(pad, padn + 8);
    static const char digits[] = "0123456789ABCDEF";
    std::string       out;
    for (uint32_t v : h_) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out += digits[(v >> shift) & 0xF];
        }
    }
    return out;
}

std::string sha256_hex(const std::string & data) {
    Sha256 s;
    s.update(data.data(), data.size());
    return s.finish();
}

bool sha256_file(const std::string & path, std::string & hex, std::string & error, const std::function<bool()> & cancel) {
    FILE * f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        error = "cannot read " + base_name_of(path) + ": " + std::strerror(errno);
        return false;
    }
    std::vector<unsigned char> buf(1u << 20);
    Sha256                     sha;
    for (;;) {
        if (cancel && cancel()) {
            std::fclose(f);
            error = "cancelled";
            return false;
        }
        const size_t n = std::fread(buf.data(), 1, buf.size(), f);
        if (n > 0) {
            sha.update(buf.data(), n);
        }
        if (n < buf.size()) {
            const bool bad = std::ferror(f) != 0;
            std::fclose(f);
            if (bad) {
                error = "cannot read " + base_name_of(path);
                return false;
            }
            hex = sha.finish();
            return true;
        }
    }
}

// ---- the routes ---------------------------------------------------------------------------------

namespace {

// 400 for a path that is not a LoRA file's; 404 "not_found" (code_missing) when the file is not there.
// True when the file is there.
bool lora_file(const std::string & dir, const std::string & rel, const char * missing_code, std::string & file,
               std::string & out, int & status) {
    std::string error;
    if (!valid_dest(rel, error) || !is_model_file_name(rel.substr(rel.rfind('/') + 1))) {
        out    = err_json("not a LoRA file: " + rel, "invalid_request");
        status = 400;
        return false;
    }
    file = dir + "/" + rel;
    if (!is_file(file)) {
        out    = err_json("no LoRA " + rel, missing_code);
        status = 404;
        return false;
    }
    return true;
}

int caught(const std::exception * e, std::string & out) {
    out = err_json(std::string("error: ") + (e != nullptr ? e->what() : "unknown error"), "server_error");
    return 500;
}

}  // namespace

int lora_info_get(const std::string & dir, const std::string & rel, std::string & out) {
    try {
        std::string file;
        int         status = 0;
        if (!lora_file(dir, rel, "not_found", file, out, status)) {
            return status;
        }
        out = dump(lora_info_view(rel, read_sidecar(file), !find_preview_on_disk(file).empty()));
        return 200;
    } catch (const std::exception & e) {
        return caught(&e, out);
    } catch (...) {
        return caught(nullptr, out);
    }
}

int lora_info_post(const std::string & dir, const std::string & rel, const std::string & body, std::string & out) {
    try {
        std::string file;
        int         status = 0;
        if (!lora_file(dir, rel, "not_found", file, out, status)) {
            return status;
        }
        const Json  j = Json::parse(body, nullptr, false);
        std::string error;
        bool        changed = false;
        Json        probe   = Json::object();
        if (!apply_info_post(probe, j, changed, error)) {
            out = err_json(error, "invalid_request");
            return 400;
        }
        if (changed && !update_sidecar(file, [&](Json & sc) {
                bool ignored = false;
                std::string e;
                apply_info_post(sc, j, ignored, e);
            }, error)) {
            out = err_json(error, "server_error");
            return 500;
        }
        out = dump(lora_info_view(rel, read_sidecar(file), !find_preview_on_disk(file).empty()));
        return 200;
    } catch (const std::exception & e) {
        return caught(&e, out);
    } catch (...) {
        return caught(nullptr, out);
    }
}

int lora_preview_get(const std::string & dir, const std::string & rel, std::string & out, std::string & content_type) {
    content_type = "application/json";
    try {
        std::string file;
        int         status = 0;
        if (!lora_file(dir, rel, "not_found", file, out, status)) {
            return status;
        }
        const std::string preview = find_preview_on_disk(file);
        struct stat       st;
        if (preview.empty() || ::stat(preview.c_str(), &st) != 0) {
            out = err_json("no preview for " + rel, "not_found");
            return 404;
        }
        if (static_cast<uint64_t>(st.st_size) > kMaxPreview) {
            out = err_json("the preview of " + rel + " is over 16 MiB", "payload_too_large");
            return 413;
        }
        FILE * f = std::fopen(preview.c_str(), "rb");
        if (f == nullptr) {
            out = err_json("no preview for " + rel, "not_found");
            return 404;
        }
        std::string bytes(static_cast<size_t>(st.st_size), '\0');
        const size_t got = std::fread(&bytes[0], 1, bytes.size(), f);
        std::fclose(f);
        bytes.resize(got);
        out          = std::move(bytes);
        content_type = preview_content_type(preview);
        return 200;
    } catch (const std::exception & e) {
        content_type = "application/json";
        return caught(&e, out);
    } catch (...) {
        content_type = "application/json";
        return caught(nullptr, out);
    }
}

namespace {

enum class Got { Ok, Missing, Failed };

int lookup_impl(const std::string & dir, const std::string & rel, const LookupIo & io, std::string & out) {
    std::string file;
    int         status = 0;
    if (!lora_file(dir, rel, "no_lora", file, out, status)) {
        return status;
    }
    const auto stopping = [&] { return io.cancelled && io.cancelled(); };
    const auto stop     = [&] {
        out = err_json("the app is stopping", "stopping");
        return 503;
    };
    const Json        sidecar    = read_sidecar(file);
    const std::string version_id = sidecar_civitai_version(sidecar);
    const std::string repo       = sidecar_hf_repo(sidecar);

    std::string fail;
    // One JSON from a server: Ok (HTTP 200, an object), Missing (HTTP 4xx), Failed (cannot be reached,
    // HTTP 5xx or 429, not JSON; and for Civitai a refusal: a token may be needed).
    const auto get = [&](const std::string & url, const char * who, bool refusal_fails, Json & j) {
        int         st = 0;
        std::string body, error;
        if (!io.get_text(url, st, body, error)) {
            fail = std::string("cannot reach ") + who + ": " + error;
            return Got::Failed;
        }
        if (st == 200) {
            j = Json::parse(body, nullptr, false);
            if (j.is_object()) {
                return Got::Ok;
            }
            fail = std::string(who) + " sent something that is not JSON";
            return Got::Failed;
        }
        if (st == 429 || st >= 500 || (refusal_fails && (st == 401 || st == 403))) {
            fail = std::string(who) + " answered HTTP " + std::to_string(st) +
                   (st == 401 || st == 403 ? "; a token may be needed (POST tokens)" : "");
            return Got::Failed;
        }
        return Got::Missing;
    };
    const auto bad_gateway = [&] {
        out = err_json(fail, "bad_gateway");
        return 502;
    };

    Json version, hf;
    bool via_civitai = false, via_hf = false;
    if (stopping()) {
        return stop();
    }
    if (!version_id.empty()) {
        const Got g = get("https://civitai.com/api/v1/model-versions/" + version_id, "Civitai", true, version);
        if (g == Got::Failed) {
            return bad_gateway();
        }
        via_civitai = g == Got::Ok;
    }
    if (!via_civitai && !repo.empty()) {
        const size_t slash = repo.find('/');
        const Got g = get("https://huggingface.co/api/models/" + url_encode(repo.substr(0, slash)) + "/" +
                              url_encode(repo.substr(slash + 1)),
                          "Hugging Face", false, hf);
        if (g == Got::Failed) {
            return bad_gateway();
        }
        via_hf = g == Got::Ok;
    }
    if (!via_civitai && !via_hf) {
        // The file's SHA-256, kept while its size and time stay the same.
        struct stat st;
        if (::stat(file.c_str(), &st) != 0) {
            out = err_json("no LoRA " + rel, "no_lora");
            return 404;
        }
        const uint64_t    size  = static_cast<uint64_t>(st.st_size);
        const int64_t     mtime = static_cast<int64_t>(st.st_mtime);
        const Json &      ps    = ps5sd_const(sidecar);
        std::string       sha   = str_of(ps, "sha256");
        const auto        s_it  = ps.find("sha256_size");
        const auto        m_it  = ps.find("sha256_mtime");
        const bool cached = sha.size() == 64 && s_it != ps.end() && m_it != ps.end() && s_it->is_number_integer() &&
                            m_it->is_number_integer() && s_it->get<uint64_t>() == size && m_it->get<int64_t>() == mtime;
        if (!cached) {
            std::string error;
            if (!sha256_file(file, sha, error, io.cancelled)) {
                if (stopping()) {
                    return stop();
                }
                out = err_json(error, "server_error");
                return 500;
            }
            if (!update_sidecar(file, [&](Json & sc) {
                    Json & p         = ps5sd_of(sc);
                    p["sha256"]       = sha;
                    p["sha256_size"]  = size;
                    p["sha256_mtime"] = mtime;
                }, error)) {
                out = err_json(error, "server_error");
                return 500;
            }
        }
        if (stopping()) {
            return stop();
        }
        const Got g = get("https://civitai.com/api/v1/model-versions/by-hash/" + sha, "Civitai", true, version);
        if (g == Got::Failed) {
            return bad_gateway();
        }
        via_civitai = g == Got::Ok;
    }

    const std::string now = iso_utc(static_cast<int64_t>(std::time(nullptr)));
    if (!via_civitai && !via_hf) {
        std::string error;
        if (!update_sidecar(file, [&](Json & sc) {
                Json & p      = ps5sd_of(sc);
                p["lookup"]    = "not_found";
                p["looked_up"] = now;
            }, error)) {
            out = err_json(error, "server_error");
            return 500;
        }
        out = err_json("Civitai does not know this file", "not_found");
        return 404;
    }

    // what the answer fills in
    Json model;  // null unless Civitai's model page answered
    std::function<void(Json &)> fill;
    if (via_civitai) {
        const std::string model_id = id_of(version.value("modelId", Json()));
        if (!model_id.empty() && !stopping()) {
            Json m;
            if (get("https://civitai.com/api/v1/models/" + model_id, "Civitai", false, m) == Got::Ok) {
                model = m;
            }
        }
        fill = [&](Json & sc) { civitai_to_sidecar(version, model, sc, now); };
    } else {
        fill = [&](Json & sc) { hf_to_sidecar(hf, repo, sc, now); };
    }

    // the preview: only when the LoRA has none (a user's own stays, and so does one of ours)
    std::string preview_name;
    int         preview_level = -1;
    if (find_preview_on_disk(file).empty() && io.get_image && !stopping()) {
        Json scratch = sidecar;
        fill(scratch);
        const Json & images = ps5sd_const(scratch).value("images", Json::array());
        const int    pick   = pick_preview_image(images);
        if (pick >= 0) {
            std::string content_type, bytes, error;
            if (io.get_image(str_of(images[static_cast<size_t>(pick)], "url"), content_type, bytes, error) &&
                !bytes.empty() && bytes.size() <= kMaxImage && !preview_extension(content_type).empty()) {
                const std::string path = preview_path(file, preview_extension(content_type));
                if (write_file_atomic(path, bytes, 0666, error)) {
                    preview_name = base_name_of(path);
                    const auto level = images[static_cast<size_t>(pick)].find("nsfw_level");
                    if (level != images[static_cast<size_t>(pick)].end() && level->is_number_integer()) {
                        preview_level = level->get<int>();
                    }
                }
            }
        }
    }

    Json        after;
    std::string error;
    if (!update_sidecar(file, [&](Json & sc) {
            fill(sc);
            Json & p = ps5sd_of(sc);
            if (!preview_name.empty()) {
                p["preview"] = preview_name;
                if (preview_level >= 0) {
                    p["preview_nsfw_level"] = preview_level;
                } else {
                    p.erase("preview_nsfw_level");
                }
            }
            after = sc;
        }, error)) {
        out = err_json(error, "server_error");
        return 500;
    }
    out = dump(lora_info_view(rel, after, !find_preview_on_disk(file).empty()));
    return 200;
}

bool token_present(const std::string & path) {
    FILE * f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    char   line[512];
    const char * got = std::fgets(line, sizeof(line), f);
    std::fclose(f);
    return got != nullptr && !trim_of(line).empty();
}

}  // namespace

int lora_lookup(const std::string & dir, const std::string & rel, const LookupIo & io, std::string & out) {
    try {
        return lookup_impl(dir, rel, io, out);
    } catch (const std::exception & e) {
        return caught(&e, out);
    } catch (...) {
        return caught(nullptr, out);
    }
}

int tokens_get(const std::string & hf_file, const std::string & civitai_file, std::string & out) {
    out = dump(Json{ { "huggingface", token_present(hf_file) }, { "civitai", token_present(civitai_file) } });
    return 200;
}

int tokens_post(const std::string & hf_file, const std::string & civitai_file, const std::string & body,
                std::string & out) {
    try {
        const Json j = Json::parse(body, nullptr, false);
        const char * usage = "the body must be {\"civitai\": \"<token>\"} and/or {\"huggingface\": \"<token>\"}";
        if (!j.is_object() || (!j.contains("civitai") && !j.contains("huggingface"))) {
            out = err_json(usage, "invalid_request");
            return 400;
        }
        struct Todo {
            const char * key;
            std::string  file, value;
        };
        std::vector<Todo> todo;
        for (const Todo & t : { Todo{ "huggingface", hf_file, "" }, Todo{ "civitai", civitai_file, "" } }) {
            const auto it = j.find(t.key);
            if (it == j.end()) {
                continue;
            }
            if (!it->is_string()) {
                out = err_json(std::string(t.key) + " must be a string", "invalid_request");
                return 400;
            }
            const std::string v = trim_of(it->get<std::string>());
            if (v.size() > 200 || !std::all_of(v.begin(), v.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                })) {
                out = err_json(std::string(t.key) + " is a token of at most 200 letters, digits and \"-_.\"", "invalid_request");
                return 400;
            }
            todo.push_back({ t.key, t.file, v });
        }
        for (const Todo & t : todo) {
            std::string error;
            if (t.value.empty()) {
                if (::unlink(t.file.c_str()) != 0 && errno != ENOENT) {
                    out = err_json(std::string("cannot delete the ") + t.key + " token: " + std::strerror(errno), "server_error");
                    return 500;
                }
            } else if (!write_file_atomic(t.file, t.value + "\n", 0600, error)) {
                out = err_json(error, "server_error");
                return 500;
            }
        }
        return tokens_get(hf_file, civitai_file, out);
    } catch (const std::exception & e) {
        return caught(&e, out);
    } catch (...) {
        return caught(nullptr, out);
    }
}

}  // namespace ps5sd
