// The model library: what the server can load and which one it loads.
//
// Two kinds of entries:
//   - a checkpoint file directly in /data/PS5SD/models (.gguf, .safetensors,
//     .ckpt, .sft): loaded with -m, its id is the file name;
//   - a preset, /data/PS5SD/presets/<id>.txt: sd-server arguments, one a line
//     like server-args.txt, for models made of several files (SDXL with its
//     VAE, FLUX with a text encoder and a VAE). Comment lines name it and give
//     the settings a client should start from:
//
//       # name: SDXL Base 1.0 (Q8_0)
//       # defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a
//       -m
//       stable-diffusion-xl-base-1.0-Q8_0.gguf
//       --vae
//       vae/sdxl_vae_fp16_fix.safetensors
//
//     A relative path after a model option is taken from the models
//     directory, so components can sit in subdirectories (vae/,
//     text_encoders/, diffusion_models/) without showing up as checkpoints.
//
//     "# prompt: <text>" and "# negative: <text>" are prompt starters: the
//     server puts them before the prompt and the negative prompt of every
//     request while the preset is loaded (once: not before a prompt that
//     starts with them already), as Pony models want "score_9, score_8_up,
//     ...". A variant made by "Save as preset" takes its base's.
//
//     "# base: <id>" makes a preset a variant of another entry: the same
//     model with other settings, as the web UI's "Save as preset" writes it.
//     A variant is settings on a model, never a model of its own: the model
//     lists of the APIs leave it out. Deleting a variant deletes only its
//     file, never a model file; deleting its base deletes the variants with
//     it.
//
//     The settings of the loaded preset ("# defaults:": steps, cfg, sampler,
//     scheduler, width, height) are the server's defaults for requests that do
//     not name them. The application sets them at run time (preset_settings,
//     sd_server_set_request_defaults, patches/sdcpp/0063), over those of
//     server-args.txt, so switching between presets of one model needs no
//     restart.
//
// /data/PS5SD/model.txt says which entry is active: an id, or (as before
// presets) the absolute path of a checkpoint.
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ps5sd {

struct ModelEntry {
    std::string                                      id;
    std::string                                      name;
    std::string                                      kind;  // "file" or "preset"
    std::vector<std::string>                         args;  // sd-server arguments, paths absolute
    std::vector<std::string>                         files;    // the files the args name
    std::vector<std::string>                         missing;  // those of them that are not there
    uint64_t                                         bytes = 0;  // their size together
    std::vector<std::pair<std::string, std::string>> defaults;   // from "# defaults:", in order
    std::string                                      base;  // "# base:": the entry this preset is a variant of
    std::string                                      prompt_starter;    // "# prompt:"
    std::string                                      negative_starter;  // "# negative:"
};

struct ModelLibrary {
    std::string models_dir;   // /data/PS5SD/models
    std::string presets_dir;  // /data/PS5SD/presets

    // Every entry, presets first, each group sorted by id.
    std::vector<ModelEntry> list() const;

    // The entry for an id, a file name in models_dir or an absolute path to a
    // checkpoint; false with a message for one that is not there.
    bool find(const std::string & id, ModelEntry & out, std::string & error) const;

    // Deletes an entry: a preset's file with the files it names, or a
    // checkpoint. A file another preset names, or one in `keep` (the files of
    // the model the server runs), stays and is listed in `kept`; a checkpoint
    // that a preset names is refused. Only files inside models_dir are
    // deleted. A preset whose files are missing can be deleted too.
    bool remove(const std::string & id, const std::vector<std::string> & keep, std::vector<std::string> & deleted,
                std::vector<std::string> & kept, std::string & error) const;

    // A preset's entry from its text (one argument a line, "# key: value"
    // comments); exposed for the tests.
    ModelEntry parse_preset(const std::string & id, const std::string & text) const;

    // A new variant preset of the entry base_id (the root entry, when base_id
    // is a variant itself): its model arguments, the name and the defaults
    // (keys as in "# defaults:": steps, cfg, width, height, sampler,
    // scheduler, loras, hires_scale, hires_steps, hires_denoise,
    // hires_upscaler). The id comes from the name. False with a message
    // for a name, a default or a base that does not do.
    bool create_preset(const std::string & name, const std::string & base_id,
                       const std::vector<std::pair<std::string, std::string>> & defaults, std::string & id,
                       std::string & error) const;

    // A preset's file as it is; false when there is none.
    bool read_preset(const std::string & id, std::string & text, std::string & error) const;

    // Replaces an existing preset's file with text (at most 64 KB, naming a
    // model); false with a message otherwise.
    bool write_preset(const std::string & id, const std::string & text, std::string & error) const;

    // Deletes a variant preset's file (and nothing else). A preset without a
    // base is a model: false, to be deleted with remove().
    bool remove_variant(const std::string & id, std::string & error) const;
};

// The model options whose value is a path (-m, --vae, --llm, ...).
bool is_model_path_option(const std::string & option);

// The sd-server options a preset's "# defaults:" stand for: --steps,
// --cfg-scale, --sampling-method, --scheduler, --width, --height, --clip-skip. A value that
// cannot be one (not a number, out of range, an odd name, a size that is not a
// multiple of 8, "default") is left out; the other settings of "# defaults:"
// (loras, hires_*) are for the web UI only.
std::vector<std::string> defaults_to_args(const std::vector<std::pair<std::string, std::string>> & defaults);

// The settings of a preset's "# defaults:" as the server's request defaults
// (sd_server_set_request_defaults): what defaults_to_args gives, as numbers
// and names. A field that is not set keeps its -1 (or "").
struct PresetSettings {
    int         steps = -1;
    float       cfg   = -1;
    std::string sampler;
    std::string scheduler;
    int         width  = -1;
    int         height = -1;
    int         clip_skip = -1;  // 1 to 12; -1: sd.cpp's own (2 for SDXL, 1 for SD 1.5)
};

PresetSettings preset_settings(const std::vector<std::pair<std::string, std::string>> & defaults);

// The library as JSON for GET /ps5sd/v1/app/models: {"active", "loaded",
// "switching", "standby", "load_error", "models": [{"id", "name", "kind",
// "bytes", "files", "missing", "base", "control_net", "family", "model_file",
// "part", "prompt", "negative", "defaults": {...}}]}. prompt, negative: the prompt
// starters ("# prompt:", "# negative:"), or "". control_net: the file name of the preset's
// --control-net, or "". part: what a single file is when it is only a part of a
// model (net_part), else "". standby: the server runs without a model.
// load_error: set_load_error's, or null.
std::string models_json(const ModelLibrary & library, const std::string & active, const std::string & loaded,
                        bool switching, bool standby = false);

// {"deleted": [...], "kept": [...]} with the library as models_json gives it.
std::string removed_json(const ModelLibrary & library, const std::vector<std::string> & deleted,
                         const std::vector<std::string> & kept, const std::string & active, const std::string & loaded);

// The entries whose files are all there, as the A1111 API's
// /sdapi/v1/sd-models lists them: "title" is the id, which a client sends
// back as sd_model_checkpoint.
std::string sd_models_json(const ModelLibrary & library);

// The same entries as the OpenAI API's /v1/models lists them.
std::string openai_models_json(const ModelLibrary & library);

// GET /ps5sd/v1/app/loras: {"loras": [{"name", "path", "bytes"}]}, the model
// files under dir and its subfolders (as sd-server's /sdapi/v1/loras lists
// them, with sizes), sorted by path.
std::string loras_json(const std::string & dir);

// The network an entry runs (net_family.hpp): that of its diffusion model, else of its checkpoint.
std::string model_family(const ModelEntry & e);

// The file name (no folder) of an entry's main model file: its diffusion
// model, else its checkpoint. The web UI groups the presets of one model by
// it; a variant has its base's (create_preset copies the model arguments).
std::string model_file(const ModelEntry & e);

// For a single file that is only a part of a model (net_part: "diffusion
// model", "text encoder" or "VAE"), why it cannot be loaded on its own and what
// to do; "" for a preset and for a file that can run alone.
std::string part_reason(const ModelEntry & e);

// The last entry that did not load and why, which models_json reports as
// "load_error": {"id", "message"} until the next model loads (an empty id
// clears it); safe from several threads.
void set_load_error(const std::string & id, const std::string & message);

// GET /ps5sd/v1/app/upscalers: {"upscalers": [{"name", "file", "bytes"}]},
// the upscaler files (.pth, .pt, .safetensors, .gguf) directly in dir, as
// sd-server lists them (name is the file name without its extension), sorted.
std::string upscalers_json(const std::string & dir);

// The id of the entry an A1111 client names as sd_model_checkpoint: its id or
// its name, either with A1111's " [hash]" after it. Empty when none matches.
std::string checkpoint_id(const ModelLibrary & library, const std::string & checkpoint);

// The A1111 API's GET /sdapi/v1/options: {"samples_format": "png",
// "sd_model_checkpoint": checkpoint}.
std::string options_json(const std::string & checkpoint);

// sd_model_checkpoint of a POST /sdapi/v1/options body; empty when the body
// does not set it.
std::string checkpoint_from_options(const std::string & body);

// GET /ps5sd/v1/app/info: {"name": "PS5SD", "version", "sd_cpp": {"version", "commit"}}.
std::string info_json(const std::string & version, const std::string & sd_version, const std::string & sd_commit);

// The API key: 32 random bytes in lower-case base32 (a-z, 2-7), 52
// characters. A person reads it off the TV in groups of four
// (api_key_groups); the login takes it with or without the spaces.
std::string api_key_from_bytes(const unsigned char * bytes, size_t n);
std::string api_key_groups(const std::string & key);
// True for a key as api_key_from_bytes makes them (any length > 0, a-z and 2-7).
bool valid_api_key(const std::string & key);

// GET /ps5sd/v1/app/auth: {"enabled": bool, "key": "..." or ""}.
std::string auth_json(const std::string & key);

// POST /ps5sd/v1/app/gallery: {"image": "<base64 PNG, or a data URL>",
// "parameters": "<what made it>" (optional)}. The PNG's bytes, with the
// parameters in a tEXt "parameters" chunk as the generated images have them,
// or false with a message (not JSON, no image, not base64, not a PNG).
bool png_from_json(const std::string & body, std::string & png, std::string & error);

// The PNG with a tEXt chunk keyword/text right after its IHDR; the PNG as it
// is when it does not start with one.
std::string png_with_text(const std::string & png, const std::string & keyword, const std::string & text);

// A free gallery file name in dir for an image the web UI made at time t:
// sd_YYYYMMDD_HHMMSS_edit.png, then _edit2, _edit3, ...
std::string edited_image_name(const std::string & dir, long long t);

// {"error": message}.
std::string error_json(const std::string & message);

// The "id" of a POST body {"id": "..."}; empty when there is none.
std::string id_from_json(const std::string & body);

// The app's own settings: GET and POST /ps5sd/v1/app/settings, kept in
// /data/PS5SD/settings.json.
struct AppSettings {
    bool        tv_images  = true;     // the TV shows the images and the prompts
    bool        tv_api_key = true;     // the TV shows the API key (when there is one)
    std::string fan        = "quiet";  // "quiet" (the console's own), "balanced", "cool", "manual"
    int         fan_target = 85;       // manual's target temperature, 70..95 C (ps5/fan/fan_policy.h)
    // The preset the web UI loads for a model (model_file -> preset id), set
    // with its "Make default". Only the web UI reads it; the APIs switch to the
    // id they name.
    std::map<std::string, std::string> default_presets;
};

// {"tv_images": true, "tv_api_key": true, "fan": "quiet", "fan_target": 85,
// "default_presets": {"model.gguf": "preset-id"}}.
std::string settings_json(const AppSettings & settings);

// Applies what a body (or the settings file) sets to settings; a field it
// leaves out keeps its value. default_presets is merged: a model file with an
// id sets its default, one with "" or null drops it. False, with error, when the body is not a JSON
// object or a field has the wrong type (settings is then unchanged).
bool apply_settings(const std::string & body, AppSettings & settings, std::string & error);

}  // namespace ps5sd
