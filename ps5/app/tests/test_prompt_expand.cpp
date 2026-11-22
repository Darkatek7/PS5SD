// Host test for prompt_expand.hpp: {a|b}, __name__, __lora__, the negative-prompt rule, the lora list, seeds.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_expand.hpp"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <set>
#include <string>

using namespace ps5sd;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

PromptBookMap books() {
    PromptBookMap b;
    b["people"]   = { { "an old fisherman", "" }, { "a young {sailor|pilot}", "blurry" }, { "a __hats__ wearer", "" } };
    b["hats"]     = { { "cap", "" } };
    b["loop"]     = { { "again __loop__", "" } };
    b["empty"]    = {};
    b["blank"]    = { { "", "" } };
    b["single"]   = { { "only", "ugly" } };
    b["bad"]      = { { "lowres", "" }, { "jpeg artifacts", "" } };
    b["styled"]   = { { "__lora:other/watercolor__ look", "" } };
    return b;
}

struct Out {
    PromptExpansion          e;
    std::vector<std::string> unknown;
};

Out run(const std::string & t, const std::string & n = "", const std::string & lora = "", uint64_t seed = 1) {
    const PromptBookMap bm = books();
    PromptRng           rng(seed);
    PromptExpander      x(bm, lora, rng);
    Out                 o;
    o.e       = x.expand(t, n);
    o.unknown = x.unknown();
    return o;
}

std::set<std::string> seen(const std::string & t, int n = 300) {
    const PromptBookMap bm = books();
    PromptRng           rng(7);
    PromptExpander      x(bm, "", rng);
    std::set<std::string> s;
    for (int i = 0; i < n; i++) {
        s.insert(x.expand(t, "").prompt);
    }
    return s;
}

void test_expand() {
    check(run("plain text").e.prompt == "plain text", "no placeholder: as typed");
    const auto ab = seen("{a|b|c} x");
    check(ab == std::set<std::string>({ "a x", "b x", "c x" }), "{a|b|c} gives each option");
    check(seen("{a|}") == std::set<std::string>({ "a", "" }), "an empty option");
    check(run("{solo} {x|y").e.prompt == "{solo} {x|y", "a brace without a | stays");
    check(seen("{x|{y|z}}") == std::set<std::string>({ "x", "y", "z" }), "nested pair");

    check(seen("__hats__") == std::set<std::string>({ "cap" }), "__name__");
    check(seen("a __HATS__ b") == std::set<std::string>({ "a cap b" }), "case-insensitive name");
    const auto nested = seen("p: __people__");
    check(nested == std::set<std::string>({ "p: an old fisherman", "p: a young sailor", "p: a young pilot", "p: a cap wearer" }),
          "an entry with braces and another __name__");
    check(run("x __loop__").e.prompt == "x again again again again again again again again __loop__", "a self-reference stops after 8 passes");

    Out u = run("__nope__ and __empty__ and __nope__");
    check(u.e.prompt == "__nope__ and __empty__ and __nope__", "unknown and empty collections stay as typed");
    check(u.unknown.size() == 2 && u.unknown[0] == "nope" && u.unknown[1] == "empty", "unknown lists each once");
    check(run("long_hair, ^_^, a__b, __, ____, _x_").e.prompt == "long_hair, ^_^, a__b, __, ____, _x_", "Danbooru-like text is untouched");
    check(run("a __x y__ b").e.prompt == "a __x y__ b", "a space is not a name");
    check(run("__" + std::string(41, 'a') + "__").e.prompt == "__" + std::string(41, 'a') + "__", "a name of 41 characters is not one");
    check(run("a__b__c").unknown.size() == 1 && run("a__b__c").unknown[0] == "b", "a name inside a word");
}

void test_lora() {
    check(run("__lora__, portrait", "", "pixel art, style").e.prompt == "pixel art, style, portrait", "__lora__ with words");
    check(run("__lora__, portrait", "", "").e.prompt == "portrait", "__lora__ without words: separators tidied");
    check(run("a, __lora__, b", "", "").e.prompt == "a, b", "in the middle");
    check(run("a, __lora__", "", "").e.prompt == "a", "at the end");
    check(run("a __LoRA__ b", "", "w").e.prompt == "a w b", "any case");
    check(run("a", "x, __lora__", "w").e.negative == "x, __lora__", "the negative prompt leaves __lora__ as typed");
    check(run("__lora__", "", "w").unknown.empty(), "__lora__ is never unknown");
    check(run("__blank__, x").e.prompt == "x", "an empty entry is tidied");
    check(run("keep,  , this").e.prompt == "keep,  , this", "nothing emptied: no tidying");
}

void test_negative() {
    Out o = run("__single__", "typed");
    check(o.e.prompt == "only" && o.e.negative == "ugly, typed", "one __name__: the entry's negative, then the typed one");
    o = run("  __single__  ", "");
    check(o.e.negative == "ugly", "spaces around, no typed negative");
    o = run("__single__ extra", "typed");
    check(o.e.negative == "typed", "not exactly one name: no entry negative");
    o = run("__bad__", "x");
    check(o.e.negative == "x", "an entry without negative");
    o = run("__lora__", "x", "w");
    check(o.e.prompt == "w" && o.e.negative == "x", "__lora__ alone is not a collection");
    o = run("__single__", "{p|p}, __single__");
    check(o.e.negative == "ugly, p, only", "the typed negative expands too (a name gives its prompt)");
}

void test_seed() {
    const std::string t = "{a|b|c|d} __people__ {x|y|z}";
    const PromptBookMap bm = books();
    std::string         first, second, third;
    for (int run_no = 0; run_no < 3; run_no++) {
        PromptRng      rng(42);
        PromptExpander x(bm, "", rng);
        std::string    all;
        for (int i = 0; i < 20; i++) {
            all += x.expand(t, "{n|m}").prompt + "|";
        }
        (run_no == 0 ? first : run_no == 1 ? second : third) = all;
    }
    check(first == second && second == third, "the same seed gives the same items");
    PromptRng      rng(43);
    PromptExpander x(bm, "", rng);
    std::string    other;
    for (int i = 0; i < 20; i++) {
        other += x.expand(t, "{n|m}").prompt + "|";
    }
    check(other != first, "another seed gives other items");
    // a sequence of items differs from one another
    PromptRng      r2(5);
    PromptExpander y(bm, "", r2);
    std::set<std::string> s;
    for (int i = 0; i < 30; i++) {
        s.insert(y.expand("__people__", "").prompt);
    }
    check(s.size() > 1, "each item is picked anew");
}

LoraTriggers trig(std::initializer_list<std::pair<const char *, bool>> items, const char * mode = "all", int count = 2) {
    LoraTriggers t;
    for (const auto & i : items) {
        t.items.push_back({ i.first, i.second });
    }
    t.mode  = mode;
    t.count = count;
    return t;
}

// What the "stored" LoRAs say, by file with its extension.
LoraTriggers stored(const std::string & f) {
    if (f == "a.safetensors") return trig({ { "one", true } });
    if (f == "sub/b.safetensors") return trig({ { "two", true } });
    if (f == "c.safetensors") return LoraTriggers();
    if (f == "tag.safetensors") return trig({ { "tagged", true } });
    return trig({ { "unexpected", true } });
}

void test_collect() {
    LoraSource  src;
    std::string error;
    nlohmann::json field = nlohmann::json::parse(R"([{"path": "a.safetensors", "multiplier": 0.8}, "sub/b.safetensors", {"path": "c.safetensors"},
        {"path": "off.safetensors", "multiplier": 0}, {"path": "a.safetensors"}])");
    check(collect_lora_job(field, true, "p", stored, src, error) && src.job.size() == 3 && src.job[0].key == "a" &&
              src.job[1].key == "sub/b" && src.job[2].key == "c",
          "the lora field: 0 is off, once each");
    check(collect_lora_job(nlohmann::json::array(), true, "<lora:tag:1>", stored, src, error) && src.job.empty(), "lora: [] means none, tags do not count");
    check(collect_lora_job(nullptr, false, "x <lora:tag:0.7> y <lora:tag:1> <lora:gone:0>", stored, src, error) && src.job.size() == 1 &&
              src.job[0].key == "tag",
          "tags without a lora field");
    check(collect_lora_job(nullptr, false, "no tags", stored, src, error) && src.job.empty(), "no tags, no LoRAs");
    check(!collect_lora_job(nlohmann::json("x"), true, "", stored, src, error), "lora that is not a list");
    check(!collect_lora_job(nlohmann::json::parse("[1]"), true, "", stored, src, error), "a lora entry that is a number");
    check(!collect_lora_job(nlohmann::json::parse(R"([{"path": "../x.safetensors"}])"), true, "", stored, src, error), "a path with ..");
    check(!collect_lora_job(nlohmann::json::parse(R"(["/etc/x"])"), true, "", stored, src, error), "an absolute path");
    check(!collect_lora_job(nlohmann::json::parse(R"([{"path": "a", "multiplier": "1"}])"), true, "", stored, src, error), "a multiplier that is a string");
    check(collect_lora_job(nullptr, false, "<lora:../x:1>", stored, src, error) && src.job.empty(), "a tag with .. is skipped");
    // overrides
    LoraSource ov;
    check(collect_lora_job(nlohmann::json::parse(R"([{"path": "a.safetensors", "triggers": ["x", {"text": "y", "on": false}, "X"],
        "trigger_mode": "some", "trigger_count": 3}])"), true, "", stored, ov, error) && ov.overrides.size() == 1,
          "an override is read");
    const LoraOverride & o = ov.overrides["a"];
    check(o.has_list && o.list.size() == 2 && o.list[1].text == "y" && !o.list[1].on && o.mode == "some" && o.count == 3, "its fields");
    for (const char * bad : { R"([{"path": "a", "trigger_mode": "many"}])", R"([{"path": "a", "trigger_count": 0}])",
                              R"([{"path": "a", "trigger_count": 21}])", R"([{"path": "a", "triggers": "x"}])",
                              R"([{"path": "a", "triggers": [""]}])", R"([{"path": "a", "triggers": [5]}])"}) {
        check(!collect_lora_job(nlohmann::json::parse(bad), true, "", stored, ov, error), std::string("a bad override: ") + bad);
    }
}

std::vector<LoraEntry> installed_list() {
    return { { "style/pixel-art", trig({ { "pixel art", true }, { "pixelated", true } }) },
             { "pixel-art", trig({ { "root pixel", true } }) },
             { "other/watercolor", trig({ { "watercolor", true }, { "soft", false } }) },
             { "dim", trig({ { "a", false }, { "b", false } }) },
             { "styles/ink (v2)", trig({ { "ink", true } }) } };
}

// Expands n images of t with the job's LoRAs (by key, from installed_list).
std::vector<std::string> lora_run(const std::string & t, std::initializer_list<const char *> job, int n = 1, uint64_t seed = 1,
                                  std::map<std::string, LoraOverride> ov = {}, std::vector<std::string> * unknown = nullptr) {
    LoraSource src;
    for (const char * k : job) {
        for (const auto & e : installed_list()) {
            if (e.key == k) src.job.push_back(e);
        }
    }
    src.overrides = ov;
    src.installed = installed_list;
    const PromptBookMap bm = books();
    PromptRng           rng(seed);
    PromptExpander      x(bm, src, rng);
    std::vector<std::string> out;
    for (int i = 0; i < n; i++) out.push_back(x.expand(t, "").prompt);
    if (unknown) *unknown = x.unknown();
    return out;
}

void test_triggers() {
    // all: the items that are on, in list order
    check(lora_run("__lora__", { "other/watercolor" })[0] == "watercolor", "all: an item that is off is left out");
    check(lora_run("x, __lora__", { "dim" })[0] == "x", "none on: nothing, and the comma is tidied");
    check(lora_run("__lora__", { "style/pixel-art", "other/watercolor" })[0] == "pixel art, pixelated, watercolor", "several LoRAs in order");
    // one
    std::map<std::string, LoraOverride> one, some, some9;
    one["style/pixel-art"].has_mode  = true;
    one["style/pixel-art"].mode      = "one";
    std::set<std::string> seen_one;
    const auto            ones = lora_run("__lora__", { "style/pixel-art" }, 60, 3, one);
    for (const auto & w : ones) {
        check(w == "pixel art" || w == "pixelated", "one: a single item, got " + w);
        seen_one.insert(w);
    }
    check(seen_one.size() == 2, "one: both items come up over 60 images");
    check(ones == lora_run("__lora__", { "style/pixel-art" }, 60, 3, one), "one: the same seed repeats");
    check(ones != lora_run("__lora__", { "style/pixel-art" }, 60, 4, one), "one: another seed differs");
    // some
    LoraEntry five = { "five", trig({ { "a", true }, { "b", true }, { "c", true }, { "d", true }, { "e", true } }, "some", 3) };
    {
        LoraSource src;
        src.job.push_back(five);
        const PromptBookMap bm = books();
        PromptRng           rng(9);
        PromptExpander      x(bm, src, rng);
        std::set<std::string> kinds;
        bool                  ordered = true, three = true;
        for (int i = 0; i < 80; i++) {
            const std::string w = x.expand("__lora__", "").prompt;
            kinds.insert(w);
            three = three && std::count(w.begin(), w.end(), ',') == 2;
            std::string letters;
            for (char c : w) if (std::isalpha(static_cast<unsigned char>(c))) letters += c;
            ordered = ordered && std::is_sorted(letters.begin(), letters.end());
        }
        check(three, "some: 3 different items");
        check(ordered, "some: in list order");
        check(kinds.size() > 3, "some: it varies");
        PromptRng      r1(5), r2(5);
        PromptExpander y1(bm, src, r1), y2(bm, src, r2);
        check(y1.expand("__lora__", "").prompt == y2.expand("__lora__", "").prompt, "some: the same seed repeats");
        src.job[0].triggers.count = 9;
        PromptRng      r3(5);
        PromptExpander y3(bm, src, r3);
        check(y3.expand("__lora__", "").prompt == "a, b, c, d, e", "some: count above the number of items gives all");
    }
    // __lora:NAME__
    std::vector<std::string> unknown;
    check(lora_run("__lora:style/pixel-art__", {})[0] == "pixel art, pixelated", "by path, a LoRA that is not in the job");
    check(lora_run("__lora:Pixel-Art__", {})[0] == "root pixel", "by file name, any case, the path wins when both match");
    check(lora_run("__lora:watercolor__, x", {})[0] == "watercolor, x", "by file name");
    check(lora_run("__lora:dim__ end", {})[0] == "end", "named, none on: tidied");
    check(lora_run("__lora:nope__ x", {}, 1, 1, {}, &unknown)[0] == "__lora:nope__ x" && unknown.size() == 1 && unknown[0] == "lora:nope",
          "an unknown name stays as typed and is listed");
    check(lora_run("__lora__ and __lora:style/pixel-art__", { "style/pixel-art", "other/watercolor" })[0] ==
              "watercolor and pixel art, pixelated",
          "__lora__ leaves out the named LoRA");
    check(lora_run("__lora:watercolor__ __lora:WATERCOLOR__", {})[0] == "watercolor watercolor", "twice");
    check(lora_run("__lora__, __styled__", { "style/pixel-art", "other/watercolor" })[0] == "pixel art, pixelated, watercolor look",
          "__lora__ goes last and leaves out a LoRA that a collection's entry names");
    check(lora_run("__lora:ink (v2)__ x", {})[0] == "ink x", "a name with a space and brackets (the page's rule)");
    check(lora_run("__lora:styles/ink (v2)__", {})[0] == "ink", "the same by path");
    check(lora_run("__lora: ink (v2)__", {})[0] == "__lora: ink (v2)__", "a name that starts with a space is not one");
    check(lora_run("__lora:ink (v2) __", {})[0] == "__lora:ink (v2) __", "a name that ends with a space is not one");
    // not in the negative prompt
    {
        LoraSource src;
        src.installed = installed_list;
        const PromptBookMap bm = books();
        PromptRng           rng(1);
        PromptExpander      x(bm, src, rng);
        const PromptExpansion e = x.expand("p", "__lora:watercolor__");
        check(e.negative == "__lora:watercolor__" && x.unknown().empty(), "the negative prompt keeps it as typed");
    }
    // an override of the call
    std::map<std::string, LoraOverride> ovr;
    ovr["style/pixel-art"].has_list = true;
    ovr["style/pixel-art"].list     = { { "own", true }, { "mine", false } };
    check(lora_run("__lora__", { "style/pixel-art" }, 1, 1, ovr)[0] == "own", "an override list replaces the stored one");
    check(lora_run("__lora:style/pixel-art__", {}, 1, 1, ovr)[0] == "own", "and for the named LoRA too");
}

}  // namespace

int main() {
    test_expand();
    test_lora();
    test_negative();
    test_seed();
    test_collect();
    test_triggers();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("prompt_expand: all passed\n");
    return 0;
}
