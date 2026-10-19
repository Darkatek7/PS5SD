# PS5SD HTTP API

PS5SD runs stable-diffusion.cpp's `sd-server` inside the title. One server on port 7860 (or the port that
`server-args.txt` names) serves the web UI and four APIs:

| Base | What it is |
|---|---|
| `/` | the web UI |
| `/sdapi/v1/` | the AUTOMATIC1111 (A1111) API, for clients made for it |
| `/v1/` | the OpenAI images API |
| `/sdcpp/v1/` | sd-server's own API, with async jobs |
| `/ps5sd/v1/` | PS5SD's routes: the gallery, the job queue, the model library and the downloader |

Every route allows any origin (CORS), so a page on a PC can call the console directly. There is no
authentication: keep port 7860 on your own network.

The examples use `<console>` for the PS5's address.

## One model at a time

The server holds one model in memory. A model is a **library entry**:

- a **preset** in `/data/PS5SD/presets/<id>.txt`: the model files, the server options that suit it and
  its default settings;
- a **checkpoint**: a single `.safetensors`, `.ckpt` or `.gguf` file directly in `/data/PS5SD/models`
  (its id is the file name).

`/data/PS5SD/model.txt` names the active entry. Switching the model writes that file and starts the
server again with the new entry. Before it switches, the server finishes the job that is running.

- A switch takes as long as loading the model: a few seconds for small models, about a minute for SDXL
  or FLUX.
- During a switch the API is gone for that time. Only the web UI and the library keep answering, and
  `GET /ps5sd/v1/app/models` reports `"switching": true`.
- If the new model does not load, the server goes back to the last model that worked.
- If no model has ever loaded, it goes to **standby**.

There is no separate unload. On the console, memory that the server frees does not go back to the pool
the next model is loaded from. Unloading would free nothing that a switch does not free.

### Standby: no model

When there is no model (a fresh install), or the only one does not load, the server starts without one.
In standby it serves:

- the web UI;
- the gallery;
- `/ps5sd/v1/app/*`: the library, the downloader and switching;
- `POST /sdapi/v1/options`.

Every other route answers `503 {"error": "...", "standby": true}`. In standby,
`GET /ps5sd/v1/app/models` reports `"standby": true`. A switch (`POST /ps5sd/v1/app/models/active`)
loads a model and ends standby.

## Model library and switching

| Route | |
|---|---|
| `GET /ps5sd/v1/app/info` | `{"name": "PS5SD", "version": "0.1.0", "sd_cpp": {"version", "commit"}}` |
| `GET /ps5sd/v1/app/models` | the library; see below |
| `POST /ps5sd/v1/app/models/active` | `{"id": "<id>"}`: switch to that entry; the responses are below |
| `DELETE /ps5sd/v1/app/models/<id>` | delete an entry; see below |

`GET /ps5sd/v1/app/models` returns:

```json
{"active": "flux3-klein-q4", "loaded": "flux3-klein-q4", "switching": false, "standby": false,
 "models": [{"id": "flux3-klein-q4", "name": "FLUX.3 Image Klein (Q4_K)", "kind": "preset",
             "bytes": 4912345678, "files": ["..."], "missing": [],
             "defaults": {"steps": "16", "cfg": "1", "width": "1024", "height": "1024", "sampler": "euler"}}]}
```

- `active` is the entry `model.txt` names.
- `loaded` is the entry the server has ready. It is `""` while a model loads and in standby.
- `missing` lists the entry's files that are not on the console. An entry with missing files cannot be
  loaded.
- `defaults` are the settings that suit the model. The web UI applies them when you press **Use**.

`POST /ps5sd/v1/app/models/active` answers:

| Status | Meaning |
|---|---|
| `202` | the switch has started; poll `GET models` until `loaded` is the new id |
| `200` | that entry is loaded already |
| `400` | the body is not `{"id": "..."}` |
| `404` | there is no such entry |
| `409` | a switch is already running, or the title is stopping |
| `500` | `model.txt` could not be written |

`DELETE /ps5sd/v1/app/models/<id>` deletes an entry:

- **A preset:** its file is deleted together with the model files it names. A file stays when another
  preset or the loaded model uses it, for example a shared VAE.
- **A checkpoint:** the file is deleted. A checkpoint that a preset names cannot be deleted; delete the
  preset instead.

The answer is the library, plus `"deleted"` (the deleted files) and `"kept"` (the files that stayed).
`409` means the entry is in use or a switch is running, `404` that there is no such entry.

```sh
curl http://<console>:7860/ps5sd/v1/app/models
curl -X POST http://<console>:7860/ps5sd/v1/app/models/active -d '{"id": "sdxl-base"}'
curl -X DELETE http://<console>:7860/ps5sd/v1/app/models/sd-turbo
```

## Downloading models

The downloader runs on its own threads. It keeps going while the server switches models, and its queue
survives a restart of the title (`/data/PS5SD/downloads.json`).

| Route | |
|---|---|
| `GET /ps5sd/v1/app/catalog` | the recommended models: `{"entries": [...]}`, each with its files, its size and `installed` |
| `POST /ps5sd/v1/app/downloads` | `{"catalog": "<id>"}`: fetch a catalog entry; its preset is written once every file is there |
| `POST /ps5sd/v1/app/downloads` | `{"url": "...", "dest": "<path under models/>"}`: fetch one file |
| `GET /ps5sd/v1/app/downloads` | the queue, oldest first: state, bytes, speed |
| `DELETE /ps5sd/v1/app/downloads/<id>` | cancel a download, or remove a finished one from the list |
| `POST /ps5sd/v1/app/downloads/<id>/retry` | resume a failed or cancelled download from its `.part` file |
| `POST /ps5sd/v1/app/downloads/clear` | remove the finished downloads from the list |
| `POST /ps5sd/v1/app/hf/search` | `{"q": "...", "limit": 20}`: search Hugging Face for model files |

A Hugging Face or Civitai token for gated models goes in `/data/PS5SD/hf-token.txt` or
`/data/PS5SD/civitai-token.txt`.

## Jobs: queue and cancel

| Route | |
|---|---|
| `GET /ps5sd/v1/jobs` | the queued and running async jobs: `{"jobs": [{"id", "status", "queue_position", "prompt", ...}]}` |
| `POST /ps5sd/v1/jobs/cancel` | cancel every queued job and stop the one that runs: `{"cancelled_queued": n, "interrupted": true}` |
| `POST /sdcpp/v1/jobs/<id>/cancel` | cancel one job: a queued one at once (`200`); a running one stops after the current step (`202`, then status `cancelled`) |
| `POST /sdapi/v1/interrupt` | A1111: stop the generation that runs, whichever API started it (`/sdapi/v1/skip` does the same) |

A stopped generation returns no image. A synchronous request (`txt2img`, `/v1/images/generations`)
answers with an error.

## The AUTOMATIC1111 API

| Route | |
|---|---|
| `POST /sdapi/v1/txt2img`, `POST /sdapi/v1/img2img` | generate; the answer has `images` as base64 PNG |
| `GET /sdapi/v1/sd-models` | the library entries that can be loaded; `title` is the entry id |
| `GET /sdapi/v1/options` | `{"sd_model_checkpoint": "<id>"}`: the loaded model, or the one a switch is loading |
| `POST /sdapi/v1/options` | `{"sd_model_checkpoint": "<id or name>"}` switches the model; the other options are ignored |
| `GET /sdapi/v1/samplers`, `/schedulers`, `/loras`, `/upscalers`, `/latent-upscale-modes` | lists |
| `GET /sdapi/v1/progress` | an A1111-shaped progress answer |

There are two differences from A1111:

- `POST /sdapi/v1/options` answers at once, while A1111 answers once the model has loaded. Until then,
  generate requests fail or wait. Poll `GET /ps5sd/v1/app/models` (or `GET /sdapi/v1/options`) for the
  switch to end.
- In standby only `POST /sdapi/v1/options` answers. The other `/sdapi/` routes answer `503`.

```sh
curl http://<console>:7860/sdapi/v1/txt2img -d '{"prompt": "a lighthouse at dusk", "steps": 16, "cfg_scale": 1, "width": 1024, "height": 1024}'
curl -X POST http://<console>:7860/sdapi/v1/options -d '{"sd_model_checkpoint": "flux3-klein-q4"}'
```

## The OpenAI API

| Route | |
|---|---|
| `GET /v1/models` | the library entries that can be loaded, as `{"object": "list", "data": [{"id": "<id>"}]}` |
| `POST /v1/images/generations` | generate; `b64_json` in the answer |
| `POST /v1/images/edits` | image to image |

The `model` field of a generation request does not switch the model; the loaded one answers. Switch with
`POST /ps5sd/v1/app/models/active` first.

## sd-server's own API

| Route | |
|---|---|
| `GET /sdcpp/v1/capabilities` | the defaults and what the server supports |
| `POST /sdcpp/v1/img_gen` | queue an image job: `202 {"id", "status", ...}`; `429` when the queue is full |
| `POST /sdcpp/v1/vid_gen` | queue a video job (video models only) |
| `GET /sdcpp/v1/jobs/<id>` | the job: `status` is `queued`, `generating`, `completed`, `failed` or `cancelled`; the images are in `result` |
| `POST /sdcpp/v1/jobs/<id>/cancel` | cancel it; see "Jobs" above |
| `POST /sdcpp/v1/upscale` | upscale an image with an upscaler model |

## Gallery

Every generated image is also saved as a PNG in `/data/PS5SD/outputs`.

| Route | |
|---|---|
| `GET /ps5sd/v1/gallery?limit=n` | the newest images first, with name, time and size |
| `GET /ps5sd/v1/gallery/<name>` | one image as PNG |
