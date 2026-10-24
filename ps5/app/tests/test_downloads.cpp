// Host test for download_core.cpp: the URLs the downloader takes and how it
// rewrites them, where files may land, the catalog, the queue file and the
// answers of Hugging Face and Civitai it reads.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "download_core.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

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

std::string read(const fs::path & p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_size(const fs::path & p, uint64_t bytes) {
    fs::create_directories(p.parent_path());
    { std::ofstream(p, std::ios::binary); }
    fs::resize_file(p, bytes);
}

}  // namespace

int main() {
    using namespace ps5sd;
    std::string error;

    // ---- URLs
    Url u;
    check(parse_url("https://huggingface.co/a/b/resolve/main/x.gguf?download=true", u), "parse a URL");
    check(u.host == "huggingface.co" && u.port == 443 && u.target == "/a/b/resolve/main/x.gguf?download=true",
          "host, port and target");
    check(parse_url("http://Example.COM:8080", u) && u.host == "example.com" && u.port == 8080 && u.target == "/",
          "port and an empty path");
    check(!parse_url("ftp://x/y", u), "no ftp");
    check(!parse_url("https://user:pw@host/x", u), "no credentials in a URL");
    check(!parse_url("https://host/a b", u), "no raw spaces");
    check(!parse_url("https://host:99999/", u), "port range");
    check(parse_url("https://cdn.example/a/b.gguf?sig=1", u) &&
              resolve_location(u, "/c/d?x") == "https://cdn.example/c/d?x",
          "absolute-path Location");
    check(resolve_location(u, "https://other/z") == "https://other/z", "absolute Location");
    check(resolve_location(u, "e.gguf") == "https://cdn.example/a/e.gguf", "relative Location");
    check(resolve_location(u, "//x.y/z") == "https://x.y/z", "scheme-relative Location");

    Source s;
    check(classify_url("https://huggingface.co/concedo/sdxs-512-tinySDdistilled-GGUF/resolve/main/"
                       "sdxs-512-tinySDdistilled_Q8_0.gguf?download=true",
                       s, error),
          "HF resolve URL");
    check(s.kind == "hf" && s.hf_repo == "concedo/sdxs-512-tinySDdistilled-GGUF" &&
              s.file_name == "sdxs-512-tinySDdistilled_Q8_0.gguf" &&
              s.url == "https://huggingface.co/concedo/sdxs-512-tinySDdistilled-GGUF/resolve/main/"
                       "sdxs-512-tinySDdistilled_Q8_0.gguf",
          "HF resolve URL kept, query dropped");
    check(classify_url("https://huggingface.co/Comfy-Org/flux2-dev/blob/main/split_files/vae/flux2-vae.safetensors", s,
                       error) &&
              s.url == "https://huggingface.co/Comfy-Org/flux2-dev/resolve/main/split_files/vae/flux2-vae.safetensors" &&
              s.file_name == "flux2-vae.safetensors",
          "HF blob URL rewritten to resolve, file name from the last part");
    check(classify_url("https://huggingface.co/a/b/resolve/main/sub%20dir/my%20model.gguf", s, error) &&
              s.file_name == "my model.gguf",
          "percent-decoded file name");
    check(!classify_url("https://huggingface.co/a/b/tree/main/x.gguf", s, error), "tree URL refused");
    check(!classify_url("https://huggingface.co/a/b", s, error), "repo URL refused");
    check(!classify_url("https://huggingface.co/a/b/resolve/main/../x.gguf", s, error), ".. in an HF path");
    check(!classify_url("http://huggingface.co/a/b/resolve/main/x.gguf", s, error), "plain http refused");
    check(classify_url("https://civitai.com/models/4384/dreamshaper?modelVersionId=128713", s, error) &&
              s.kind == "civitai" && s.civitai_version == "128713" &&
              s.url == "https://civitai.com/api/download/models/128713",
          "Civitai model page with a version");
    check(classify_url("https://civitai.com/api/download/models/128713?type=Model&format=SafeTensor", s, error) &&
              s.url == "https://civitai.com/api/download/models/128713" && s.civitai_file.empty(),
          "Civitai download URL, extra query dropped");
    check(classify_url("https://civitai.com/api/download/models/128713?fileId=93211", s, error) &&
              s.url == "https://civitai.com/api/download/models/128713?fileId=93211" && s.civitai_file == "93211",
          "Civitai fileId kept");
    check(!classify_url("https://civitai.com/models/4384", s, error), "Civitai page without a version");
    check(!classify_url("https://civitai.com/api/download/models/abc", s, error), "Civitai version not a number");
    check(!classify_url("https://example.com/x.gguf", s, error), "other hosts refused");

    // ---- destinations
    check(valid_dest("model.gguf", error), "plain name");
    check(valid_dest("vae/sdxl_vae_fp16_fix.safetensors", error), "subdirectory");
    check(valid_dest("a/b/c.SFT", error), "upper-case extension");
    check(!valid_dest("/data/PS5SD/x.gguf", error), "absolute dest");
    check(!valid_dest("../x.gguf", error), ".. dest");
    check(!valid_dest("a/../x.gguf", error), "inner ..");
    check(!valid_dest("a//x.gguf", error), "empty part");
    check(!valid_dest(".hidden.gguf", error), "hidden file");
    check(!valid_dest("a\\x.gguf", error), "backslash");
    check(!valid_dest("x.txt", error), "not a model");
    check(!valid_dest("x.gguf.part", error), "a .part");
    check(!valid_dest("", error), "empty");
    check(!valid_dest("C:x.gguf", error), "drive letter");
    check(!valid_dest(std::string("a\nb.gguf"), error), "control character");
    check(download_id("a.gguf") == download_id("a.gguf") && download_id("a.gguf") != download_id("b.gguf") &&
              download_id("a.gguf").size() == 16,
          "ids are stable and differ");

    // ---- headers
    check(content_disposition_name("attachment; filename=\"dreamshaper_8.safetensors\"") == "dreamshaper_8.safetensors",
          "quoted filename");
    check(content_disposition_name("attachment; filename=a.gguf; filename*=UTF-8''b%20c.gguf") == "b c.gguf",
          "filename* wins");
    check(content_disposition_name("attachment; filename=\"../../etc/x.gguf\"") == "x.gguf", "never a path");
    check(content_disposition_name("inline").empty(), "no name");
    check(content_range_total("bytes 0-0/682847200") == 682847200, "Content-Range total");
    check(content_range_total("bytes 0-0/*") == -1, "unknown total");

    // ---- catalog
    std::set<std::string> ids;
    for (const CatalogEntry & e : catalog()) {
        ids.insert(e.id);
        check(!e.files.empty(), "every entry has files");
        for (const CatalogFile & f : e.files) {
            Source src;
            check(classify_url(f.url, src, error) && src.kind == "hf", "catalog URLs are HF resolve URLs");
            check(valid_dest(f.dest, error), "catalog dests are valid");
            check(f.bytes > 0, "catalog sizes are known");
        }
        check(std::string(e.preset).find("# name:") == 0, "presets start with their name");
    }
    check(ids.size() == catalog().size() && ids.count("sdxs-512") && ids.count("sdxl-base") && ids.count("sd-turbo"),
          "catalog ids are unique, the starters are there");
    check(find_catalog("sdxs-512") != nullptr && find_catalog("nope") == nullptr, "find_catalog");
    {
        const CatalogEntry * canny = find_catalog("sdxl-canny");
        check(canny != nullptr && !canny->featured && canny->files.size() == 3 &&
                  std::string(canny->files[2].dest) == "controlnet/xinsir-canny-sdxl-v2.safetensors",
              "sdxl-canny: not featured, the ControlNet is its third file");
    }

    const fs::path root = fs::temp_directory_path() / "ps5sd-test-downloads";
    fs::remove_all(root);
    const fs::path models  = root / "models";
    const fs::path presets = root / "presets";
    fs::create_directories(models);
    const CatalogEntry & sdxl = *find_catalog("sdxl-base");
    check(!catalog_files_present(sdxl, models.u8string()), "nothing there yet");
    for (const CatalogFile & f : sdxl.files) {
        write_size(models / f.dest, f.bytes);  // sparse
    }
    check(catalog_files_present(sdxl, models.u8string()), "files at their size count");
    {
        const json j = json::parse(catalog_json(models.u8string(), presets.u8string()));
        bool       found = false;
        for (const auto & e : j["entries"]) {
            if (e["id"] == "sdxl-base") {
                found = true;
                check(e["installed"] == false, "not installed without its preset");
                check(e["files"].size() == 2 && e["files"][1]["dest"] == "vae/sdxl_vae_fp16_fix.safetensors",
                      "files in the JSON");
                check(e["bytes"] == 5040957760ull + 334641162ull, "bytes add up");
                check(e["preset"].get<std::string>().find("--vae-tile-size\n640") != std::string::npos,
                      "the preset's text");
            }
        }
        check(found, "sdxl-base in the catalog JSON");
    }
    write(presets / "sdxl-base.txt", sdxl.preset);
    {
        // an older PS5SD's Wan preset gets the current one; a changed one and the others stay
        const std::string wan   = find_catalog("wan21-t2v-1.3b")->preset;
        const std::string tiles = "--vae-tile-size\n256\n--vae-tile-overlap\n0.25\n";
        const size_t      at    = wan.find(tiles);
        check(at != std::string::npos, "Wan decodes in 256 px tiles at 0.25");
        // 0.9.0's, as the console has it
        const std::string old = wan.substr(0, at) + "--vae-tile-size\n128\n" + wan.substr(at + tiles.size());
        check(update_catalog_presets(presets.u8string()).empty(), "nothing to update without the file");
        write(presets / "wan21-t2v-1.3b.txt", old);
        const auto updated = update_catalog_presets(presets.u8string());
        check(updated.size() == 1 && updated[0] == "wan21-t2v-1.3b" && read(presets / "wan21-t2v-1.3b.txt") == wan &&
                  !fs::exists(presets / "wan21-t2v-1.3b.txt.tmp"),
              "the old preset becomes the current one");
        check(update_catalog_presets(presets.u8string()).empty(), "the current one is left alone");
        const std::string changed = old + "--seed\n5\n";
        write(presets / "wan21-t2v-1.3b.txt", changed);
        check(update_catalog_presets(presets.u8string()).empty() && read(presets / "wan21-t2v-1.3b.txt") == changed,
              "a preset someone changed stays");
        check(read(presets / "sdxl-base.txt") == sdxl.preset, "other presets stay");
        const CatalogEntry * tae = find_catalog("wan21-t2v-1.3b-tae");
        check(tae != nullptr && !tae->featured && tae->files.size() == 3 &&
                  std::string(tae->files[0].dest) == find_catalog("wan21-t2v-1.3b")->files[0].dest &&
                  std::string(tae->preset).find("--taesd\nvae/taew2_1.safetensors\n") != std::string::npos,
              "the Wan variant with TAEHV shares the model files");
        const CatalogEntry * ti2v = find_catalog("wan22-ti2v-5b");
        check(ti2v != nullptr && ti2v->files.size() == 3 &&
                  std::string(ti2v->files[1].dest) == find_catalog("wan21-t2v-1.3b")->files[1].dest &&
                  std::string(ti2v->preset).find("--vae\nvae/wan2.2_vae.safetensors\n") != std::string::npos &&
                  std::string(ti2v->preset).find("width=1280 height=704") != std::string::npos,
              "Wan 2.2 TI2V shares the text encoder, has its own VAE and its own size");
        fs::remove(presets / "wan21-t2v-1.3b.txt");
    }
    {
        // 0.9.1's SDXL preset: euler_a at 25 steps, 512 px tiles, no --eager-load
        const std::string now   = sdxl.preset;
        const std::string ays   = "steps=10 cfg=6 width=1024 height=1024 sampler=dpm++2m scheduler=ays\n";
        const std::string tiles = "--vae-tile-size\n640\n--eager-load\n";
        const size_t      a     = now.find(ays);
        const size_t      t     = now.find(tiles);
        check(a != std::string::npos && t != std::string::npos && t + tiles.size() == now.size(),
              "SDXL samples with ays at 10 steps and decodes in 640 px tiles");
        const std::string old = now.substr(0, a) + "steps=25 cfg=6 width=1024 height=1024 sampler=euler_a\n" +
                                now.substr(a + ays.size(), t - a - ays.size()) + "--vae-tile-size\n512\n";
        write(presets / "sdxl-base.txt", old);
        const auto updated = update_catalog_presets(presets.u8string());
        check(updated.size() == 1 && updated[0] == "sdxl-base" && read(presets / "sdxl-base.txt") == now,
              "0.9.1's SDXL preset becomes the current one");
    }
    {
        const json j = json::parse(catalog_json(models.u8string(), presets.u8string()));
        for (const auto & e : j["entries"]) {
            if (e["id"] == "sdxl-base") {
                check(e["installed"] == true, "installed with files and preset");
            } else if (e["id"] == "sdxs-512") {
                check(e["installed"] == false, "others are not");
            }
        }
    }
    fs::resize_file(models / sdxl.files[1].dest, 5);
    check(!catalog_files_present(sdxl, models.u8string()), "a file of another size does not count");
    {
        const CatalogEntry & klein = *find_catalog("flux3-klein-q4");
        const CatalogFile &  vae   = klein.files.back();
        check(vae.alt_bytes == 336211292ull, "the Klein VAE has BFL's size as an alternative");
        write_size(models / vae.dest, vae.alt_bytes);
        check(catalog_file_present(vae, models.u8string()), "the alternative size counts");
        const json j = json::parse(catalog_json(models.u8string(), presets.u8string()));
        int featured = 0;
        for (const auto & e : j["entries"]) {
            featured += e["featured"].get<bool>() ? 1 : 0;
            if (e["id"] == "flux3-klein-q4") {
                check(e["files"][2]["installed"] == true && e["files"][0]["installed"] == false,
                      "per-file installed");
            }
        }
        check(featured == 7 && find_catalog("sdxl-full")->featured && find_catalog("z-image-turbo")->featured &&
                  find_catalog("wan21-t2v-1.3b")->featured && find_catalog("flux2-klein-4b")->featured &&
                  find_catalog("flux2-klein-9b")->featured && find_catalog("wan22-ti2v-5b")->featured,
              "SDXL full, Klein Q4, FLUX.2 Klein 4B and 9B, Z-Image Turbo, Wan 2.1 and Wan 2.2 TI2V are featured");
        const CatalogEntry * k9 = find_catalog("flux2-klein-9b");
        check(k9 != nullptr && k9->files.size() == 3 &&
                  std::string(k9->files[1].dest) == "text_encoders/Qwen3-8B-Q4_K_M.gguf" &&
                  std::string(k9->files[2].dest) == klein.files.back().dest &&
                  std::string(k9->preset).find("--llm\ntext_encoders/Qwen3-8B-Q4_K_M.gguf\n") != std::string::npos,
              "Klein 9B: Qwen3-8B and the Klein VAE");
        check(j["entries"][0]["id"] == "sdxl-full" && j["entries"][1]["id"] == "flux3-klein-q4" &&
                  j["entries"][2]["id"] == "z-image-turbo" && j["entries"][0]["good_for"].is_string(),
              "featured order and good_for");
        check(std::string(find_catalog("sdxl-full")->preset).find("--type\nq8_0\n") != std::string::npos,
              "the full SDXL is converted to Q8_0 while it loads");
    }

    // ---- the queue file
    std::vector<DownloadRecord> list(3);
    list[0].url     = "https://huggingface.co/a/b/resolve/main/x.gguf";
    list[0].dest    = "x.gguf";
    list[0].id      = download_id("x.gguf");
    list[0].state   = "running";
    list[0].total   = 20ull << 20;
    list[0].piece   = 8ull << 20;
    list[0].pieces  = "101";
    list[0].catalog = "sdxs-512";
    list[1].url     = "https://civitai.com/api/download/models/1";
    list[1].dest    = "c/y.safetensors";
    list[1].id      = download_id("c/y.safetensors");
    list[1].state   = "done";
    list[1].total   = 77;
    list[2].url     = "https://huggingface.co/a/b/resolve/main/z.gguf";
    list[2].dest    = "../z.gguf";  // not valid: dropped on load
    list[2].state   = "queued";
    const std::string text = queue_to_json(list);
    const auto back = queue_from_json(text);
    check(back.size() == 2, "an invalid dest is dropped on load");
    check(back.size() == 2 && back[0].state == "queued", "running comes back queued");
    check(back.size() == 2 && back[0].pieces == "101" && back[0].bytes == (8ull << 20) + (4ull << 20),
          "the piece map and its bytes (last piece short)");
    check(back.size() == 2 && back[0].catalog == "sdxs-512" && back[0].id == download_id("x.gguf"), "catalog and id");
    check(back.size() == 2 && back[1].state == "done" && back[1].bytes == 77, "done keeps its size");
    {
        auto bad      = list;
        bad[0].pieces = "10";  // wrong length for 20 MiB / 8 MiB
        bad.resize(1);
        const auto b = queue_from_json(queue_to_json(bad));
        check(b.size() == 1 && b[0].pieces == "000" && b[0].bytes == 0, "a piece map of the wrong length starts over");
    }
    check(queue_from_json("not json").empty() && queue_from_json("{}").empty(), "garbage gives an empty queue");
    {
        std::vector<DownloadRecord> l(2);
        l[0].url  = "https://huggingface.co/a/b/resolve/main/style.safetensors";
        l[0].dest = "style.safetensors";
        l[0].kind = "loras";
        l[1]      = l[0];
        l[1].dest = "x/style.safetensors";  // no folders for LoRAs: dropped on load
        const auto b = queue_from_json(queue_to_json(l));
        check(b.size() == 1 && b[0].kind == "loras" && b[0].id == download_id("loras/style.safetensors") &&
                  b[0].id != download_id("style.safetensors"),
              "a LoRA download keeps its kind, and its id differs from a model's of the same name");
        check(queue_from_json(R"({"downloads":[{"url":"https://huggingface.co/a/b/resolve/main/m.gguf","dest":"m.gguf","state":"done"}]})")[0].kind ==
                  "models",
              "an old record without kind is a model");
    }
    check(is_lora_type("LORA") && is_lora_type("LoCon") && is_lora_type("DoRA") && !is_lora_type("Checkpoint") &&
              !is_lora_type("TextualInversion"),
          "Civitai's LoRA types");
    check(valid_target("loras", "style.safetensors", error) && !valid_target("loras", "a/style.safetensors", error) &&
              valid_target("models", "a/m.gguf", error) && !valid_target("vae", "v.safetensors", error),
          "download targets");
    {
        std::string kind, name, e;
        check(valid_target("upscalers", "RealESRGAN_x4plus.pth", error) && valid_target("upscalers", "4x.safetensors", error) &&
                  !valid_target("upscalers", "esrgan/4x.pth", error) && !valid_target("upscalers", "4x.ckpt", error) &&
                  !valid_target("upscalers", ".4x.pth", error) && !valid_target("upscalers", "../4x.pth", error),
              "upscaler targets: .pth/.pt/.safetensors/.gguf, no folders");
        check(upload_target("upscalers/4x-UltraSharp.pth", kind, name, e) && kind == "upscalers" && name == "4x-UltraSharp.pth",
              "upload: an upscaler");
        check(download_key("upscalers", "a.pth") == "upscalers/a.pth" && download_key("models", "a.gguf") == "a.gguf",
              "an upscaler's id differs from a model's");
        DownloadRecord u;
        u.url  = "https://huggingface.co/a/b/resolve/main/x4.pth";
        u.dest = "x4.pth";
        u.kind = "upscalers";
        u.id   = download_id(download_key(u.kind, u.dest));
        const auto back = queue_from_json(queue_to_json({ u }));
        check(back.size() == 1 && back[0].kind == "upscalers", "an upscaler download keeps its kind");
        DownloadRequest dr;
        check(parse_download_request("{\"url\":\"u\",\"kind\":\"upscalers\"}", dr, error) && dr.kind == "upscalers",
              "an upscaler download request");
        check(!parse_download_request("{\"catalog\":\"sdxs-512\",\"kind\":\"upscalers\"}", dr, error),
              "no catalog entry as an upscaler");
        std::string n, url, type;
        uint64_t    bytes = 0;
        check(parse_civitai_version(R"({"model":{"type":"Upscaler"},"files":[{"type":"Model","name":"4x_foo.pth","downloadUrl":"https://civitai.com/api/download/models/1"}]})",
                                    "", n, url, bytes, error, &type) &&
                  n == "4x_foo.pth" && type == "Upscaler",
              "a Civitai upscaler may be a .pth");
        check(!parse_civitai_version(R"({"model":{"type":"Checkpoint"},"files":[{"type":"Model","name":"x.pth","downloadUrl":"u"}]})",
                                     "", n, url, bytes, error, &type),
              "a checkpoint must not be a .pth");
    }

    // The .part grows as pieces land: a done piece must lie inside it.
    check(pieces_within("1100", 10, 35, 20) && pieces_within("0000", 10, 35, 0), "pieces inside the .part");
    check(!pieces_within("1010", 10, 35, 25), "a piece past the end of the .part");
    check(pieces_within("0001", 10, 35, 35) && !pieces_within("0001", 10, 35, 34), "the short last piece");
    check(!pieces_within("1", 0, 35, 35), "no piece size");
    check(pieces_in_order("1101", 10, 35, 35) == "1100", "in order: a done piece after a gap is fetched again");
    check(gallery_name_ok("sd_20261020_185438_001.png") && gallery_name_ok("my-image.PNG"), "gallery: image names");
    check(!gallery_name_ok("../model.txt") && !gallery_name_ok("../x.png") && !gallery_name_ok("a/b.png") &&
              !gallery_name_ok(".png") && !gallery_name_ok(".hidden.png") && !gallery_name_ok("x.txt") &&
              !gallery_name_ok("a b.png") && !gallery_name_ok(""),
          "gallery: no paths, hidden files or other files");
    check(gallery_name_ok("sd_20261021_183000_000.webm") && gallery_name_ok("clip.WEBP") && gallery_name_ok("a.avi") &&
              !gallery_name_ok(".webm") && !gallery_name_ok("x.mp4") && !gallery_name_ok("x.webm.txt"),
          "gallery: video names");
    {
        std::string kind, name, e;
        check(upload_target("models/vae/my vae.safetensors", kind, name, e) && kind == "models" &&
                  name == "vae/my vae.safetensors",
              "upload: a model in a folder");
        check(upload_target("loras/style.safetensors", kind, name, e) && kind == "loras", "upload: a LoRA");
        check(!upload_target("loras/x/style.safetensors", kind, name, e), "upload: no folders for LoRAs");
        check(upload_target("presets/my-model.txt", kind, name, e), "upload: a preset");
        check(upload_target("tags/danbooru.csv", kind, name, e) && kind == "tags" && name == "danbooru.csv", "upload: tags");
        check(upload_target("tags/mine.TXT", kind, name, e), "upload: tags in a .txt");
        check(!upload_target("tags/danbooru.json", kind, name, e), "upload: tags are .csv or .txt");
        check(!upload_target("tags/x/danbooru.csv", kind, name, e), "upload: no folders for tags");
        check(!upload_target("presets/my-model.gguf", kind, name, e), "upload: a preset must be .txt");
        check(!upload_target("presets/.hidden.txt", kind, name, e), "upload: no hidden preset");
        check(!upload_target("models/../env.txt", kind, name, e) && !upload_target("models/a.txt", kind, name, e),
              "upload: no .. and no other files into models");
        check(!upload_target("outputs/a.gguf", kind, name, e) && !upload_target("models", kind, name, e),
              "upload: only the four folders");
    }
    check(pieces_in_order("1110", 10, 35, 25) == "1100", "in order: a piece the file does not hold");
    check(pieces_in_order("1111", 10, 35, 35) == "1111" && pieces_in_order("1111", 10, 35, 34) == "1110",
          "in order: the short last piece");
    check(pieces_in_order("0111", 10, 35, 35) == "0000" && pieces_in_order("11", 0, 35, 35) == "00",
          "in order: nothing kept");
    {
        const json j = json::parse(downloads_json(back));
        check(j["downloads"].size() == 2, "downloads JSON");
        const auto & d = j["downloads"][0];
        for (const char * k : { "id", "url", "dest", "bytes", "total", "state", "error", "bytes_per_second", "catalog" }) {
            check(d.contains(k), k);
        }
        check(!d.contains("pieces"), "the piece map is not in the API");
    }

    // ---- request bodies
    DownloadRequest dr;
    check(parse_download_request("{\"url\":\"u\",\"dest\":\"d\"}", dr, error) && dr.url == "u" && dr.dest == "d",
          "download request");
    check(parse_download_request("{\"catalog\":\"sdxs-512\"}", dr, error) && dr.url.empty() && dr.catalog == "sdxs-512",
          "catalog install request");
    check(!parse_download_request("{}", dr, error), "empty request");
    check(parse_download_request("{\"url\":\"u\",\"kind\":\"loras\"}", dr, error) && dr.kind == "loras", "a LoRA download");
    check(!parse_download_request("{\"url\":\"u\",\"kind\":\"vae\"}", dr, error), "an unknown kind");
    check(!parse_download_request("{\"catalog\":\"sdxs-512\",\"kind\":\"loras\"}", dr, error), "no catalog entry as a LoRA");
    check(!parse_download_request("[1]", dr, error), "not an object");
    check(parse_download_request("{\"url\":5,\"catalog\":\"x\"}", dr, error) && dr.url.empty(),
          "a wrong type is ignored, not thrown");
    SearchRequest sr;
    check(parse_search_request("{\"q\":\"sdxl\",\"limit\":500}", sr, error) && sr.q == "sdxl" && sr.limit == 50,
          "search limit capped");
    check(parse_search_request("{\"q\":\"x\"}", sr, error) && sr.limit == 20, "search default limit");

    // ---- Hugging Face and Civitai answers
    const std::string search = R"([
        {"id":"a/gguf-repo","downloads":10,"likes":2,"siblings":[{"rfilename":"README.md"},{"rfilename":"m-Q8_0.gguf"}]},
        {"id":"b/no-models","downloads":5,"siblings":[{"rfilename":"README.md"}]},
        {"id":"c/diffusers","downloads":3,"siblings":[{"rfilename":"unet/diffusion_pytorch_model.safetensors"}]}
    ])";
    auto results = parse_hf_search(search);
    check(results.size() == 2 && results[0].repo == "a/gguf-repo" && results[1].repo == "c/diffusers",
          "repositories without a model file drop out");
    check(results.size() == 2 && results[0].files.size() == 1 && results[0].downloads == 10 && results[0].likes == 2,
          "files and counts");
    parse_hf_model_files(R"({"id":"a/gguf-repo","downloads":11,"siblings":[
        {"rfilename":"README.md","size":219},
        {"rfilename":"m-Q8_0.gguf","size":134,"lfs":{"size":682847200}},
        {"rfilename":"m-Q4_0.gguf","size":400}]})",
                         results[0]);
    check(results[0].files.size() == 2 && results[0].files[0].bytes == 682847200 && results[0].files[1].bytes == 400 &&
              results[0].downloads == 11,
          "sizes from blobs=true, the LFS size wins");
    {
        const json j = json::parse(search_json(results));
        check(j["results"].size() == 2 && j["results"][0]["files"][0]["path"] == "m-Q8_0.gguf", "search JSON");
        check(j["results"][1]["files"][0]["url"] ==
                  "https://huggingface.co/c/diffusers/resolve/main/unet/diffusion_pytorch_model.safetensors",
              "search gives each file's resolve URL");
        Source back_src;
        check(classify_url(j["results"][1]["files"][0]["url"], back_src, error), "which the downloader takes");
    }
    check(parse_hf_search("{\"error\":\"x\"}").empty(), "an error object gives no results");

    const std::string civ = R"({"id":128713,"files":[
        {"id":1,"name":"dreamshaper_8.vae.pt","type":"VAE","sizeKB":300},
        {"id":93211,"name":"dreamshaper_8.safetensors","type":"Model","primary":true,"sizeKB":2082642.474609375,
         "downloadUrl":"https://civitai.com/api/download/models/128713?fileId=93211"},
        {"id":2,"name":"dreamshaper_8_inpaint.safetensors","type":"Model","sizeKB":10}]})";
    std::string name, url;
    uint64_t    bytes = 0;
    check(parse_civitai_version(civ, "", name, url, bytes, error) && name == "dreamshaper_8.safetensors" &&
              url == "https://civitai.com/api/download/models/128713?fileId=93211" && bytes == 2132625894ull,
          "Civitai primary model file");
    check(parse_civitai_version(civ, "2", name, url, bytes, error) && name == "dreamshaper_8_inpaint.safetensors",
          "Civitai file by id");
    check(!parse_civitai_version(civ, "1", name, url, bytes, error), "a VAE .pt is not a model file");
    {
        std::string type = "x";
        check(parse_civitai_version(civ, "", name, url, bytes, error, &type) && type.empty(), "no model object: no type");
        const std::string lora = R"({"model":{"name":"Detail","type":"LORA"},"files":[
            {"id":5,"name":"detail.safetensors","type":"Model","primary":true,"sizeKB":1}]})";
        check(parse_civitai_version(lora, "", name, url, bytes, error, &type) && type == "LORA" &&
                  name == "detail.safetensors",
              "Civitai's model type");
    }
    check(!parse_civitai_version("{\"error\":\"Model not found\"}", "", name, url, bytes, error) &&
              error.find("Model not found") != std::string::npos,
          "Civitai's error is passed on");

    fs::remove_all(root);
    if (failures == 0) {
        std::printf("test_downloads: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
