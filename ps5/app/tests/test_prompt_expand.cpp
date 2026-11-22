// Host test for prompt_expand.hpp: {a|b}, __name__, __lora__, the negative-prompt rule, the lora list, seeds.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_expand.hpp"

#include <cstdio>
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

void test_collect() {
    const auto words = [](const std::string & f) {
        if (f == "a.safetensors") return std::string(" one, ");
        if (f == "sub/b.safetensors") return std::string("two");
        if (f == "c.safetensors") return std::string("");
        if (f == "tag.safetensors") return std::string("tagged");
        return std::string("unexpected:" + f);
    };
    std::string w, error;
    nlohmann::json field = nlohmann::json::parse(R"([{"path": "a.safetensors", "multiplier": 0.8}, "sub/b.safetensors", {"path": "c.safetensors"},
        {"path": "off.safetensors", "multiplier": 0}, {"path": "a.safetensors"}])");
    check(collect_lora_words(field, true, "p", words, w, error) && w == "one, two", "the lora field: words joined, 0 is off, no words adds nothing, once each");
    check(collect_lora_words(nlohmann::json::array(), true, "<lora:tag:1>", words, w, error) && w.empty(), "lora: [] means none, tags do not count");
    check(collect_lora_words(nullptr, false, "x <lora:tag:0.7> y <lora:tag:1> <lora:gone:0>", words, w, error) && w == "tagged",
          "tags without a lora field");
    check(collect_lora_words(nullptr, false, "no tags", words, w, error) && w.empty(), "no tags, no words");
    check(!collect_lora_words(nlohmann::json("x"), true, "", words, w, error), "lora that is not a list");
    check(!collect_lora_words(nlohmann::json::parse("[1]"), true, "", words, w, error), "a lora entry that is a number");
    check(!collect_lora_words(nlohmann::json::parse(R"([{"path": "../x.safetensors"}])"), true, "", words, w, error), "a path with ..");
    check(!collect_lora_words(nlohmann::json::parse(R"(["/etc/x"])"), true, "", words, w, error), "an absolute path");
    check(!collect_lora_words(nlohmann::json::parse(R"([{"path": "a", "multiplier": "1"}])"), true, "", words, w, error), "a multiplier that is a string");
    check(collect_lora_words(nullptr, false, "<lora:../x:1>", words, w, error) && w.empty(), "a tag with .. is skipped");
}

}  // namespace

int main() {
    test_expand();
    test_lora();
    test_negative();
    test_seed();
    test_collect();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("prompt_expand: all passed\n");
    return 0;
}
