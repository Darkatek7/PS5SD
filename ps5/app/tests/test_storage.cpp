// Host test for storage.hpp, usb_copy.hpp and pwa.hpp: the space rule, drive
// ids, mount detection, folder sizes, the copy (names, skip rule, parameters),
// the settings field and the manifest.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "models.hpp"
#include "pwa.hpp"
#include "storage.hpp"
#include "usb_copy.hpp"

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

void write(const fs::path & p, const std::string & text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path & p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

constexpr uint64_t GB = 1000ull * 1000 * 1000;

void test_space() {
    check(space_shortfall(0, 0).empty(), "size not known: fits");
    check(space_shortfall(GB, 3 * GB).empty(), "fits");
    check(space_shortfall(GB, GB + kSpaceMargin).empty(), "exactly with the margin fits");
    check(!space_shortfall(GB, GB + kSpaceMargin - 1).empty(), "one byte short of the margin does not");
    check(space_shortfall(7300000000ull, 5100000000ull) ==
              "Not enough space on the console: it needs 7.3 GB, 5.1 GB are free.",
          "the text: " + space_shortfall(7300000000ull, 5100000000ull));
    check(space_shortfall(100, 0).find("0.0 GB are free") != std::string::npos, "no room at all");
}

void test_ids_and_mounts() {
    check(usb_id_ok("usb0") && usb_id_ok("usb7"), "usb0 and usb7");
    check(!usb_id_ok("usb8") && !usb_id_ok("usb") && !usb_id_ok("usb00") && !usb_id_ok("USB0") && !usb_id_ok("") &&
              !usb_id_ok("../usb0") && !usb_id_ok("usb0/"),
          "other ids");
    check(is_mount_point(6, 5), "another device");
    check(!is_mount_point(5, 5), "an empty mount point");

    const fs::path root = fs::temp_directory_path() / "ps5sd-test-mnt";
    fs::remove_all(root);
    for (int i = 0; i < 8; i++) {
        fs::create_directories(root / ("usb" + std::to_string(i)));
    }
    check(usb_drives(root.string()).empty(), "empty mount points are no drives");
    check(!usb_mounted(root.string(), "usb9") && !usb_mounted((root / "none").string(), "usb0"), "no such drive");
    fs::remove_all(root);
}

void test_sizes_and_json() {
    const fs::path dir = fs::temp_directory_path() / "ps5sd-test-size";
    fs::remove_all(dir);
    write(dir / "a.png", std::string(100, 'a'));
    write(dir / ".thumbs" / "a.png.jpg", std::string(20, 'b'));
    check(dir_bytes(dir.string()) == 120, "hidden folders count");
    check(dir_bytes((dir / "nope").string()) == 0, "no folder: 0");

    SizeCache cache(30);
    const auto t0 = std::chrono::steady_clock::now();
    int        calls = 0;
    auto       f     = [&] { return static_cast<uint64_t>(++calls); };
    check(cache.get("k", t0, f) == 1 && cache.get("k", t0 + std::chrono::seconds(29), f) == 1 && calls == 1,
          "kept for 30 s");
    check(cache.get("k", t0 + std::chrono::seconds(31), f) == 2, "computed again after 30 s");
    check(cache.get("other", t0, f) == 3, "keys apart");

    StorageInfo s;
    s.console_free  = 4;
    s.models        = 1;
    s.gallery       = 3;
    s.usb.push_back({ "usb0", "/mnt/usb0", 50 });
    s.usb.push_back({ "usb1", "/mnt/usb1", -1 });
    const auto j = nlohmann::json::parse(storage_json(s));
    check(j["console"]["path"] == "/data" && j["console"]["free"] == 4 && !j["console"].contains("total"), "console");
    check(j["used"]["models"] == 1 && j["used"]["loras"] == 0 && j["used"]["gallery"] == 3 && j["used"]["upscalers"] == 0,
          "used");
    check(j["usb"].size() == 2 && j["usb"][0]["id"] == "usb0" && j["usb"][0]["path"] == "/mnt/usb0" &&
              j["usb"][0]["free"] == 50 && j["usb"][1]["free"].is_null(),
          "usb; a free space not known is null");
    check(nlohmann::json::parse(storage_json(StorageInfo{}))["console"]["free"].is_null(), "console free not known: null");
    fs::remove_all(dir);
}

void test_request() {
    std::string              to, error;
    std::vector<std::string> names;
    check(parse_copy_request(R"({"to": "usb1"})", to, names, error) && to == "usb1" && names.empty(), "the whole gallery");
    check(parse_copy_request(R"({"to": "usb1", "names": []})", to, names, error) && names.empty(), "empty list: all");
    check(parse_copy_request(R"({"to": "usb2", "names": ["sd_1.png", "sd_2.webm", "sd_1.png"]})", to, names, error) &&
              names.size() == 2,
          "names, once each");
    check(!parse_copy_request(R"({"to": "usb8"})", to, names, error), "usb8");
    check(!parse_copy_request(R"({"to": "/mnt/usb0"})", to, names, error), "a path");
    check(!parse_copy_request(R"({"names": []})", to, names, error), "no drive");
    check(!parse_copy_request("[]", to, names, error) && !parse_copy_request("", to, names, error), "not an object");
    for (const char * bad : { "../sd_1.png", "a/b.png", "a\\\\b.png", "..", ".hidden.png", "sd_1.txt", "tmp_sd_1.png", "sd_1.png\\u0000" }) {
        const std::string body = std::string(R"({"to": "usb0", "names": [")") + bad + "\"]}";
        check(!parse_copy_request(body, to, names, error), std::string("refused: ") + bad);
    }
    check(!parse_copy_request(R"({"to": "usb0", "names": [1]})", to, names, error), "a number as a name");
    check(!parse_copy_request(R"({"to": "usb0", "names": "sd_1.png"})", to, names, error), "names not a list");
    check(copy_is_there(5, true, 5) && !copy_is_there(5, true, 4) && !copy_is_there(5, false, 0), "skip rule");
}

void test_copy() {
    const fs::path base = fs::temp_directory_path() / "ps5sd-test-copy";
    fs::remove_all(base);
    const fs::path out = base / "outputs", root = base / "mnt";
    write(out / "sd_1.png", "one");
    write(out / "sd_2.webm", "video!");
    write(out / ".thumbs" / "sd_2.webm.txt", "prompt");
    write(out / "sd_3.png", "three");
    write(out / "tmp_sd_4.png", "temporary");
    write(out / ".thumbs" / "sd_1.png.jpg", "thumb");
    fs::create_directories(root / "usb0");

    check(gallery_files(out.string()) == (std::vector<std::string>{ "sd_1.png", "sd_2.webm", "sd_3.png" }),
          "the gallery lists no tmp_ and no hidden files");

    std::atomic<bool> stop{ false };
    LocalSink         sink;
    uint64_t          bytes = 0;
    std::string       error;
    const std::string dest = (root / "usb0" / "PS5SD").string();
    check(copy_gallery_file(sink, out.string(), dest, "sd_1.png", stop, bytes, error) == CopyResult::Copied &&
              read(dest + "/sd_1.png") == "one" && bytes == 3,
          "copied: " + error);
    check(!fs::exists(dest + "/.thumbs"), "no parameters, no .thumbs");
    check(copy_gallery_file(sink, out.string(), dest, "sd_1.png", stop, bytes, error) == CopyResult::Skipped && bytes == 3,
          "same size: skipped");
    write(dest + "/sd_1.png", "x");
    check(copy_gallery_file(sink, out.string(), dest, "sd_1.png", stop, bytes, error) == CopyResult::Copied &&
              read(dest + "/sd_1.png") == "one",
          "another size: copied again");
    check(copy_gallery_file(sink, out.string(), dest, "sd_2.webm", stop, bytes, error) == CopyResult::Copied &&
              read(dest + "/sd_2.webm") == "video!" && read(dest + "/.thumbs/sd_2.webm.txt") == "prompt",
          "the parameters go along");
    check(!fs::exists(dest + "/sd_2.webm.part"), "no .part left");
    check(copy_gallery_file(sink, out.string(), dest, "sd_9.png", stop, bytes, error) == CopyResult::Missing, "a file that is gone");
    stop = true;
    write(dest + "/sd_3.png", "x");
    check(copy_gallery_file(sink, out.string(), dest, "sd_3.png", stop, bytes, error) == CopyResult::Failed &&
              error == "stopped",
          "stop");

    // the worker: a manual copy of everything, then a background one
    auto only_usb0 = [](const std::string & id) { return id == "usb0"; };
    GalleryCopier copier(out.string(), root.string(), [](std::string &) { return std::make_unique<LocalSink>(); }, only_usb0);
    check(nlohmann::json::parse(copier.status_json())["state"] == "idle", "idle at first");
    bool busy = false;
    check(copier.start("usb0", {}, busy), "started");
    auto wait_for = [&](const char * state) {
        for (int i = 0; i < 500; i++) {
            if (nlohmann::json::parse(copier.status_json())["state"] == state) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    check(wait_for("done"), "done: " + copier.status_json());
    auto st = nlohmann::json::parse(copier.status_json());
    check(st["to"] == "usb0" && st["total"] == 3 && st["done"] == 3 && st["error"] == "", "counts: " + copier.status_json());
    check(st["skipped"] == 2 && st["bytes"] == 5, "sd_1.png and sd_2.webm were there: " + copier.status_json());
    check(read(dest + "/sd_3.png") == "three" && !fs::exists(dest + "/tmp_sd_4.png"), "gallery copied, tmp_ not");

    write(out / "sd_5.png", "five");
    copier.enqueue("usb0", "sd_5.png");
    for (int i = 0; i < 500 && !fs::exists(dest + "/sd_5.png"); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(read(dest + "/sd_5.png") == "five", "a background copy");
    copier.enqueue("usb3", "sd_5.png");  // an empty mount point: skipped with a log line
    copier.stop_and_join();
    check(!copier.start("usb0", {}, busy), "no start once stopped");

    // a drive that is not there: failed
    GalleryCopier c2(out.string(), (base / "nothing").string(), [](std::string &) { return std::make_unique<LocalSink>(); },
                    [](const std::string &) { return false; });
    check(c2.start("usb0", { "sd_1.png" }, busy), "started (2)");
    for (int i = 0; i < 500 && nlohmann::json::parse(c2.status_json())["state"] == "copying"; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    st = nlohmann::json::parse(c2.status_json());
    check(st["state"] == "failed" && !st["error"].get<std::string>().empty(), "failed: " + c2.status_json());
    c2.stop_and_join();
    fs::remove_all(base);
}

void test_settings_and_manifest() {
    AppSettings s;
    std::string error;
    check(s.usb_copy.empty() && nlohmann::json::parse(settings_json(s))["usb_copy"] == "", "off by default");
    check(apply_settings(R"({"usb_copy": "usb2"})", s, error) && s.usb_copy == "usb2", "set");
    check(nlohmann::json::parse(settings_json(s))["usb_copy"] == "usb2", "in the json");
    check(apply_settings(R"({"tv_images": false})", s, error) && s.usb_copy == "usb2", "kept");
    check(!apply_settings(R"({"usb_copy": "usb9"})", s, error) && s.usb_copy == "usb2", "usb9 refused: " + error);
    check(!apply_settings(R"({"usb_copy": true})", s, error) && !apply_settings(R"({"usb_copy": "/mnt/usb0"})", s, error),
          "wrong type, a path");
    check(apply_settings(R"({"usb_copy": ""})", s, error) && s.usb_copy.empty(), "off");
    s.usb_copy = "usb1";
    check(apply_settings(R"({"usb_copy": null})", s, error) && s.usb_copy.empty(), "null is off");

    std::string png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\x02\0\0\0\x02\0\x08\x06", 26);
    uint32_t    w = 0, h = 0;
    check(png_dimensions(png, w, h) && w == 512 && h == 512, "PNG size");
    check(!png_dimensions("not a png at all, really not", w, h) && !png_dimensions("", w, h), "not a PNG");
    const auto m = nlohmann::json::parse(manifest_json("512x512"));
    check(m["name"] == "PS5SD" && m["short_name"] == "PS5SD" && m["start_url"] == "/" && m["display"] == "standalone" &&
              m["background_color"] == "#f2f2f7" && m["theme_color"] == "#007aff",
          "manifest fields");
    check(m["icons"].size() == 1 && m["icons"][0]["src"] == "/ps5sd/v1/app/icon.png" &&
              m["icons"][0]["sizes"] == "512x512" && m["icons"][0]["type"] == "image/png",
          "manifest icon");
    check(!nlohmann::json::parse(manifest_json(""))["icons"][0].contains("sizes"), "no size when not known");
}

}  // namespace

int main() {
    test_space();
    test_ids_and_mounts();
    test_sizes_and_json();
    test_request();
    test_copy();
    test_settings_and_manifest();
    if (failures == 0) {
        std::printf("test_storage: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
