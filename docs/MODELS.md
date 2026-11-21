# Models: getting them onto the PS5

PS5SD runs the models stable-diffusion.cpp supports:

- SD 1.x, SD 2.x, SDXL and their finetunes;
- SD 3.x;
- FLUX.1, FLUX.2 and FLUX.3 Klein;
- Wan 2.1 and Wan 2.2 TI2V 5B, for short videos (the second also from a picture); see **Video** below;
- Z-Image, MOSS-Image (see **MOSS-Image** below), Qwen-Image, Krea 2 (see **Krea 2 Turbo** below) and others; see stable-diffusion.cpp's [docs](https://github.com/leejet/stable-diffusion.cpp/tree/master/docs).

It reads `.safetensors`, `.gguf`, `.ckpt` and `.sft` files. Everything lives under `/data/PS5SD` on the console:

| Folder | What goes there |
|---|---|
| `/data/PS5SD/models` | model files; a single-file checkpoint directly here shows up in the library by itself |
| `/data/PS5SD/models/diffusion_models`, `text_encoders`, `vae`, `controlnet`, `tokenizers` | the parts of multi-file models (FLUX, Z-Image, MOSS-Image) and ControlNets (see **ControlNet** below); any subfolder works, these are the catalog's |
| `/data/PS5SD/presets` | one `<id>.txt` per multi-file model, or per checkpoint that needs its own options |
| `/data/PS5SD/loras` | LoRAs, with their info (`name.json`) and picture (`name.preview.png`); see **LoRAs** below |
| `/data/PS5SD/embeddings` | textual inversions (embeddings) for sd.cpp, passed as `--embd-dir` |
| `/data/PS5SD/upscalers` | upscalers (ESRGAN, `.pth`): see **Upscalers** below |
| `/data/PS5SD/tags` | tag files (`.csv` or `.txt`) for the prompt's tag completion; see [API.md](API.md#tag-completion) |

There are four ways to get a model there:

1. **Download in the web UI.** On the Models tab, **Get more** has four tabs: **Catalog** (the starter
   models, and a few LoRAs and upscalers), **Civitai** and **Hugging Face** (a search, and **Add by link**
   on top of both: paste a file link), and **Upload**. **Downloads** shows how far they are. The console downloads the files itself, so the PC can
   be off. For a gated or login-only model, save a token under **Civitai API key** or **Hugging Face token**
   (see **Tokens** below). **Get more** and **Downloads** show how much is free on the console, and
   a catalog entry that does not fit says so. A download that does not fit (its size plus 1 GB) stops at
   once with an error. Settings, under **Storage**, shows what the models, LoRAs and the gallery take.
2. **Upload in the web UI.** For any file on the computer the browser runs on: models from other
   sites, your own merges and trainings, files you already downloaded. See below.
3. **FTP from the PC.** The same, with an FTP client or a script. See below.
4. **A USB drive.** A preset can name files on it. See below.

## Upload in the web UI

1. Open `http://<console IP>:7860` and go to **Models**.
2. Under **Get more**, **Upload**, pick what the files are (Model, LoRA, Embedding, Preset, Upscaler
   or Tags), press **Choose files** and pick them. It takes several files, which go one after the other.
   The upload starts at once and shows its progress.
3. Keep the page open until it is done. A model or preset is then in the list; press **Use**.

- The file keeps its name, and an existing file is not replaced. To replace one, delete it first.
- A model goes into `/data/PS5SD/models` itself. A model of several files needs a preset that names
  them (below); upload the preset last, as a Preset. Until then the Models tab marks each of its files
  **Part of a model** (the diffusion model, the text encoder, the VAE), and they cannot be used alone.
  **Get more** installs such models with all their files and the preset.
- Over wired gigabit Ethernet, a 3 GB file took 36.5 s (82 MB/s). The app passes the file on to
  ftpsrv; without ftpsrv it writes the file itself, which slows to a few MB/s after about 1.4 GB.
  Any ftpsrv on port 2121 will do, also one up to 0.15 (without EPSV). If yours cannot write a test
  byte, PS5SD starts its own copy on 2122 and uses that instead.
- From a terminal it is one `curl` (`docs/API.md`, "Uploading files"):

```sh
curl -T myModel.safetensors http://<console IP>:7860/ps5sd/v1/app/uploads/models/myModel.safetensors
```

## FTP from the PC

The console runs an FTP server for homebrew (ftpsrv, part of the requirements), on port 2121 with
no password.

**With an FTP client** (FileZilla, WinSCP, Cyberduck):

1. Connect to `<console IP>`, port `2121`, as an anonymous user, in plain FTP (not SFTP).
2. Go to `/data/PS5SD/models`. Create it if it is not there yet; the app creates it on its first start.
3. Upload the file. For a multi-file model, upload each part into the folder its preset names.
4. In the web UI, open **Models** and press **Refresh**. A single-file checkpoint shows up as itself.
   A multi-file model needs a preset first (below).
5. Press **Use**.

**From a terminal** (curl comes with Windows, macOS and Linux):

```sh
curl -T myModel.safetensors ftp://<console IP>:2121/data/PS5SD/models/
curl --ftp-create-dirs -T ae.safetensors ftp://<console IP>:2121/data/PS5SD/models/vae/
```

**With this repository's script**, which uploads the file, checks its size and writes `/data/PS5SD/model.txt`.
A running title keeps its model until you press **Use** in the web UI or restart the title:

```sh
PS5_HOST=<console IP> scripts/ps5sd-app.sh model myModel.safetensors
```

Notes:

- FTP is the fastest way in: over wired gigabit Ethernet a 4.9 GB file took about a minute (80 MB/s).
  The console does not slow ftpsrv's writes the way it slows the title's own
  (see [CONSOLE.md](CONSOLE.md)).
- Keep the PS5SD title open or closed as you like; the library is read again on every **Refresh**.
- Don't upload over a file the server has loaded. Switch to another model first, or upload under a new
  name.
- File names: letters, digits, `.`, `-` and `_` are safest. The file name (with its extension) becomes
  the model's id.

## Presets: models of several files, and options

A single-file checkpoint loads with the server's default options. A preset is a text file in
`/data/PS5SD/presets`. Its file name without `.txt` is the model's id. It holds:

- `# name:` the name shown in the web UI;
- `# defaults:` the settings the web UI applies when you press **Use**, and, with the preset loaded, the
  server's defaults for a request that names none of them (`steps`, `cfg`, `sampler`, `scheduler`,
  `width`, `height`, `clip_skip`; a request's own values override them, and they win over the same
  options in `server-args.txt`);
- `# prompt:` and `# negative:` (optional), the prompt starters: text the server puts before the prompt and
  the negative prompt of every run while the preset is loaded (once: not before a prompt that starts with it
  already). The Create tab shows them above its prompt fields;
- the server's options, one per line, with the option and its value on separate lines.

Paths are relative to `/data/PS5SD/models` unless they start with `/`.

An SDXL finetune (for example a Civitai checkpoint) with the fixed SDXL VAE:

```text
# name: My SDXL finetune
# defaults: steps=10 cfg=6 width=1024 height=1024 sampler=dpm++2m scheduler=ays
-m
myFinetune.safetensors
--vae
vae/sdxl_vae_fp16_fix.safetensors
--type
q8_0
--vae-tiling
--vae-tile-size
640
--eager-load
```

A FLUX.3 Klein model in three parts:

```text
# name: FLUX.3 Image Klein (Q4_K)
# defaults: steps=16 cfg=1 width=1024 height=1024 sampler=euler
--diffusion-model
diffusion_models/flux3-image-klein-Q4_K.gguf
--llm
text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf
--vae
vae/flux2-vae.safetensors
--vae-tiling
--vae-tile-size
512
--cache-mode
spectrum
--cache-option
warmup=3
```

The options that matter on the PS5:

| Option | Why |
|---|---|
| `--type q8_0` | For an f16 or f32 SDXL `.safetensors` (6–7 GB). The weights are converted to 8 bits as they load. Without it, the model does not fit beside the VAE and is streamed in for every image, at about 30 s more per image. A `.gguf` is already quantized and doesn't need it. |
| `--vae-tiling --vae-tile-size 512` | Decodes the image in tiles. At 1024×1024 the VAE otherwise needs about 6 GB at once and pushes the model out of memory. |
| `--cache-mode spectrum` | Predicts the steps it can and skips them, from the fifth step on. The catalog's FLUX.3 Klein, Z-Image Turbo and Wan 2.1 presets have it. On 1.3.0 FLUX.3 Klein took about 67 s instead of 115, Z-Image Turbo 55 s instead of 71, a Wan video at 20 steps 86 s instead of 140, with the same picture. A request turns it off with `"cache_mode": "disabled"` (`/sdcpp/v1/img_gen` and `vid_gen`). |
| `--cache-option warmup=3` | Spectrum from the fourth step on, which skips one step more. The FLUX.3 Klein presets have it (on 1.3.0, 67 s instead of 74). At `warmup=2` hands and objects moved. A request sets it with `"cache_option"`. |
| `--cache-mode ucache --cache-option threshold=1.5` | Skips steps that change little: SDXL at 25 steps runs about 40% faster, with small changes in detail. |

The `# defaults:` keys are `steps`, `cfg`, `width`, `height`, `sampler`, `scheduler`, `clip_skip` and `loras`
(`path:strength`, several with commas: `loras=detail.safetensors:0.8,styles/ink.gguf:1`). A value is one word,
so a LoRA whose name has other characters than letters, digits and `. _ - /` goes in percent-encoded, as in a
URL: `loras=my%20style%20%5Bv2%5D.safetensors:0.8` for `my style [v2].safetensors`. **Save as preset** does
that itself. `width` and `height` take any whole number from 64 to 4096, as the web UI's fields do; sd.cpp
rounds a size up to the multiple the model works in and logs it. The samplers
are listed by `GET /sdapi/v1/samplers`, for example `euler`, `euler_a`, `dpm++2m` or `lcm`.

The web UI also keeps the hires fix (see Upscalers) there, and **Save as preset** writes it: `hires_scale`
(1 or more; it turns the hires fix on), `hires_steps` (0: as many as the first pass), `hires_denoise`
(for example `0.5`) and `hires_upscaler` (the name, percent-encoded: `Latent%20(nearest-exact)`; `Latent`
when it is left out). The server does not use them itself.

For a video model, `--video-frames`, `--fps` and `--flow-shift` in its preset are the defaults of a video
request, as `steps` and the others are for an image.

A Pony model (an SDXL finetune) wants its score tags at the start of every prompt, and CLIP skip 2. sd.cpp
takes CLIP skip 2 for every SDXL model by itself, so `clip_skip=2` only says it; an SD 1.5 anime model that
wants 2 needs it. A `clip_skip=1` counts only for an SD 1.5 model: the server ignores it for the others,
because with it an SDXL model makes noise.

```text
# name: Pony Diffusion V6 XL
# prompt: score_9, score_8_up, score_7_up
# negative: score_4, score_5, score_6
# defaults: steps=25 cfg=7 width=832 height=1216 sampler=euler_a clip_skip=2
-m
ponyDiffusionV6XL.safetensors
--vae
vae/sdxl_vae_fp16_fix.safetensors
--vae-tiling
--vae-tile-size
640
```

### Krea 2 Turbo

Krea 2 Turbo runs on the PS5, but it is not in the catalog. Its licence, the
[Krea 2 Community License](https://huggingface.co/realrebelai/KREA-2_GGUFs/blob/main/LICENSE), asks everyone who
deploys the model for a content filter (section 4.2), and PS5SD has none. Read the licence before you use it.
It is also slow: about 3 minutes an image at 1024×1024 and 8 steps (FLUX.3 Klein: about 1 minute).

It needs three files:

- the model: paste
  `https://huggingface.co/realrebelai/KREA-2_GGUFs/resolve/main/TURBO/Krea-2-Turbo-Q4_K_M.gguf` under
  **Get more** as a Model (7.2 GB). It goes into `/data/PS5SD/models`;
- the text encoder `text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf`: FLUX.3 Klein from the catalog has it;
- the VAE `vae/wan_2.1_vae.safetensors`: Wan 2.1 from the catalog has it.

Without those two models, paste their links instead
(`https://huggingface.co/Qwen/Qwen3-VL-4B-Instruct-GGUF/resolve/main/Qwen3VL-4B-Instruct-Q4_K_M.gguf` and
`https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged/resolve/main/split_files/vae/wan_2.1_vae.safetensors`)
and leave the folders out of the preset. Then upload the preset as a Preset, for example as `krea2-turbo.txt`:

```text
# name: Krea 2 Turbo (Q4_K_M)
# defaults: steps=8 cfg=1 width=1024 height=1024 sampler=euler
--diffusion-model
Krea-2-Turbo-Q4_K_M.gguf
--llm
text_encoders/Qwen3VL-4B-Instruct-Q4_K_M.gguf
--vae
vae/wan_2.1_vae.safetensors
--vae-tiling
--vae-tile-size
512
```

At 4 steps an image takes 88 s, but it is darker and has less detail. `--cache-mode spectrum` saves about a
quarter, but it changed the text in the picture.

### MOSS-Image

MOSS-Image (OpenMOSS, Apache 2.0) reads long prompts, tag lists and JSON layouts, and writes text into the
picture well. The catalog has it twice, under **Get more**:

- **MOSS-Image 4-step (Q4_0)** (featured): the SFT model with a 4-step distillation merged in
  ([mischiefaudio's GGUF](https://huggingface.co/mischiefaudio/MOSS-Image-SFT-4step-merged-GGUF)). 1024×1024
  in 4 steps at CFG 1, about 35 s an image.
- **MOSS-Image SFT (Q4_K)**: the model as OpenMOSS made it, at CFG 3 with a negative prompt. Its light and
  colours are more natural. 20 steps take about 4 min 35 s; OpenMOSS uses 50.

Both use Qwen3.5 9B as the text encoder, its tokenizer (`--tokenizer`, a `.json` in
`/data/PS5SD/models/tokenizers`) and the FLUX.2 VAE, which FLUX.2 Klein from the catalog has too. Each is
9.4 GB, of which the two share 6 GB. Sizes are multiples of 16. MOSS-Image LoRAs (on Civitai as base model
"MOSS Image") work with both.

### Saved presets: a model with your settings

The same model with other settings, say a portrait size and a fast sampler, is a preset with
`# base:`, the id of the model it belongs to:

```text
# name: iLustMix portrait (fast)
# base: ilustmix_v111.safetensors
# defaults: width=832 height=1216 steps=10 cfg=6 sampler=dpm++2m scheduler=ays
-m
ilustmix_v111.safetensors
```

The web UI makes these: set up the Create tab, then press **Save as preset** under the model's name. It
copies the model's own options (VAE, tiling, `--type`) and its prompt starters into the preset. On the
Models tab, under **Installed**, each model is listed once (by its main model file), with all its presets
under it: the ones it came with and the ones you saved. With more than six models, a field above the list
finds a model or a preset by its name or family. The list under the model's name on the Create
tab shows the same, each preset as a button.

- **Use** on a preset switches to it. When it loads the same files with the same options as the model
  that is loaded, the switch is at once, without loading anything. Its settings take effect at once too:
  they are the defaults of the API, set without loading anything. Other files or options load the model
  again (about a minute for SDXL); the web UI and the API stay up meanwhile.
- **Use** on the model loads its default preset, marked ★. A click on the ☆ of another preset makes that
  one the default; it is kept on the console (`default_presets` in the app's settings, see
  [API.md](API.md#app-settings)), so it holds in every browser. Only the web UI reads it: an API that
  switches models loads the preset it names.
- **Edit text** (in the **⋯** menu) shows the preset file to change, for any preset.
- **Reset** and **Update preset** (on the Create tab, under the model) show when the form is not what the
  loaded preset says. Reset puts its settings back; Update preset writes the form's settings into the
  preset, after a second press, and keeps its name, model and prompt starters.
- **Models, Presets** lists every preset by name with its model, and a search. **Apply settings** there
  puts a preset's settings into Create without switching, for another model of the loaded one's family
  (two SDXL checkpoints, the two FLUX.2 Klein 4B files): the family comes from the tensors, so its
  LoRAs fit too.
- **Delete** (in the **⋯** menu) removes a saved preset's file only, never the model.

Deleting a model on the Models tab deletes its saved presets too. A preset without `# base:` (the catalog's, or your own of a model in several files) is a
model: it is on the Models tab, and the checkpoint it loads is not listed again apart from it. The model
lists of the A1111 and OpenAI APIs count the same way: models only, never a saved preset. A client can
still ask for a saved preset by its id. Deleting a saved preset, in the web UI or with
`DELETE /ps5sd/v1/app/models/<id>`, removes its file only.

The catalog's presets in [ps5/app/catalog.hpp](../ps5/app/catalog.hpp) are working examples for SDXL,
FLUX.2 Klein 4B and 9B, FLUX.3 Klein, Z-Image Turbo, MOSS-Image, Wan 2.1 and Wan 2.2 TI2V 5B.

Put the preset in place like a model file, with FTP or the script:

```sh
curl -T my-finetune.txt ftp://<console IP>:2121/data/PS5SD/presets/
PS5_HOST=<console IP> scripts/ps5sd-app.sh model my-finetune
```

The script only writes `model.txt`; press **Use** in the web UI, or restart the title, to load the model.

## LoRAs

A LoRA adds a style, a character or a look to a model. It works only with models of the family it was
made for: an SDXL LoRA with SDXL and its finetunes (Illustrious, Pony, ...), an SD 1.5 LoRA with SD 1.5, a
FLUX.2 Klein 9B LoRA with Klein 9B (not with the 4B).
With a model of another family it does nothing.

Getting one onto the console:

- **Upload**: on the Models tab, under **Get more**, **Upload**, pick **LoRA**.
- **Search Civitai**: under **Get more**, **Civitai**, search, pick a version and press **Download** (see
  **Search Civitai** below).
- **Download**: under **Get more**, **Civitai** or **Hugging Face**, **Add by link**: pick **LoRA** and paste the
  link. A Civitai link to a LoRA goes to the loras folder by itself.
- **FTP**: into `/data/PS5SD/loras`; subfolders work.

The Models tab offers three with one click, under **Get more**. DMD2 and SDXL-Lightning make an SDXL model fast, CausVid a
Wan 2.1 1.3B video model: use CFG 1 and the steps and sampler of the table.

| | For | Settings | Size |
|---|---|---|---|
| [DMD2](https://huggingface.co/tianweiy/DMD2) | anime, Illustrious and Pony models | LCM, 4 to 8 steps, CFG 1 | 394 MB |
| [SDXL-Lightning 8-step](https://huggingface.co/ByteDance/SDXL-Lightning) | SDXL base and photo models | Euler, sgm_uniform, 8 steps, CFG 1 | 394 MB |
| [CausVid](https://github.com/tianweiy/CausVid) (Kijai's conversion) | Wan 2.1 T2V 1.3B videos | Euler, 4 to 8 steps, CFG 1 | 91 MB |

DMD2 is under CC BY-NC 4.0 (no commercial use); SDXL-Lightning under the OpenRAIL++ licence; CausVid under
CC BY-NC-SA 4.0 (no commercial use). On the console, a 1024x1024 SDXL image with DMD2 at 4 steps takes
11 s instead of 29, an 832x1216 iLustMix image 10 s, and a 1024x1024 image with Lightning at 8 steps 20 s.
A 33-frame video at 832x480 with CausVid at 4 steps takes about 2 minutes 15 s instead of about 14.5
minutes.

Adding one of them on the Create tab sets the settings of the table; removing it brings back the ones
from before, unless you changed them. DMD2 does not suit every finetune: Raehoshi IllustXL came out grey
and flat at CFG 1, and clean only at CFG 1.5 and 8 steps, 26 s instead of 32 without it
([CONSOLE.md](CONSOLE.md#faster-sdxl-2026-10-22)).

The Models tab lists the LoRAs under **Installed**, with their size, a small picture and the trigger words
when the console knows them. The **⋯** menu has **Info**, **Look up online** and Delete.

Using them: on the Create tab, open **Style** and pick them there, each with a strength. With more than 8
LoRAs, **Add a LoRA** is a search field: type part of a name.
1 is the full effect, 0.5 half of it, and a negative strength works against the LoRA. The field takes
-100 to 100: most LoRAs are made for about 0 to 1, but some, such as slider LoRAs, for -8 to 8 or more (the
LoRA's page says). Several LoRAs work together. The web UI keeps the picks per model, with the model's other settings. Many LoRAs also
need their trigger words in the prompt (the LoRA's page names them), for example `pixel art` for
pixel-art-xl. When the console knows them, **Add trigger words** under the LoRA's name puts the ones that
are missing at the end of the prompt.

### LoRA info and trigger words

The console keeps what it knows about a LoRA in a small JSON file next to it: `name.json` beside
`name.safetensors`. The keys are AUTOMATIC1111's (`activation text`, `preferred weight`, `description`,
`sd version`, `notes`, `negative text`), so a LoRA folder copied from A1111 brings its info along, and A1111
reads ours. What PS5SD adds (where the file came from, its SHA-256, the Civitai or Hugging Face page, the
pictures) is under one key, `ps5sd`. A picture is `name.preview.png` (or `.jpeg`, `.jpg`, `.webp`) next to the
LoRA, which is also A1111's name. Neither file shows as a LoRA or a model. Deleting a LoRA deletes both.

- **Info** (⋯ on a LoRA) shows the trigger words, the preferred strength and your notes, which you can change
  and **Save**, and the description, the base model, the link to the Civitai or Hugging Face page and the
  pictures, which are read-only. A preferred strength is where the LoRA starts when you add it on the Create tab.
- **Look up online** asks Civitai and fills the file's info. Civitai finds a LoRA by the SHA-256 of the file,
  so a renamed file is found too. A file that came from Hugging Face is asked there. A lookup fills only what is
  still empty: your own text stays. A 650 MB LoRA took about 10 s on the console, most of it for the hash;
  a second lookup asks Civitai for the version it found and takes half a second.
- **Look up all** (above the list) does the LoRAs that have no info yet, one after the other, with a **Stop**.
  A file that Civitai does not know is marked and not asked again.
- A LoRA downloaded in the web UI gets its info on its own when the download ends.

### Search Civitai

**Get more**, **Civitai** searches civitai.com. Type a name, pick **LoRA** or **Checkpoint**, a base model
(the one that fits the loaded model is picked first, **Any** when none does) and the order. A result shows a
picture, the creator, the base model, the downloads, the trigger words and the size of the file; a model with
several versions has a list to pick one. **Download** puts the file into the loras folder (a LoRA) or the
models folder (a checkpoint). **Installed** shows when a file of that name is there already. **Show more**
loads the next results.

- The search runs in the browser, not on the console, so the device with the browser needs the internet. The
  console only downloads the file.
- Results are safe for work. **Mature results** turns the others on. A picture that Civitai rates R or higher
  stays blurred until you click it, also with the switch on.
- A pasted `civitai.red` link works like the same link on `civitai.com`. A Civitai page link without a version
  takes the newest version.

### Tokens

Some files on Civitai need an account, and gated models on Hugging Face need a token. Under **Get more**,
**Civitai** has **Civitai API key** and **Hugging Face** has **Hugging Face token**: paste the key and press
**Save**. The console keeps it in `/data/PS5SD/civitai-token.txt` or `/data/PS5SD/hf-token.txt` (you can write
the files yourself) and never sends it back: the page only shows **Set** or **Not set**. **Remove** deletes the
file.

A `<lora:name:1>` tag in the prompt does nothing: sd-server leaves such tags out on purpose. Clients
of the API send the LoRAs in a `lora` field instead; see [API.md](API.md).

On the console, a LoRA costs no time per step: it is merged into the loaded weights, and taken out
again for a request without it. Changing the set of LoRAs makes the first step of that request longer
once (about 1.5 s for the first SDXL LoRA, about 13 s when a second one comes or goes); the next
requests with the same LoRAs run at full speed. The measurements are in [CONSOLE.md](CONSOLE.md).

## Upscalers

An upscaler makes an image larger and sharper. PS5SD uses one in two places: the **Upscale** button
under an image and in the gallery (the result goes into the gallery), and the **Hires fix** on the Create
tab (its own group), which generates, scales up and samples again at the larger size. The hires fix works
without one, with the built-in `Latent`, `Lanczos` and `Nearest`; a model keeps more detail.

The Models tab offers two with one click, under **Get more**:

| | For | Size |
|---|---|---|
| Real-ESRGAN x4plus | photos and most images | 67 MB |
| Real-ESRGAN x4plus anime 6B | anime and drawings | 18 MB |

Both are byte for byte the files of [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s releases
(BSD-3-Clause). Others (4x-UltraSharp, the models on [OpenModelDB](https://openmodeldb.info) with the
ESRGAN or MoSR architecture): a Hugging Face or Civitai link as **Upscaler** under **Add by link**, a
`.pth` file as **Upscaler** under **Upload**, or by FTP into `/data/PS5SD/upscalers` (no subfolders).
Most make an image 4 times as wide and high; an Upscale of a 1024x1024 image is 4096x4096.

## Video

Wan 2.1 makes a short video from a text prompt. Its catalog entry is **Wan 2.1 T2V 1.3B (video)**, featured
on the Models tab. One click installs its three files, 6.7 GB in all. Wan 2.1 is under the Apache 2.0 licence.

| File | Size | From |
|---|---|---|
| `diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors` | 2.84 GB | [Comfy-Org/Wan_2.1_ComfyUI_repackaged](https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged) |
| `text_encoders/umt5-xxl-encoder-Q4_K_M.gguf` | 3.66 GB | [city96/umt5-xxl-encoder-gguf](https://huggingface.co/city96/umt5-xxl-encoder-gguf) |
| `vae/wan_2.1_vae.safetensors` | 0.25 GB | Comfy-Org/Wan_2.1_ComfyUI_repackaged |

The catalog's preset for it. Wan's text encoder goes in `--t5xxl`, not `--llm`:

```text
# name: Wan 2.1 T2V 1.3B (video)
# defaults: steps=20 cfg=6 width=832 height=480 sampler=euler
--diffusion-model
diffusion_models/wan2.1_t2v_1.3B_fp16.safetensors
--t5xxl
text_encoders/umt5-xxl-encoder-Q4_K_M.gguf
--vae
vae/wan_2.1_vae.safetensors
--vae-tiling
--vae-tile-size
256
--vae-tile-overlap
0.25
--flow-shift
3
--video-frames
33
--fps
16
```

The Create tab switches to video mode when the loaded model makes videos. **Frames** and **Frames per
second** replace the count next to **Generate**. Frames are 4n+1 (5, 9, 13, ... 81); the web UI offers 5 to 81.
33 frames at 16 frames per second is 2 seconds of video, 81 frames 5 seconds. A video has no hires fix, and no
Upscale or Edit. **Flow shift** in the Video group overrides the preset's `--flow-shift` for a run (empty is
Auto, the preset's). Wan 2.1 takes no start image; Wan 2.2 TI2V does (below).

On the console, a run of 33 frames at 832x480 with 20 steps and CFG 6 took about 14.5 minutes: 36 s a
step, 12 minutes of sampling and 2 minutes to decode the video. The model loads in a few seconds once its
files are cached. The progress shows the steps, then "Decoding the video…".

It is faster with the CausVid LoRA. Get it on the Models tab under **Get more** (one click), then pick it on
the Create tab under **Style**, at strength 1. With CFG 1 and 4 steps, the same 33 frames take about
2 minutes 15 s: 1.3 minutes of sampling and 0.9 of decoding. 8 steps took 5 minutes and looked no better.
81 frames (5 seconds) take about 8 minutes: 5.4 of sampling and 2.2 of decoding. The scene stays the same
from the first frame to the last. (Before 0.9.1 the preset decoded in smaller tiles that overlapped more:
33 frames took 3 minutes 40 s, 49 frames 5.5 minutes and 81 frames 10.6 minutes.)
CausVid has a non-commercial licence.

The model, the text encoder and the VAE fit in the console's memory; after the run, 4.0 of 11.44 GiB
were free. The preset decodes in 256 px tiles that overlap by a quarter (`--vae-tile-size 256
--vae-tile-overlap 0.25`), less than half the time of 128 px tiles at sd.cpp's overlap of a half, with no
seams; an overlap of 0 leaves visible seams between the tiles. 256 px tiles ran out of memory before 0.9.0.
PS5SD 0.9.1 gives a Wan preset that 0.9.0 or older wrote, and that nobody changed, the new tiles when it
starts.

**Wan 2.1 T2V 1.3B, fast decoder** is the same model with TAEHV, a small decoder (22.6 MB, MIT, by
madebyollin), instead of the VAE: 33 frames with CausVid in about 1 minute 30 s and 81 frames in about 6
minutes; the decode of 33 frames takes 4 s instead of 55. The frames are a little softer. It shares the model
files with the other Wan entry, so getting both costs 23 MB more.

The video is a WebM file (VP8, about 4 MB for 33 frames and 10 MB for 81). It plays on the page in Chrome, Firefox and Edge.
Safari on iPhone may not play WebM; download the file and play it in VLC. Videos are saved in
`/data/PS5SD/outputs` as `sd_<date>_<time>_<n>.webm`.

The gallery shows a video with a ▶ mark. Its viewer plays it, and Download and Delete work as they do for
images. The TV shows the progress of a video run, but not the video: the dashboard draws images only.

### Video from a picture: Wan 2.2 TI2V 5B

Wan 2.2 TI2V 5B makes a video from a picture, which becomes its first frame, or from a prompt alone. Its
catalog entry is **Wan 2.2 TI2V 5B (video from a picture)**, featured on the Models tab: QuantStack's Q8_0
of the model, the same text encoder as Wan 2.1, and the Wan 2.2 VAE, 10.5 GB in all (Apache 2.0).

| File | Size | From |
|---|---|---|
| `diffusion_models/Wan2.2-TI2V-5B-Q8_0.gguf` | 5.40 GB | [QuantStack/Wan2.2-TI2V-5B-GGUF](https://huggingface.co/QuantStack/Wan2.2-TI2V-5B-GGUF) |
| `text_encoders/umt5-xxl-encoder-Q4_K_M.gguf` | 3.66 GB | city96/umt5-xxl-encoder-gguf, as for Wan 2.1 |
| `vae/wan2.2_vae.safetensors` | 1.41 GB | [Comfy-Org/Wan_2.2_ComfyUI_Repackaged](https://huggingface.co/Comfy-Org/Wan_2.2_ComfyUI_Repackaged) |

With it loaded, the Create tab shows **Start from an image** (or press **Edit** on an image of the gallery).
The size follows the picture's aspect; the picture is cut to it at the middle. The API takes the picture as
`init_image` in `POST /sdcpp/v1/vid_gen`.

Measured on the console, right after a start:

| Size, 33 frames | A step | Decoding | 20 steps in all |
|---|---|---|---|
| 1280x704 | 36 s | 196 s | about 16 minutes |
| 832x480 | 11.7 s | 88 s | about 6 minutes |

From a prompt alone the model needs its own size, 1280x704: at 832x480 and smaller the videos came out
washed out, with or without more steps, while the same sizes from a picture are clean. For video from a
prompt, Wan 2.1 is faster. The preset uses the model's own settings: CFG 5, flow shift 5, 24 frames per
second. After the 1280x704 run, 2.48 GiB were free in one block; longer videos at that size are not measured.

## Inpainting

Inpainting changes only a part of an image: on the Create tab, **Start from an image** (or **Edit** on any
image in the gallery), **Paint a mask** over what should change, and Generate. Any model can do it.
An inpainting model, made for it, blends the new part into the rest better. The catalog has two, from
gpustack's GGUF conversions:

| | Size | For |
|---|---|---|
| SDXL Inpainting 0.1 (Q8_0) | 5.2 GB with the SDXL VAE | SDXL images, 1024x1024: about 2 minutes at 25 steps |
| SD 1.5 Inpainting (Q8_0) | 1.9 GB | 512x512: about 12 s at 25 steps |

A normal model inpaints too: SDXL Base at its 10 steps took 53 s for 1024x1024 on the console.

An inpainting checkpoint from elsewhere (a Civitai model whose name ends in "inpainting") works too; sd.cpp
recognises it by its weights.

### Extend a picture (outpainting)

**Extend** under the start image makes the picture larger: pick 128 to 512 px for each side that should
grow, and press **Extend**. The page puts the picture on a larger canvas, fills the new part with the
colours of the picture's edge and paints a mask over it, a little way into the picture too, so the join
blends. Write what the new part should show and press Generate: this is the masked inpainting above, with
the new canvas as the start image. The strength goes to 1 so the new part comes from the prompt, and
"Only the masked area" stays on, so the picture keeps its own pixels and the result is the whole larger
canvas. The part that is made reaches as far into the picture as the new part is wide, so the model sees
what it continues. **Undo** puts the picture back as it was; Extend can be pressed again for another side. A picture
can grow to 4096 px a side. Any model does it; an inpainting model blends the join better.

## Edit by the prompt

FLUX.2 Klein, FLUX.3 Klein and other edit models can change an image by a sentence, without a mask. On the
Create tab, choose **Edit** and put an image under **Start from an image**. A picture can also be dropped
on the box or pasted with Ctrl+V. With a model that takes references loaded, the box **Edit by the prompt**
shows, on by default. The image goes to the model as a reference, and the prompt says what to change ("the
same house in winter, snow on the roof"); the layout and the rest stay. Turn the box off to use the image
as an ordinary start image, with a strength and a mask.

Which models take a reference: the server says it for the loaded one. In the catalog these are FLUX.2 Klein
4B and 9B and FLUX.3 Klein. Edit models you set up yourself, like Qwen Image Edit or FIBO 2.0 Edit, take
one as well.

A **Second picture (optional)** puts a figure or a thing into a scene: "the goblin from image 1 stands on
the cliff in front of the lighthouse from image 2". It does not carry a pose over: a skeleton or a posed
figure as the second picture was ignored by every Klein model we tried.

An edit takes longer than an image of its own. At 1024×1024 with one reference: Klein 4B about 36 s,
Klein 9B at 4 steps about 64 s, FLUX.3 Klein about 100 s. A second picture adds to it: Klein 4B took 51 s
with two. [API.md](API.md#edit-by-the-prompt) has the request.

## ControlNet

A ControlNet makes the new image follow a guide image. The Canny kind takes the edges of the guide, so the
layout stays and the prompt sets the rest. sd.cpp loads ControlNets for SD 1.5, SD 2 and SDXL, in the
original or the diffusers layout, and the Union ControlNet of Z-Image Turbo (below), which takes a pose.

The catalog has one for SDXL, **SDXL Base 1.0 (Q8_0) + Canny ControlNet**, on the Models tab under **Get more**, not featured.
One click installs it, 7.9 GB in all. A file that is there already (the SDXL GGUF or the VAE, from another
entry) is not fetched again.

| File | Size | From |
|---|---|---|
| `stable-diffusion-xl-base-1.0-Q8_0.gguf` | 5.0 GB | [gpustack/stable-diffusion-xl-base-1.0-GGUF](https://huggingface.co/gpustack/stable-diffusion-xl-base-1.0-GGUF) |
| `vae/sdxl_vae_fp16_fix.safetensors` | 0.33 GB | [madebyollin/sdxl-vae-fp16-fix](https://huggingface.co/madebyollin/sdxl-vae-fp16-fix) |
| `controlnet/xinsir-canny-sdxl-v2.safetensors` | 2.5 GB | [xinsir/controlnet-canny-sdxl-1.0](https://huggingface.co/xinsir/controlnet-canny-sdxl-1.0), V2, Apache 2.0 |

Its preset:

```text
# name: SDXL Base 1.0 (Q8_0) + Canny ControlNet
# defaults: steps=25 cfg=6 width=1024 height=1024 sampler=euler_a
-m
stable-diffusion-xl-base-1.0-Q8_0.gguf
--vae
vae/sdxl_vae_fp16_fix.safetensors
--control-net
controlnet/xinsir-canny-sdxl-v2.safetensors
--vae-tiling
--vae-tile-size
512
```

On the Create tab, with a ControlNet loaded, the **Guide image (ControlNet)** part appears. Pick a file for
the guide, or press **Guide** on a gallery image or under a result.

- **Strength** (0.1 to 1.5): how closely the result follows the guide. 0.5 is loose, 1 is close. It starts at
  0.6 for SDXL's Canny ControlNet and at 0.9 for Z-Image's Union ControlNet.
  A guide with many edges (trees, grass) looks painted at 0.8 and up; 0.5 to 0.65 kept the layout and
  looked natural.
- **Find the edges first (Canny)**: on by default. Turn it off when the guide is already an edge map, or a
  pose or depth image for a ControlNet of that kind.
- The guide is scaled to the image size and cropped to fit. It works with LoRAs, batches and a start image.

The ControlNet stays in memory with its model; its weights are about 2.4 GB. After loading, 6.2 of the
console's 11.44 GiB were free; after a 1024x1024 image, 2.7 GiB. Measured at 1024x1024 with strength 0.8,
20 steps of euler_a at CFG 6 took 130 s, about 4.6 s a step (about 3 s a step without a ControlNet). With the
SDXL-Lightning LoRA, 8 steps at CFG 1 took 55 s. The layout follows the guide: in one test, a house by a
lake stayed where it was when the prompt asked for winter.

To use the ControlNet with another SDXL model, an Illustrious or Pony finetune for example, add these two
lines to that model's preset, among its other options:

```text
--control-net
controlnet/xinsir-canny-sdxl-v2.safetensors
```

Edit the preset on the Models tab (**⋯**, **Edit text**), or in `/data/PS5SD/presets`. A checkpoint with no
preset of its own needs one first: set up the Create tab, then press **Save as preset**, which copies
the model's VAE and tiling options. Changed settings count from the next load of the preset, so press
**Use** on it.

A ControlNet works only with models of its family: an SDXL ControlNet with SDXL models and their finetunes,
not with SD 1.5 or FLUX. With a model that has no ControlNet, the guide image is ignored.

Other SDXL ControlNets work the same way, for example OpenPose or depth
(`xinsir/controlnet-openpose-sdxl-1.0`). PS5SD has only Canny built in, so the guide for those must already
be a pose or depth image: make it elsewhere, use it as the guide, and turn **Find the edges first (Canny)**
off. Put the model file in `/data/PS5SD/models/controlnet/`, by FTP into that folder, or with
`POST /ps5sd/v1/app/downloads` and `{"url": "...", "dest": "controlnet/<name>.safetensors"}`
([API.md](API.md)). The subfolder matters: a `.safetensors` file directly in `models` shows up in the library
as a checkpoint. Then add it to a preset as above.

With the hires fix, measured on the console (0.9.18, SDXL Base with xinsir's Canny ControlNet, 25 steps,
the second pass at a strength of 0.5): 1024x1024 alone in about 2 min 45 s, to 1536x1536 in about 6 minutes.
2048x2048 runs the console's CPU memory out: the run fails with "Not enough memory", and PS5SD stays. Before
0.9.18 a guide image with the hires fix ended PS5SD; keep it at 1536x1536 or below.

### A pose: Z-Image Turbo with the Union ControlNet

The catalog's **Z-Image Turbo (Q4_K) + Union ControlNet** (Models, **Get more**, not featured) adds
alibaba-pai's Z-Image-Turbo-Fun-Controlnet-Union 2.1 to Z-Image Turbo. One ControlNet takes a pose skeleton
(OpenPose), a depth map or edges. One click installs it, 10.3 GB in all; the files of Z-Image Turbo are not
fetched again.

| File | Size | From |
|---|---|---|
| `controlnet/z-image-turbo-fun-controlnet-union-2.1-q8_0.gguf` | 3.6 GB | [Solictous/Z-Image-Turbo-Fun-Controlnet-Union-2.1-GGUF](https://huggingface.co/Solictous/Z-Image-Turbo-Fun-Controlnet-Union-2.1-GGUF) (Q8_0 of [alibaba-pai/Z-Image-Turbo-Fun-Controlnet-Union-2.1](https://huggingface.co/alibaba-pai/Z-Image-Turbo-Fun-Controlnet-Union-2.1), 2602, 8 steps), Apache 2.0 |

For a pose, make the guide a pose skeleton: the coloured stick figure of OpenPose on black. Pose editors in a
browser make one (search for "OpenPose editor"), or take one from a pose collection. Then:

1. **Use** the preset, open **Guide image (ControlNet)** on the Create tab and pick the skeleton.
2. Turn **Find the edges first (Canny)** off: the skeleton is the guide as it is.
3. Keep the strength at 0.9 and write the prompt for the figure and the scene.

Measured on the console (2026-11-19, 1024x1024, 8 steps): at 0.9 a knight took the skeleton's pose (one arm
raised, a hand on the hip, the legs apart) from a prompt that did not name it; at 0.7 he stood as the prompt
liked, unless the prompt named the pose too; at 1.0 the colours went flat. An image takes about 80 s (48 s
without the ControlNet), and 2.7 GiB were free after it.

The preset has no VAE tiles: tiles leave seams in the encoded guide. A decode that does not fit falls back
to tiles by itself.

A pose and a character from a picture at once (issue 13) take two steps, since Z-Image Turbo takes no
reference picture and FLUX.2 Klein takes no pose from a skeleton:

1. Make any figure in the pose here, with the skeleton as the guide.
2. Switch to FLUX.2 Klein, put that image into **Edit by the prompt** and the character as the second
   picture, and write: "Replace the knight in image 1 with the goblin from image 2, in the same pose. Keep the
   background of image 1."

Measured on the console (2026-11-19, Klein 4B, two pictures of 768 px, 4 steps): the goblin took the
knight's pose, one arm raised and the legs apart, and the castle stayed; 51 s.

## How big a model can be

The console gives the title about 11.4 GiB of GPU memory. The model, its text encoders and the VAE
must fit together, plus the working memory for one image.

| Model | Files | Fits |
|---|---|---|
| SDXL `.safetensors` f16 (6.9 GB) | with `--type q8_0` | yes, about 2.5 s per step at 1024×1024 |
| SDXL Q8_0 `.gguf` (5.0 GB) | as is | yes |
| SDXL Q8_0 + Canny ControlNet (fp16) | 7.9 GB | yes, 2.7 GiB free after a 1024x1024 image |
| FLUX.3 Klein Q4_K / Q6_K + Qwen3-VL-4B Q4_K_M | 6.4 / 7.7 GB | yes; Q6_K up to 1024×1024 right after a start, 896×896 after other models; not 1536×1536 |
| Z-Image Turbo Q4_K + Qwen3-4B Q4_K_M | 6.7 GB | yes |
| FLUX.2 Klein 4B Q8_0 + Qwen3-4B Q4_K_M | 7.1 GB | yes, 1024×1024 in 4 steps in about 22 s, 3.9 GiB free after it |
| FLUX.2 Klein 9B Q4_0 + Qwen3-8B Q4_K_M | 11.0 GB | yes, 1024×1024 in 4 steps with Euler in about 40 s (the catalog preset, 8 steps with dpm2, takes about 2 min 20 s), 0.9 GiB free in one block after it; larger not measured |
| Wan 2.1 T2V 1.3B (video) fp16 + umt5-xxl Q4_K_M + VAE | 6.7 GB | yes, up to 81 frames at 832×480 with 256 px VAE tiles (128 px before 0.9.1) |
| Wan 2.2 TI2V 5B Q8_0 + umt5-xxl Q4_K_M + VAE | 10.5 GB | yes, 33 frames at 832×480 in about 6 minutes, at 1280×704 in about 16 minutes (20 steps); 2.48 GiB free in one block after the 1280×704 run |
| FLUX.1 dev or schnell in f16 (24 GB) | | no; use a Q4 or Q5 `.gguf` |

If a model does not load, the server goes back to the last one that worked. The reason is in
`/data/PS5SD/app.log`.

## A USB drive

The title can read a USB drive at `/mnt/usb0` (exFAT). A preset can name files there with an absolute
path. That keeps big models off the console's storage:

```text
# name: SDXL finetune (USB)
-m
/mnt/usb0/models/myFinetune.safetensors
--type
q8_0
```

Measured with SDXS (683 MB) on an exFAT USB stick: the weights were read in 7.5 s (about 90 MB/s),
so a 7 GB SDXL checkpoint needs over a minute just to read. The weights are read at the first image
after a switch, not at the switch itself. Writing to the stick over FTP ran at about 6 MB/s. A file
on USB is listed only through a preset.

The gallery can go to a USB drive too, so a recovery of the console that wipes `/data` does not take the
pictures with it. In Settings, under **Storage**, each drive that is plugged in has **Copy the gallery here**
and **Copy each new image to this drive**. In the gallery, **Select** has **Copy to USB** for the chosen
files. The copies go to the folder `PS5SD` on the drive; a file that is there already is skipped.
