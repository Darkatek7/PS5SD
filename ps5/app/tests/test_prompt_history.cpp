// Host test for prompt_history.hpp: the list, its limit of 50, repeats, what is refused, removing,
// and a broken file.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_history.hpp"

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

nlohmann::json list_of(const PromptHistory & h) {
    return nlohmann::json::parse(h.list_json())["prompts"];
}

void test_history(const fs::path & dir) {
    fs::create_directories(dir);
    PromptHistory h((dir / "prompts.json").u8string());
    check(list_of(h).is_array() && list_of(h).empty(), "no file: an empty list");

    nlohmann::json e;
    std::string    error;
    check(h.add("  a red fox  ", "blurry", "SDXL Base", e, error, 100), "add: " + error);
    check(e["prompt"] == "a red fox" && e["negative"] == "blurry" && e["model"] == "SDXL Base" && e["time"] == 100 && e["id"] == 1,
          "trimmed, with an id and the time");
    check(h.add("a blue fox", "", "", e, error, 101) && e["id"] == 2, "a second one");
    nlohmann::json l = list_of(h);
    check(l.size() == 2 && l[0]["prompt"] == "a blue fox" && l[1]["prompt"] == "a red fox", "the newest first");

    // the newest again: nothing changes; an older one moves to the front and keeps its id
    check(h.add("a blue fox", "", "", e, error, 150) && e["id"] == 2 && e["time"] == 101, "the newest again changes nothing");
    check(list_of(h).size() == 2, "still two");
    check(h.add("a red fox", "blurry", "SDXL Base", e, error, 160) && e["id"] == 1 && e["time"] == 160, "an older one moves up");
    l = list_of(h);
    check(l.size() == 2 && l[0]["prompt"] == "a red fox" && l[1]["prompt"] == "a blue fox", "the order after the move");
    // the same prompt with another negative prompt or model is another entry
    check(h.add("a red fox", "", "SDXL Base", e, error, 170) && list_of(h).size() == 3, "another negative prompt is another entry");
    check(h.add("a red fox", "", "Z-Image", e, error, 171) && list_of(h).size() == 4, "another model is another entry");

    check(!h.add("   \n", "x", "m", e, error) && error == "the prompt is empty", "a prompt of spaces");
    check(!h.add(std::string(8001, 'a'), "", "", e, error), "a prompt over 8000 bytes");
    check(!h.add("ok", std::string(8001, 'a'), "", e, error), "a negative prompt over 8000 bytes");
    check(!h.add("ok", "", std::string(201, 'a'), e, error), "a model name over 200 bytes");
    check(!h.add("ok", "", "two\nlines", e, error), "a model name of one line");
    check(h.add(std::string(8000, 'a'), "", "", e, error), "a prompt of exactly 8000 bytes: " + error);
    check(h.add("line one\nline two \xc3\xa4", "", "", e, error) && list_of(h)[0]["prompt"] == "line one\nline two \xc3\xa4",
          "line breaks and UTF-8 stay");

    // it survives a new object over the same file
    PromptHistory again((dir / "prompts.json").u8string());
    check(list_of(again).size() == list_of(h).size() && list_of(again)[0]["prompt"] == list_of(h)[0]["prompt"], "kept in the file");
    check(!fs::exists(dir / "prompts.json.tmp"), "no temporary file is left");

    check(h.remove(1) && !h.remove(1), "remove: once");
    bool gone = true;
    for (const auto & x : list_of(h)) {
        gone = gone && x["id"] != 1;
    }
    check(gone, "the entry is gone");
    check(!h.remove(9999), "an id that is not there");
    const size_t n = list_of(h).size();
    check(h.clear() == n && list_of(h).empty() && h.clear() == 0, "clear");
}

void test_limit(const fs::path & dir) {
    fs::create_directories(dir);
    PromptHistory h((dir / "prompts.json").u8string());
    nlohmann::json e;
    std::string    error;
    for (int i = 0; i < 60; i++) {
        check(h.add("prompt " + std::to_string(i), "", "", e, error), "add " + std::to_string(i));
    }
    const nlohmann::json l = list_of(h);
    check(l.size() == 50, "at most 50 entries");
    if (l.size() != 50) {
        return;
    }
    check(l[0]["prompt"] == "prompt 59" && l[49]["prompt"] == "prompt 10", "the 50 newest stay");
    // ids go up, also when the oldest ones were dropped
    check(l[0]["id"].get<int>() == 60, "ids count on");
}

void test_broken(const fs::path & dir) {
    fs::create_directories(dir);
    std::ofstream(dir / "prompts.json") << "{not json";
    PromptHistory h((dir / "prompts.json").u8string());
    check(list_of(h).empty(), "a broken file reads as none");
    nlohmann::json e;
    std::string    error;
    check(h.add("fresh", "", "", e, error) && list_of(h).size() == 1, "the next add writes a good file");

    // entries that are not whole are left out; extra fields are dropped
    std::ofstream(dir / "prompts.json") << R"({"prompts": [{"id": 3, "prompt": "ok", "x": 1}, {"id": "a", "prompt": "bad id"},
        {"prompt": "no id"}, 7, {"id": 4, "prompt": 5}, {"id": 0, "prompt": "zero"}]})";
    const nlohmann::json l = list_of(h);
    check(l.size() == 1 && l[0]["prompt"] == "ok" && l[0]["negative"] == "" && !l[0].contains("x"), "only whole entries are read");
    check(h.add("next", "", "", e, error) && e["id"] == 4, "the next id follows the highest");

    // a directory that is not there: the write fails with a message, nothing throws
    PromptHistory nowhere((dir / "none" / "deeper" / "prompts.json").u8string());
    check(!nowhere.add("x", "", "", e, error) && error.rfind("could not write", 0) == 0, "an unwritable path");
    check(list_of(nowhere).empty(), "and reads as none");
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ps5sd-test-prompt-history";
    fs::remove_all(dir);
    test_history(dir / "a");
    test_limit(dir / "b");
    test_broken(dir / "c");
    fs::remove_all(dir);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("prompt_history: all passed\n");
    return 0;
}
