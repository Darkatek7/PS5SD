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

Every route allows any origin (CORS, without credentials), so a page on a PC can call the console
directly. Until you turn on the [API key](#the-api-key), there is no authentication: anyone who reaches
port 7860 can use the console, so keep it on your own network.

A request body is at most 64 MB (`413` above that); uploads (`PUT .../uploads/...`) have no limit.

The examples use `<console>` for the PS5's address.

## The API key

With an API key, every request needs it. The key is 32 random bytes the console makes, written as 52
letters and digits (`a-z`, `2-7`). It is kept in `/data/PS5SD/api-key.txt` and shown on the TV, under the
address, in groups of four.

Turn it on in the web UI: Create, Settings, **API key**, **Turn on**. The same place shows the key, makes
a new one (the old one stops working at once), turns it off and signs the browser out. A browser signs in
with the key once; it keeps a cookie for a year. Lost the key and cannot see the TV? Delete
`/data/PS5SD/api-key.txt` over FTP and start the title again: the key is off.

A client sends the key in one of these:

| | For |
|---|---|
| `Authorization: Bearer <key>` | OpenAI clients: the key is their API key |
| `Authorization: Basic` with the key as the password, any user name | A1111 clients with an API login (`--api-auth`) |
| `X-API-Key: <key>` | anything else |

```sh
curl -H "Authorization: Bearer <key>" http://<console>:7860/sdapi/v1/sd-models
curl -u any:<key> http://<console>:7860/sdapi/v1/sd-models
```

- Without the key: `401 {"error": "...", "auth_required": true}`.
- Five wrong keys in a row from one client lock it out for 1 s, then 2, 4, ... up to 60 s: `429` with
  `Retry-After`. Each wrong key is in the log (`app.log` and the TV's log). Behind a proxy the client is
  the address in `CF-Connecting-IP` or `X-Forwarded-For`.
- Open without the key: `GET /` (the web UI, which then asks for the key), `GET /ps5sd/v1/health`
  (`{"status": "ok"}`, for a proxy or a monitor) and the routes below.

| Route | |
|---|---|
| `GET /ps5sd/v1/auth` | `{"required": bool, "authenticated": bool}`: whether a key is needed, and whether this request has it |
| `POST /ps5sd/v1/auth/login` | `{"key": "..."}` (spaces and dashes are left out): sets the cookie `ps5sd_key` (`HttpOnly`, `SameSite=Strict`, `Secure` behind HTTPS); `401` for a wrong key |
| `POST /ps5sd/v1/auth/logout` | deletes the cookie |
| `GET /ps5sd/v1/app/auth` | `{"enabled": bool, "key": "..."}` (needs the key, of course) |
| `POST /ps5sd/v1/app/auth/enable` | makes a new key, also when there is one: `{"enabled": true, "key": "..."}` |
| `POST /ps5sd/v1/app/auth/disable` | no key any more |

A `POST`, `PUT` or `DELETE` that carries the key only as the cookie also needs the header
`X-PS5SD-Request` (any value; the web UI sends it). A page of another site cannot add that header, so it
cannot use the cookie of a signed-in browser.

### From outside your network

Do not forward port 7860 on your router: the console speaks plain HTTP, so the key would cross the
internet readable. Put HTTPS in front of it, on another machine in your network (a PC, a Raspberry Pi, a
NAS), and turn the key on first.

The key protects port 7860 only. The console's other homebrew services (ftpsrv on 2121, elfldr on 9021,
and so on) have no password: never make them reachable from the internet. Anyone on your own network can
read the key over FTP (`/data/PS5SD/api-key.txt`), as they can every other file.

**Cloudflare Tunnel** (a domain on Cloudflare, nothing opened on the router). On the machine in your
network, with [cloudflared](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/):

```sh
cloudflared tunnel login
cloudflared tunnel create ps5sd
cloudflared tunnel route dns ps5sd sd.example.com
```

`~/.cloudflared/config.yml`:

```yaml
tunnel: ps5sd
credentials-file: /home/<you>/.cloudflared/<tunnel id>.json
ingress:
  - hostname: sd.example.com
    service: http://<console>:7860
  - service: http_status:404
```

Then `cloudflared tunnel run ps5sd` (or `cloudflared service install`), and open
`https://sd.example.com`. For a quick test without a domain, `cloudflared tunnel --url
http://<console>:7860` prints a temporary `trycloudflare.com` address.

Cloudflare's limits, on the free plan:

- A request body is at most 100 MB, so model uploads through the tunnel fail. Upload in your network, or
  let the console download the model (Models, Add by link).
- Cloudflare gives up on a response after 100 s (`524`). The web UI uses jobs (`/sdcpp/v1/img_gen`), which
  answer at once. A client of the synchronous routes (`/sdapi/v1/txt2img`, `/v1/images/generations`)
  fails on images that take longer; use `img_gen` and poll the job instead.
- Cloudflare Access can ask for a login in front of the console as well. API clients then also need an
  Access service token.

**A VPN** (Tailscale, WireGuard, your router's VPN) keeps the console off the internet altogether: the phone
or laptop joins your network and uses `http://<console>:7860` as at home. Tailscale on a machine in your
network as a subnet router (`tailscale up --advertise-routes=<your network>/24`, then approve the route
in Tailscale's admin page) reaches the console without installing anything on it.

**Your own reverse proxy**, for example [Caddy](https://caddyserver.com) with a domain pointing at your
router and port 443 forwarded to it (Caddy gets the certificate itself):

```text
sd.example.com {
    reverse_proxy <console>:7860
}
```

The web UI works at the root of a host name, not under a path (`https://example.com/sd/` does not).

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
| `GET /ps5sd/v1/app/info` | `{"name": "PS5SD", "version": "0.5.0", "sd_cpp": {"version", "commit"}}` |
| `GET /ps5sd/v1/app/docs/<name>` | `MODELS.md` or `API.md` as the title carries them (`text/markdown`), for the web UI's Help tab; any other name is a 404 |
| `GET /ps5sd/v1/app/models` | the library; see below |
| `POST /ps5sd/v1/app/models/active` | `{"id": "<id>"}`: switch to that entry; the responses are below |
| `DELETE /ps5sd/v1/app/models/<id>` | delete an entry; see below |
| `GET /ps5sd/v1/app/loras` | `{"loras": [{"name", "path", "bytes"}]}`: the LoRA files in `/data/PS5SD/loras` and its subfolders |
| `DELETE /ps5sd/v1/app/loras/<path>` | delete one LoRA file; the answer is the list as `GET` gives it |

`GET /ps5sd/v1/app/models` returns:

```json
{"active": "flux3-klein-q4", "loaded": "flux3-klein-q4", "switching": false, "standby": false,
 "models": [{"id": "flux3-klein-q4", "name": "FLUX.3 Image Klein (Q4_K)", "kind": "preset",
             "bytes": 4912345678, "files": ["..."], "missing": [], "base": "",
             "defaults": {"steps": "16", "cfg": "1", "width": "1024", "height": "1024", "sampler": "euler"}}]}
```

- `base` is set for a saved preset (below): the id of the model it is a preset of.

- `active` is the entry `model.txt` names.
- `loaded` is the entry the server has ready. It is `""` while a model loads and in standby.
- `missing` lists the entry's files that are not on the console. An entry with missing files cannot be
  loaded.
- `defaults` are the settings that suit the model. The web UI applies them when you press **Use**.

`POST /ps5sd/v1/app/models/active` answers:

| Status | Meaning |
|---|---|
| `202` | the switch has started; poll `GET models` until `loaded` is the new id |
| `200` | that entry is loaded already, or it loads with the same arguments as the loaded one (a saved preset of it): the switch is done, without a restart |
| `400` | the body is not `{"id": "..."}` |
| `404` | there is no such entry |
| `409` | a switch is already running, or the title is stopping |
| `500` | `model.txt` could not be written |

`DELETE /ps5sd/v1/app/models/<id>` deletes an entry:

- **A preset:** its file is deleted together with the model files it names. A file stays when another
  preset or the loaded model uses it, for example a shared VAE.
- **A checkpoint:** the file is deleted. A checkpoint that a preset names cannot be deleted; delete the
  preset instead.

The saved presets of the entry (`base` is its id) are deleted with it. The answer is the library, plus
`"deleted"` (the deleted files) and `"kept"` (the files that stayed). `409` means the entry or a preset of
it is in use, or a switch is running; `404` that there is no such entry.

### Saved presets

A saved preset is a model with other settings: a preset file with `# base: <id>` (see
[MODELS.md](MODELS.md#presets-models-of-several-files-and-options)). The web UI's Presets tab uses these.

| Route | |
|---|---|
| `POST /ps5sd/v1/app/presets` | `{"name": "Portrait fast", "base": "<model id>", "defaults": {"width": "832", "height": "1216", "steps": "10", "cfg": "6", "sampler": "dpm++2m", "scheduler": "ays", "loras": "detail.safetensors:0.8", "hires_scale": "1.5", "hires_steps": "0", "hires_denoise": "0.5", "hires_upscaler": "Latent"}}`: `201`, the library plus `"id"` of the new preset. It takes the model's arguments from `base` (the loaded model when it is left out; the base's own base when that is a saved preset too). The id comes from the name. `400` for a name of more than one line or 100 characters, a key that is not one of these, or a value with a space |
| `GET /ps5sd/v1/app/presets/<id>` | `{"id", "text"}`: the preset file as it is |
| `POST /ps5sd/v1/app/presets/<id>` | `{"text": "..."}`: replaces the file (at most 64 KB, it must name a model); the answer is the library. Changed options count from the next load of that preset |
| `DELETE /ps5sd/v1/app/presets/<id>` | deletes a saved preset's file, never a model file. `409` when it is in use, or when the preset is a model itself (no `base`; delete it with `DELETE models/<id>`) |

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
| `POST /ps5sd/v1/app/downloads` | `{"url": "...", "dest": "<path under models/>", "kind": "models"}`: fetch one file; `"kind": "loras"` puts it into `loras/` (a file name, no folders). Without `kind`, a Civitai LoRA (model type LORA, LoCon or DoRA) goes to `loras/`, anything else to `models/` |
| `GET /ps5sd/v1/app/downloads` | the queue, oldest first: state, bytes, speed |
| `DELETE /ps5sd/v1/app/downloads/<id>` | cancel a download, or remove a finished one from the list |
| `POST /ps5sd/v1/app/downloads/<id>/retry` | resume a failed or cancelled download from its `.part` file |
| `POST /ps5sd/v1/app/downloads/clear` | remove the finished downloads from the list |
| `POST /ps5sd/v1/app/hf/search` | `{"q": "...", "limit": 20}`: search Hugging Face for model files |

A Hugging Face or Civitai token for gated models goes in `/data/PS5SD/hf-token.txt` or
`/data/PS5SD/civitai-token.txt`.

## Uploading files

`PUT /ps5sd/v1/app/uploads/<folder>/<name>` with the file as the body puts a file from the client
onto the console. The web UI's Models tab uses it.

| Folder | Name |
|---|---|
| `models` | a `.gguf`, `.safetensors`, `.ckpt` or `.sft` file, optionally in subfolders (`vae/my-vae.safetensors`) |
| `loras`, `embeddings` | a model file name, no subfolders |
| `presets` | `<id>.txt`, at most 64 KB |

- The body is passed on to ftpsrv as it arrives, so the file size is not limited by memory. Without
  ftpsrv the title writes the file itself, which slows down after about 1.4 GB.
- The file is written as `<name>.part` and renamed once all of it is there. If the upload breaks
  off, the `.part` is removed.
- Answers:
  - 200 `{"kind", "name", "bytes", "through_ftpsrv"}`
  - 400 for a name that is not allowed, or a body that broke off or differs from its
    `Content-Length`
  - 409 when the file is already there or is being uploaded; delete it first or use another name
  - 507 when the storage is full
- Measured: a 3 GB file from a PC over wired gigabit in 36.5 s (82 MB/s).

```sh
curl -T myModel.safetensors http://<console>:7860/ps5sd/v1/app/uploads/models/myModel.safetensors
curl -T my-model.txt http://<console>:7860/ps5sd/v1/app/uploads/presets/my-model.txt
```

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
- LoRAs go in a `lora` field, `[{"path": "<path as /sdapi/v1/loras lists it>", "multiplier": 1.0}]`,
  not as `<lora:name:1>` in the prompt: sd-server leaves such tags out. `POST /sdcpp/v1/img_gen` takes
  the same field. A path that is not in `/data/PS5SD/loras` fails the request.

```sh
curl http://<console>:7860/sdapi/v1/txt2img -d '{"prompt": "a lighthouse at dusk", "steps": 16, "cfg_scale": 1, "width": 1024, "height": 1024}'
curl -X POST http://<console>:7860/sdapi/v1/options -d '{"sd_model_checkpoint": "flux3-klein-q4"}'
curl http://<console>:7860/sdapi/v1/txt2img -d '{"prompt": "a lighthouse at dusk", "steps": 6, "cfg_scale": 1.5, "sampler_name": "lcm", "width": 1024, "height": 1024, "lora": [{"path": "lcm-lora-sdxl.safetensors", "multiplier": 1}]}'
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
| `POST /sdcpp/v1/upscale` | upscale an image with an upscaler model; see "Hires fix and upscaling" |

Several images in one job: `"batch_count": n` on `img_gen` (`"batch_size"` on `/sdapi/v1/txt2img`,
`"n"` on `/v1/images/generations`), up to `limits.max_batch_count` of the capabilities (8). The images
are made one after the other with the seeds `seed`, `seed + 1`, ...; while the job runs,
`preview.pass` says which of them is being sampled (1 for the first). Every image is saved to the
gallery.

## Hires fix and upscaling

Upscaler models (ESRGAN and the like: `.pth`, `.pt`, `.safetensors`, `.gguf`) live in
`/data/PS5SD/upscalers`, directly in the folder; the title passes it to sd-server as
`--hires-upscalers-dir`.

| Route | |
|---|---|
| `GET /ps5sd/v1/app/upscalers` | `{"upscalers": [{"name", "file", "bytes"}]}`: the files in that folder |
| `DELETE /ps5sd/v1/app/upscalers/<file>` | delete one; the answer is the list as `GET` gives it |
| `POST /ps5sd/v1/app/downloads` | `{"url": "...", "kind": "upscalers"}` fetches one (a Civitai model of type Upscaler goes there by itself) |
| `PUT /ps5sd/v1/app/uploads/upscalers/<file>` | uploads one |

`GET /sdcpp/v1/capabilities` lists them in `upscalers` (with the built-in `Lanczos`, `Nearest` and the
`Latent` modes); `image_upscale: true` marks the ones `POST /sdcpp/v1/upscale` can use, and `upscale` is
true when there is one.

**Hires fix**: generate, scale up, and sample again at the larger size. On `POST /sdcpp/v1/img_gen`:

```json
"hires": {"enabled": true, "upscaler": "Latent", "scale": 1.5, "steps": 0, "denoising_strength": 0.5}
```

`steps` 0 takes the first pass's steps; the strength trims them (0.5: half of them). `/sdapi/v1/txt2img`
takes A1111's `enable_hr`, `hr_scale`, `hr_upscaler`, `hr_steps`,
`hr_resize_x`/`hr_resize_y` and `denoising_strength`. A batch makes all first passes, then all second
passes: while a job runs, `preview.pass` counts them (images 1 to n, then their hires passes n + 1 to 2n).
The largest size is 4096 on each side; on the console, sizes over 2048 are not measured yet.

**Upscale an image**: `POST /sdcpp/v1/upscale` with `{"image": "<base64>", "upscaler": "<name>"}` runs the
model over it (4x for most). PS5SD saves the result to the gallery like a generated image and says under
which name in `"saved": ["..."]`; `"return_image": false` leaves the image out of the answer (a 4096 px PNG
is tens of MB). It runs on the GPU between generations, never at the same time. Measured on a Mac's CPU:
768x768 to 3072x3072 with Real-ESRGAN x4plus anime 6B in 64 s.

```sh
curl -H "Content-Type: application/json" http://<console>:7860/sdcpp/v1/upscale \
     -d '{"image": "'"$(base64 < in.png)"'", "return_image": false}'
```

Send JSON bodies with `Content-Type: application/json`: curl's `-d` otherwise sends a form, which the
server reads only up to 8 KB.

## Image to image and inpainting

`POST /sdcpp/v1/img_gen` takes a start image and a mask (base64 or data URLs):

```json
{"prompt": "...", "init_image": "data:image/png;base64,...", "mask_image": "data:image/png;base64,...",
 "strength": 0.6, "width": 1024, "height": 1024}
```

- `strength` is how much changes: below 1 only that share of the steps runs, so a light change is quick.
- In the mask, white is painted over and black stays; grays blend. Without `mask_image` the whole image
  changes.
- The start image is cropped to the aspect of `width` x `height` and scaled to it.
- `/sdapi/v1/img2img` takes A1111's `init_images`, `mask`, `denoising_strength` and
  `inpainting_mask_invert` (A1111's hires fix there is for text to image only, and the web UI turns it
  off with a start image).
- An inpainting model (the catalog's SD 1.5 and SDXL inpainting) blends the edges of the mask better;
  any other model inpaints too.

The web UI does more in the page: with "only the masked area" it sends a box around the mask, made at
the model's size, and puts the result back into the start image with a soft edge; a masked result is
always blended over the start image, so the rest of it stays exactly as it was. That finished image goes
to the gallery through:

| Route | |
|---|---|
| `POST /ps5sd/v1/app/gallery` | `{"image": "<base64 PNG or data URL>"}`: saved to `/data/PS5SD/outputs` as `sd_<date>_<time>_edit.png`; `201 {"saved": "<name>"}`, `400` for anything that is not a PNG |

## Gallery

Every generated image is also saved as a PNG in `/data/PS5SD/outputs`.

| Route | |
|---|---|
| `GET /ps5sd/v1/gallery?limit=n` | the newest images first, with name, time and size |
| `GET /ps5sd/v1/gallery/<name>` | one image as PNG |
| `DELETE /ps5sd/v1/app/gallery/<name>` | delete one image: `{"deleted": "<name>"}`; 400 for a name the gallery does not list (folders, other files), 404 when it is gone already |

## App settings

Kept in `/data/PS5SD/settings.json` on the console, so they hold for every browser and survive a
restart.

| Route | |
|---|---|
| `GET /ps5sd/v1/app/settings` | `{"tv_images": true, "tv_api_key": true}` |
| `POST /ps5sd/v1/app/settings` | change what the body sets, like `{"tv_images": false}`; the answer is the settings as `GET` gives them, 400 for a field of the wrong type |

`tv_images` false: the dashboard on the TV shows no images (no live preview, no last image, no
thumbnails) and no prompts. The web UI's switch is in Settings, "Show images and prompts on the TV".

`tv_api_key` false: the TV does not show the API key. The switch is in Settings, under API key.
