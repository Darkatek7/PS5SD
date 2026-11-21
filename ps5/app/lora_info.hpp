// What the title knows about a LoRA: a sidecar file next to it, <stem>.json, with AUTOMATIC1111's keys
// ("description", "sd version", "activation text", "preferred weight", "negative text", "notes"), so a
// LoRA folder copied from A1111 brings its info along and A1111 reads ours. Our own part is the key
// "ps5sd": where the file came from, its SHA-256, what Civitai or Hugging Face said, the preview.
// A preview image is <stem>.preview.<png|jpeg|jpg|webp> (A1111's name too); a <stem>.png, .jpg, .jpeg or
// .webp from A1111 counts as one as well.
//
// This file holds the whole logic of the LoRA info routes: reading, merging and writing the sidecar
// (unknown keys stay), the views the web UI gets, what Civitai's and Hugging Face's JSON fill in, the
// lookup itself (the network is passed in as LookupIo, so ps5/app/tests runs it against fakes), HTML to
// plain text, and a SHA-256. No console code and no TLS, so ps5/app/tests builds it on the PC
// (test_lora_info.cpp). Nothing in the *_get, *_post and lora_lookup functions throws.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "json.hpp"

namespace ps5sd {

using Json = nlohmann::json;

// ---- names -------------------------------------------------------------------------------------

// "style/a.b.safetensors" -> "style/a.b" (the last extension goes, as A1111 does it).
std::string lora_stem(const std::string & file);
// "<stem>.json".
std::string sidecar_path(const std::string & file);
// "<stem>.preview.<ext>".
std::string preview_path(const std::string & file, const std::string & ext);
// The preview of a LoRA file ("" when there is none): <stem>.preview.png|jpeg|jpg|webp, then
// <stem>.png|jpg|jpeg|webp. `exists` says whether a path is a file (what the caller can ask cheaply).
std::string find_preview(const std::string & file, const std::function<bool(const std::string &)> & exists);
// The same against the disk.
std::string find_preview_on_disk(const std::string & file);
// "image/png" for a .png and so on; "" for a name that is not an image the web UI shows.
std::string preview_content_type(const std::string & path);
// "png", "jpeg" or "webp" for a Content-Type (parameters allowed); "" for any other.
std::string preview_extension(const std::string & content_type);
// Deletes the sidecar and every <stem>.preview.* of a LoRA file (not A1111's <stem>.png).
void remove_lora_extras(const std::string & file);

// ---- the sidecar ------------------------------------------------------------------------------

// The sidecar as an object; {} when there is none or it is not JSON (or not an object).
Json read_sidecar(const std::string & file);
// Written whole (a .tmp, fsync, rename).
bool write_sidecar(const std::string & file, const Json & sidecar, std::string & error);
// Reads, lets fn change it and writes it, under one lock: two requests never lose each other's change.
bool update_sidecar(const std::string & file, const std::function<void(Json &)> & fn, std::string & error);

// The fields GET loras adds to a LoRA's entry (only those that are known): "trigger", "weight", "base",
// "preview", "info", "lookup". has_sidecar says whether the file exists.
Json lora_list_extras(const Json & sidecar, bool has_sidecar, bool has_preview);
// The object of GET loras/info/<path>: {"path", "trigger", "weight", "description", "notes", "base",
// "source", "sha256", "civitai", "hf", "images", "preview", "preview_nsfw_level", "looked_up",
// "lookup"}; what is not known is left out.
Json lora_info_view(const std::string & path, const Json & sidecar, bool has_preview);
// POST loras/info/<path>: {"trigger", "weight", "notes", "description"} merged into the sidecar. Other
// keys are ignored. `changed` says whether anything was in the body. False with a message for a body
// that is not an object or a value of the wrong type (weight: a number up to 100, or null or 0 to clear).
bool apply_info_post(Json & sidecar, const Json & body, bool & changed, std::string & error);

// ---- Civitai and Hugging Face -------------------------------------------------------------------

// Civitai's model version (/api/v1/model-versions/<id> or .../by-hash/<sha>) and, when there is one, its
// model (/api/v1/models/<modelId>: the description) into the sidecar. The user's own "activation text",
// "description" and "sd version" stay; "ps5sd" is refreshed (civitai, images, lookup "found", looked_up).
// model may be null.
void civitai_to_sidecar(const Json & version, const Json & model, Json & sidecar, const std::string & looked_up);
// Hugging Face's /api/models/<repo>: cardData.instance_prompt, cardData.base_model, the widget's images.
void hf_to_sidecar(const Json & model, const std::string & repo, Json & sidecar, const std::string & looked_up);

// HTML as plain text: tags out (a paragraph, heading, list item or <br> ends a line), entities decoded,
// blank lines kept (never more than one), at most max_chars characters (an "…" ends a cut text).
std::string html_to_text(const std::string & html, size_t max_chars = 2000);
// Civitai's ".../original=true/x.jpeg" as the small ".../width=450/x.jpeg".
std::string civitai_thumb_url(const std::string & url);
// The image to keep as the preview: the first whose "nsfw_level" is at most 2 (none given counts as 1),
// else the first. -1 when there are none.
int pick_preview_image(const Json & images);
// 2026-11-21T22:00:00Z
std::string iso_utc(int64_t unix_time);

// The Civitai version a sidecar knows: ps5sd.civitai.version_id, else the one in ps5sd.source's link.
std::string sidecar_civitai_version(const Json & sidecar);
// The Hugging Face repository (owner/repo) of ps5sd.source, "" when it is not a Hugging Face link.
std::string sidecar_hf_repo(const Json & sidecar);

// A LoRA download finished: the sidecar gets ps5sd.source (the link it came from) and, for a Civitai link,
// ps5sd.civitai.version_id ("" for another link). Creates the sidecar.
bool note_lora_source(const std::string & file, const std::string & source, const std::string & civitai_version,
                      std::string & error);

// ---- SHA-256 -----------------------------------------------------------------------------------

class Sha256 {
  public:
    Sha256();
    void        update(const void * data, size_t n);
    // Upper case hex; the object is spent afterwards.
    std::string finish();

  private:
    void     block(const unsigned char * p);
    uint32_t h_[8];
    unsigned char buf_[64];
    size_t   buffered_ = 0;
    uint64_t total_    = 0;
};

// Upper case hex of a whole string.
std::string sha256_hex(const std::string & data);
// Of a file, in 1 MiB reads. cancel (optional) is asked between reads. False with a message otherwise.
bool sha256_file(const std::string & path, std::string & hex, std::string & error,
                 const std::function<bool()> & cancel = nullptr);

// ---- the routes ---------------------------------------------------------------------------------

// The network, as the lookup needs it. The title's HTTPS client in downloader.cpp; fakes in the tests.
struct LookupIo {
    // GET url, a whole small body (a JSON); true with the HTTP status, false with a message when the
    // server could not be reached.
    std::function<bool(const std::string & url, int & status, std::string & body, std::string & error)> get_text;
    // GET an image of at most 4 MB; true with its bytes and Content-Type only for an HTTP 200.
    std::function<bool(const std::string & url, std::string & content_type, std::string & bytes,
                       std::string & error)>
        get_image;
    // The app is ending: stop (optional).
    std::function<bool()> cancelled;
};

// Each takes the LoRA folder and the LoRA's path in it, answers the HTTP status and fills `out` with the
// JSON ({"error": {"message", "code"}} as every route of the app answers an error).
//
// GET loras/info/<path>: 200 as lora_info_view, 400 "invalid_request", 404 "not_found".
int lora_info_get(const std::string & dir, const std::string & rel, std::string & out);
// POST loras/info/<path>: 200 as GET, 400 for a bad path or body, 404 "not_found".
int lora_info_post(const std::string & dir, const std::string & rel, const std::string & body, std::string & out);
// GET loras/preview/<path>: 200 with the image's bytes in out and its Content-Type; else a JSON error and
// "application/json" (404 "not_found", 413 for one over 16 MiB).
int lora_preview_get(const std::string & dir, const std::string & rel, std::string & out, std::string & content_type);
// POST loras/lookup/<path>: asks Civitai (the version the sidecar names, else the file's SHA-256) or
// Hugging Face (ps5sd.source) and fills the sidecar. 200 as GET info with "lookup": "found"; 404
// "not_found" when they do not know the file (kept in the sidecar as lookup "not_found"); 404 "no_lora"
// when the file is not there; 400 "invalid_request"; 502 when a server cannot be reached; 503 "stopping".
int lora_lookup(const std::string & dir, const std::string & rel, const LookupIo & io, std::string & out);

// POST/GET tokens: the token files of Hugging Face and Civitai.
// GET gives {"huggingface": bool, "civitai": bool}; POST takes {"civitai": "<token>"} and/or
// {"huggingface": "<token>"} (trimmed; at most 200 characters of letters, digits and "-_."; "" deletes the
// file) and answers as GET; 400 otherwise.
int tokens_get(const std::string & hf_file, const std::string & civitai_file, std::string & out);
int tokens_post(const std::string & hf_file, const std::string & civitai_file, const std::string & body,
                std::string & out);

}  // namespace ps5sd
