// The expansion of a prompt template for one image, as the web UI does it (js/09f-wildcards.js) and as
// POST /ps5sd/v1/app/prompts/expand does it for API clients:
//   {a|b|c}     one of the options, picked anew for each image (an option may be empty, {x|{y|z}} works
//               from the inside out; a brace without a "|" stays as it is)
//   __name__    a random entry of the collection "name" (prompt_books.hpp), without regard to case
//   __lora__    the trigger words of the LoRAs that go into the job (positive prompt only)
// Up to 8 passes, so entries may hold braces and other __names__, and an entry that names its own
// collection stops there. A __name__ without a collection (or with an empty one) stays as typed.
// Where a placeholder became empty, the separators it leaves are tidied (", ," -> ",", no comma or
// space at the ends). A prompt that is exactly one __name__ takes the negative prompt of the entry it
// picked, before the typed negative prompt.
//
// Header only, no console code (ps5/app/tests builds it on the PC). The random source is seedable, so
// the same seed, collections and LoRAs give the same items.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "json.hpp"
#include "prompt_books.hpp"

namespace ps5sd {

using PromptBookMap = std::map<std::string, std::vector<PromptBookEntry>>;  // lower-case name -> entries

// A number in [0, 1) from a 64-bit engine, written out so a seed gives the same on every platform.
class PromptRng {
  public:
    explicit PromptRng(uint64_t seed) : engine_(seed) {}
    double next() { return static_cast<double>(engine_() >> 11) / 9007199254740992.0; }

  private:
    std::mt19937_64 engine_;
};

struct PromptExpansion {
    std::string prompt;
    std::string negative;
};

class PromptExpander {
  public:
    static constexpr int kPasses = 8;

    // lora_words: what __lora__ becomes in the prompt (may be empty); the negative prompt leaves it as typed.
    PromptExpander(const PromptBookMap & books, std::string lora_words, PromptRng & rng)
        : books_(books), lora_(std::move(lora_words)), rng_(rng) {}

    // The names of __name__ that matched no collection with entries (as typed the first time), so far.
    const std::vector<std::string> & unknown() const { return unknown_; }

    // One image's prompt and negative prompt from the templates.
    PromptExpansion expand(const std::string & tmpl, const std::string & neg_tmpl) {
        std::string extra, prompt;
        std::string name;
        const PromptBookEntry * picked = nullptr;
        if (exactly_one_name(tmpl, name) && !is_lora(name)) {
            const auto it = books_.find(lower(name));
            if (it != books_.end() && !it->second.empty()) {
                picked = &it->second[pick(it->second.size())];
            }
        }
        if (picked != nullptr) {
            prompt = text(picked->prompt, true);
            if (!picked->negative.empty()) {
                extra = text(picked->negative, false);
            }
        } else {
            prompt = text(tmpl, true);
        }
        const std::string typed = text(neg_tmpl, false);
        std::string       neg   = extra;
        if (!typed.empty()) {
            neg += (neg.empty() ? "" : ", ") + typed;
        }
        return { prompt, neg };
    }

    static std::string tidy(const std::string & t) {
        std::string out;
        size_t      i = 0;
        while (i < t.size()) {  // ",(\s*,)+" -> ","
            if (t[i] == ',') {
                size_t j = i + 1;
                size_t last = i;
                while (j < t.size()) {
                    size_t k = j;
                    while (k < t.size() && std::isspace(static_cast<unsigned char>(t[k]))) {
                        ++k;
                    }
                    if (k < t.size() && t[k] == ',') {
                        last = k;
                        j    = k + 1;
                    } else {
                        break;
                    }
                }
                out += ',';
                i = last + 1;
            } else {
                out += t[i++];
            }
        }
        const auto edge = [](char c) { return c == ',' || std::isspace(static_cast<unsigned char>(c)) != 0; };
        size_t     a = 0, b = out.size();
        while (a < b && edge(out[a])) {
            ++a;
        }
        while (b > a && edge(out[b - 1])) {
            --b;
        }
        return out.substr(a, b - a);
    }

  private:
    static std::string lower(std::string s) {
        for (char & c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }
    static bool is_lora(const std::string & n) { return lower(n) == "lora"; }
    static bool name_char(char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        return (std::isalnum(u) && u < 128) || c == '_' || c == '-';
    }

    size_t pick(size_t n) { return std::min(n - 1, static_cast<size_t>(rng_.next() * static_cast<double>(n))); }

    // /__([A-Za-z0-9_-]{1,40})__/ at s[i]: the name's length, or 0.
    static size_t name_at(const std::string & s, size_t i) {
        if (i + 1 >= s.size() || s[i] != '_' || s[i + 1] != '_') {
            return 0;
        }
        size_t run = 0;
        while (i + 2 + run < s.size() && run < 40 && name_char(s[i + 2 + run])) {
            ++run;
        }
        for (size_t len = run; len >= 1; --len) {
            if (i + 2 + len + 1 < s.size() && s[i + 2 + len] == '_' && s[i + 3 + len] == '_') {
                return len;
            }
        }
        return 0;
    }

    static bool exactly_one_name(const std::string & t, std::string & name) {
        size_t a = 0, b = t.size();
        while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) {
            ++a;
        }
        while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) {
            --b;
        }
        const std::string core = t.substr(a, b - a);
        const size_t      len  = name_at(core, 0);
        if (len == 0 || core.size() != len + 4) {
            return false;
        }
        name = core.substr(2, len);
        return true;
    }

    void note_unknown(const std::string & name) {
        const std::string l = lower(name);
        for (const auto & u : unknown_) {
            if (lower(u) == l) {
                return;
            }
        }
        unknown_.push_back(name);
    }

    // is_prompt: __lora__ is expanded (the positive prompt); otherwise it stays as typed.
    std::string text(std::string t, bool is_prompt) {
        bool emptied = false;
        for (int pass = 0; pass < kPasses; ++pass) {
            bool again = false;
            // {a|b}: the innermost braces with a "|"
            std::string out;
            for (size_t i = 0; i < t.size();) {
                if (t[i] == '{') {
                    size_t j = i + 1;
                    while (j < t.size() && t[j] != '{' && t[j] != '}') {
                        ++j;
                    }
                    if (j < t.size() && t[j] == '}' && t.find('|', i) < j) {
                        std::vector<std::string> options;
                        size_t                   from = i + 1;
                        for (size_t k = i + 1; k <= j; ++k) {
                            if (k == j || t[k] == '|') {
                                options.push_back(t.substr(from, k - from));
                                from = k + 1;
                            }
                        }
                        out += options[pick(options.size())];
                        again = true;
                        i     = j + 1;
                        continue;
                    }
                }
                out += t[i++];
            }
            t = out;
            // __name__
            out.clear();
            for (size_t i = 0; i < t.size();) {
                const size_t len = name_at(t, i);
                if (len == 0) {
                    out += t[i++];
                    continue;
                }
                const std::string name  = t.substr(i + 2, len);
                const std::string whole = t.substr(i, len + 4);
                i += len + 4;
                if (is_lora(name)) {
                    if (is_prompt) {
                        again = true;
                        if (lora_.empty()) {
                            emptied = true;
                        }
                        out += lora_;
                    } else {
                        out += whole;
                    }
                    continue;
                }
                const auto it = books_.find(lower(name));
                if (it == books_.end() || it->second.empty()) {
                    note_unknown(name);
                    out += whole;
                    continue;
                }
                const std::string & entry = it->second[pick(it->second.size())].prompt;
                again                     = true;
                emptied                   = emptied || entry.empty();
                out += entry;
            }
            t = out;
            if (!again) {
                break;
            }
        }
        return emptied ? tidy(t) : t;
    }

    const PromptBookMap &    books_;
    std::string              lora_;
    PromptRng &              rng_;
    std::vector<std::string> unknown_;
};

// The LoRAs whose words __lora__ stands for: the "lora" field of the request ([{"path", "multiplier"}] or
// plain path strings; a multiplier of 0 is off), else the <lora:NAME:weight> tags of the prompt. words_of
// gives the trigger words of a file under the loras folder (its path with the extension; for a tag, NAME
// with ".safetensors" behind it, which has the stem NAME). A LoRA with no words adds nothing. Returns false
// (with the reason) when "lora" is not that shape or a path is not a plain relative path.
inline bool collect_lora_words(const nlohmann::json & lora_field, bool has_field, const std::string & prompt,
                               const std::function<std::string(const std::string &)> & words_of, std::string & words,
                               std::string & error) {
    std::vector<std::string> files;
    const auto               add = [&](const std::string & f) {
        if (std::find(files.begin(), files.end(), f) == files.end()) {
            files.push_back(f);
        }
    };
    const auto plain = [](const std::string & p) {
        if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find('\0') != std::string::npos || p.find('\\') != std::string::npos) {
            return false;
        }
        size_t at = 0;
        while (at <= p.size()) {
            const size_t next = p.find('/', at);
            const std::string seg = p.substr(at, next == std::string::npos ? std::string::npos : next - at);
            if (seg.empty() || seg == "." || seg == "..") {
                return false;
            }
            if (next == std::string::npos) {
                break;
            }
            at = next + 1;
        }
        return true;
    };
    if (has_field) {
        if (!lora_field.is_array()) {
            error = "lora must be a list of {\"path\": \"...\", \"multiplier\": 1.0}";
            return false;
        }
        for (const auto & l : lora_field) {
            std::string path;
            double      mult = 1.0;
            if (l.is_string()) {
                path = l.get<std::string>();
            } else if (l.is_object() && l.contains("path") && l["path"].is_string() &&
                       (!l.contains("multiplier") || l["multiplier"].is_number())) {
                path = l["path"].get<std::string>();
                mult = l.contains("multiplier") ? l["multiplier"].get<double>() : 1.0;
            } else {
                error = "lora must be a list of {\"path\": \"...\", \"multiplier\": 1.0}";
                return false;
            }
            if (!plain(path)) {
                error = "not a LoRA path: " + path;
                return false;
            }
            if (mult != 0.0) {
                add(path);
            }
        }
    } else {
        size_t at = 0;
        while ((at = prompt.find("<lora:", at)) != std::string::npos) {
            const size_t end = prompt.find('>', at);
            if (end == std::string::npos) {
                break;
            }
            std::string inner = prompt.substr(at + 6, end - at - 6);
            at                = end + 1;
            const size_t colon = inner.find(':');
            std::string  name  = inner.substr(0, colon);
            if (colon != std::string::npos) {
                const std::string w = inner.substr(colon + 1);
                char *            e = nullptr;
                const double      v = std::strtod(w.c_str(), &e);
                if (e != w.c_str() && v == 0.0) {
                    continue;
                }
            }
            if (plain(name)) {
                add(name + ".safetensors");
            }
        }
    }
    words.clear();
    for (const auto & f : files) {
        std::string w = words_of(f);
        size_t      a = 0, b = w.size();
        const auto  edge = [](char c) { return c == ',' || std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (a < b && edge(w[a])) {
            ++a;
        }
        while (b > a && edge(w[b - 1])) {
            --b;
        }
        w = w.substr(a, b - a);
        if (!w.empty()) {
            words += (words.empty() ? "" : ", ") + w;
        }
    }
    return true;
}

}  // namespace ps5sd
