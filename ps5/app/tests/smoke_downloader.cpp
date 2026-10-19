// A smoke test of the downloader against the real Hugging Face and Civitai,
// on the PC (needs the network and a host build of mbedTLS 3.6; not part of
// ctest). Runs the routes as the web UI would: a search, a Civitai name, a
// catalog install with a cancel and a resume, the preset at the end.
//
//   cmake -S ps5/app/tests -B build/app-tests -DPS5SD_NET_SMOKE=ON -DMBEDTLS_ROOT=<install prefix>
//   cmake --build build/app-tests --target smoke_downloader && build/app-tests/smoke_downloader [catalog id]
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "downloader.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

json call(const char * method, const std::string & path, const std::string & body, int & status) {
    std::string out;
    status = ps5sd::downloader_route(method, path, body, out);
    return json::parse(out, nullptr, false);
}

json downloads() {
    int st = 0;
    return call("GET", "downloads", "", st)["downloads"];
}

}  // namespace

int main(int argc, char ** argv) {
    const std::string entry = argc > 1 ? argv[1] : "sdxs-512";
    const fs::path    root  = fs::temp_directory_path() / "ps5sd-smoke";
    fs::remove_all(root);
    fs::create_directories(root);
    ps5sd::DownloaderConfig cfg;
    cfg.models_dir  = (root / "models").string();
    cfg.presets_dir = (root / "presets").string();
    cfg.queue_file  = (root / "downloads.json").string();
    ps5sd::downloader_start(cfg);
    int st = 0;

    json r = call("POST", "hf/search", R"({"q":"sdxs","limit":5})", st);
    check(st == 200 && r["results"].is_array() && !r["results"].empty(), "hf/search answers");
    if (st == 200 && !r["results"].empty()) {
        std::printf("  first: %s, %zu file(s), %s\n", r["results"][0]["repo"].get<std::string>().c_str(),
                    r["results"][0]["files"].size(), r["results"][0]["files"].dump().substr(0, 200).c_str());
    }

    // Civitai: the name comes from its API; cancel at once and remove it.
    r = call("POST", "downloads", R"({"url":"https://civitai.com/models/4384/dreamshaper?modelVersionId=128713"})", st);
    check(st == 202 && r["downloads"].size() == 1, "Civitai URL queued (HTTP " + std::to_string(st) + ")");
    if (st == 202) {
        const std::string id = r["downloads"][0]["id"];
        std::printf("  dest %s\n", r["downloads"][0]["dest"].get<std::string>().c_str());
        call("DELETE", "downloads/" + id, "", st);
        check(st == 200, "Civitai download cancelled");
        for (int i = 0; i < 100; i++) {
            std::string out;
            if (ps5sd::downloader_route("DELETE", "downloads/" + id, "", out) == 200) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    r = call("POST", "downloads", R"({"url":"https://example.com/x.gguf"})", st);
    check(st == 400, "other hosts refused");
    r = call("POST", "downloads", R"({"url":"https://huggingface.co/a/b/resolve/main/x.gguf","dest":"../x.gguf"})", st);
    check(st == 400, "a dest outside the models directory refused");

    // The catalog entry: start, cancel at about a third, resume, finish.
    r = call("POST", "downloads", "{\"catalog\":\"" + entry + "\"}", st);
    check(st == 202 && !r["downloads"].empty(), "catalog install queued");
    if (st != 202) {
        return 1;
    }
    const std::string id = r["downloads"][0]["id"];
    const auto        t0 = std::chrono::steady_clock::now();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const json d = downloads().back();
        std::printf("  %s %llu / %llu, %.1f MB/s %s\n", d["state"].get<std::string>().c_str(),
                    (unsigned long long) d["bytes"].get<uint64_t>(), (unsigned long long) d["total"].get<uint64_t>(),
                    d["bytes_per_second"].get<double>() / 1e6, d["error"].get<std::string>().c_str());
        if (d["total"].get<uint64_t>() > 0 && d["bytes"].get<uint64_t>() * 3 > d["total"].get<uint64_t>()) {
            break;
        }
        if (d["state"] == "failed") {
            check(false, "download failed: " + d["error"].get<std::string>());
            return 1;
        }
    }
    call("DELETE", "downloads/" + id, "", st);
    check(st == 200, "cancel");
    r = call("POST", "downloads", "{\"catalog\":\"" + entry + "\"}", st);
    check(st == 409, "a resume while it is still stopping is refused (409)");
    for (int i = 0; i < 300 && st == 409; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        r = call("POST", "downloads/" + id + "/retry", "", st);
    }
    check(st == 202, "retry after the cancel");
    std::printf("  resumed at %llu bytes\n", (unsigned long long) r["downloads"][0]["bytes"].get<uint64_t>());
    check(r["downloads"][0]["bytes"].get<uint64_t>() > 0, "the resume keeps what was fetched");
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const json d = downloads().back();
        if (d["state"] == "done" || d["state"] == "failed") {
            check(d["state"] == "done", "finished: " + d["error"].get<std::string>());
            break;
        }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  %.1f s from the first request\n", s);
    r = call("GET", "catalog", "", st);
    for (const auto & e : r["entries"]) {
        if (e["id"] == entry) {
            check(e["installed"] == true, "the catalog entry is installed (files and preset)");
        }
    }
    r = call("POST", "downloads", "{\"catalog\":\"" + entry + "\"}", st);
    check(st == 200 && r["downloads"].empty(), "installing it again downloads nothing");
    r = call("POST", "downloads/clear", "", st);
    check(st == 200 && r["downloads"].empty(), "clear the finished");
    ps5sd::downloader_stop();
    std::printf(failures == 0 ? "smoke_downloader: all passed\n" : "smoke_downloader: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
