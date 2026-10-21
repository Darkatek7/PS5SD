# Models: getting them onto the PS5

PS5SD runs the models stable-diffusion.cpp supports:

- SD 1.x, SD 2.x, SDXL and their finetunes;
- SD 3.x;
- FLUX.1, FLUX.2 and FLUX.3 Klein;
- Z-Image, Qwen-Image and others; see stable-diffusion.cpp's [docs](https://github.com/leejet/stable-diffusion.cpp/tree/master/docs).

It reads `.safetensors`, `.gguf`, `.ckpt` and `.sft` files. Everything lives under `/data/PS5SD` on the console:

| Folder | What goes there |
|---|---|
| `/data/PS5SD/models` | model files; a single-file checkpoint directly here shows up in the library by itself |
| `/data/PS5SD/models/diffusion_models`, `text_encoders`, `vae` | the parts of multi-file models (FLUX, Z-Image); any subfolder works, these are the catalog's |
| `/data/PS5SD/presets` | one `<id>.txt` per multi-file model, or per checkpoint that needs its own options |
| `/data/PS5SD/loras` | LoRAs; see **LoRAs** below |
| `/data/PS5SD/embeddings` | textual inversions |
| `/data/PS5SD/upscalers` | upscalers (ESRGAN, `.pth`): see **Upscalers** below |

There are four ways to get a model there:

1. **Download in the web UI.** On the Models tab, install from the catalog, paste a Hugging Face or
   Civitai file link, or search Hugging Face. The console downloads the files itself, so the PC can
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
  them (below); upload the preset last, as a Preset.
- Over wired gigabit Ethernet, a 3 GB file took 36.5 s (82 MB/s). The app passes the file on to
  ftpsrv; without ftpsrv it writes the file itself, which slows to a few MB/s after about 1.4 GB.
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
- `# defaults:` the settings the web UI applies when you press **Use**;
- the server's options, one per line, with the option and its value on separate lines.

Paths are relative to `/data/PS5SD/models` unless they start with `/`.

An SDXL finetune (for example a Civitai checkpoint) with the fixed SDXL VAE:

```text
# name: My SDXL finetune
# defaults: steps=28 cfg=6 width=1024 height=1024 sampler=euler_a
-m
myFinetune.safetensors
--vae
vae/sdxl_vae_fp16_fix.safetensors
--type
q8_0
--vae-tiling
--vae-tile-size
512
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

The `# defaults:` keys are `steps`, `cfg`, `width`, `height`, `sampler`, `scheduler` and `loras`
(`path:strength`, several with commas: `loras=detail.safetensors:0.8,styles/ink.gguf:1`). The samplers
are listed by `GET /sdapi/v1/samplers`, for example `euler`, `euler_a`, `dpm++2m` or `lcm`.

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

The web UI makes these: set up the Create tab, then **Presets**, **Save the current settings**. It
copies the model's own options (VAE, tiling, `--type`) into the preset. On the Presets tab:

- **Use** switches to the preset. When it loads the same files with the same options as the model that is
  loaded (a preset saved from it), the switch is at once, without loading anything.
- **Edit** shows the preset file to change.
- **Delete** removes the preset file only, never the model.

The Models tab lists the models only, not their saved presets. Deleting a model there deletes its saved
presets too. A preset without `# base:` (the catalog's, or your own of a model in several files) is a
model: it is on the Models tab, and the checkpoint it loads is not listed again apart from it.

The catalog's presets in [ps5/app/catalog.hpp](../ps5/app/catalog.hpp) are working examples for SDXL,
FLUX.3 Klein and Z-Image Turbo.

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

- **Upload**: on the Models tab, under **Upload from this computer**, pick **LoRA**.
- **Download**: under **Add by link**, pick **LoRA** and paste the link. A Civitai link to a LoRA goes
  to the loras folder by itself.
- **FTP**: into `/data/PS5SD/loras`; subfolders work.

The Models tab lists the LoRAs, with their size and a Delete button.

Using them: on the Create tab, open **Settings** and pick them under **LoRAs**, each with a strength.
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
tab (Settings), which generates, scales up and samples again at the larger size. The hires fix works
without one, with the built-in `Latent`, `Lanczos` and `Nearest`; a model keeps more detail.

The Models tab offers two with one click:

| | For | Size |
|---|---|---|
| Real-ESRGAN x4plus | photos and most images | 67 MB |
| Real-ESRGAN x4plus anime 6B | anime and drawings | 18 MB |

Both are byte for byte the files of [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s releases
(BSD-3-Clause). Others (4x-UltraSharp, the models on [OpenModelDB](https://openmodeldb.info) with the
ESRGAN or MoSR architecture): a Hugging Face or Civitai link as **Upscaler** under **Add by link**, a
`.pth` file as **Upscaler** under **Upload**, or by FTP into `/data/PS5SD/upscalers` (no subfolders).
Most make an image 4 times as wide and high; an Upscale of a 1024x1024 image is 4096x4096.

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

## How big a model can be

The console gives the title about 11.4 GiB of GPU memory. The model, its text encoders and the VAE
must fit together, plus the working memory for one image.

| Model | Files | Fits |
|---|---|---|
| SDXL `.safetensors` f16 (6.9 GB) | with `--type q8_0` | yes, about 2.5 s per step at 1024×1024 |
| SDXL Q8_0 `.gguf` (5.0 GB) | as is | yes |
| FLUX.3 Klein Q4_K / Q6_K + Qwen3-VL-4B Q4_K_M | 6.4 / 7.7 GB | yes |
| Z-Image Turbo Q4_K + Qwen3-4B Q4_K_M | 6.7 GB | yes |
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
