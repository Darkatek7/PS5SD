// Host test for prompt_tags.cpp: the lines of tagcomplete's CSV and of a plain
// list, the prompt form of a tag, matching on the start of a tag or an alias,
// the most used first, and a folder that changes.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_tags.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "json.hpp"

using namespace ps5sd;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void test_lines() {
    std::string              name;
    uint64_t                 count = 0;
    std::vector<std::string> aliases;
    check(TagIndex::parse_line("long_hair,0,4350000,\"longhair,long_hairs\"\r", name, count, aliases) && name == "long_hair" &&
              count == 4350000 && aliases.size() == 2 && aliases[0] == "longhair",
          "tagcomplete's CSV with aliases and a CR");
    check(TagIndex::parse_line("1girl,0,6000000,", name, count, aliases) && name == "1girl" && aliases.empty(),
          "no aliases");
    check(TagIndex::parse_line("scenery,1200", name, count, aliases) && name == "scenery" && count == 1200, "name,count");
    check(TagIndex::parse_line("night", name, count, aliases) && name == "night" && count == 0, "a tag alone");
    check(!TagIndex::parse_line("name,type,count,aliases", name, count, aliases), "the header");
    check(!TagIndex::parse_line("# my tags", name, count, aliases), "a comment");
    check(!TagIndex::parse_line("", name, count, aliases), "an empty line");
}

void test_find(const fs::path & dir) {
    fs::create_directories(dir);
    std::ofstream(dir / "danbooru.csv") << "name,type,count,aliases\n"
                                           "long_hair,0,4350000,\"longhair\"\n"
                                           "looking_at_viewer,0,3000000,\n"
                                           "lowres,5,90000,\n"
                                           "gothic_(style),0,500,\n"
                                           "night_sky,0,200000,\n";
    std::ofstream(dir / "mine.txt") << "lonely house\nlong_hair\n";
    std::ofstream(dir / "notes.md") << "long_note\n";
    TagIndex tags(dir.u8string());

    std::vector<TagHit> hits = tags.find("lo", 10);
    check(hits.size() == 4, "four tags start with lo (not notes.md)");
    check(hits.size() == 4 && hits[0].tag == "long hair" && hits[1].tag == "looking at viewer" && hits[2].tag == "lowres",
          "the most used first, underscores as spaces");
    check(hits.size() == 4 && hits[3].tag == "lonely house", "a tag of a plain list");
    int long_hair = 0;
    for (const TagHit & h : hits) {
        long_hair += h.tag == "long hair";
    }
    check(long_hair == 1, "a tag in two files counts once");

    check(tags.find("long h", 10).size() == 1 && tags.find("long_h", 10).size() == 1, "spaces and underscores are the same");
    check(tags.find("LONG H", 10).size() == 1, "any case");
    hits = tags.find("longha", 10);
    check(hits.size() == 1 && hits[0].tag == "long hair" && hits[0].alias == "longhair", "an alias finds its tag");
    hits = tags.find("goth", 10);
    check(hits.size() == 1 && hits[0].tag == "gothic \\(style\\)", "parentheses escaped for the prompt");
    check(tags.find("l", 10).empty(), "at least two characters");
    check(tags.find("lo", 2).size() == 2, "the limit");

    auto info = nlohmann::json::parse(tags.info_json());
    check(info["files"].size() == 2 && info["tags"] == 6, "info: two files, six tags");
    auto j = nlohmann::json::parse(TagIndex::hits_json(tags.find("night", 5)));
    check(j["tags"].size() == 1 && j["tags"][0]["tag"] == "night sky" && j["tags"][0]["count"] == 200000, "hits json");

    TagIndex none((dir / "none").u8string());
    check(none.find("lo", 10).empty() && nlohmann::json::parse(none.info_json())["tags"] == 0, "no folder, no tags");
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ps5sd-test-prompt-tags";
    fs::remove_all(dir);
    test_lines();
    test_find(dir);
    fs::remove_all(dir);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("prompt_tags: all passed\n");
    return 0;
}
