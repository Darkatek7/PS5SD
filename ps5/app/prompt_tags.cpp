// Tag completion from the user's tag files: see prompt_tags.hpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_tags.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "json.hpp"

namespace fs = std::filesystem;

namespace ps5sd {

namespace {

constexpr size_t kMaxTags = 500000;  // a Danbooru list has about 100,000; this bounds the memory

// lower case, spaces as underscores: "Long Hair" and "long_hair" are the same
std::string key_of(const std::string & s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(c == ' ' ? '_' : static_cast<char>(std::tolower(c)));
    }
    return out;
}

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

// The fields of a CSV line; a field in quotes may hold commas ("" is a quote).
std::vector<std::string> csv_fields(const std::string & line) {
    std::vector<std::string> out(1);
    bool                     quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                out.back().push_back('"');
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                out.back().push_back(c);
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.emplace_back();
        } else {
            out.back().push_back(c);
        }
    }
    return out;
}

bool all_digits(const std::string & s) {
    return !s.empty() && s.size() <= 18 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// "long_hair" -> "long hair", "name_(artist)" -> "name \(artist\)", as the prompt takes it
std::string prompt_form(const std::string & name) {
    std::string out;
    for (char c : name) {
        if (c == '_') {
            out.push_back(' ');
        } else if (c == '(' || c == ')') {
            out.push_back('\\');
            out.push_back(c);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool is_tag_file(const fs::path & p) {
    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".csv" || ext == ".txt";
}

}  // namespace

bool TagIndex::parse_line(const std::string & raw, std::string & name, uint64_t & count, std::vector<std::string> & aliases) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
        return false;
    }
    const std::vector<std::string> f = csv_fields(line);
    name                             = trim(f[0]);
    count                            = 0;
    aliases.clear();
    if (name.empty() || (name == "name" && f.size() > 1 && trim(f[1]) == "type")) {
        return false;  // the header some lists have
    }
    // name,type,count,"aliases" (tagcomplete), or name,count
    if (f.size() >= 3 && all_digits(trim(f[2]))) {
        count = std::strtoull(trim(f[2]).c_str(), nullptr, 10);
    } else if (f.size() == 2 && all_digits(trim(f[1]))) {
        count = std::strtoull(trim(f[1]).c_str(), nullptr, 10);
    }
    if (f.size() >= 4) {
        for (const std::string & a : csv_fields(f[3])) {
            const std::string t = trim(a);
            if (!t.empty() && t != name) {
                aliases.push_back(t);
            }
        }
    }
    return true;
}

void TagIndex::load_if_changed() {
    const int64_t now = now_seconds();
    if (now - checked_ < 5) {
        return;
    }
    checked_ = now;
    std::vector<fs::path> paths;
    std::error_code       ec;
    for (fs::directory_iterator it(fs::u8path(dir_), ec), end; !ec && it != end; it.increment(ec)) {
        if (is_tag_file(it->path()) && it->is_regular_file(ec)) {
            paths.push_back(it->path());
        }
    }
    std::sort(paths.begin(), paths.end());
    std::string stamp;
    for (const fs::path & p : paths) {
        std::error_code e;
        const auto      size = static_cast<unsigned long long>(fs::file_size(p, e));
        // the console's file clock counts in a 128-bit type
        const auto      time = static_cast<long long>(fs::last_write_time(p, e).time_since_epoch().count());
        stamp += p.filename().u8string() + "|" + std::to_string(size) + "|" + std::to_string(time) + "\n";
    }
    if (stamp == stamp_) {
        return;
    }
    stamp_ = stamp;
    entries_.clear();
    files_.clear();
    std::vector<Entry> all;
    for (const fs::path & p : paths) {
        files_.push_back(p.filename().u8string());
        std::ifstream            in(p, std::ios::binary);
        std::string              name;
        uint64_t                 count = 0;
        std::vector<std::string> aliases;
        for (std::string line; std::getline(in, line) && all.size() < kMaxTags;) {
            if (!parse_line(line, name, count, aliases)) {
                continue;
            }
            Entry e;
            e.key   = key_of(name);
            e.tag   = prompt_form(name);
            e.count = count;
            for (const std::string & a : aliases) {
                e.alias_keys.push_back(key_of(a));
            }
            all.push_back(std::move(e));
        }
    }
    // most used first; a tag that is in two files counts once (the first)
    std::stable_sort(all.begin(), all.end(), [](const Entry & a, const Entry & b) { return a.count > b.count; });
    std::unordered_set<std::string> taken;
    taken.reserve(all.size());
    for (Entry & e : all) {
        if (taken.insert(e.key).second) {
            entries_.push_back(std::move(e));
        }
    }
    entries_.shrink_to_fit();
}

std::vector<TagHit> TagIndex::find(const std::string & typed, size_t limit) {
    std::vector<TagHit> out;
    const std::string   q = key_of(trim(typed));
    if (q.size() < 2 || limit == 0) {
        return out;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    load_if_changed();
    for (const Entry & e : entries_) {
        if (out.size() >= limit) {
            break;
        }
        if (e.key.compare(0, q.size(), q) == 0) {
            out.push_back({ e.tag, e.count, "" });
            continue;
        }
        for (const std::string & a : e.alias_keys) {
            if (a.compare(0, q.size(), q) == 0) {
                out.push_back({ e.tag, e.count, prompt_form(a) });
                break;
            }
        }
    }
    return out;
}

std::string TagIndex::info_json() {
    std::lock_guard<std::mutex> lock(mutex_);
    load_if_changed();
    return nlohmann::json{ { "files", files_ }, { "tags", entries_.size() } }.dump(-1, ' ', false,
                                                                                 nlohmann::json::error_handler_t::replace);
}

std::string TagIndex::hits_json(const std::vector<TagHit> & hits) {
    nlohmann::json list = nlohmann::json::array();
    for (const TagHit & h : hits) {
        list.push_back({ { "tag", h.tag }, { "count", h.count }, { "alias", h.alias } });
    }
    return nlohmann::json{ { "tags", list } }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace ps5sd
