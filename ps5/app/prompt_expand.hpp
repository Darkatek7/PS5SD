// The expansion of a prompt template for one image, as the web UI does it (js/09f-wildcards.js) and as
// POST /ps5sd/v1/app/prompts/expand does it for API clients:
//   {a|b|c}     one of the options, picked anew for each image (an option may be empty, {x|{y|z}} works
//               from the inside out; a brace without a "|" stays as it is)
//   __name__    a random entry of the collection "name" (prompt_books.hpp), without regard to case
//   __lora__    the trigger words of the LoRAs that go into the job (positive prompt only), each LoRA's
//               list by its mode (all / one at random / some at random), picked anew for each image;
//               LoRAs that a __lora:NAME__ of the same prompt names are left out
//   __lora:NAME__  only that installed LoRA's words (NAME: its path under the loras folder without the
//               extension, or its file name without it, any case; no match stays as typed)
// Up to 8 passes, so entries may hold braces and other __names__, and an entry that names its own
// collection stops there. A __name__ without a collection (or with an empty one) stays as typed.
// Where a placeholder became empty, the separators it leaves are tidied (", ," -> ",", no comma or
// space at the ends). A prompt that is exactly one __name__ takes the negative prompt of the entry it
// picked, before the typed negative prompt. expand_books() takes a random entry of several collections
// together as the template of one image (the page's "Random prompt for each image").
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
#include <set>
#include <string>
#include <vector>

#include "json.hpp"
#include "lora_info.hpp"
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

// "style/Pixel.safetensors" -> "style/pixel": lower case, a model file's extension off.
inline std::string lora_key(const std::string & path) {
    std::string k = trigger_lower(path);
    const size_t slash = k.rfind('/');
    const size_t dot   = k.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        const std::string ext = k.substr(dot);
        if (ext == ".safetensors" || ext == ".ckpt" || ext == ".pt" || ext == ".pth" || ext == ".gguf" || ext == ".bin") {
            k.resize(dot);
        }
    }
    return k;
}

// A LoRA the expansion knows: key = lora_key(path).
struct LoraEntry {
    std::string  key;
    LoraTriggers triggers;
};

// A request's change for one LoRA, for this call only.
struct LoraOverride {
    bool                     has_list = false, has_mode = false, has_count = false;
    std::vector<LoraTrigger> list;
    std::string              mode;
    int                      count = 0;

    void apply(LoraTriggers & t) const {
        if (has_list) {
            t.items = list;
        }
        if (has_mode) {
            t.mode = mode;
        }
        if (has_count) {
            t.count = count;
        }
    }
};

// What __lora__ and __lora:NAME__ stand for.
struct LoraSource {
    std::vector<LoraEntry>                  job;        // the job's LoRAs, in order
    std::map<std::string, LoraOverride>     overrides;  // by key, for the job's and the named ones
    // Every installed LoRA in the order of the list (called once, when a __lora:NAME__ needs it); may be empty.
    std::function<std::vector<LoraEntry>()> installed;
};

struct PromptExpansion {
    std::string prompt;
    std::string negative;
};

class PromptExpander {
  public:
    static constexpr int kPasses = 8;

    // lora_words: what __lora__ becomes in the prompt (may be empty): one item, always put in. The negative
    // prompt leaves it as typed.
    PromptExpander(const PromptBookMap & books, const std::string & lora_words, PromptRng & rng) : books_(books), rng_(rng) {
        const std::string w = trigger_trim(lora_words);
        if (!w.empty()) {
            LoraEntry e;
            e.triggers.items.push_back({ w, true });
            src_.job.push_back(e);
        }
    }
    PromptExpander(const PromptBookMap & books, LoraSource source, PromptRng & rng)
        : books_(books), src_(std::move(source)), rng_(rng) {}

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

    // One image from a random entry of several collections together (the page's "Random prompt for each
    // image", POST prompts/expand's "books"): each entry is as likely as any other, a name given twice counts
    // once, a name without a collection goes to unknown(). The entry is this image's template: tmpl is its
    // prompt, neg_tmpl its negative prompt and then the typed one (neg_typed), joined with ", ". false: none of
    // the collections has an entry.
    bool expand_books(const std::vector<std::string> & names, const std::string & neg_typed, PromptExpansion & out,
                      std::string & tmpl, std::string & neg_tmpl) {
        std::vector<const std::vector<PromptBookEntry> *> lists;
        std::set<std::string>                             seen;
        size_t                                            total = 0;
        for (const auto & n : names) {
            const std::string k = lower(n);
            if (!seen.insert(k).second) {
                continue;
            }
            const auto it = is_lora(k) ? books_.end() : books_.find(k);
            if (it == books_.end()) {
                note_unknown(n);
                continue;
            }
            if (!it->second.empty()) {
                lists.push_back(&it->second);
                total += it->second.size();
            }
        }
        if (total == 0) {
            return false;
        }
        size_t                  i = pick(total);
        const PromptBookEntry * e = nullptr;
        for (const auto * l : lists) {
            if (i < l->size()) {
                e = &(*l)[i];
                break;
            }
            i -= l->size();
        }
        tmpl     = e->prompt;
        neg_tmpl = e->negative;
        if (!neg_typed.empty()) {
            neg_tmpl += (neg_tmpl.empty() ? "" : ", ") + neg_typed;
        }
        out.prompt              = text(e->prompt, true);
        std::string       neg   = e->negative.empty() ? std::string() : text(e->negative, false);
        const std::string typed = text(neg_typed, false);
        if (!typed.empty()) {
            neg += (neg.empty() ? "" : ", ") + typed;
        }
        out.negative = neg;
        return true;
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

    // "__lora:NAME__" at s[i]: its whole length, or 0. NAME is any text without a line break, up to 1024
    // bytes, that neither starts nor ends with a space or "_", and it ends at the first "__" (the page's
    // /__lora:([^_\s](?:(?:[^_\n]|_(?!_))*[^_\s])?)__/i).
    static size_t lora_name_at(const std::string & s, size_t i, std::string & name) {
        if (i + 7 >= s.size() || s[i] != '_' || s[i + 1] != '_' || lower(s.substr(i + 2, 5)) != "lora:") {
            return 0;
        }
        const auto   edge = [](char c) { return c == '_' || std::isspace(static_cast<unsigned char>(c)) != 0; };
        const size_t from = i + 7;
        if (edge(s[from])) {
            return 0;
        }
        for (size_t j = from + 1; j + 1 < s.size() && j - from <= 1024; ++j) {
            if (s[j] == '_' && s[j + 1] == '_') {
                if (edge(s[j - 1])) {
                    return 0;
                }
                name = s.substr(from, j - from);
                return j + 2 - i;
            }
            if (s[j] == '\n') {
                return 0;
            }
        }
        return 0;
    }

    const std::vector<LoraEntry> & installed() {
        if (!installed_loaded_) {
            installed_loaded_ = true;
            if (src_.installed) {
                installed_ = src_.installed();
            }
        }
        return installed_;
    }

    // The installed LoRA a NAME means: the path first, then the file name; null when none.
    const LoraEntry * resolve(const std::string & name) {
        const std::string l = lower(name);
        const auto &      all = installed();
        for (const auto & e : all) {
            if (e.key == l) {
                return &e;
            }
        }
        for (const auto & e : all) {
            const size_t slash = e.key.rfind('/');
            if ((slash == std::string::npos ? e.key : e.key.substr(slash + 1)) == l) {
                return &e;
            }
        }
        return nullptr;
    }

    // One image's words of a LoRA: the items that are on, by its mode.
    std::string words_of(const LoraEntry & e) {
        LoraTriggers t   = e.triggers;
        const auto   ov  = src_.overrides.find(e.key);
        if (ov != src_.overrides.end()) {
            ov->second.apply(t);
        }
        std::vector<std::string> on;
        for (const auto & it : t.items) {
            if (it.on) {
                on.push_back(it.text);
            }
        }
        if (on.empty()) {
            return "";
        }
        std::vector<size_t> idx;
        if (t.mode == "one") {
            idx.push_back(pick(on.size()));
        } else if (t.mode == "some") {
            std::vector<size_t> all(on.size());
            for (size_t k = 0; k < all.size(); ++k) {
                all[k] = k;
            }
            const size_t want = std::min(static_cast<size_t>(std::max(t.count, 1)), on.size());
            for (size_t k = 0; k < want; ++k) {  // partial shuffle
                const size_t at = k + pick(all.size() - k);
                std::swap(all[k], all[at]);
                idx.push_back(all[k]);
            }
            std::sort(idx.begin(), idx.end());
        } else {
            for (size_t k = 0; k < on.size(); ++k) {
                idx.push_back(k);
            }
        }
        std::string out;
        for (size_t k : idx) {
            out += (out.empty() ? "" : ", ") + on[k];
        }
        return out;
    }

    // What __lora__ is for this image: the job's LoRAs but those a __lora:NAME__ names.
    std::string job_words() {
        std::string out;
        for (const auto & e : src_.job) {
            if (named_.count(e.key) != 0) {
                continue;
            }
            const std::string w = words_of(e);
            if (!w.empty()) {
                out += (out.empty() ? "" : ", ") + w;
            }
        }
        return out;
    }
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

    // is_prompt: __lora__ and __lora:NAME__ are expanded (the positive prompt); otherwise they stay as typed.
    // __lora__ goes last, in the pass where nothing else is left to expand, so it leaves out every LoRA a
    // __lora:NAME__ took on the way (also one from a collection's entry), as the page does.
    std::string text(std::string t, bool is_prompt) {
        bool emptied   = false;
        bool lora_done = false;
        if (is_prompt) {
            named_.clear();
        }
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
                std::string  lname;
                const size_t llen = lora_name_at(t, i, lname);
                if (llen != 0) {
                    const LoraEntry * e = is_prompt ? resolve(lname) : nullptr;
                    if (e == nullptr) {
                        if (is_prompt) {
                            note_unknown("lora:" + lname);
                        }
                        out += t.substr(i, llen);
                    } else {
                        named_.insert(e->key);
                        const std::string w = words_of(*e);
                        again               = true;
                        emptied             = emptied || w.empty();
                        out += w;
                    }
                    i += llen;
                    continue;
                }
                const size_t len = name_at(t, i);
                if (len == 0) {
                    out += t[i++];
                    continue;
                }
                const std::string name  = t.substr(i + 2, len);
                const std::string whole = t.substr(i, len + 4);
                i += len + 4;
                if (is_lora(name)) {
                    out += whole;  // __lora__: below, once nothing else is left
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
            if (!again && !lora_done && is_prompt) {
                lora_done = true;
                out.clear();
                for (size_t i = 0; i < t.size();) {
                    const size_t len = name_at(t, i);
                    if (len != 0 && is_lora(t.substr(i + 2, len))) {
                        const std::string w = job_words();
                        again               = true;
                        emptied             = emptied || w.empty();
                        out += w;
                        i += len + 4;
                        continue;
                    }
                    if (len != 0) {
                        out += t.substr(i, len + 4);
                        i += len + 4;
                        continue;
                    }
                    out += t[i++];
                }
                t = out;
            }
            if (!again) {
                break;
            }
        }
        return emptied ? tidy(t) : t;
    }

    const PromptBookMap &    books_;
    LoraSource               src_;
    PromptRng &              rng_;
    std::vector<std::string> unknown_;
    std::vector<LoraEntry>   installed_;
    bool                     installed_loaded_ = false;
    std::set<std::string>    named_;  // keys of the LoRAs the current template names
};

// The LoRAs __lora__ stands for: the "lora" field of the request ([{"path", "multiplier"}] or plain path
// strings; a multiplier of 0 is off), else the <lora:NAME:weight> tags of the prompt. triggers_of gives the
// stored words of a file under the loras folder (its path with the extension; for a tag, NAME with
// ".safetensors" behind it, which has the stem NAME). An item of the field may carry "triggers", "trigger_mode"
// and "trigger_count": they replace the stored ones for this call (also for a __lora:NAME__ of that LoRA).
// Returns false (with the reason) when "lora" is not that shape, a path is not a plain relative path or an
// override is bad.
inline bool collect_lora_job(const nlohmann::json & lora_field, bool has_field, const std::string & prompt,
                             const std::function<LoraTriggers(const std::string &)> & triggers_of, LoraSource & src,
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
            if (l.is_object()) {
                LoraOverride ov;
                if (l.contains("triggers")) {
                    ov.has_list = true;
                    if (!parse_trigger_items(l["triggers"], ov.list, error)) {
                        return false;
                    }
                }
                if (l.contains("trigger_mode")) {
                    ov.has_mode = true;
                    if (!parse_trigger_mode(l["trigger_mode"], ov.mode, error)) {
                        return false;
                    }
                }
                if (l.contains("trigger_count")) {
                    ov.has_count = true;
                    if (!parse_trigger_count(l["trigger_count"], ov.count, error)) {
                        return false;
                    }
                }
                if (ov.has_list || ov.has_mode || ov.has_count) {
                    src.overrides[lora_key(path)] = ov;
                }
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
    src.job.clear();
    for (const auto & f : files) {
        src.job.push_back({ lora_key(f), triggers_of(f) });
    }
    return true;
}

}  // namespace ps5sd
