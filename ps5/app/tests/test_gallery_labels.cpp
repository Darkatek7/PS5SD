// Host test for gallery_labels.cpp: names and tags kept in .labels.json, what
// is refused, the entries of deleted files dropped, and the images that are
// not kept deleted at start.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gallery_labels.hpp"

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

void touch(const fs::path & p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << "x";
}

nlohmann::json labels_of(const GalleryLabels & g) {
    return nlohmann::json::parse(g.all_json())["labels"];
}

void test_labels(const fs::path & dir) {
    touch(dir / "sd_1.png");
    touch(dir / "sd_2.png");
    GalleryLabels g(dir.u8string());
    check(labels_of(g).empty(), "no file: no labels");

    GalleryLabel out;
    std::string  error;
    check(g.set("sd_1.png", "  Old town  ", { "night", " #houses ", "Night", "" }, out, error), "set: " + error);
    check(out.title == "Old town", "the name is trimmed");
    check(out.tags.size() == 2 && out.tags[0] == "night" && out.tags[1] == "houses", "a tag counts once, without #");
    nlohmann::json all = labels_of(g);
    check(all["sd_1.png"]["title"] == "Old town" && all["sd_1.png"]["tags"].size() == 2, "kept in the file");

    check(!g.set("sd_9.png", "x", {}, out, error) && error.rfind("no file", 0) == 0, "a file that is not there");
    check(!g.set("../sd_1.png", "x", {}, out, error), "no folders");
    check(!g.set(".labels.json", "x", {}, out, error), "not a hidden file");
    check(!g.set("sd_2.png", "two\nlines", {}, out, error), "a name of one line");
    check(!g.set("sd_2.png", std::string(101, 'a'), {}, out, error), "a name of at most 100 characters");
    check(g.set("sd_2.png", std::string(50, 'a') + "\xc3\xa4\xc3\xa4", {}, out, error), "UTF-8 counts as characters: " + error);
    check(!g.set("sd_2.png", "", { "a,b" }, out, error), "no commas in a tag");
    check(!g.set("sd_2.png", "", { std::string(41, 't') }, out, error), "a tag of at most 40 characters");
    std::vector<std::string> many;
    for (int i = 0; i < 21; i++) {
        many.push_back("t" + std::to_string(i));
    }
    check(!g.set("sd_2.png", "", many, out, error), "at most 20 tags");

    check(g.set("sd_2.png", "", {}, out, error), "clear: " + error);
    check(!labels_of(g).contains("sd_2.png"), "an empty name without tags is no entry");

    // a deleted file's entry goes with the next change, or with remove()
    fs::remove(dir / "sd_1.png");
    touch(dir / "sd_3.png");
    check(g.set("sd_3.png", "three", {}, out, error), "set three: " + error);
    all = labels_of(g);
    check(!all.contains("sd_1.png") && all.contains("sd_3.png"), "the entry of a deleted file is dropped");
    g.remove("sd_3.png");
    check(labels_of(g).empty(), "remove");

    // a broken file is no labels, and the next set writes a good one
    std::ofstream(dir / ".labels.json") << "{not json";
    check(labels_of(g).empty(), "a broken file reads as none");
    check(g.set("sd_3.png", "again", {}, out, error) && labels_of(g)["sd_3.png"]["title"] == "again", "written again");
}

void test_temporary(const fs::path & dir) {
    touch(dir / "tmp_sd_20261023_1.png");
    touch(dir / "tmp_sd_20261023_2.webm");
    touch(dir / ".thumbs" / "tmp_sd_20261023_1.png.jpg");
    touch(dir / ".thumbs" / "tmp_sd_20261023_2.webm.txt");
    touch(dir / "sd_20261023_3.png");
    touch(dir / ".thumbs" / "sd_20261023_3.png.jpg");
    check(is_temporary_name("tmp_sd_1.png") && !is_temporary_name("sd_1.png"), "tmp_ names");
    check(drop_temporary(dir.u8string()) == 2, "two images not kept");
    check(!fs::exists(dir / "tmp_sd_20261023_1.png") && !fs::exists(dir / ".thumbs" / "tmp_sd_20261023_1.png.jpg") &&
              !fs::exists(dir / ".thumbs" / "tmp_sd_20261023_2.webm.txt"),
          "with their thumbnails and parameters");
    check(fs::exists(dir / "sd_20261023_3.png") && fs::exists(dir / ".thumbs" / "sd_20261023_3.png.jpg"), "the others stay");
    check(drop_temporary((dir / "none").u8string()) == 0, "no folder");

    touch(dir / "tmp_sd_20261023_4.png");
    touch(dir / ".thumbs" / "tmp_sd_20261023_4.png.jpg");
    std::string kept, error;
    check(keep_temporary(dir.u8string(), "tmp_sd_20261023_4.png", kept, error) && kept == "sd_20261023_4.png", "keep: " + error);
    check(fs::exists(dir / "sd_20261023_4.png") && fs::exists(dir / ".thumbs" / "sd_20261023_4.png.jpg") &&
              !fs::exists(dir / "tmp_sd_20261023_4.png"),
          "kept with its thumbnail");
    check(!keep_temporary(dir.u8string(), "sd_20261023_3.png", kept, error), "only tmp_ files");
    touch(dir / "tmp_sd_20261023_3.png");
    check(!keep_temporary(dir.u8string(), "tmp_sd_20261023_3.png", kept, error) && fs::exists(dir / "tmp_sd_20261023_3.png"),
          "not over a file that is there");
    check(!keep_temporary(dir.u8string(), "tmp_../x.png", kept, error), "no folders");
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ps5sd-test-gallery-labels";
    fs::remove_all(dir);
    test_labels(dir / "a");
    test_temporary(dir / "b");
    fs::remove_all(dir);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gallery_labels: all passed\n");
    return 0;
}
