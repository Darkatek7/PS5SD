// The web UI's prompt history: the last 50 prompts, each with its negative prompt and the model,
// kept on the console so every browser has them (GET, POST and DELETE /ps5sd/v1/app/prompts).
//
// One JSON file, /data/PS5SD/prompts.json: {"prompts": [{"id", "prompt", "negative", "model",
// "time"}, ...]}, the newest first. A prompt that is there already (with the same negative prompt
// and model) moves to the front instead of coming twice. The file is small, so it is written the
// way the gallery's labels are: a temporary file, then a rename.
//
// Header only (build-app.sh compiles each .cpp by name), no console code, so ps5/app/tests builds
// it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include "json.hpp"

namespace ps5sd {

class PromptHistory {
  public:
    static constexpr size_t kMaxEntries = 50;    // the list keeps this many
    static constexpr size_t kMaxText    = 8000;  // bytes of a prompt or a negative prompt
    static constexpr size_t kMaxModel   = 200;   // bytes of the model's name

    explicit PromptHistory(std::string path) : path_(std::move(path)) {}

    // {"prompts": [...]}, the newest first.
    std::string list_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dump(read());
    }

    // Puts a prompt at the front. A prompt of only spaces, a text that is too long, or a control
    // character other than a line break or tab in the model's name is refused (error says why).
    // out is the entry as kept. A prompt that is the newest already (same negative prompt and
    // model) changes nothing; one further down moves to the front.
    bool add(const std::string & prompt, const std::string & negative, const std::string & model, nlohmann::json & out,
             std::string & error, int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        const std::string p = trim(prompt), n = trim(negative), m = trim(model);
        if (p.empty()) {
            error = "the prompt is empty";
            return false;
        }
        if (p.size() > kMaxText || n.size() > kMaxText) {
            error = "a prompt is at most 8000 bytes";
            return false;
        }
        if (m.size() > kMaxModel || m.find_first_of("\r\n\t") != std::string::npos) {
            error = "the model is a name of one line, at most 200 bytes";
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all  = read();
        nlohmann::json &            list = all["prompts"];
        int64_t                     next = 1;
        for (const auto & e : list) {
            next = std::max<int64_t>(next, e.value("id", int64_t(0)) + 1);
        }
        int64_t id = next;   // an entry that moves to the front keeps its id
        for (size_t i = 0; i < list.size(); i++) {
            const auto & e = list[i];
            if (e.value("prompt", std::string()) == p && e.value("negative", std::string()) == n &&
                e.value("model", std::string()) == m) {
                if (i == 0) {
                    out = e;
                    return true;
                }
                id = e.value("id", next);
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        nlohmann::json entry = { { "id", id }, { "prompt", p }, { "negative", n }, { "model", m }, { "time", now } };
        list.insert(list.begin(), entry);
        while (list.size() > kMaxEntries) {
            list.erase(list.end() - 1);
        }
        out = entry;
        if (!write(all)) {
            error = "could not write " + path_;
            return false;
        }
        return true;
    }

    // Removes the entry with this id; false when there is none.
    bool remove(int64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all  = read();
        nlohmann::json &            list = all["prompts"];
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].value("id", int64_t(0)) == id) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                write(all);
                return true;
            }
        }
        return false;
    }

    // Empties the list; how many entries went.
    size_t clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        const size_t                n   = all["prompts"].size();
        if (n > 0) {
            write(nlohmann::json{ { "prompts", nlohmann::json::array() } });
        }
        return n;
    }

    const std::string & path() const { return path_; }

  private:
    static std::string trim(const std::string & s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
            ++a;
        }
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
            --b;
        }
        return s.substr(a, b - a);
    }

    static std::string dump(const nlohmann::json & j) {
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    // The file's list; an empty one when there is none or it does not parse. Entries that are not
    // whole are dropped, and the list is cut to kMaxEntries.
    nlohmann::json read() const {
        namespace fs = std::filesystem;
        nlohmann::json out = { { "prompts", nlohmann::json::array() } };
        std::ifstream  in(fs::u8path(path_), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, false);
        if (!j.is_object() || !j.contains("prompts") || !j["prompts"].is_array()) {
            return out;
        }
        for (const auto & e : j["prompts"]) {
            if (out["prompts"].size() >= kMaxEntries) {
                break;
            }
            if (e.is_object() && e.contains("id") && e["id"].is_number_integer() && e.contains("prompt") &&
                e["prompt"].is_string() && e["id"].get<int64_t>() > 0) {
                out["prompts"].push_back({ { "id", e["id"].get<int64_t>() },
                                           { "prompt", e["prompt"].get<std::string>() },
                                           { "negative", e.value("negative", std::string()) },
                                           { "model", e.value("model", std::string()) },
                                           { "time", e.value("time", int64_t(0)) } });
            }
        }
        return out;
    }

    bool write(const nlohmann::json & all) const {
        namespace fs = std::filesystem;
        const fs::path p   = fs::u8path(path_);
        const fs::path tmp = fs::u8path(path_ + ".tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << dump(all);
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

    std::string        path_;
    mutable std::mutex mutex_;
};

}  // namespace ps5sd
