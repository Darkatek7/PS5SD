// The gallery's names and tags, and the images that are not kept: see gallery_labels.hpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gallery_labels.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;

namespace ps5sd {

namespace {

constexpr size_t kTitleMax = 100;  // characters (UTF-8 sequences), as the web UI's field
constexpr size_t kTagMax   = 40;
constexpr size_t kTagsMax  = 20;

std::string trim(const std::string & s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Characters, counting a UTF-8 sequence as one.
size_t chars(const std::string & s) {
    size_t n = 0;
    for (unsigned char c : s) {
        n += (c & 0xC0) != 0x80;
    }
    return n;
}

bool has_control(const std::string & s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

// A file of the gallery by its name alone: no folders, not hidden.
bool plain_file_name(const std::string & f) {
    if (f.empty() || f.size() > 200 || f[0] == '.') {
        return false;
    }
    for (unsigned char c : f) {
        if (!(std::isalnum(c) && c < 0x80) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

bool write_whole(const fs::path & p, const std::string & text) {
    const fs::path tmp = fs::u8path(p.u8string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.good()) {
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// The file's entries; an empty object when there is none or it does not parse.
nlohmann::json read_labels(const fs::path & p) {
    std::ifstream      in(p, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

}  // namespace

std::string GalleryLabels::all_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nlohmann::json{ { "labels", read_labels(fs::u8path(path())) } }.dump(-1, ' ', false,
                                                                                 nlohmann::json::error_handler_t::replace);
}

bool GalleryLabels::set(const std::string & file, const std::string & title_in, const std::vector<std::string> & tags_in,
                        GalleryLabel & out, std::string & error) {
    std::error_code ec;
    if (!plain_file_name(file) || !fs::is_regular_file(fs::u8path(dir_) / fs::u8path(file), ec)) {
        error = "no file " + file + " in the gallery";
        return false;
    }
    const std::string title = trim(title_in);
    if (chars(title) > kTitleMax || has_control(title)) {
        error = "a name is one line of at most 100 characters";
        return false;
    }
    std::vector<std::string> tags;
    for (const std::string & raw : tags_in) {
        std::string t = trim(raw);
        while (!t.empty() && t[0] == '#') {
            t = trim(t.substr(1));
        }
        if (t.empty()) {
            continue;
        }
        if (chars(t) > kTagMax || has_control(t) || t.find(',') != std::string::npos) {
            error = "a tag is at most 40 characters, without commas: " + t;
            return false;
        }
        const bool seen = std::any_of(tags.begin(), tags.end(), [&](const std::string & o) { return lower(o) == lower(t); });
        if (!seen) {
            tags.push_back(t);
        }
    }
    if (tags.size() > kTagsMax) {
        error = "at most 20 tags a file";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json              all = read_labels(fs::u8path(path()));
    nlohmann::json              kept = nlohmann::json::object();
    for (auto it = all.begin(); it != all.end(); ++it) {
        if (it.key() != file && plain_file_name(it.key()) && fs::exists(fs::u8path(dir_) / fs::u8path(it.key()), ec)) {
            kept[it.key()] = it.value();
        }
    }
    if (!title.empty() || !tags.empty()) {
        kept[file] = { { "title", title }, { "tags", tags } };
    }
    if (!write_whole(fs::u8path(path()), kept.dump(1))) {
        error = "could not write " + path();
        return false;
    }
    out.title = title;
    out.tags  = tags;
    return true;
}

void GalleryLabels::remove(const std::string & file) {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json              all = read_labels(fs::u8path(path()));
    if (all.erase(file) > 0) {
        write_whole(fs::u8path(path()), all.dump(1));
    }
}

bool is_temporary_name(const std::string & file) {
    return file.compare(0, 4, "tmp_") == 0;
}

bool keep_temporary(const std::string & outputs_dir, const std::string & file, std::string & kept, std::string & error) {
    const fs::path  dir = fs::u8path(outputs_dir);
    std::error_code ec;
    if (!is_temporary_name(file) || !plain_file_name(file) || file.size() <= 4 ||
        !fs::is_regular_file(dir / fs::u8path(file), ec)) {
        error = "no file " + file + " that is not kept";
        return false;
    }
    kept = file.substr(4);
    if (fs::exists(dir / fs::u8path(kept), ec)) {
        error = kept + " is there already";
        return false;
    }
    fs::rename(dir / fs::u8path(file), dir / fs::u8path(kept), ec);
    if (ec) {
        error = "could not rename " + file + ": " + ec.message();
        return false;
    }
    for (const char * ext : { ".jpg", ".txt" }) {
        std::error_code e;
        const fs::path  from = dir / ".thumbs" / fs::u8path(file + ext);
        if (fs::exists(from, e)) {
            fs::rename(from, dir / ".thumbs" / fs::u8path(kept + ext), e);
        }
    }
    return true;
}

int drop_temporary(const std::string & outputs_dir) {
    int             n = 0;
    std::error_code ec;
    const fs::path  dir = fs::u8path(outputs_dir);
    std::vector<fs::path> gone;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (is_temporary_name(it->path().filename().u8string())) {
            gone.push_back(it->path());
        }
    }
    for (const fs::path & p : gone) {
        std::error_code e;
        if (fs::remove(p, e)) {
            ++n;
        }
        fs::remove(dir / ".thumbs" / fs::u8path(p.filename().u8string() + ".jpg"), e);
        fs::remove(dir / ".thumbs" / fs::u8path(p.filename().u8string() + ".txt"), e);
    }
    return n;
}

}  // namespace ps5sd
