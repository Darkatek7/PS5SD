// Host test for lora_info.cpp: the info file next to a LoRA (kept keys, merged edits), the previews, what Civitai's
// and Hugging Face's JSON fill in (trimmed copies of real answers for Pixel Art XL), the lookup against a fake
// network (every branch of its order), HTML to text, SHA-256 and the token files.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "lora_info.hpp"

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

template <class T, class U> void eq(const T & got, const U & want, const std::string & what) {
    if (!(got == want)) {
        std::ostringstream o;
        o << what << ": got [" << got << "], want [" << want << "]";
        check(false, o.str());
    }
}

// Civitai's /api/v1/model-versions/by-hash/BBF3D8DE... (Pixel Art XL v1.1), trimmed: the fields the title reads.
const char * kVersion = R"JSON({
 "id": 135931,
 "modelId": 120096,
 "name": "v1.1",
 "nsfwLevel": 1,
 "trainedWords": [],
 "baseModel": "SDXL 1.0",
 "model": {
  "name": "Pixel Art XL",
  "type": "LORA",
  "nsfw": false,
  "poi": false
 },
 "downloadUrl": "https://civitai.com/api/download/models/135931",
 "images": [
  {
   "url": "https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/0770e5fc-4260-4e3c-a0ee-bf2253028e9f/original=true/1918193.jpeg",
   "nsfwLevel": 1,
   "width": 1024,
   "height": 1024,
   "type": "image"
  },
  {
   "url": "https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/684efffb-c290-4e5b-b0be-90b3e7234a89/original=true/1918242.jpeg",
   "nsfwLevel": 1,
   "width": 1024,
   "height": 1024,
   "type": "image"
  },
  {
   "url": "https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/6c54e73b-ee6f-4ca4-b683-f6aa619894c1/original=true/1918331.jpeg",
   "nsfwLevel": 1,
   "width": 1024,
   "height": 1024,
   "type": "image"
  },
  {
   "url": "https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/21af48d8-0538-4065-bba2-385e4b189d90/original=true/1918359.jpeg",
   "nsfwLevel": 1,
   "width": 1024,
   "height": 1024,
   "type": "image"
  }
 ]
})JSON";
// /api/v1/models/120096, trimmed to id, name, type and description.
const char * kModel = R"JSON({
 "id": 120096,
 "name": "Pixel Art XL",
 "type": "LORA",
 "description": "<h1 id=\"heading-63\">Pixel Art XL</h1><p>Consider supporting further research on <a target=\"_blank\" rel=\"ugc\" href=\"https://ko-fi.com/nerijs\">Ko-Fi</a> or <a target=\"_blank\" rel=\"ugc\" href=\"https://twitter.com/nerijs\">Twitter</a><br />If you have a request, you can do it via Ko-Fi<br /><br /><a target=\"_blank\" rel=\"ugc\" href=\"https://astropulse.gumroad.com/l/RetroDiffusion\">Checkout my other models at RetroDiffusion<br /><img src=\"https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/c4c4d3a2-b85f-40c1-a232-6212771987f9/width=525/c4c4d3a2-b85f-40c1-a232-6212771987f9.jpeg\" /><br /></a><br />Best used in conjunction with: <a target=\"_blank\" rel=\"ugc\" href=\"https://github.com/Astropulse/pixeldetector\"><strong>Astropulse PixelDetector</strong></a></p><p></p><p>Downscale 8 times to get pixel perfect images (use Nearest Neighbors)</p><p></p><h3 id=\"heading-7\"><strong>Use a fixed VAE to avoid artifacts (0.9 or fp16 fix)</strong></h3><h3 id=\"heading-8\"><strong><br />Best results without using, pixel art in the prompt</strong></h3><p></p><h2 id=\"heading-1982\">Tips:</h2><ul><li><p>Don't use refiner</p></li><li><p>Works great with only 1 text encoder</p></li><li><p>No style prompt required</p></li><li><p>No trigger keyword require</p></li><li><p>Works great with isometric and non-isometric</p></li><li><p>Works with 0.9 and 1.0</p></li></ul><p></p><h2 id=\"heading-1983\">Changelog</h2><ul><li><p>v1: Initial release</p></li></ul>"
})JSON";
// huggingface.co/api/models/nerijs/pixel-art-xl, trimmed to id and cardData.
const char * kHf = R"JSON({
 "id": "nerijs/pixel-art-xl",
 "cardData": {
  "license": "creativeml-openrail-m",
  "tags": [
   "text-to-image",
   "stable-diffusion",
   "lora",
   "diffusers"
  ],
  "base_model": "stabilityai/stable-diffusion-xl-base-1.0",
  "instance_prompt": "pixel art",
  "widget": [
   {
    "text": "pixel art, a cute corgi, simple, flat colors"
   }
  ]
 }
})JSON";

const std::string kSha256 = "BBF3D8DEFBFB3FB71331545225C0CF50C74A748D2525F7C19EBB8F74445DE274";

std::string slurp(const fs::path & p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void put(const fs::path & p, const std::string & text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// ---- the network, faked ---------------------------------------------------------------------------

struct Fake {
    std::map<std::string, std::pair<int, std::string>>        text;    // url -> HTTP status, body
    std::map<std::string, std::pair<std::string, std::string>> images;  // url -> Content-Type, bytes
    std::vector<std::string>                                  asked;   // every url asked for, in order
    std::string                                               unreachable;  // a url that cannot be reached
    bool                                                      stop = false;

    LookupIo io() {
        LookupIo io;
        io.get_text = [this](const std::string & url, int & status, std::string & body, std::string & error) {
            asked.push_back(url);
            if (url == unreachable) {
                error = "connection refused";
                return false;
            }
            const auto it = text.find(url);
            status        = it == text.end() ? 404 : it->second.first;
            body          = it == text.end() ? "{}" : it->second.second;
            return true;
        };
        io.get_image = [this](const std::string & url, std::string & type, std::string & bytes, std::string & error) {
            asked.push_back(url);
            const auto it = images.find(url);
            if (it == images.end()) {
                error = "HTTP 404";
                return false;
            }
            type  = it->second.first;
            bytes = it->second.second;
            return true;
        };
        io.cancelled = [this] { return stop; };
        return io;
    }
    int count(const std::string & url) const {
        int n = 0;
        for (const auto & a : asked) {
            n += a == url;
        }
        return n;
    }
};

const char * kByHash = "https://civitai.com/api/v1/model-versions/by-hash/";
const char * kThumb0 =
    "https://image.civitai.com/xG1nkqKTMzGDvpLrqFT7WA/0770e5fc-4260-4e3c-a0ee-bf2253028e9f/width=450/1918193.jpeg";

// ---- the tests --------------------------------------------------------------------------------------

void test_sha256() {
    eq(sha256_hex(""), "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", "sha256 of nothing");
    eq(sha256_hex("abc"), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "sha256 of abc");
    eq(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
       "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", "sha256 of 56 bytes (two blocks)");
    Sha256 million;  // pieces of every size across the block edge
    const std::string a(1000, 'a');
    for (int i = 0; i < 1000; i++) {
        million.update(a.data(), a.size());
    }
    eq(million.finish(), "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0", "sha256 of a million a");
    Sha256 odd;
    const std::string abc = "abc";
    for (char c : abc) {
        odd.update(&c, 1);
    }
    eq(odd.finish(), sha256_hex("abc"), "sha256 byte by byte");
}

void test_sha256_file(const fs::path & root) {
    std::string data;
    for (size_t i = 0; i < (3u << 20) + 12345; i++) {
        data += static_cast<char>((i * 131 + (i >> 9)) & 0xFF);
    }
    put(root / "big.bin", data);
    std::string hex, error;
    check(sha256_file((root / "big.bin").string(), hex, error), "sha256_file reads: " + error);
    eq(hex, sha256_hex(data), "sha256_file over 1 MiB reads");
    check(!sha256_file((root / "none.bin").string(), hex, error) && !error.empty(), "sha256_file: no file");
    int  calls = 0;
    check(!sha256_file((root / "big.bin").string(), hex, error, [&] { return ++calls > 1; }) && error == "cancelled",
          "sha256_file stops when asked");
}

void test_names() {
    eq(lora_stem("style/a.b.safetensors"), "style/a.b", "stem keeps inner dots");
    eq(lora_stem("a.safetensors"), "a", "stem of a name");
    eq(lora_stem("dir.d/noext"), "dir.d/noext", "stem: no extension in the name");
    eq(sidecar_path("style/a.safetensors"), "style/a.json", "sidecar path");
    eq(preview_path("style/a.safetensors", "webp"), "style/a.preview.webp", "preview path");
    std::vector<std::string> files = { "x/a.png", "x/a.jpg", "x/a.preview.jpeg" };
    const auto               has   = [&](const std::string & p) { return std::find(files.begin(), files.end(), p) != files.end(); };
    eq(find_preview("x/a.safetensors", has), "x/a.preview.jpeg", "a .preview. file comes before A1111's plain one");
    files = { "x/a.jpg", "x/a.png" };
    eq(find_preview("x/a.safetensors", has), "x/a.png", "A1111's own preview names");
    files = { "x/b.png" };
    eq(find_preview("x/a.safetensors", has), "", "no preview");
    eq(preview_content_type("a.preview.jpeg"), "image/jpeg", "jpeg type");
    eq(preview_content_type("a.PNG"), "image/png", "png type");
    eq(preview_content_type("a.json"), "", "json is no image");
    eq(preview_extension("image/jpeg"), "jpeg", "ext of jpeg");
    eq(preview_extension("Image/WebP; charset=x"), "webp", "ext of webp with a parameter");
    eq(preview_extension("image/avif"), "", "no ext for avif");
    eq(preview_extension("video/mp4"), "", "no ext for video");
}

void test_html() {
    const Json model = Json::parse(kModel);
    const std::string text = html_to_text(model["description"].get<std::string>());
    check(text.rfind("Pixel Art XL\n\nConsider supporting further research on Ko-Fi or Twitter\n"
                     "If you have a request, you can do it via Ko-Fi\n\nCheckout my other models at RetroDiffusion\n\n"
                     "Best used in conjunction with: Astropulse PixelDetector",
                     0) == 0,
          "html: paragraphs, br, links, images: " + text);
    check(text.find('<') == std::string::npos && text.find("&amp;") == std::string::npos, "html: no tags left");
    check(text.find("\n\n\n") == std::string::npos, "html: never two blank lines");
    check(text.find("- Don't use refiner\n- Works great with only 1 text encoder") != std::string::npos, "html: list items: " + text);
    check(text.find("Changelog\n\n- v1: Initial release") != std::string::npos, "html: a heading, then its list");
    eq(html_to_text("<p>Fish &amp; chips &lt;3 &#39;quoted&#39; &quot;x&quot; &#x41;&nbsp;B &unknown; a < b</p>"),
       "Fish & chips <3 'quoted' \"x\" A B &unknown; a < b", "html: entities");
    eq(html_to_text("a<script>var x = '<b>';</script>b<style>p{}</style>c"), "abc", "html: script and style go");
    eq(html_to_text("  <div> one \n\t two </div>  <div>three</div> "), "one two\nthree", "html: spaces and divs");
    eq(html_to_text(""), "", "html: nothing");
    const std::string long_text = std::string(3000, 'x');
    const std::string cut       = html_to_text(long_text, 2000);
    eq(cut.size(), 2000u - 1 + 3, "html: cut at 2000 characters (the ellipsis is one)");
    check(cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0, "html: ends with an ellipsis");
    std::string utf8;
    for (int i = 0; i < 100; i++) {
        utf8 += "\xC3\xA9";  // é
    }
    const std::string cut2 = html_to_text(utf8, 10);
    eq(cut2, std::string("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9") + "\xE2\x80\xA6", "html: cut on a character edge");
    eq(civitai_thumb_url("https://image.civitai.com/k/u/original=true/1.jpeg"), "https://image.civitai.com/k/u/width=450/1.jpeg",
       "original -> width=450");
    eq(civitai_thumb_url("https://x/y/z.png"), "https://x/y/z.png", "another url stays");
    eq(iso_utc(1795298400), "2026-11-21T22:00:00Z", "iso time");
}

void test_civitai() {
    const Json version = Json::parse(kVersion), model = Json::parse(kModel);
    Json       sc = Json::object();
    civitai_to_sidecar(version, model, sc, "2026-11-21T22:00:00Z");
    const Json & ps = sc["ps5sd"];
    eq(sc["sd version"], "SDXL 1.0", "civitai: base model fills sd version");
    check(!sc.contains("activation text"), "civitai: no trained words, no activation text");
    check(sc["description"].get<std::string>().rfind("Pixel Art XL\n\nConsider supporting", 0) == 0, "civitai: model description as text");
    eq(ps["civitai"]["model_id"], 120096, "civitai model id");
    eq(ps["civitai"]["version_id"], 135931, "civitai version id");
    eq(ps["civitai"]["model"], "Pixel Art XL", "civitai model name");
    eq(ps["civitai"]["version"], "v1.1", "civitai version name");
    eq(ps["civitai"]["base_model"], "SDXL 1.0", "civitai base model");
    eq(ps["civitai"]["url"], "https://civitai.com/models/120096?modelVersionId=135931", "civitai url");
    eq(ps["lookup"], "found", "lookup found");
    eq(ps["looked_up"], "2026-11-21T22:00:00Z", "looked up time");
    eq(ps["images"].size(), 4u, "civitai images");
    eq(ps["images"][0]["url"], kThumb0, "civitai image url is the small one");
    eq(ps["images"][0]["nsfw_level"], 1, "civitai image level");
    eq(ps["images"][0]["width"], 1024, "civitai image width");

    // the user's own text stays; trained words become the trigger when there is none
    Json v2 = version;
    v2["trainedWords"] = Json::array({ "pixel art", " pixelated ", "" });
    Json mine = { { "description", "mine" }, { "sd version", "SD1" }, { "activation text", "my words" }, { "notes", "n" }, { "future key", { 1, 2 } } };
    civitai_to_sidecar(v2, model, mine, "t");
    eq(mine["description"], "mine", "user's description stays");
    eq(mine["sd version"], "SD1", "user's sd version stays");
    eq(mine["activation text"], "my words", "user's trigger stays");
    eq(mine["notes"], "n", "notes stay");
    check(mine.contains("future key") && mine["future key"].size() == 2, "unknown keys stay");
    Json empty = { { "activation text", "  " }, { "sd version", "Unknown" } };  // what A1111 writes
    civitai_to_sidecar(v2, Json(), empty, "t");
    eq(empty["activation text"], "pixel art, pixelated", "trained words joined");
    eq(empty["sd version"], "SDXL 1.0", "'Unknown' counts as empty");
    check(!empty.contains("description"), "no model page, no description (the version has none)");

    // more images than six, a video, one at level 4, an old-style answer
    Json many = version;
    many["images"] = Json::array();
    many["images"].push_back({ { "url", "https://image.civitai.com/a/b/original=true/v.mp4" }, { "type", "video" }, { "nsfwLevel", 1 } });
    for (int i = 0; i < 8; i++) {
        many["images"].push_back({ { "url", "https://image.civitai.com/a/b/original=true/" + std::to_string(i) + ".jpeg" },
                                   { "type", "image" }, { "nsfwLevel", i == 0 ? 4 : 1 } });
    }
    Json sc3 = Json::object();
    civitai_to_sidecar(many, Json(), sc3, "t");
    eq(sc3["ps5sd"]["images"].size(), 6u, "at most 6 images");
    eq(sc3["ps5sd"]["images"][0]["url"], "https://image.civitai.com/a/b/width=450/0.jpeg", "the video is skipped");
    eq(pick_preview_image(sc3["ps5sd"]["images"]), 1, "preview: the first image at level 2 or less");
    Json mature = Json::array();
    mature.push_back(Json{ { "nsfw_level", 4 } });
    mature.push_back(Json{ { "nsfw_level", 8 } });
    eq(pick_preview_image(mature), 0, "preview: else the first");
    eq(pick_preview_image(Json::array()), -1, "preview: none");
    Json old = version;
    old["images"] = Json::array({ { { "url", "https://x/original=true/1.jpeg" }, { "nsfw", "Mature" } },
                                  { { "url", "https://x/original=true/2.jpeg" }, { "nsfw", "None" } } });
    Json sc4 = Json::object();
    civitai_to_sidecar(old, Json(), sc4, "t");
    eq(sc4["ps5sd"]["images"][0]["nsfw_level"], 4, "old style 'Mature' is level 4");
    eq(sc4["ps5sd"]["images"][1]["nsfw_level"], 1, "old style 'None' is level 1");
}

void test_hf() {
    const Json model = Json::parse(kHf);
    Json       sc    = Json::object();
    hf_to_sidecar(model, "nerijs/pixel-art-xl", sc, "2026-11-21T22:00:00Z");
    eq(sc["activation text"], "pixel art", "hf instance prompt");
    eq(sc["sd version"], "stabilityai/stable-diffusion-xl-base-1.0", "hf base model");
    eq(sc["ps5sd"]["hf"]["repo"], "nerijs/pixel-art-xl", "hf repo");
    eq(sc["ps5sd"]["hf"]["url"], "https://huggingface.co/nerijs/pixel-art-xl", "hf url");
    eq(sc["ps5sd"]["lookup"], "found", "hf lookup found");
    eq(sc["ps5sd"]["images"].size(), 0u, "no widget image in the real card");

    const Json card = Json::parse(R"({"cardData": {"instance_prompt": ["a", "b"], "base_model": ["x/y", "z/w"],
        "widget": [{"text": "t", "output": {"url": "images/one two.png"}}, {"output": {"url": "https://cdn.example.com/p.JPG?x=1"}},
                   {"output": {"url": "/images/three.webp"}}, {"output": {"url": "clip.mp4"}}, {"text": "no output"}]}})");
    Json sc2 = Json::object();
    hf_to_sidecar(card, "o/r", sc2, "t");
    eq(sc2["activation text"], "a, b", "hf prompt list");
    eq(sc2["sd version"], "x/y", "hf first base model");
    eq(sc2["ps5sd"]["images"].size(), 3u, "hf images: png, jpg and webp only");
    eq(sc2["ps5sd"]["images"][0]["url"], "https://huggingface.co/o/r/resolve/main/images/one%20two.png", "hf relative url");
    eq(sc2["ps5sd"]["images"][1]["url"], "https://cdn.example.com/p.JPG?x=1", "hf absolute url");
    eq(sc2["ps5sd"]["images"][2]["url"], "https://huggingface.co/o/r/resolve/main/images/three.webp", "hf url with a leading slash");
    check(!sc2["ps5sd"]["images"][0].contains("nsfw_level"), "hf images have no level");
    eq(pick_preview_image(sc2["ps5sd"]["images"]), 0, "hf preview: the first");
    Json bare = Json::object();
    hf_to_sidecar(Json::parse(R"({"id": "o/r"})"), "o/r", bare, "t");
    eq(bare["ps5sd"]["lookup"], "found", "hf repo without a card is still found");
}

void test_sources() {
    Json sc = { { "ps5sd", { { "source", "https://civitai.com/api/download/models/135931" } } } };
    eq(sidecar_civitai_version(sc), "135931", "version from the source link");
    sc["ps5sd"]["civitai"] = { { "version_id", 77 } };
    eq(sidecar_civitai_version(sc), "77", "version_id first");
    sc["ps5sd"]["civitai"]["version_id"] = "88";
    eq(sidecar_civitai_version(sc), "88", "version_id as a string");
    eq(sidecar_civitai_version(Json::object()), "", "no version");
    eq(sidecar_hf_repo(sc), "", "civitai is no hf source");
    sc["ps5sd"]["source"] = "https://huggingface.co/nerijs/pixel-art-xl/resolve/main/pixel-art-xl.safetensors";
    eq(sidecar_hf_repo(sc), "nerijs/pixel-art-xl", "hf repo from a resolve link");
    sc["ps5sd"]["source"] = "https://huggingface.co/nerijs/pixel-art-xl/blob/main/x.safetensors?download=true";
    eq(sidecar_hf_repo(sc), "nerijs/pixel-art-xl", "hf repo from a blob link");
    sc["ps5sd"]["source"] = "https://huggingface.co/nerijs/pixel-art-xl";
    eq(sidecar_hf_repo(sc), "nerijs/pixel-art-xl", "hf repo from the repo link");
    sc["ps5sd"]["source"] = "https://hf.co/api/models";
    eq(sidecar_hf_repo(sc), "", "no repo in an api link");
    sc["ps5sd"]["source"] = "https://example.com/a/b";
    eq(sidecar_hf_repo(sc), "", "another host");
}

void test_info_routes(const fs::path & root) {
    const std::string dir = (root / "loras").string();
    std::string       out;
    put(root / "loras/style/pixel.safetensors", "weights");
    eq(lora_info_get(dir, "style/pixel.safetensors", out), 200, "info of a LoRA without a file");
    eq(Json::parse(out), Json{ { "path", "style/pixel.safetensors" } }, "just the path");
    eq(lora_info_get(dir, "style/none.safetensors", out), 404, "info: no LoRA");
    eq(Json::parse(out)["error"]["code"], "not_found", "info 404 code");
    eq(lora_info_get(dir, "../etc/passwd.safetensors", out), 400, "info: ..");
    eq(Json::parse(out)["error"]["code"], "invalid_request", "info 400 code");
    eq(lora_info_get(dir, "/etc/x.safetensors", out), 400, "info: absolute");
    eq(lora_info_get(dir, "style/pixel.json", out), 400, "info: not a model file name");
    eq(lora_info_get(dir, "style/.hidden.safetensors", out), 400, "info: hidden");
    eq(lora_info_post(dir, "../x.safetensors", "{}", out), 400, "post: ..");
    eq(lora_info_post(dir, "style/none.safetensors", "{}", out), 404, "post: no LoRA");

    // an A1111 file brings its info; POST merges and keeps the rest
    put(root / "loras/style/pixel.json",
        R"({"description": "d", "sd version": "SDXL", "activation text": "pixel art", "preferred weight": 0.8, "negative text": "blur", "notes": "", "extra": [1]})");
    put(root / "loras/style/pixel.png", "png");
    eq(lora_info_get(dir, "style/pixel.safetensors", out), 200, "info with an A1111 file");
    Json v = Json::parse(out);
    eq(v["trigger"], "pixel art", "trigger");
    eq(v["weight"], 0.8, "weight");
    eq(v["base"], "SDXL", "base");
    eq(v["description"], "d", "description");
    eq(v["preview"], true, "A1111's plain png is the preview");
    check(!v.contains("notes") && !v.contains("civitai") && !v.contains("lookup"), "unknown fields left out");
    eq(lora_info_post(dir, "style/pixel.safetensors", R"({"trigger": " pixel art, 8bit ", "weight": 0.6, "notes": "my note"})", out), 200, "post");
    v = Json::parse(out);
    eq(v["trigger"], "pixel art, 8bit", "trigger trimmed and set");
    eq(v["weight"], 0.6, "weight set");
    eq(v["notes"], "my note", "notes set");
    Json file = Json::parse(slurp(root / "loras/style/pixel.json"));
    eq(file["negative text"], "blur", "negative text kept");
    eq(file["extra"].size(), 1u, "unknown key kept");
    eq(file["description"], "d", "description kept");
    eq(file["activation text"], "pixel art, 8bit", "A1111's key written");
    eq(lora_info_post(dir, "style/pixel.safetensors", R"({"weight": 0})", out), 200, "clear weight with 0");
    check(!Json::parse(out).contains("weight"), "weight 0 is not set");
    eq(Json::parse(slurp(root / "loras/style/pixel.json"))["preferred weight"], 0, "0 in the file");
    lora_info_post(dir, "style/pixel.safetensors", R"({"weight": 1.5})", out);
    lora_info_post(dir, "style/pixel.safetensors", R"({"weight": null})", out);
    check(!Json::parse(out).contains("weight"), "weight null clears it");
    for (const char * bad : { "[]", "3", "", "not json", R"({"weight": "x"})", R"({"trigger": 5})", R"({"notes": null})", R"({"weight": 1000})",
                              R"({"description": ["a"]})" }) {
        eq(lora_info_post(dir, "style/pixel.safetensors", bad, out), 400, std::string("post 400 for ") + bad);
    }
    check(Json::parse(out)["error"]["message"].is_string(), "400 has a message");
    const std::string before = slurp(root / "loras/style/pixel.json");
    eq(lora_info_post(dir, "style/pixel.safetensors", R"({"other": 1})", out), 200, "post without known keys");
    eq(slurp(root / "loras/style/pixel.json"), before, "...leaves the file alone");
    eq(lora_info_post(dir, "style/pixel.safetensors", std::string(R"({"notes": ")") + std::string(30000, 'x') + "\"}", out), 400, "too long");

    // a new sidecar for a LoRA without one
    put(root / "loras/plain.safetensors", "w");
    eq(lora_info_post(dir, "plain.safetensors", R"({"trigger": "go"})", out), 200, "post creates the info file");
    eq(Json::parse(slurp(root / "loras/plain.json")), (Json{ { "activation text", "go" } }), "the new file has just that key");

    // the preview route
    std::string type;
    eq(lora_preview_get(dir, "style/pixel.safetensors", out, type), 200, "preview");
    eq(out, "png", "preview bytes");
    eq(type, "image/png", "preview type");
    put(root / "loras/style/pixel.preview.jpeg", "jpegbytes");
    eq(lora_preview_get(dir, "style/pixel.safetensors", out, type), 200, "preview 2");
    eq(out, "jpegbytes", ".preview. wins");
    eq(type, "image/jpeg", "jpeg type");
    eq(lora_preview_get(dir, "plain.safetensors", out, type), 404, "no preview");
    eq(type, "application/json", "404 is JSON");
    eq(lora_preview_get(dir, "gone.safetensors", out, type), 404, "preview: no LoRA");
    eq(lora_preview_get(dir, "../x.safetensors", out, type), 400, "preview: ..");

    // delete: the info file and the .preview. files, not A1111's png
    remove_lora_extras((root / "loras/style/pixel.safetensors").string());
    check(!fs::exists(root / "loras/style/pixel.json") && !fs::exists(root / "loras/style/pixel.preview.jpeg"), "extras removed");
    check(fs::exists(root / "loras/style/pixel.png") && fs::exists(root / "loras/style/pixel.safetensors"), "the png and the LoRA stay");
}

void test_lookup(const fs::path & root) {
    const std::string dir = (root / "lk").string();
    std::string       out;
    const std::string weights = "pretend these are the weights of Pixel Art XL";
    put(root / "lk/pixel-art-xl.safetensors", weights);
    const std::string sha = sha256_hex(weights);
    const std::string by_hash = kByHash + sha;
    const std::string jpeg = "\xFF\xD8\xFFjpegdata";

    eq(lora_lookup(dir, "gone.safetensors", Fake().io(), out), 404, "lookup: no LoRA");
    eq(Json::parse(out)["error"]["code"], "no_lora", "lookup: no_lora code");
    eq(lora_lookup(dir, "../x.safetensors", Fake().io(), out), 400, "lookup: ..");
    eq(lora_lookup(dir, "/x.safetensors", Fake().io(), out), 400, "lookup: absolute");

    // 1. nothing known: the hash, a miss
    Fake none;
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", none.io(), out), 404, "lookup: not on Civitai");
    eq(Json::parse(out)["error"]["code"], "not_found", "lookup: not_found code");
    eq(Json::parse(out)["error"]["message"], "Civitai does not know this file", "lookup: message");
    check(none.count(by_hash) == 1 && none.asked.size() == 1, "lookup: asked by hash only");
    Json sc = Json::parse(slurp(root / "lk/pixel-art-xl.json"));
    eq(sc["ps5sd"]["lookup"], "not_found", "not_found kept in the sidecar");
    eq(sc["ps5sd"]["sha256"], sha, "hash kept");
    eq(sc["ps5sd"]["sha256_size"], weights.size(), "hash size kept");
    check(sc["ps5sd"]["looked_up"].is_string(), "looked_up kept");
    out.clear();
    lora_info_get(dir, "pixel-art-xl.safetensors", out);
    eq(Json::parse(out)["lookup"], "not_found", "info shows not_found");

    // 2. found by hash (the hash is not computed again; the answer's version id is kept)
    Fake net;
    net.text[by_hash]                                      = { 200, kVersion };
    net.text["https://civitai.com/api/v1/models/120096"]   = { 200, kModel };
    net.images[kThumb0]                                    = { "image/jpeg", jpeg };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", net.io(), out), 200, "lookup: found by hash");
    Json v = Json::parse(out);
    eq(v["lookup"], "found", "found");
    eq(v["base"], "SDXL 1.0", "base");
    eq(v["civitai"]["version_id"], 135931, "version id");
    eq(v["sha256"], sha, "sha256 in the view");
    eq(v["preview"], true, "preview downloaded");
    eq(v["preview_nsfw_level"], 1, "preview level");
    eq(v["images"].size(), 4u, "images");
    check(v["description"].get<std::string>().rfind("Pixel Art XL", 0) == 0, "description");
    eq(slurp(root / "lk/pixel-art-xl.preview.jpeg"), jpeg, "preview file");
    sc = Json::parse(slurp(root / "lk/pixel-art-xl.json"));
    eq(sc["ps5sd"]["preview"], "pixel-art-xl.preview.jpeg", "preview name in the sidecar");
    eq(sc["description"].get<std::string>().rfind("Pixel Art XL", 0), 0u, "A1111 key description filled");
    eq(sc["sd version"], "SDXL 1.0", "A1111 key sd version filled");
    eq(net.count(by_hash), 1, "by-hash asked once");
    eq(net.count(kThumb0), 1, "first image fetched");

    // 3. again: the version id is known now (no hash, no by-hash), the preview stays
    Fake again;
    again.text["https://civitai.com/api/v1/model-versions/135931"] = { 200, kVersion };
    again.text["https://civitai.com/api/v1/models/120096"]         = { 200, kModel };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", again.io(), out), 200, "lookup again");
    eq(again.count(by_hash), 0, "the version is asked for by its id");
    eq(again.count(kThumb0), 0, "the preview is not fetched again");
    eq(Json::parse(out)["preview"], true, "preview still there");

    // 4. the user's own text survives a lookup
    lora_info_post(dir, "pixel-art-xl.safetensors", R"({"trigger": "my trigger", "description": "my text", "notes": "mine"})", out);
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", again.io(), out), 200, "lookup after edits");
    v = Json::parse(out);
    eq(v["trigger"], "my trigger", "trigger kept");
    eq(v["description"], "my text", "description kept");
    eq(v["notes"], "mine", "notes kept");

    // 5. the hash is cached by size and mtime, and computed again when they change
    fs::remove(root / "lk/pixel-art-xl.json");
    fs::remove(root / "lk/pixel-art-xl.preview.jpeg");
    Fake h1;  // a first hash lookup that finds nothing
    lora_lookup(dir, "pixel-art-xl.safetensors", h1.io(), out);
    sc = Json::parse(slurp(root / "lk/pixel-art-xl.json"));
    sc["ps5sd"]["sha256"] = "FAKEHASH" + std::string(56, '0');  // a stored hash of the right length: used while size and mtime match
    std::ofstream(root / "lk/pixel-art-xl.json") << sc.dump();
    Fake h2;
    lora_lookup(dir, "pixel-art-xl.safetensors", h2.io(), out);
    check(h2.count(kByHash + sc["ps5sd"]["sha256"].get<std::string>()) == 1, "the stored hash is used while size and mtime fit");
    put(root / "lk/pixel-art-xl.safetensors", weights + "!");  // a new size
    Fake h3;
    lora_lookup(dir, "pixel-art-xl.safetensors", h3.io(), out);
    check(h3.count(kByHash + sha256_hex(weights + "!")) == 1, "a changed file is hashed again");
    put(root / "lk/pixel-art-xl.safetensors", weights);

    // 6. a user's own preview is not replaced
    fs::remove(root / "lk/pixel-art-xl.json");
    put(root / "lk/pixel-art-xl.png", "mine");
    Fake keep = net;
    keep.asked.clear();
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", keep.io(), out), 200, "lookup with an A1111 preview");
    eq(keep.count(kThumb0), 0, "no image fetched");
    check(!fs::exists(root / "lk/pixel-art-xl.preview.jpeg"), "no second preview");
    eq(slurp(root / "lk/pixel-art-xl.png"), "mine", "the user's preview untouched");
    fs::remove(root / "lk/pixel-art-xl.png");
    fs::remove(root / "lk/pixel-art-xl.json");

    // 7. unreachable Civitai: 502, and the sidecar is not marked not_found
    Fake down;
    down.unreachable = by_hash;
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", down.io(), out), 502, "lookup: unreachable");
    eq(Json::parse(out)["error"]["code"], "bad_gateway", "502 code");
    check(Json::parse(out)["error"]["message"].get<std::string>().find("connection refused") != std::string::npos, "502 names the reason");
    check(!Json::parse(slurp(root / "lk/pixel-art-xl.json"))["ps5sd"].contains("lookup"), "an unreachable server is not 'not_found'");
    Fake sick;
    sick.text[by_hash] = { 503, "" };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", sick.io(), out), 502, "lookup: HTTP 503");
    Fake locked;
    locked.text[by_hash] = { 401, "{}" };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", locked.io(), out), 502, "lookup: HTTP 401");
    check(Json::parse(out)["error"]["message"].get<std::string>().find("token") != std::string::npos, "401 mentions the token");
    Fake junk;
    junk.text[by_hash] = { 200, "<html>" };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", junk.io(), out), 502, "lookup: not JSON");

    // 8. the app is ending
    Fake stopping;
    stopping.stop = true;
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", stopping.io(), out), 503, "lookup: stopping");
    eq(Json::parse(out)["error"]["code"], "stopping", "stopping code");

    // 9. a download wrote the source: Civitai by version id; 404 there falls back to the hash
    fs::remove(root / "lk/pixel-art-xl.json");
    std::string error;
    check(note_lora_source((root / "lk/pixel-art-xl.safetensors").string(), "https://civitai.com/api/download/models/135931", "135931", error),
          "note_lora_source: " + error);
    sc = Json::parse(slurp(root / "lk/pixel-art-xl.json"));
    eq(sc["ps5sd"]["source"], "https://civitai.com/api/download/models/135931", "source written");
    eq(sc["ps5sd"]["civitai"]["version_id"], 135931, "version id written as a number");
    Fake byid;
    byid.text["https://civitai.com/api/v1/model-versions/135931"] = { 200, kVersion };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", byid.io(), out), 200, "lookup by the downloaded version");
    eq(byid.asked[0], "https://civitai.com/api/v1/model-versions/135931", "first the version");
    eq(Json::parse(out)["source"], "https://civitai.com/api/download/models/135931", "source in the view");
    check(!Json::parse(out).contains("description"), "model page missing: no description, still found");
    check(Json::parse(out)["preview"].is_null(), "no image to fetch from this fake: no preview");
    Fake gone;  // the version is gone from Civitai; the file is known by its hash
    fs::remove(root / "lk/pixel-art-xl.json");
    note_lora_source((root / "lk/pixel-art-xl.safetensors").string(), "https://civitai.com/api/download/models/999", "999", error);
    gone.text[by_hash] = { 200, kVersion };
    eq(lora_lookup(dir, "pixel-art-xl.safetensors", gone.io(), out), 200, "lookup: version 404, then the hash");
    eq(gone.asked[0], "https://civitai.com/api/v1/model-versions/999", "version first");
    eq(Json::parse(out)["civitai"]["version_id"], 135931, "the hash's version wins");

    // 10. Hugging Face
    put(root / "lk/hf/pixel-art-xl.safetensors", "hf weights");
    note_lora_source((root / "lk/hf/pixel-art-xl.safetensors").string(),
                     "https://huggingface.co/nerijs/pixel-art-xl/resolve/main/pixel-art-xl.safetensors", "", error);
    Fake hf;
    hf.text["https://huggingface.co/api/models/nerijs/pixel-art-xl"] = { 200, kHf };
    eq(lora_lookup(dir, "hf/pixel-art-xl.safetensors", hf.io(), out), 200, "lookup: Hugging Face");
    v = Json::parse(out);
    eq(v["trigger"], "pixel art", "hf trigger");
    eq(v["base"], "stabilityai/stable-diffusion-xl-base-1.0", "hf base");
    eq(v["hf"]["repo"], "nerijs/pixel-art-xl", "hf repo");
    eq(v["lookup"], "found", "hf found");
    eq(hf.asked.size(), 1u, "hf: one request, no hash");
    check(!v.contains("preview"), "hf card without images: no preview");
    // a private repository (401) is not fatal: the hash is tried
    Fake priv;
    priv.text["https://huggingface.co/api/models/nerijs/pixel-art-xl"] = { 401, "{}" };
    eq(lora_lookup(dir, "hf/pixel-art-xl.safetensors", priv.io(), out), 404, "lookup: private repo, unknown hash");
    eq(priv.asked.size(), 2u, "hf then hash");
    Fake hfdown;
    hfdown.unreachable = "https://huggingface.co/api/models/nerijs/pixel-art-xl";
    eq(lora_lookup(dir, "hf/pixel-art-xl.safetensors", hfdown.io(), out), 502, "lookup: Hugging Face unreachable");

    // 11. a preview that is not a usable image, or too big, is skipped; the lookup still succeeds
    put(root / "lk/video.safetensors", "v");
    Fake odd;
    odd.text[kByHash + sha256_hex("v")]                     = { 200, kVersion };
    odd.images[kThumb0]                                     = { "image/avif", "x" };
    eq(lora_lookup(dir, "video.safetensors", odd.io(), out), 200, "lookup with an avif preview");
    check(!fs::exists(root / "lk/video.preview.avif") && Json::parse(out)["preview"].is_null(), "no preview from an avif");
    odd.images[kThumb0] = { "image/png", std::string((4u << 20) + 1, 'x') };
    eq(lora_lookup(dir, "video.safetensors", odd.io(), out), 200, "lookup with a 4 MB+ preview");
    check(Json::parse(out)["preview"].is_null(), "no preview over 4 MB");
    odd.images[kThumb0] = { "image/webp", "RIFFxxxxWEBP" };
    eq(lora_lookup(dir, "video.safetensors", odd.io(), out), 200, "lookup with a webp preview");
    eq(Json::parse(out)["preview"], true, "webp preview kept");
    check(fs::exists(root / "lk/video.preview.webp"), "webp file");
}

void test_tokens(const fs::path & root) {
    const std::string hf = (root / "hf-token.txt").string(), civ = (root / "civitai-token.txt").string();
    std::string       out;
    eq(tokens_get(hf, civ, out), 200, "tokens get");
    eq(Json::parse(out), (Json{ { "huggingface", false }, { "civitai", false } }), "no tokens");
    eq(tokens_post(hf, civ, R"({"civitai": "  abc-DEF_1.2  "})", out), 200, "tokens post");
    eq(Json::parse(out), (Json{ { "huggingface", false }, { "civitai", true } }), "civitai set");
    eq(slurp(civ), "abc-DEF_1.2\n", "token file (trimmed)");
    check(out.find("abc") == std::string::npos, "the token is never sent back");
    eq(tokens_post(hf, civ, R"({"huggingface": "hf_xyz", "civitai": ""})", out), 200, "both at once");
    eq(Json::parse(out), (Json{ { "huggingface", true }, { "civitai", false } }), "hf set, civitai deleted");
    check(!fs::exists(civ), "empty deletes the file");
    eq(tokens_post(hf, civ, R"({"civitai": ""})", out), 200, "deleting a missing token is fine");
    for (const char * bad : { "{}", "[]", "x", R"({"civitai": 5})", R"({"civitai": "a b"})", R"({"civitai": "a/b"})", R"({"huggingface": "ok", "civitai": "bad token!"})",
                              R"({"civitai": ")" }) {
        eq(tokens_post(hf, civ, bad, out), 400, std::string("tokens 400 for ") + bad);
    }
    eq(tokens_post(hf, civ, std::string(R"({"civitai": ")") + std::string(201, 'a') + "\"}", out), 400, "token over 200");
    eq(slurp(hf), "hf_xyz\n", "a refused body changes nothing");
    check((fs::status(hf).permissions() & (fs::perms::group_read | fs::perms::others_read)) == fs::perms::none, "token file mode 0600");
    put(root / "empty-token.txt", "\n");
    eq(tokens_get((root / "empty-token.txt").string(), civ, out), 200, "empty file");
    eq(Json::parse(out)["huggingface"], false, "an empty token file is no token");
}

void test_list_extras() {
    eq(lora_list_extras(Json::object(), false, false), Json::object(), "extras: nothing");
    eq(lora_list_extras(Json::object(), false, true), (Json{ { "preview", true } }), "extras: preview only");
    eq(lora_list_extras(Json{ { "ps5sd", { { "preview_nsfw_level", 4 } } } }, true, true),
       (Json{ { "preview", true }, { "preview_nsfw_level", 4 }, { "info", true } }), "extras: the preview's level");
    eq(lora_list_extras(Json{ { "ps5sd", { { "preview_nsfw_level", 4 } } } }, true, false), (Json{ { "info", true } }),
       "extras: no level without a preview");
    const Json sc = { { "activation text", " t " }, { "preferred weight", "0.5" }, { "sd version", "Unknown" },
                      { "ps5sd", { { "civitai", { { "base_model", "Pony" } } }, { "lookup", "not_found" } } } };
    const Json e  = lora_list_extras(sc, true, false);
    eq(e["trigger"], "t", "extras trigger");
    eq(e["weight"], 0.5, "extras weight from a string");
    eq(e["base"], "Pony", "extras base from civitai when sd version is Unknown");
    eq(e["info"], true, "extras info");
    eq(e["lookup"], "not_found", "extras lookup");
    check(!e.contains("preview"), "extras: no preview");
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("ps5sd-lora-info-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    test_sha256();
    test_sha256_file(root);
    test_names();
    test_html();
    test_civitai();
    test_hf();
    test_sources();
    test_list_extras();
    test_info_routes(root);
    test_lookup(root);
    test_tokens(root);
    fs::remove_all(root);
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("lora_info: all checks passed\n");
    return 0;
}
