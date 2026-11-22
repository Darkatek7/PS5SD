// The web UI's prompt collections ("prompt books"): named lists of prompts, kept on the console so every
// browser has them (GET, POST and DELETE /ps5sd/v1/app/promptbooks...). A prompt in the web UI may say
// __name__ for a random entry of the collection "name".
//
// One JSON file, /data/PS5SD/promptbooks.json: {"books": [{"name", "time", "next_id", "entries": [{"id",
// "prompt", "negative"}, ...]}, ...]}. The books are in the order they were made in, the entries in the
// user's order. "next_id" keeps ids from coming back after a delete; the answers do not show it.
// A name is 1 to 40 of letters, digits, "-" and "_", unique without regard to case ("lora" is reserved).
// The file is written as prompts.json is: a temporary file, then a rename.
//
// Header only (build-app.sh compiles each .cpp by name), no console code, so ps5/app/tests builds it on
// the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace ps5sd {

struct PromptBookEntry {
    std::string prompt;
    std::string negative;
};

enum class PromptBookStatus { Ok, Created, Invalid, TooMany, NotFound, Conflict, WriteFailed };

class PromptBooks {
  public:
    static constexpr size_t kMaxBooks   = 200;
    static constexpr size_t kMaxEntries = 2000;  // in one book
    static constexpr size_t kMaxName    = 40;
    static constexpr size_t kMaxText    = 8000;  // bytes of a prompt or a negative prompt

    explicit PromptBooks(std::string path) : path_(std::move(path)) {}

    // {"books": [<book>, ...]}
    std::string list_json() const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json              out = { { "books", nlohmann::json::array() } };
        for (const auto & b : all["books"]) {
            out["books"].push_back(view(b));
        }
        return dump(out);
    }

    // Every book's entries by the lower-case name, for the expansion (prompt_expand.hpp).
    std::map<std::string, std::vector<PromptBookEntry>> entries_by_name() const {
        std::lock_guard<std::mutex>                         lock(mutex_);
        std::map<std::string, std::vector<PromptBookEntry>> out;
        const nlohmann::json                                all = read();
        for (const auto & b : all["books"]) {
            auto & list = out[lower(b["name"].get<std::string>())];
            for (const auto & e : b["entries"]) {
                list.push_back({ e["prompt"].get<std::string>(), e["negative"].get<std::string>() });
            }
        }
        return out;
    }

    PromptBookStatus get(const std::string & name, nlohmann::json & out, std::string & error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const nlohmann::json        all = read();
        const nlohmann::json *      b   = find(all, name);
        if (b == nullptr) {
            error = "no collection " + name;
            return PromptBookStatus::NotFound;
        }
        out = view(*b);
        return PromptBookStatus::Ok;
    }

    // Makes a collection (Created); Conflict when the name is taken.
    PromptBookStatus create(const std::string & name, const std::vector<PromptBookEntry> & entries, nlohmann::json & out,
                            std::string & error, int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        if (!name_ok(name, error)) {
            return PromptBookStatus::Invalid;
        }
        std::vector<PromptBookEntry> clean;
        PromptBookStatus             st = clean_entries(entries, clean, error);
        if (st != PromptBookStatus::Ok) {
            return st;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        if (find(all, name) != nullptr) {
            error = "there is a collection " + name + " already";
            return PromptBookStatus::Conflict;
        }
        if (all["books"].size() >= kMaxBooks) {
            error = "at most 200 collections";
            return PromptBookStatus::TooMany;
        }
        nlohmann::json book = make_book(name, now);
        fill(book, clean);
        all["books"].push_back(book);
        out = view(book);
        return save(all, error) ? PromptBookStatus::Created : PromptBookStatus::WriteFailed;
    }

    // Renames (new_name) and/or replaces the entries (entries; new ids from 1). Nothing changes when
    // one part is refused.
    PromptBookStatus update(const std::string & name, const std::string * new_name, const std::vector<PromptBookEntry> * entries,
                            nlohmann::json & out, std::string & error, int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        if (new_name != nullptr && !name_ok(*new_name, error)) {
            return PromptBookStatus::Invalid;
        }
        std::vector<PromptBookEntry> clean;
        if (entries != nullptr) {
            PromptBookStatus st = clean_entries(*entries, clean, error);
            if (st != PromptBookStatus::Ok) {
                return st;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json *            b   = find(all, name);
        if (b == nullptr) {
            error = "no collection " + name;
            return PromptBookStatus::NotFound;
        }
        if (new_name != nullptr) {
            const nlohmann::json * other = find(all, *new_name);
            if (other != nullptr && other != b) {
                error = "there is a collection " + *new_name + " already";
                return PromptBookStatus::Conflict;
            }
            (*b)["name"] = *new_name;
        }
        if (entries != nullptr) {
            fill(*b, clean);
        }
        (*b)["time"] = now;
        out          = view(*b);
        return save(all, error) ? PromptBookStatus::Ok : PromptBookStatus::WriteFailed;
    }

    // Removes a collection; name is set to the spelling it had.
    PromptBookStatus remove_book(std::string & name, std::string & error) {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json *            b   = find(all, name);
        if (b == nullptr) {
            error = "no collection " + name;
            return PromptBookStatus::NotFound;
        }
        name = (*b)["name"].get<std::string>();
        auto & list = all["books"];
        list.erase(list.begin() + (b - &list[0]));
        return save(all, error) ? PromptBookStatus::Ok : PromptBookStatus::WriteFailed;
    }

    // Adds an entry at the end; makes the collection when there is none. The same prompt and negative
    // prompt there already: Ok with that entry, nothing added. Created for a new entry.
    PromptBookStatus add_entry(const std::string & name, const std::string & prompt, const std::string & negative, nlohmann::json & out,
                               std::string & error, int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        PromptBookEntry e{ trim(prompt), trim(negative) };
        if (!entry_ok(e, error)) {
            return PromptBookStatus::Invalid;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json *            b   = find(all, name);
        if (b == nullptr) {
            if (!name_ok(name, error)) {
                return PromptBookStatus::Invalid;
            }
            if (all["books"].size() >= kMaxBooks) {
                error = "at most 200 collections";
                return PromptBookStatus::TooMany;
            }
            all["books"].push_back(make_book(name, now));
            b = &all["books"].back();
        }
        for (const auto & x : (*b)["entries"]) {
            if (x["prompt"] == e.prompt && x["negative"] == e.negative) {
                out = x;
                return PromptBookStatus::Ok;
            }
        }
        if ((*b)["entries"].size() >= kMaxEntries) {
            error = "at most 2000 entries in a collection";
            return PromptBookStatus::TooMany;
        }
        const int64_t  id    = (*b)["next_id"].get<int64_t>();
        nlohmann::json entry = { { "id", id }, { "prompt", e.prompt }, { "negative", e.negative } };
        (*b)["entries"].push_back(entry);
        (*b)["next_id"] = id + 1;
        (*b)["time"]    = now;
        out             = entry;
        return save(all, error) ? PromptBookStatus::Created : PromptBookStatus::WriteFailed;
    }

    // Changes an entry's prompt and/or negative prompt (null: unchanged).
    PromptBookStatus edit_entry(const std::string & name, int64_t id, const std::string * prompt, const std::string * negative,
                                nlohmann::json & out, std::string & error, int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        std::string p = prompt != nullptr ? trim(*prompt) : std::string(), n = negative != nullptr ? trim(*negative) : std::string();
        if (prompt != nullptr && p.empty()) {
            error = "the prompt is empty";
            return PromptBookStatus::Invalid;
        }
        if (p.size() > kMaxText || n.size() > kMaxText) {
            error = "a prompt is at most 8000 bytes";
            return PromptBookStatus::Invalid;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json *            b   = find(all, name);
        if (b == nullptr) {
            error = "no collection " + name;
            return PromptBookStatus::NotFound;
        }
        for (auto & x : (*b)["entries"]) {
            if (x["id"].get<int64_t>() == id) {
                if (prompt != nullptr) {
                    x["prompt"] = p;
                }
                if (negative != nullptr) {
                    x["negative"] = n;
                }
                (*b)["time"] = now;
                out          = x;
                return save(all, error) ? PromptBookStatus::Ok : PromptBookStatus::WriteFailed;
            }
        }
        error = "no entry " + std::to_string(id) + " in " + name;
        return PromptBookStatus::NotFound;
    }

    PromptBookStatus remove_entry(const std::string & name, int64_t id, std::string & error,
                                  int64_t now = static_cast<int64_t>(std::time(nullptr))) {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json              all = read();
        nlohmann::json *            b   = find(all, name);
        if (b == nullptr) {
            error = "no collection " + name;
            return PromptBookStatus::NotFound;
        }
        auto & list = (*b)["entries"];
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i]["id"].get<int64_t>() == id) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
                (*b)["time"] = now;
                return save(all, error) ? PromptBookStatus::Ok : PromptBookStatus::WriteFailed;
            }
        }
        error = "no entry " + std::to_string(id) + " in " + name;
        return PromptBookStatus::NotFound;
    }

    // [{"prompt", "negative"}, ...] from a request body; false (with the reason) when it is not that.
    static bool parse_entries(const nlohmann::json & j, std::vector<PromptBookEntry> & out, std::string & error) {
        out.clear();
        if (!j.is_array()) {
            error = "entries must be a list of {\"prompt\": \"...\", \"negative\": \"...\"}";
            return false;
        }
        for (const auto & e : j) {
            if (!e.is_object() || !e.contains("prompt") || !e["prompt"].is_string() ||
                (e.contains("negative") && !e["negative"].is_string())) {
                error = "entries must be a list of {\"prompt\": \"...\", \"negative\": \"...\"}";
                return false;
            }
            out.push_back({ e["prompt"].get<std::string>(), e.value("negative", std::string()) });
        }
        return true;
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

    static std::string lower(std::string s) {
        for (char & c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    static std::string dump(const nlohmann::json & j) {
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    static bool name_chars_ok(const std::string & name) {
        if (name.empty() || name.size() > kMaxName) {
            return false;
        }
        for (unsigned char c : name) {
            if (!(std::isalnum(c) && c < 128) && c != '-' && c != '_') {
                return false;
            }
        }
        return true;
    }

    static bool name_ok(const std::string & name, std::string & error) {
        if (!name_chars_ok(name)) {
            error = "a name is 1 to 40 letters, digits, - and _";
            return false;
        }
        if (lower(name) == "lora") {
            error = "the name lora is reserved for __lora__";
            return false;
        }
        return true;
    }

    static bool entry_ok(const PromptBookEntry & e, std::string & error) {
        if (e.prompt.empty()) {
            error = "the prompt is empty";
            return false;
        }
        if (e.prompt.size() > kMaxText || e.negative.size() > kMaxText) {
            error = "a prompt is at most 8000 bytes";
            return false;
        }
        return true;
    }

    static PromptBookStatus clean_entries(const std::vector<PromptBookEntry> & in, std::vector<PromptBookEntry> & out,
                                          std::string & error) {
        if (in.size() > kMaxEntries) {
            error = "at most 2000 entries in a collection";
            return PromptBookStatus::TooMany;
        }
        out.clear();
        for (const auto & e : in) {
            PromptBookEntry c{ trim(e.prompt), trim(e.negative) };
            if (!entry_ok(c, error)) {
                return PromptBookStatus::Invalid;
            }
            out.push_back(std::move(c));
        }
        return PromptBookStatus::Ok;
    }

    static nlohmann::json make_book(const std::string & name, int64_t now) {
        return { { "name", name }, { "time", now }, { "next_id", int64_t(1) }, { "entries", nlohmann::json::array() } };
    }

    // The entries replaced, numbered from 1.
    static void fill(nlohmann::json & book, const std::vector<PromptBookEntry> & entries) {
        book["entries"]  = nlohmann::json::array();
        int64_t id       = 1;
        for (const auto & e : entries) {
            book["entries"].push_back({ { "id", id++ }, { "prompt", e.prompt }, { "negative", e.negative } });
        }
        book["next_id"] = id;
    }

    static nlohmann::json view(const nlohmann::json & b) {
        return { { "name", b["name"] }, { "time", b["time"] }, { "entries", b["entries"] } };
    }

    // The book of this name without regard to case; null when there is none.
    template <class J> static J * find_in(J & all, const std::string & name) {
        const std::string l = lower(name);
        for (auto & b : all["books"]) {
            if (lower(b["name"].template get<std::string>()) == l) {
                return &b;
            }
        }
        return nullptr;
    }
    static const nlohmann::json * find(const nlohmann::json & all, const std::string & name) { return find_in(all, name); }
    static nlohmann::json *       find(nlohmann::json & all, const std::string & name) { return find_in(all, name); }

    // The file's books; an empty list when there is none or it does not parse. Books and entries that
    // are not whole are dropped (and so are the second book of a name and the ones over the limits).
    nlohmann::json read() const {
        namespace fs = std::filesystem;
        nlohmann::json     out = { { "books", nlohmann::json::array() } };
        std::ifstream      in(fs::u8path(path_), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, false);
        if (!j.is_object() || !j.contains("books") || !j["books"].is_array()) {
            return out;
        }
        for (const auto & b : j["books"]) {
            if (out["books"].size() >= kMaxBooks) {
                break;
            }
            if (!b.is_object() || !b.contains("name") || !b["name"].is_string() || !name_chars_ok(b["name"].get<std::string>()) ||
                find(out, b["name"].get<std::string>()) != nullptr) {
                continue;
            }
            nlohmann::json book = make_book(b["name"].get<std::string>(), b.value("time", int64_t(0)));
            int64_t        next = 1;
            if (b.contains("entries") && b["entries"].is_array()) {
                for (const auto & e : b["entries"]) {
                    if (book["entries"].size() >= kMaxEntries) {
                        break;
                    }
                    if (e.is_object() && e.contains("id") && e["id"].is_number_integer() && e["id"].get<int64_t>() > 0 &&
                        e.contains("prompt") && e["prompt"].is_string()) {
                        book["entries"].push_back({ { "id", e["id"].get<int64_t>() },
                                                    { "prompt", e["prompt"].get<std::string>() },
                                                    { "negative", e.value("negative", std::string()) } });
                        next = std::max<int64_t>(next, e["id"].get<int64_t>() + 1);
                    }
                }
            }
            if (b.contains("next_id") && b["next_id"].is_number_integer()) {
                next = std::max<int64_t>(next, b["next_id"].get<int64_t>());
            }
            book["next_id"] = next;
            out["books"].push_back(book);
        }
        return out;
    }

    bool save(const nlohmann::json & all, std::string & error) const {
        if (write(all)) {
            return true;
        }
        error = "could not write " + path_;
        return false;
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
