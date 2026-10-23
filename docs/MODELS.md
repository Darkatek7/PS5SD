# Models: getting them onto the PS5

PS5SD runs the models stable-diffusion.cpp supports:

- SD 1.x, SD 2.x, SDXL and their finetunes;
- SD 3.x;
- FLUX.1, FLUX.2 and FLUX.3 Klein;
- Wan 2.1 and Wan 2.2 TI2V 5B, for short videos (the second also from a picture); see **Video** below;
- Z-Image, Qwen-Image and others; see stable-diffusion.cpp's [docs](https://github.com/leejet/stable-diffusion.cpp/tree/master/docs).

It reads `.safetensors`, `.gguf`, `.ckpt` and `.sft` files. Everything lives under `/data/PS5SD` on the console:

| Folder | What goes there |
|---|---|
| `/data/PS5SD/models` | model files; a single-file checkpoint directly here shows up in the library by itself |
| `/data/PS5SD/models/diffusion_models`, `text_encoders`, `vae`, `controlnet` | the parts of multi-file models (FLUX, Z-Image) and ControlNets (see **ControlNet** below); any subfolder works, these are the catalog's |
| `/data/PS5SD/presets` | one `<id>.txt` per multi-file model, or per checkpoint that needs its own options |
| `/data/PS5SD/loras` | LoRAs; see **LoRAs** below |
| `/data/PS5SD/embeddings` | textual inversions |
| `/data/PS5SD/upscalers` | upscalers (ESRGAN, `.pth`): see **Upscalers** below |

There are four ways to get a model there:

1. **Download in the web UI.** On the Models tab, under **Get more**, install from the catalog, paste a
   Hugging Face or Civitai file link, or search Hugging Face; **Downloads** shows how far they are. The console downloads the files itself, so the PC can
   be off. For a gated or login-only model, put a token in `/data/PS5SD/hf-token.txt` or
   `/data/PS5SD/civitai-token.txt`.
2. **Upload in the web UI.** For any file on the computer the browser runs on: models from other
   sites, your own merges and trainings, files you already downloaded. See below.
3. **FTP from the PC.** The same, with an FTP client or a script. See below.
4. **A USB drive.** A preset can name files on it. See below.

## Upload in the web UI

1. Open `http://<console IP>:7860` and go to **Models**.
2. Under **Upload from this computer**, pick what the file is (Model, LoRA, Embedding or Preset),
   press **Choose a file** and pick it. The upload starts at once and shows its progress.
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

**With this repository's script**, which uploads, checks the size and selects the model:

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
```

The options that matter on the PS5:

| Option | Why |
|---|---|
| `--type q8_0` | For an f16 or f32 SDXL `.safetensors` (6–7 GB). The weights are converted to 8 bits as they load. Without it, the model does not fit beside the VAE and is streamed in for every image, at about 30 s more per image. A `.gguf` is already quantized and doesn't need it. |
| `--vae-tiling --vae-tile-size 512` | Decodes the image in tiles. At 1024×1024 the VAE otherwise needs about 6 GB at once and pushes the model out of memory. |
| `--cache-mode ucache --cache-option threshold=1.5` | Skips steps that change little: SDXL at 25 steps runs about 40% faster, with small changes in detail. |

The `# defaults:` keys are `steps`, `cfg`, `width`, `height`, `sampler`, `scheduler`, `clip_skip` and `loras`
(`path:strength`, several with commas: `loras=detail.safetensors:0.8,styles/ink.gguf:1`). The samplers
are listed by `GET /sdapi/v1/samplers`, for example `euler`, `euler_a`, `dpm++2m` or `lcm`.

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

The web UI makes these: set up the Create tab, then press **Save as preset** at its top. It copies the
model's own options (VAE, tiling, `--type`) and its prompt starters into the preset. On the Models tab, under **Installed**, the
saved presets of a model are listed under it (as "saved settings"), and the Create tab's preset menu
offers them too:

- **Use** switches to the preset. When it loads the same files with the same options as the model that
  is loaded, the switch is at once, without loading anything. Its settings take effect at once too: they
  are the defaults of the API, set without a restart. Other files or options restart the server (the
  model loads again, about a minute for SDXL).
- **Edit** (in the **⋯** menu) shows the preset file to change.
- **Delete** (in the **⋯** menu) removes the preset file only, never the model.

Deleting a model on the Models tab deletes its saved presets too. A preset without `# base:` (the catalog's, or your own of a model in several files) is a
model: it is on the Models tab, and the checkpoint it loads is not listed again apart from it. The model
lists of the A1111 and OpenAI APIs count the same way: models only, never a saved preset. A client can
still ask for a saved preset by its id. Deleting a saved preset, in the web UI or with
`DELETE /ps5sd/v1/app/models/<id>`, removes its file only.

The catalog's presets in [ps5/app/catalog.hpp](../ps5/app/catalog.hpp) are working examples for SDXL,
FLUX.2 Klein 4B, FLUX.3 Klein, Z-Image Turbo, Wan 2.1 and Wan 2.2 TI2V 5B.

Put the preset in place like a model file, with FTP or the script:

```sh
curl -T my-finetune.txt ftp://<console IP>:2121/data/PS5SD/presets/
PS5_HOST=<console IP> scripts/ps5sd-app.sh model my-finetune
```

## LoRAs

A LoRA adds a style, a character or a look to a model. It works only with models of the family it was
made for: an SDXL LoRA with SDXL and its finetunes (Illustrious, Pony, ...), an SD 1.5 LoRA with SD 1.5.
With a model of another family it does nothing.

Getting one onto the console:

- **Upload**: on the Models tab, under **Get more**, **Upload from this computer**, pick **LoRA**.
- **Download**: under **Add by link**, pick **LoRA** and paste the link. A Civitai link to a LoRA goes
  to the loras folder by itself.
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

The Models tab lists the LoRAs under **Installed**, with their size and Delete in the **⋯** menu.

Using them: on the Create tab, open **Style** and pick them there, each with a strength.
1 is the full effect, 0.5 half of it, and a negative strength works against the LoRA. Several LoRAs
work together. The web UI keeps the picks per model, with the model's other settings. Many LoRAs also
need their trigger words in the prompt (the LoRA's page names them), for example `pixel art` for
pixel-art-xl.

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
128
--flow-shift
3
--video-frames
33
--fps
16
```

The Create tab switches to video mode when the loaded model makes videos. **Frames** and **Frames per
second** replace **Images per run**. Frames are 4n+1 (5, 9, 13, ... 81); the web UI offers 5 to 81.
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
| SDXL Inpainting 0.1 (Q8_0) | 5.2 GB with the SDXL VAE | SDXL images, 1024x1024 |
| SD 1.5 Inpainting (Q8_0) | 1.9 GB | 512x512 |

An inpainting checkpoint from elsewhere (a Civitai model whose name ends in "inpainting") works too; sd.cpp
recognises it by its weights.

## ControlNet

A ControlNet makes the new image follow a guide image. The Canny kind takes the edges of the guide, so the
layout stays and the prompt sets the rest. sd.cpp loads ControlNets for SD 1.5, SD 2 and SDXL, in the
original or the diffusers layout.

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

- **Strength** (0.1 to 1.5, default 0.6): how closely the result follows the guide. 0.5 is loose, 1 is close.
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

Edit the preset on the Models tab (**⋯**, **Edit**), or in `/data/PS5SD/presets`. A checkpoint with no
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

The hires fix with a guide image is not measured yet.

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
| FLUX.2 Klein 4B Q8_0 + Qwen3-4B Q4_K_M | 7.1 GB | yes, 1024×1024 in 4 steps in about 30 s, 3.9 GiB free after it |
| Wan 2.1 T2V 1.3B (video) fp16 + umt5-xxl Q4_K_M + VAE | 6.7 GB | yes, up to 81 frames at 832×480 with 256 px VAE tiles (128 px before 0.9.1) |
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
