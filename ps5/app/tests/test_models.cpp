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

    // The app's settings.
    {
        ps5sd::AppSettings settings;
        check(nlohmann::json::parse(ps5sd::settings_json(settings))["tv_images"] == true, "settings default");
        check(ps5sd::apply_settings(R"({"tv_images": false})", settings, error) && !settings.tv_images, "tv off");
        check(ps5sd::apply_settings("{}", settings, error) && !settings.tv_images, "a missing field keeps its value");
        check(!ps5sd::apply_settings(R"({"tv_images": "no"})", settings, error) && !settings.tv_images, "wrong type");
        check(!ps5sd::apply_settings("[]", settings, error) && !ps5sd::apply_settings("", settings, error), "not an object");
        check(ps5sd::apply_settings(ps5sd::settings_json(ps5sd::AppSettings{}), settings, error) && settings.tv_images,
              "round trip");
    }

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
    // Variant presets ("# base:"), as the web UI saves them.
    {
        write(models / "ilu.safetensors", std::string(500, 'x'));
        write(models / "flux" / "dit.gguf", std::string(50, 'x'));
        write(models / "flux" / "vae.safetensors", std::string(5, 'x'));
        write(presets / "flux.txt", "# name: FLUX\n# defaults: steps=16\n--diffusion-model\nflux/dit.gguf\n--vae\n"
                                    "flux/vae.safetensors\n--vae-tiling\n");
        std::string id;
        check(lib.create_preset("Portrait, fast!", "ilu.safetensors",
                                { { "width", "832" }, { "height", "1216" }, { "sampler", "dpm++2m" },
                                  { "loras", "styles/ink.gguf:0.8,detail.safetensors:1" } },
                                id, error) &&
                  id == "portrait-fast",
              "create a variant of a checkpoint");
        ps5sd::ModelEntry v;
        check(lib.find("portrait-fast", v, error) && v.name == "Portrait, fast!" && v.base == "ilu.safetensors" &&
                  v.args.size() == 2 && v.args[1] == (models / "ilu.safetensors").lexically_normal().u8string(),
              "the variant loads the checkpoint");
        check(v.defaults.size() == 4 && v.defaults[0].second == "832" && v.defaults[3].first == "loras",
              "the variant keeps its defaults");
        check(lib.create_preset("Portrait, fast!", "ilu.safetensors", {}, id, error) && id == "portrait-fast-2",
              "a taken id gets a number");
        check(lib.create_preset("Of a variant", "portrait-fast-2", {}, id, error) && lib.find(id, v, error) &&
                  v.base == "ilu.safetensors",
              "a variant of a variant has the root as its base");
        check(lib.create_preset("Flux 512", "flux", { { "width", "512" } }, id, error) && lib.find(id, v, error) &&
                  v.base == "flux" && v.files.size() == 2 && v.args.back() == "--vae-tiling",
              "a variant of a preset copies its arguments");
        check(!lib.create_preset("", "flux", {}, id, error), "no name");
        check(!lib.create_preset("a\nb", "flux", {}, id, error), "a name of two lines");
        check(!lib.create_preset("x", "flux", { { "steps", "1 --evil" } }, id, error), "a default with a space");
        check(!lib.create_preset("x", "flux", { { "prompt", "a" } }, id, error), "an unknown default");
        check(lib.create_preset("Flux hires", "flux",
                                { { "hires_scale", "1.5" }, { "hires_steps", "0" }, { "hires_denoise", "0.45" },
                                  { "hires_upscaler", "Latent%20%28nearest-exact%29" } },
                                id, error) &&
                  lib.find(id, v, error) && v.defaults.size() == 4 && v.defaults[3].second == "Latent%20%28nearest-exact%29",
              "hires defaults, the upscaler percent-encoded");
        check(!lib.create_preset("x", "flux", { { "hires_upscaler", "Latent (nearest)" } }, id, error),
              "an upscaler name with spaces must be encoded");
        check(!lib.create_preset("x", "nope", {}, id, error), "no such base");
        const auto j = nlohmann::json::parse(ps5sd::models_json(lib, "", "", false));
        bool       seen = false;
        for (const auto & m : j["models"]) {
            seen = seen || (m["id"] == "portrait-fast" && m["base"] == "ilu.safetensors");
        }
        check(seen, "models json has the base");

        std::string text;
        check(lib.read_preset("flux-512", text, error) && text.find("# base: flux") != std::string::npos, "read a preset");
        check(lib.write_preset("flux-512", "# name: Flux small\n--diffusion-model\nflux/dit.gguf\n", error) &&
                  lib.find("flux-512", v, error) && v.name == "Flux small",
              "write a preset");
        check(!lib.write_preset("flux-512", "# name: nothing\n--vae-tiling\n", error), "a preset without a model");
        check(!lib.write_preset("nope", "-m\nilu.safetensors\n", error), "only existing presets");
        check(!lib.write_preset("../x", "-m\nilu.safetensors\n", error), "no paths");
        check(!lib.remove_variant("flux", error), "a model is not a variant");
        check(lib.remove_variant("portrait-fast-2", error) && !fs::exists(presets / "portrait-fast-2.txt") &&
                  fs::exists(models / "ilu.safetensors"),
              "deleting a variant leaves the model");

        // Deleting the checkpoint takes its variants with it (they do not keep it).
        deleted.clear();
        kept.clear();
        check(lib.remove("ilu.safetensors", {}, deleted, kept, error) && !fs::exists(models / "ilu.safetensors") &&
                  !fs::exists(presets / "portrait-fast.txt") && !fs::exists(presets / "of-a-variant.txt") &&
                  deleted.size() == 3,
              "a checkpoint goes with its variants");
        // A preset that names flux's file without being its variant keeps the file.
        deleted.clear();
        check(lib.write_preset("flux-512", "# base: flux\n--diffusion-model\nflux/dit.gguf\n", error), "rebase");
        write(presets / "other.txt", "--vae\nflux/vae.safetensors\n");
        check(lib.remove("flux", {}, deleted, kept, error) && !fs::exists(models / "flux" / "dit.gguf") &&
                  !fs::exists(presets / "flux-512.txt") && fs::exists(models / "flux" / "vae.safetensors") &&
                  kept.size() == 1,
              "a preset goes with its variants; another preset's file stays");
        fs::remove(presets / "other.txt");
        deleted.clear();
        kept.clear();
    }
    // The API key.
    {
        unsigned char bytes[32];
        for (int i = 0; i < 32; i++) {
            bytes[i] = static_cast<unsigned char>(i * 37 + 11);
        }
        const std::string key = ps5sd::api_key_from_bytes(bytes, 32);
        check(key.size() == 52 && ps5sd::valid_api_key(key), "a key is 52 base32 characters");
        const unsigned char zeros[5] = { 0, 0, 0, 0, 0 };
        check(ps5sd::api_key_from_bytes(zeros, 5) == "aaaaaaaa", "base32 of zeros");
        const unsigned char ff[1] = { 0xff };
        check(ps5sd::api_key_from_bytes(ff, 1) == "74", "base32 pads the last bits");
        check(ps5sd::api_key_groups("abcdefghij") == "abcd efgh ij", "groups of four");
        check(!ps5sd::valid_api_key("") && !ps5sd::valid_api_key("abc1") && !ps5sd::valid_api_key("ABC"),
              "not a key");
        const auto a = nlohmann::json::parse(ps5sd::auth_json(key));
        check(a["enabled"] == true && a["key"] == key, "auth json");
        check(nlohmann::json::parse(ps5sd::auth_json(""))["enabled"] == false, "auth json, no key");
        ps5sd::AppSettings settings;
        check(settings.tv_api_key && ps5sd::apply_settings(R"({"tv_api_key": false})", settings, error) &&
                  !settings.tv_api_key && settings.tv_images,
              "tv_api_key setting");
        check(!ps5sd::apply_settings(R"({"tv_api_key": 1})", settings, error), "tv_api_key wrong type");
    }
    // A PNG from the web UI for the gallery.
    {
        std::string png;
        // "\x89PNG\r\n\x1a\n" + "ab" in base64
        check(ps5sd::png_from_json(R"({"image": "iVBORw0KGgphYg=="})", png, error) && png.size() == 10 && png.substr(8) == "ab",
              "a base64 PNG");
        check(ps5sd::png_from_json(R"({"image": "data:image/png;base64,iVBORw0KGgphYg=="})", png, error) && png.size() == 10,
              "a data URL");
        check(!ps5sd::png_from_json(R"({"image": "aGVsbG8gd29ybGQ="})", png, error), "not a PNG");
        check(!ps5sd::png_from_json(R"({"image": "iVBOR w0KGgo"})", png, error), "not base64");
        check(!ps5sd::png_from_json(R"({"img": "x"})", png, error) && !ps5sd::png_from_json("[]", png, error), "no image");
        const fs::path out = root / "outputs";
        fs::create_directories(out);
        const std::string a = ps5sd::edited_image_name(out.u8string(), 1792570000);
        check(a.rfind("sd_", 0) == 0 && a.size() > 13 && a.compare(a.size() - 9, 9, "_edit.png") == 0, "an edit's name");
        std::ofstream(out / a) << "x";
        const std::string b = ps5sd::edited_image_name(out.u8string(), 1792570000);
        check(b != a && b.compare(b.size() - 10, 10, "_edit2.png") == 0, "the next free name");
    }
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
        const fs::path ups = root / "upscalers";
        fs::create_directories(ups / "sub");
        std::ofstream(ups / "RealESRGAN_x4plus.pth") << "123";
        std::ofstream(ups / "4x.safetensors") << "1";
        std::ofstream(ups / "4x.pth.part") << "1";
        std::ofstream(ups / "sub" / "deep.pth") << "1";
        const auto u = nlohmann::json::parse(ps5sd::upscalers_json(ups.u8string()))["upscalers"];
        check(u.size() == 2 && u[0]["file"] == "4x.safetensors" && u[1]["name"] == "RealESRGAN_x4plus" && u[1]["bytes"] == 3,
              "upscalers json: the top of the folder, upscaler files only");
    }

    fs::remove_all(root);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "ok" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
