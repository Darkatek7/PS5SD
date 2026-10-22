// The downloader's pure parts: which URLs it takes and how it rewrites them,
// where a file may land, the starter catalog, and the JSON of the queue and
// of the routes. No sockets and no TLS in here, so ps5/app/tests builds it on
// the PC (test_downloads.cpp); downloader.cpp does the network side.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ps5sd {

// https://host[:port]/path?query, split. Only https and http.
struct Url {
    std::string scheme;  // "https" or "http"
    std::string host;    // lower case
    int         port = 0;
    std::string target;  // "/path?query", never empty
};

bool parse_url(const std::string & text, Url & out);
std::string url_string(const Url & u);
// A Location header against the URL it came from (absolute, or relative to its host).
std::string resolve_location(const Url & base, const std::string & location);

// Where a download comes from, after the rewrite.
struct Source {
    std::string kind;       // "hf" or "civitai"
    std::string url;        // what is fetched: the HF resolve URL, or Civitai's API download URL
    std::string file_name;  // the name the URL gives (HF: the path's last part), "" for Civitai
    std::string hf_repo;    // owner/repo
    std::string civitai_version;  // the model version id
    std::string civitai_file;     // fileId= of an api/download URL, or ""
};

// Takes https://huggingface.co/<owner>/<repo>/resolve/<rev>/<path> (or /blob/,
// rewritten to /resolve/), https://civitai.com/models/<id>[/<slug>]?modelVersionId=<v>
// and https://civitai.com/api/download/models/<v>[?...]. False with a message otherwise.
bool classify_url(const std::string & text, Source & out, std::string & error);

// The model file extensions that may land in the models directory.
bool is_model_file_name(const std::string & name);

// The upscaler (ESRGAN) file extensions sd-server reads: .pth, .pt,
// .safetensors, .gguf.
bool is_upscaler_file_name(const std::string & name);

// A destination relative to the models directory: no absolute path, no "..",
// no empty or dot segment, no backslash or control character, a model file
// extension. False with a message otherwise.
bool valid_dest(const std::string & dest, std::string & error);

// An upload's target, from "<kind>/<name>" (PUT /ps5sd/v1/app/uploads/...).
// kind "models" takes a name as valid_dest does (folders allowed); "loras"
// and "embeddings" a model file name without folders; "presets" a file name
// ending in .txt; "upscalers" an upscaler file name (is_upscaler_file_name)
// without folders. False with a message otherwise.
bool upload_target(const std::string & path, std::string & kind, std::string & name, std::string & error);

// Civitai's model types that are LoRAs (LORA, LoCon, DoRA): such a download
// goes to the loras folder.
bool is_lora_type(const std::string & civitai_type);

// A download's target: kind "models" takes dest as valid_dest does, "loras"
// and "upscalers" a file name without folders (as upload_target). The id of
// a LoRA or an upscaler is made from "<kind>/<dest>", so it differs from a
// model of the same name.
bool valid_target(const std::string & kind, const std::string & dest, std::string & error);
std::string download_key(const std::string & kind, const std::string & dest);

// A gallery file (a PNG or a video) name (DELETE /ps5sd/v1/app/gallery/<name>):
// letters, digits, '_', '-' and '.', ending in .png, .webm, .webp or .avi, as
// the gallery lists them, not starting with '.' and without folders.
bool gallery_name_ok(const std::string & name);

// A stable id for a destination (16 hex digits): the same file is one entry.
std::string download_id(const std::string & dest);

// The file name of a Content-Disposition header ("attachment; filename=\"a.safetensors\""
// or filename*=UTF-8''a%20b.safetensors), "" when it has none. Never a path.
std::string content_disposition_name(const std::string & header);

// "bytes 0-0/12345" -> 12345; -1 when it does not say.
int64_t content_range_total(const std::string & header);

// ---- the starter catalog (catalog.hpp holds the data) ------------------------

struct CatalogFile {
    const char * url;
    const char * dest;   // relative to the models directory
    uint64_t     bytes;
    uint64_t     alt_bytes;  // another size that counts as this file (the same weights elsewhere), or 0
};

struct CatalogEntry {
    const char *             id;  // also the preset's name: presets/<id>.txt
    const char *             name;
    const char *             description;
    std::vector<CatalogFile> files;
    const char *             preset;  // the preset's text, written once every file is there
    bool                     featured;  // shown first by the web UI
    const char *             good_for;  // "Good for: ..." on a featured card, or ""
};

const std::vector<CatalogEntry> & catalog();
const CatalogEntry *              find_catalog(const std::string & id);

// True when the file is in the models directory at its size (or alt_bytes).
bool catalog_file_present(const CatalogFile & f, const std::string & models_dir);

// Gives each catalog preset in presets_dir that is still byte for byte one an older PS5SD wrote
// (catalog_data::old_presets) the entry's current text. Returns the ids it updated.
std::vector<std::string> update_catalog_presets(const std::string & presets_dir);
// True when every file of the entry is.
bool catalog_files_present(const CatalogEntry & e, const std::string & models_dir);

// GET catalog: {"entries": [{"id", "name", "description", "bytes", "installed", "featured", "good_for",
// "files": [{"url", "dest", "bytes", "installed"}], "preset"}]}; an entry is
// installed when every file is there at its size and the preset exists.
std::string catalog_json(const std::string & models_dir, const std::string & presets_dir);

// ---- the queue -----------------------------------------------------------------

struct DownloadRecord {
    std::string id;
    std::string url;      // as asked for (after the blob -> resolve rewrite)
    std::string dest;     // relative to the folder of kind
    std::string kind = "models";  // "models", "loras" or "upscalers"
    std::string catalog;  // the catalog entry it belongs to, or ""
    std::string state;    // queued, running, done, failed, cancelled
    std::string error;
    uint64_t    bytes = 0;  // done
    uint64_t    total = 0;  // 0 while not known
    double      bytes_per_second = 0;
    // Resume: the piece size and which pieces are on disk ('1') in <dest>.part.
    uint64_t    piece = 0;
    std::string pieces;
};

// The object of GET downloads for one record.
std::string download_json(const DownloadRecord & d);
// {"downloads": [...]}.
std::string downloads_json(const std::vector<DownloadRecord> & list);

// downloads.json: the records with their piece maps. A record that was
// running when the app stopped comes back queued; one without a valid dest is
// dropped.
std::string queue_to_json(const std::vector<DownloadRecord> & list);
std::vector<DownloadRecord> queue_from_json(const std::string & text);

// The bytes done according to a piece map.
uint64_t pieces_bytes(const std::string & pieces, uint64_t piece, uint64_t total);

// Whether every piece the map marks done lies inside a .part of file_size
// bytes (the file grows as pieces land; a shorter one lost some).
bool pieces_within(const std::string & pieces, uint64_t piece, uint64_t total, uint64_t file_size);

// For a .part written in file order (through ftpsrv): the map with only its
// leading done pieces that lie wholly inside file_size bytes; the rest is
// fetched again, and the next write cuts the file at the first of them.
std::string pieces_in_order(const std::string & pieces, uint64_t piece, uint64_t total, uint64_t file_size);

// ---- request bodies and Hugging Face's answers -----------------------------------

struct DownloadRequest {
    std::string url, dest, catalog;
    std::string kind;  // "models", "loras", "upscalers", or "" (models; Civitai: from the model's type)
};
bool parse_download_request(const std::string & body, DownloadRequest & out, std::string & error);

struct SearchRequest {
    std::string q;
    int         limit = 20;
};
bool parse_search_request(const std::string & body, SearchRequest & out, std::string & error);

struct SearchFile {
    std::string path;
    uint64_t    bytes = 0;
};
struct SearchResult {
    std::string             repo;
    uint64_t                downloads = 0;
    uint64_t                likes     = 0;
    std::vector<SearchFile> files;
};

// /api/models?search=...&expand[]=siblings: the repositories with at least one
// model file, in order.
std::vector<SearchResult> parse_hf_search(const std::string & json);
// /api/models/<repo>?blobs=true: its model files with their sizes (into r.files).
void parse_hf_model_files(const std::string & json, SearchResult & r);
// {"results": [{"repo", "downloads", "likes", "files": [{"path", "bytes", "url"}]}]},
// url being the file's resolve URL on the main branch.
std::string search_json(const std::vector<SearchResult> & results);

// Civitai's /api/v1/model-versions/<v>: the file to fetch (the one with
// file_id when given, else the primary model file): its name, its download
// URL and its size (from sizeKB, rounded; the exact one comes from the
// download), and the model's type (model.type: Checkpoint, LORA, ...) when
// model_type is given. False with a message when there is none.
bool parse_civitai_version(const std::string & json, const std::string & file_id, std::string & name,
                           std::string & url, uint64_t & bytes, std::string & error,
                           std::string * model_type = nullptr);

std::string url_encode(const std::string & s);

}  // namespace ps5sd
