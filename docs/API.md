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

Send a JSON body with `Content-Type: application/json`. curl's `-d` otherwise sends form data, which the
server reads only up to 8 KB, so a larger body (a base64 image, say) needs the header.

The examples use `<console>` for the PS5's address.

## The API key

With an API key, every request needs it. The key is 32 random bytes the console makes, written as 52
letters and digits (`a-z`, `2-7`). It is kept in `/data/PS5SD/api-key.txt` and shown on the TV, under the
address, in groups of four.

Turn it on in the web UI: Create, Settings, **API key**, **Turn on**. The same place shows the key, makes
a new one (the old one stops working at once), turns it off and signs the browser out. A browser signs in
with the key once; it keeps a cookie for a year. While the TV shows the key, its QR code carries it too
(`http://<console>:7860/#key=<key>`): a phone that scans it is signed in, and the page takes the key
out of its address. Lost the key and cannot see the TV? Delete
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

## The console's readings

`GET /ps5sd/v1/app/console` answers with the console's temperature, power, fan and load, now and over the
last two minutes and the last hour. The web UI's header and its Settings tab show them, and so does the TV.

```json
{
  "exporter": "running",
  "now": { "soc_c": 52.1, "cpu_c": 49.0, "watts": 61.3, "fan": 0.23, "cpu_ghz": 3.5, "cpu": 0.12, "job": 0.98 },
  "sensors": { "cpu": 49.0, "soc0": 51.0, "soc1": 52.1 },
  "seconds": { "soc_c": [ ... ], "watts": [ ... ], ... },
  "seconds_interval": 1,
  "minutes": { "soc_c": [ ... ], ... },
  "minutes_interval": 30
}
```

- `soc_c` is the hottest of the SoC's sensors and `cpu_c` the CPU's, in °C. `watts` is the whole SoC
  (CPU, GPU and memory). `fan` is the fan's duty from 0 to 1; its speed cannot be read. `cpu_ghz` is the
  CPU's clock.
- `cpu` is the share of all the console's CPUs that PS5SD itself uses (`getrusage`), from 0 to 1.
- `job` is the share of the last second in which an image or video job ran. The GPU has no load reading;
  while a job runs it is the GPU that works.
- `seconds` has a point a second for 120 s, `minutes` the mean of each 30 s for an hour, oldest first.
  A value that was not read is `null`.
- The temperature, the power, the fan and the clock come from
  [ps5-exporter](https://github.com/Marice/ps5-exporter) (GPL-3.0), a payload that serves them on
  `127.0.0.1:9100`: a PS5 app may not read them. PS5SD carries a copy (`payloads/ps5-exporter.elf`)
  and sends it to the ELF loader on port 9021 when nothing answers on 9100. `exporter` says how that
  went: `running`, `not running` (started, not answering yet), `not answering` (sent twice, a minute
apart, and still silent) or `no ELF loader`. Without it those
  values are `null`; `cpu` and `job` are there either way. The exporter keeps running after PS5SD closes,
  until the console restarts.

## Model library and switching

| Route | |
|---|---|
| `GET /ps5sd/v1/app/info` | `{"name": "PS5SD", "version": "0.8.1", "sd_cpp": {"version", "commit"}}` |
| `GET /ps5sd/v1/app/docs/<name>` | `MODELS.md` or `API.md` as the title carries them (`text/markdown`), for the web UI's Help tab; any other name is a 404 |
| `GET /ps5sd/v1/app/models` | the library; see below |
| `POST /ps5sd/v1/app/models/active` | `{"id": "<id>"}`: switch to that entry; the responses are below |
| `DELETE /ps5sd/v1/app/models/<id>` | delete an entry; see below |
| `POST /ps5sd/v1/app/fit` | the body of an `img_gen` or `vid_gen` request (`width`, `height`, `hires`, `video_frames` count): will it fit in the GPU memory of the loaded model? `{"verdict", "message", "family", "width", "height", "frames"}`, with `width` and `height` of the largest pass (the hires target). `verdict` is `fits`, `tight` (larger than measured, or than every run since the model loaded: it may run out of memory, and a restart of PS5SD gives the most room) or `too_big` (measured not to fit); `message` says it for people. Nothing runs, and a run is never refused for it. See [CONSOLE.md](CONSOLE.md#memory-what-fits-and-why-runs-failed-2026-10-22) |
| `GET /ps5sd/v1/app/loras` | `{"loras": [{"name", "path", "bytes", "family"}]}`: the LoRA files in `/data/PS5SD/loras` and its subfolders; `family` as for a model (below) |
| `DELETE /ps5sd/v1/app/loras/<path>` | delete one LoRA file; the answer is the list as `GET` gives it |

`GET /ps5sd/v1/app/models` returns:

```json
{"active": "flux3-klein-q4", "loaded": "flux3-klein-q4", "switching": false, "standby": false,
 "models": [{"id": "flux3-klein-q4", "name": "FLUX.3 Image Klein (Q4_K)", "kind": "preset",
             "bytes": 4912345678, "files": ["..."], "missing": [], "base": "", "control_net": "",
             "family": "FLUX.3 Klein", "defaults": {"steps": "16", "cfg": "1", "width": "1024", "height": "1024", "sampler": "euler"}}]}
```

- `base` is set for a saved preset (below): the id of the model it is a preset of.
- `prompt` and `negative` are the preset's prompt starters (`# prompt:`, `# negative:`), `""` for none.
  While the preset is loaded, the server puts them before the prompt and the negative prompt of every
  request, on every route (A1111, OpenAI, `img_gen`, `vid_gen`), joined with `, `. A prompt that starts with
  its starter already does not get it twice, so the prompt saved in an image can be sent again as it is.

- `active` is the entry `model.txt` names.
- `load_error` is `{"id", "message"}` when the last model asked for did not load, with the reason: what
  sd.cpp logged as an error, or what the file is (below). It is `null` otherwise, and is cleared by the
  next switch.
- `part` is set for a single file that is only a part of a model, told from its tensor names:
  `diffusion model` (a network without its text encoder, as FLUX, Z-Image and Wan come), `text encoder`
  (CLIP, T5, an LLM such as Qwen3) or `VAE`. Such a file runs only in a preset that names the other
  files ([MODELS.md](MODELS.md#presets-models-of-several-files-and-options)), and a switch to it is
  refused with `422`. `""` for a preset and for a file that runs alone.
- `loaded` is the entry the server has ready. It is `""` while a model loads and in standby.
- `missing` lists the entry's files that are not on the console. An entry with missing files cannot be
  loaded.
- `control_net` is the file name of the preset's `--control-net` (see [MODELS.md](MODELS.md#controlnet)), or `""`.
- `family` is the network the entry runs, read from the tensor names and shapes of its diffusion model
  (not from its name or metadata): `FLUX.1`, `FLUX.2 Klein 4B`, `FLUX.2 Klein 9B`, `FLUX.2 Dev`, `FLUX.3 Klein`,
  `SDXL`, `SD 2.1`, `SD 1.5`, `SD 3`, `Wan 1.3B`, `Wan 5B`, `Wan 14B`, `Z-Image`; `FLUX`, `FLUX.2` or `Wan` when only the
  kind can be told, `""` when nothing can (a `.ckpt`). A LoRA's `family` (`/ps5sd/v1/app/loras`) uses the same names: a
  LoRA works on a model of the same family.
- `defaults` are the settings that suit the model. The web UI applies them when you press **Use**. With
  the entry loaded, `steps`, `cfg`, `sampler`, `scheduler`, `width` and `height` are also **the server's
  defaults**: a request that names none of them gets these values, and a request that names one overrides
  it (`steps`, `cfg_scale`, `sampler_name`, `scheduler`, `width` and `height` on the A1111 routes). So is
  `clip_skip` (1 to 12; `clip_skip` in a request overrides it); without it sd.cpp takes 2 for SDXL and SD 2
  and 1 for SD 1.5. A `clip_skip` of 1 counts only for an SD 1.x model: any other keeps its own, because
  with 1 an SDXL model makes noise. They win
  over the same options in `server-args.txt`, which stay the defaults of a model without settings. A value
  that cannot be an option (not a number, a size that is not a multiple of 8, ...) is left out.

`POST /ps5sd/v1/app/models/active` answers:

| Status | Meaning |
|---|---|
| `202` | the switch has started; poll `GET models` until `loaded` is the new id |
| `200` | that entry is loaded already, or it loads with the same arguments as the loaded one (a saved preset of it): the switch is done, without a restart, and its settings become the server's defaults |
| `400` | the body is not `{"id": "..."}` |
| `404` | there is no such entry |
| `409` | a switch is already running, or the title is stopping |
| `422` | the entry is a single file that is only a part of a model (`part`); `error` says what is missing and what to do |
| `500` | `model.txt` could not be written |

`DELETE /ps5sd/v1/app/models/<id>` deletes an entry:

- **A saved preset** (it has a `base`): only its own file. A model file is never deleted with it.
- **A preset that is a model** (no `base`): its file is deleted together with the model files it names. A
  file stays when another preset or the loaded model uses it, for example a shared VAE.
- **A checkpoint:** the file is deleted. A checkpoint that a preset names cannot be deleted; delete the
  preset instead.

The saved presets of the entry (`base` is its id) are deleted with it. The answer is the library, plus
`"deleted"` (the deleted files) and `"kept"` (the files that stayed). `409` means the entry or a preset of
it is in use, or a switch is running; `404` that there is no such entry.

### Saved presets

A saved preset is a model with other settings: a preset file with `# base: <id>` (see
[MODELS.md](MODELS.md#presets-models-of-several-files-and-options)). The web UI's Models tab and its Save as preset box use these.

| Route | |
|---|---|
| `POST /ps5sd/v1/app/presets` | `{"name": "Portrait fast", "base": "<model id>", "defaults": {"width": "832", "height": "1216", "steps": "10", "cfg": "6", "sampler": "dpm++2m", "scheduler": "ays", "clip_skip": "2", "loras": "detail.safetensors:0.8", "hires_scale": "1.5", "hires_steps": "0", "hires_denoise": "0.5", "hires_upscaler": "Latent"}}`: `201`, the library plus `"id"` of the new preset. It takes the model's arguments and its prompt starters from `base` (the loaded model when it is left out; the base's own base when that is a saved preset too). The id comes from the name. `400` for a name of more than one line or 100 characters, a key that is not one of these, or a value with a space |
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
| `upscalers` | a `.pth`, `.pt`, `.safetensors` or `.gguf` file, no subfolders |
| `tags` | a `.csv` or `.txt` tag file for the prompt's tag completion (below), at most 64 MB |

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

## Tag completion

The prompt fields of the web UI suggest tags while you type, from the tag files in `/data/PS5SD/tags`.
PS5SD brings none: put a `.csv` there in the format of
[a1111-sd-webui-tagcomplete](https://github.com/DominikDoom/a1111-sd-webui-tagcomplete) (`name,type,count,"aliases"`,
a tag a line; its `danbooru.csv` works as it is), or a `.txt` with a tag a line, through Upload or FTP.

| Route | |
|---|---|
| `GET /ps5sd/v1/app/tags` | `{"files": ["danbooru.csv"], "tags": 100231}`: the tag files and how many tags they hold |
| `POST /ps5sd/v1/app/tags` | `{"q": "long h", "limit": 8}`: `{"tags": [{"tag": "long hair", "count": 4350000, "alias": ""}]}`, the tags (at most `limit`, 50 the most) whose name or one of whose aliases starts with `q` (at least 2 characters; spaces and underscores count the same, any case), the most used first. `tag` is as the prompt takes it: underscores as spaces, parentheses escaped (`\(style\)`); `alias` is the alias that matched, `""` when the name did |

The files are read on the first request and again when they change.

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
| `GET /sdapi/v1/sd-models` | the models that can be loaded; `title` is the entry id. A saved preset is settings on a model, not a model, so it is not listed; `sd_model_checkpoint` still takes its id |
| `GET /sdapi/v1/options` | `{"sd_model_checkpoint": "<id>"}`: the loaded model, or the one a switch is loading |
| `POST /sdapi/v1/options` | `{"sd_model_checkpoint": "<id or name>"}` switches the model; the other options are ignored |
| `GET /sdapi/v1/samplers`, `/schedulers`, `/loras`, `/upscalers`, `/latent-upscale-modes` | lists |
| `GET /sdapi/v1/progress` | an A1111-shaped progress answer |

These differ from A1111:

- `txt2img` without `width` and `height` makes the server's default size (the loaded preset's, or
  `-W`/`-H` of `server-args.txt`), 512x512 only when there is none. A request that names one takes the
  other from the default too. `img2img` without a size keeps the size of its image.
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
| `GET /v1/models` | the models that can be loaded (not the saved presets), as `{"object": "list", "data": [{"id": "<id>"}]}` |
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
| `GET /sdcpp/v1/jobs/<id>` | the job: `status` is `queued`, `generating`, `completed`, `failed` or `cancelled`; the images, or the video, are in `result` |
| `POST /sdcpp/v1/jobs/<id>/cancel` | cancel it; see "Jobs" above |
| `POST /sdcpp/v1/upscale` | upscale an image with an upscaler model; see "Hires fix and upscaling" |

A failed job says why in `error.message`: `Not enough memory on the console for this run. ...` when the GPU
ran out of memory (a smaller size, fewer images or frames, a lower hires scale or a smaller model fits),
otherwise `Generation failed: ` and the server's last error. The A1111 and OpenAI routes answer with the
same message (`Cancelled` after `POST /sdapi/v1/interrupt`); a cancelled job stays `cancelled`.

Several images in one job: `"batch_count": n` on `img_gen` (`"batch_size"` on `/sdapi/v1/txt2img`,
`"n"` on `/v1/images/generations`), up to `limits.max_batch_count` of the capabilities (8). The images
are made one after the other with the seeds `seed`, `seed + 1`, ...; while the job runs,
`preview.pass` says which of them is being sampled (1 for the first). Every image is saved to the
gallery.

Many images from one request: `"repeat": n` on `img_gen` runs the same request n times, one after the other
(1 to 100000, 1 by default; `limits.max_repeat` of the capabilities). Each run makes its `batch_count`
images. Each run is saved to the gallery as it is done, and it has its own activity entry. A named `seed`
(0 or more) counts up by `batch_count` per run. Without one, or with a negative one, every run gets a new
random seed. The job's `result` has the images of the last run only. While the job runs, `repeat` shows
`total`, `done` (the runs finished so far) and `saved` (the gallery file names so far, the last 50); it is
there when `repeat` is more than 1. A cancel stops the job before the next run: the images already saved
stay in the gallery, and the job ends `cancelled`. A run that makes no image ends the job as `failed`.

**Video**: `POST /sdcpp/v1/vid_gen` takes the `img_gen` fields and two more, `video_frames` (4n+1: 5 to 81,
the most measured) and `fps`. `output_format` is `webm` (the default), `webp` (animated WebP) or `avi` (MJPEG). Leave out `flow_shift` and `vae_tiling_params` to take the
loaded preset's (`"sample_params": {"flow_shift": 5}` sets it for one run). With a model that starts from a
picture (Wan 2.2 TI2V 5B), `init_image` (base64 PNG or JPEG, at the request's `width` x `height`) is the
first frame; Wan 2.1 has no use for it. The job runs as the images do, and a 33-frame video takes about 15 minutes (see
[MODELS.md](MODELS.md#video)). A finished job has the video in `result`:

- `b64_json`: the video, base64;
- `mime_type`: the video's type, `video/webm` by default;
- `fps` and `frame_count`.

The video is also saved in `/data/PS5SD/outputs` as `sd_<date>_<time>_<n>.webm`, so it shows in the gallery.

**Not kept**: `"temporary": true` on `img_gen` or `vid_gen` saves the images or the video as
`tmp_sd_<date>_<time>_<n>.<ext>` instead. They are served like the gallery's files
(`/ps5sd/v1/gallery/<name>`), but the gallery lists them only with `temporary=1`, and PS5SD deletes them when
it starts. `POST /ps5sd/v1/app/gallery/keep` keeps one after all (see [Gallery](#gallery)). The web UI's
**Don't keep on the console** sends it.

```sh
curl http://<console>:7860/sdcpp/v1/vid_gen -d '{"prompt": "a lighthouse on a cliff, waves, evening light", "width": 832, "height": 480, "video_frames": 33, "fps": 16, "sample_params": {"sample_steps": 20, "guidance": {"txt_cfg": 6}}, "output_format": "webm"}'
```

**ControlNet**: with a model whose preset loads a ControlNet (the catalog's SDXL Canny one, see
[MODELS.md](MODELS.md#controlnet)), `POST /sdcpp/v1/img_gen` takes a guide image. With a model that has no
ControlNet, the guide is ignored.

- `control_image`: the guide, a base64 PNG or a data URL.
- `control_strength`: how closely the result follows the guide (the web UI offers 0.1 to 1.5). Left out, it
  is the server's default, 0.9; the web UI sends 0.6 unless you change it.
- `image_preprocess`: `"target=control,canny=true"` runs Canny edge detection on the guide first. Leave it out
  when the guide is already an edge map, or a pose or depth image for a ControlNet of that kind.

The body, in `request.json`:

```json
{"prompt": "a stone cottage by a lake in winter", "width": 1024, "height": 1024,
 "control_image": "<base64 PNG>", "control_strength": 0.6,
 "image_preprocess": "target=control,canny=true"}
```

```sh
curl http://<console>:7860/sdcpp/v1/img_gen -H 'Content-Type: application/json' -d @request.json
```

The answer is a job, as for any `img_gen`: poll `GET /sdcpp/v1/jobs/<id>` for the image.

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
`"scale": 1` is a second pass at the same size: it refines the details without making the image larger
(on the console, 832x1216 with iLustMix at 10 steps and a strength of 0.35: 57 s with `Latent` and 63 s
with `Lanczos`, against 30 s without it).

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
| `POST /ps5sd/v1/app/gallery` | `{"image": "<base64 PNG or data URL>", "parameters": "<A1111's text>"}`: saved to `/data/PS5SD/outputs` as `sd_<date>_<time>_edit.png`, with the parameters (optional) in the PNG; `201 {"saved": "<name>"}`, `400` for anything that is not a PNG |

## Edit by the prompt (FLUX.2 Klein)

FLUX.2 Klein changes an image by a sentence, without a mask: the image goes as a reference, and the
prompt says what to change ("the same house in winter, snow on the roof"). The layout and the rest stay.

```json
{"prompt": "the same house at night, light in the windows", "ref_images": ["data:image/png;base64,..."],
 "width": 1024, "height": 1024, "sample_params": {"sample_steps": 4, "guidance": {"txt_cfg": 1}}}
```

- No `init_image` and no `strength`: with them it is image to image again.
- `/sdapi/v1/txt2img` takes the reference as `extra_images`.
- Other model families ignore the reference and make an image of the prompt alone.
- The reference adds as many tokens as its pixels, so it is best no larger than the image made; the web
  UI crops it to the aspect of the form and scales it to that size.
- Measured with FLUX.2 Klein 4B at 1024x1024 and a 1024x1024 reference (FW 13.00, 0.9.0): about 65 s at
  4 steps and 2 minutes at 8, against 30 and 48 s without one. It runs in segments, even right after a start.

## Gallery

Every generated image is also saved as a PNG in `/data/PS5SD/outputs`, and every video in its own format
(see [sd-server's own API](#sd-servers-own-api)). Since 0.9.0 each PNG carries what made it as A1111 does,
in a tEXt chunk named `parameters`, which other tools read too (a request with `"embed_image_metadata":
false` leaves it out). Since 0.9.5 that includes the loaded preset's name, as `Preset: <name>` in the text
and `"ps5sd": {"preset": "<name>"}` in sd.cpp's JSON. A video's parameters and the thumbnails are in
`/data/PS5SD/outputs/.thumbs`.

| Route | |
|---|---|
| `GET /ps5sd/v1/gallery` | the newest first: `name`, `url`, `thumb`, `created`, `size`, `type` (the MIME type: `image/png`, `video/webm`, `image/webp` or `video/x-msvideo`) and `prompt`. `limit` (100, at most 1000) and `offset` page it, `order=oldest` turns it round, `q=<text>` keeps the files whose prompt has that text (any case; not the names and tags below), `type=image` or `type=video` keeps one kind, `temporary=1` lists only the files that are not kept (`tmp_*`, which are left out otherwise). `X-Total-Count` says how many match before `limit` and `offset` |
| `GET /ps5sd/v1/gallery/<name>` | one image as PNG; a video is served with its own type |
| `GET /ps5sd/v1/gallery/thumb/<name>` | the thumbnail, a JPEG at most 320 px a side. Made when the file is saved (a video's from its first frame); a PNG saved before 0.9.0 gets its own on the first request. 404 for a video saved before 0.9.0 |
| `GET /ps5sd/v1/gallery/info/<name>` | what made the file: `{"name", "type", "parameters", "prompt", "sdcpp"}`. `parameters` is A1111's text, `sdcpp` the JSON of the request that sd.cpp puts at its end (prompt, size, seed, sampling, models, LoRAs, hires, video), `null` when there is none |
| `DELETE /ps5sd/v1/app/gallery/<name>` | delete one image or video: `{"deleted": "<name>"}`; 400 for a name the gallery does not list (folders, other files), 404 when it is gone already. Its name and tags go with it |
| `GET /ps5sd/v1/app/gallery/labels` | `{"labels": {"<file>": {"title": "Old town at night", "tags": ["night", "houses"]}}}`: the names and tags of the files that have any |
| `POST /ps5sd/v1/app/gallery/labels` | `{"name": "<file>", "title": "...", "tags": ["..."]}`: sets a file's name (one line, at most 100 characters) and tags (at most 20, each at most 40 characters, no commas; one that is there twice in any case counts once). An empty title without tags removes them. `200 {"name", "title", "tags"}` as kept; `404` for a file that is not there |
| `POST /ps5sd/v1/app/gallery/keep` | `{"name": "tmp_<file>"}`: keeps a file that was made with `"temporary": true`; it becomes `<file>`, with its thumbnail and parameters. `200 {"kept": "<file>"}`, `404` when there is no such file, `409` when `<file>` is taken |

The names and tags are kept in `/data/PS5SD/outputs/.labels.json`, not in the files, so a PNG stays as it
was made. The web UI's search finds them; the gallery's `q` does not.

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
