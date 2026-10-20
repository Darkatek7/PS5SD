// Host test for models.cpp: presets, checkpoint files, model.txt ids and the
// JSON the app serves.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "models.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "json.hpp"

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const char * what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void write(const fs::path & p, const std::string & text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "ps5sd-test-models";
    fs::remove_all(root);
    const fs::path models  = root / "models";
    const fs::path presets = root / "presets";
    write(models / "sdxs.gguf", std::string(100, 'x'));
    write(models / "sdxl.gguf", std::string(1000, 'x'));
    write(models / "vae" / "sdxl_vae.safetensors", std::string(10, 'x'));
    write(models / "notes.txt", "not a model");
    write(presets / "sdxl.txt", "# name: SDXL Base\n# defaults: steps=25 cfg=6 width=1024\n-m\nsdxl.gguf\n--vae\n"
                                "vae/sdxl_vae.safetensors\n--vae-tiling\n");
    write(presets / "broken.txt", "-m\nnot-there.gguf\n");

    ps5sd::ModelLibrary lib{ models.u8string(), presets.u8string() };
    const auto          list = lib.list();
    check(list.size() == 4, "two presets and two checkpoints (not the VAE, not notes.txt)");
    check(list.size() > 1 && list[0].id == "broken" && list[1].id == "sdxl", "presets first, by id");
    check(list.size() == 4 && list[2].id == "sdxl.gguf" && list[3].id == "sdxs.gguf", "then files, by id");

    ps5sd::ModelEntry e;
    std::string       error;
    check(lib.find("sdxl", e, error), "find a preset");
    check(e.kind == "preset" && e.name == "SDXL Base", "preset name from its comment");
    check(e.args.size() == 5 && e.args[1] == (models / "sdxl.gguf").lexically_normal().u8string(),
          "relative path after -m comes from the models directory");
    check(e.args.size() == 5 && e.args[3] == (models / "vae" / "sdxl_vae.safetensors").lexically_normal().u8string(),
          "relative path in a subdirectory");
    check(e.args.size() == 5 && e.args[4] == "--vae-tiling", "flags pass through");
    check(e.bytes == 1010 && e.missing.empty(), "sizes of the files it names");
    check(e.defaults.size() == 3 && e.defaults[0].first == "steps" && e.defaults[0].second == "25", "defaults");

    check(!lib.find("broken", e, error) && error.find("not-there.gguf") != std::string::npos,
          "a preset with a missing file is refused");
    check(lib.find("sdxs.gguf", e, error) && e.kind == "file" && e.args[0] == "-m", "find a checkpoint by name");
    check(lib.find((models / "sdxs.gguf").u8string(), e, error) && e.id == "sdxs.gguf",
          "an absolute path in models_dir gets the file name as id (old model.txt)");
    check(!lib.find("../sdxs.gguf", e, error), "no path tricks");
    check(!lib.find("", e, error), "empty id");
    check(!lib.find("notes.txt", e, error), "not a checkpoint");
    check(!lib.find("nope", e, error), "unknown id");

    const auto j = nlohmann::json::parse(ps5sd::models_json(lib, "sdxl", "sdxs.gguf", true));
    check(j["active"] == "sdxl" && j["loaded"] == "sdxs.gguf" && j["switching"] == true, "json header");
    check(j["models"].size() == 4 && j["models"][1]["defaults"]["cfg"] == "6", "json models");
    check(j["models"][0]["missing"].size() == 1, "json missing files");

    check(nlohmann::json::parse(ps5sd::error_json("no \"x\""))["error"] == "no \"x\"", "error json");
    check(!ps5sd::error_json("bad \xff byte").empty(), "error json with a byte that is not UTF-8");

    check(ps5sd::id_from_json(R"({"id":"sdxl"})") == "sdxl", "id from json");
    check(ps5sd::id_from_json("not json").empty() && ps5sd::id_from_json(R"({"id":3})").empty(), "bad bodies");

    // The A1111 and OpenAI model lists, and sd_model_checkpoint back to an id.
    {
        const auto sd = nlohmann::json::parse(ps5sd::sd_models_json(lib));
        const auto oa = nlohmann::json::parse(ps5sd::openai_models_json(lib));
        bool has_sdxl = false, has_broken = false;
        for (const auto & m : sd) {
            has_sdxl   = has_sdxl || m["title"] == "sdxl";
            has_broken = has_broken || m["title"] == "broken";
        }
        check(has_sdxl && !has_broken, "sd-models lists what loads");
        check(oa["object"] == "list" && oa["data"].size() == sd.size(), "/v1/models lists the same");
        ps5sd::ModelEntry sdxl;
        check(lib.find("sdxl", sdxl, error), "find sdxl");
        check(ps5sd::checkpoint_id(lib, "sdxl") == "sdxl" && ps5sd::checkpoint_id(lib, "sdxl [8888888888]") == "sdxl" &&
                  ps5sd::checkpoint_id(lib, sdxl.name) == "sdxl",
              "checkpoint by id, title or name");
        check(ps5sd::checkpoint_id(lib, "nope").empty() && ps5sd::checkpoint_id(lib, "").empty(), "unknown checkpoint");
        check(nlohmann::json::parse(ps5sd::options_json("sdxl"))["sd_model_checkpoint"] == "sdxl", "options json");
        const auto info = nlohmann::json::parse(ps5sd::info_json("0.1.0", "master-1", "abc"));
        check(info["version"] == "0.1.0" && info["sd_cpp"]["commit"] == "abc", "info json");
        check(ps5sd::checkpoint_from_options(R"({"sd_model_checkpoint": "sdxl", "CLIP_stop_at_last_layers": 2})") ==
                      "sdxl" &&
                  ps5sd::checkpoint_from_options(R"({"CLIP_stop_at_last_layers": 2})").empty() &&
                  ps5sd::checkpoint_from_options("not json").empty(),
              "checkpoint from an options body");
    }

    // remove(): a second preset shares the VAE; a lone checkpoint; a broken preset.
    write(presets / "sdxl-fast.txt", "-m\nsdxl.gguf\n--vae\nvae/sdxl_vae.safetensors\n--cache-mode\nucache\n");
    write(models / "lone.gguf", "x");
    std::vector<std::string> deleted, kept;
    check(!lib.remove("sdxl.gguf", {}, deleted, kept, error) && error.find("preset") != std::string::npos,
          "a checkpoint a preset names is refused");
    check(!lib.remove("lone.gguf", { (models / "lone.gguf").u8string() }, deleted, kept, error),
          "the model the server runs is refused");
    check(!lib.remove("../x", {}, deleted, kept, error) && !lib.remove("nope", {}, deleted, kept, error),
          "bad and unknown ids");
    check(lib.remove("lone.gguf", {}, deleted, kept, error) && !fs::exists(models / "lone.gguf") && deleted.size() == 1,
          "a lone checkpoint is deleted");
    deleted.clear();
    kept.clear();
    check(lib.remove("sdxl", {}, deleted, kept, error), "delete a preset");
    check(!fs::exists(presets / "sdxl.txt") && fs::exists(models / "sdxl.gguf") && kept.size() == 2,
          "files another preset names stay");
    deleted.clear();
    kept.clear();
    check(lib.remove("sdxl-fast", { (models / "vae" / "sdxl_vae.safetensors").u8string() }, deleted, kept, error),
          "delete the last preset");
    check(!fs::exists(models / "sdxl.gguf") && fs::exists(models / "vae" / "sdxl_vae.safetensors") && kept.size() == 1 &&
              deleted.size() == 2,
          "its own files go, the server's stay");
    deleted.clear();
    kept.clear();
    check(lib.remove("broken", {}, deleted, kept, error) && !fs::exists(presets / "broken.txt"),
          "a preset with missing files can be deleted");
    const auto r = nlohmann::json::parse(ps5sd::removed_json(lib, { "a" }, { "b" }, "x", "y"));
    check(r["deleted"][0] == "a" && r["kept"][0] == "b" && r["active"] == "x", "removed json");
    {
        const fs::path loras = root / "loras";
        fs::create_directories(loras / "styles");
        std::ofstream(loras / "detail.safetensors") << "12345";
        std::ofstream(loras / "styles" / "ink.gguf") << "1";
        std::ofstream(loras / "detail.safetensors.part") << "x";
        std::ofstream(loras / "notes.txt") << "x";
        const auto l = nlohmann::json::parse(ps5sd::loras_json(loras.u8string()))["loras"];
        check(l.size() == 2 && l[0]["path"] == "detail.safetensors" && l[0]["name"] == "detail" && l[0]["bytes"] == 5 &&
                  l[1]["path"] == "styles/ink.gguf",
              "loras json: model files only, with subfolders and sizes");
        check(nlohmann::json::parse(ps5sd::loras_json((root / "none").u8string()))["loras"].empty(),
              "loras json: no folder, no LoRAs");
    }

    fs::remove_all(root);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
