#!/usr/bin/env python3
"""Dev helper: serve the web UI at / and forward every other path to a PS5SD server.

    python scripts/webui-dev.py [--port 8787] [--target http://10.0.0.101:7860] [--mock-downloads] [--mock-models]
                                [--mock-gallery N] [--mock-prompts] [--mock-promptbooks]
                                [--mock-generate] [--read-only]

The page is put together from ps5/webui/src on every request (as scripts/bundle-webui.sh does
for the build), so an edit shows after a reload.

--mock-downloads answers the download routes of the app (catalog, downloads, Hugging Face
search) in this process, with simulated progress, so the Models tab can be tried before the
console has them. It also turns on --mock-models: the model library (list, switch, delete;
saved presets: save, delete; a preset's text: read, write) is then an in-memory fake too and the console is not
asked about models at all, so a delete or a switch cannot touch it. The LoRAs (list, delete; a few
fixed files, some in subfolders, with a family; some with trigger words, a strength and a picture; the info,
lookup and preview routes, and the tokens route, kept in memory) and sd-server's sampler and scheduler lists
are fakes too, so the Generate tab and the preset editor work without a console. A lookup of a LoRA named
"mystery" answers not_found, "offline" a 502 and "slow" takes two seconds; any other finds an entry. It also serves docs/MODELS.md
and docs/API.md from this checkout for the Help tab. --mock-models alone fakes only these, not the downloads. Everything else is still forwarded. Without the flag the real console answers
(an old app answers 404 and the page hides the download parts). Words in a URL steer the mock:
"fail" makes the download fail at 30 percent, "big" makes it 8 GB, "slow" makes it slow.
In a search, "err" gives an error and "none" gives no results.

--mock-storage fakes the storage and USB routes of a newer app: GET /ps5sd/v1/app/storage (the console's space,
what the models, LoRAs, gallery and upscalers take, two USB drives), POST and GET /ps5sd/v1/app/gallery/copy
(a copy that advances over a few seconds; the drive usb1 is nearly full and fails half way), the usb_copy
setting (GET /ps5sd/v1/app/settings of the console, with usb_copy added; a POST that holds only usb_copy is
answered here, even with --read-only), the title's icon.png and manifest.webmanifest, and a download that does
not fit fails at once. Everything else is still forwarded.

--mock-gallery N answers the gallery with N mock images: the list (paged as the server pages it, at most 1000 an
answer, with X-Total-Count), each file and thumbnail (the title's icon), its info, a delete, and the labels. With
more than 1000 it shows whether the page reads the whole gallery.

The labels route of --mock-gallery keeps the names, tags and favourite marks in memory as the app does ("fav" alone
changes only the star; the answer carries "favourites": true). --no-favourites answers as an app from before 1.6.0
does: no "favourites" key and no "fav" in the answer, so the page must hide the star and the filter.

--mock-prompts answers the prompt history (GET, POST and DELETE /ps5sd/v1/app/prompts) in memory, as the app does:
the newest first, at most 50, a repeat moves to the front, an empty prompt is a 400. --mock-models implies it.

--mock-promptbooks answers the prompt collections (GET, POST, DELETE /ps5sd/v1/app/promptbooks, a collection, its
entries) in memory with the app's rules: names of 1 to 40 letters, digits, - and _, unique without regard to case,
"lora" reserved (400), a taken name 409, an entry of 1 to 8000 bytes, a repeated entry answers 200 with the old one.
--mock-models implies it.

--mock-generate answers POST /sdcpp/v1/img_gen and GET /sdcpp/v1/jobs/<id> here: a job is done half a second after
it was posted and gives one PNG of the requested size (a colour from the prompt: red, green or blue when the prompt
says so, else from its hash), so a run, a queue and an edit can be tried with --read-only. GET /sdcpp/v1/capabilities answers img_gen with repeat, so the page sends jobs (not
the A1111 route). GET /__mock/requests lists the bodies it got (images cut to their length), DELETE /__mock/requests empties the list.

--mock-standby starts like a fresh install: no model is loaded, the library answers
"standby": true, and every route outside /ps5sd/ answers 503 as sd-server --standby does,
until a switch (Use) has loaded a model. It implies --mock-downloads.
"""
import argparse
import base64
import http.client
import json
import hashlib
import os
import re
import struct
import threading
import time
import urllib.parse
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "ps5", "webui", "src")
VERSION_HPP = os.path.join(HERE, "..", "ps5", "app", "version.hpp")
DOCS = os.path.join(HERE, "..", "docs")


def bundle():
    """page.html with each "@@include <file>" line replaced by that file, as bytes."""
    out = []
    with open(os.path.join(SRC, "page.html"), "rb") as f:
        for line in f.read().split(b"\n"):
            if line.startswith(b"@@include "):
                with open(os.path.join(SRC, line[10:].decode()), "rb") as part:
                    text = part.read()
                    out.append(text[:-1] if text.endswith(b"\n") else text)
            else:
                out.append(line)
    return b"\n".join(out)


APP = "/ps5sd/v1/app/"
MB = 1000 * 1000
GB = 1000 * MB


class MockDownloads:
    """In-memory stand-in for the app's download routes. One download runs at a time."""

    CATALOG = [
        {"id": "sdxl-base", "name": "SDXL Base 1.0", "preset": "sdxl-base", "featured": True,
         "description": "The classic, works with LoRAs and finetunes.",
         "bytes": 6938 * MB + 335 * MB, "installed": False,
         "files": [{"url": "https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/resolve/main/sd_xl_base_1.0.safetensors",
                    "dest": "sd_xl_base_1.0.safetensors", "bytes": 6938 * MB},
                   {"url": "https://huggingface.co/madebyollin/sdxl-vae-fp16-fix/resolve/main/sdxl_vae.safetensors",
                    "dest": "vae/sdxl_vae_fp16_fix.safetensors", "bytes": 335 * MB}]},
        {"id": "flux-klein", "name": "FLUX.3 Image Klein", "preset": "flux-klein", "featured": True,
         "description": "Best quality, writes text, slower.",
         "bytes": 3510 * MB + 2500 * MB + 340 * MB, "installed": True,
         "files": [{"url": "https://huggingface.co/example/flux3-klein/resolve/main/flux3-klein-int8.safetensors",
                    "dest": "flux3-klein-int8.safetensors", "bytes": 3510 * MB},
                   {"url": "https://huggingface.co/example/flux3-klein/resolve/main/text_encoder.safetensors",
                    "dest": "flux3/text_encoder.safetensors", "bytes": 2500 * MB},
                   {"url": "https://huggingface.co/example/flux3-klein/resolve/main/ae.safetensors",
                    "dest": "flux3/ae.safetensors", "bytes": 340 * MB}]},
        {"id": "z-image-turbo", "name": "Z-Image Turbo", "preset": "z-image-turbo", "featured": True,
         "description": "Fast photoreal, 8 steps.",
         "bytes": 3860 * MB + 2500 * MB + 330 * MB, "installed": False,
         "files": [{"url": "https://huggingface.co/example/z-image-turbo/resolve/main/z_image_turbo.safetensors",
                    "dest": "z_image_turbo.safetensors", "bytes": 3860 * MB},
                   {"url": "https://huggingface.co/example/z-image-turbo/resolve/main/text_encoder.safetensors",
                    "dest": "z-image/text_encoder.safetensors", "bytes": 2500 * MB},
                   {"url": "https://huggingface.co/example/z-image-turbo/resolve/main/ae.safetensors",
                    "dest": "z-image/ae.safetensors", "bytes": 330 * MB}]},
        {"id": "sd-turbo", "name": "SD Turbo (Q8_0)", "preset": "sd-turbo",
         "description": "One step, 512x512. Fast, good for trying prompts.", "bytes": 2023745376, "installed": False,
         "files": [{"url": "https://huggingface.co/example/sd-turbo-gguf/resolve/main/sd_turbo-f16-q8_0.gguf",
                    "dest": "sd_turbo-f16-q8_0.gguf", "bytes": 2023745376}]},
        {"id": "sdxl-lightning", "name": "SDXL Lightning 4-step (Q8_0)", "preset": "sdxl-lightning",
         "description": "SDXL quality in four steps, with the fp16 VAE.", "bytes": 5435 * MB, "installed": False,
         "files": [{"url": "https://huggingface.co/example/sdxl-lightning-gguf/resolve/main/sdxl_lightning_4step-Q8_0.gguf",
                    "dest": "sdxl_lightning_4step-Q8_0.gguf", "bytes": 5100 * MB},
                   {"url": "https://huggingface.co/madebyollin/sdxl-vae-fp16-fix/resolve/main/sdxl_vae.safetensors",
                    "dest": "vae/sdxl_vae_fp16_fix.safetensors", "bytes": 335 * MB}]},
        {"id": "sdxs", "name": "SDXS 512 (Q8_0)", "preset": "sdxs",
         "description": "Tiny and very fast, for quick drafts.", "bytes": 683 * MB, "installed": True,
         "files": [{"url": "https://huggingface.co/example/sdxs-gguf/resolve/main/sdxs-512-tinySDdistilled_Q8_0.gguf",
                    "dest": "sdxs-512-tinySDdistilled_Q8_0.gguf", "bytes": 683 * MB}]},
    ]

    def __init__(self, storage=None):
        self.storage = storage
        self.lock = threading.Lock()
        self.items = []
        self.next_id = 1
        self.last = time.time()

    @staticmethod
    def _speed(url):
        return 8 * MB if "slow" in url else 30 * MB

    def _next_queued(self):
        for d in self.items:
            if d["state"] in ("queued", "running"):
                return d
        return None

    def _advance(self):
        now = time.time()
        dt = now - self.last
        self.last = now
        run = self._next_queued()
        while run is not None and dt > 0:
            run["state"] = "running"
            speed = self._speed(run["url"])
            run["bytes_per_second"] = int(speed * (0.85 + 0.3 * ((now * 7) % 1)))
            goal = int(run["total"] * 0.3) if "fail" in run["url"] else run["total"]
            need = goal - run["bytes"]
            step = speed * dt
            if step < need:
                run["bytes"] += int(step)
                break
            run["bytes"] += int(need)
            dt -= need / float(speed)
            run["bytes_per_second"] = 0
            if "fail" in run["url"]:
                run["state"] = "failed"
                run["error"] = "HTTP 403 from the server (login required)"
            else:
                run["state"] = "done"
            run = self._next_queued()

    def _installed(self, entry):
        if entry["installed"]:
            return True
        for f in entry["files"]:
            if not any(d["dest"] == f["dest"] and d["state"] == "done" for d in self.items):
                return False
        return True

    def handle(self, method, path, body):
        """Returns (status, object) or None when the path is not a download route."""
        route = path.split("?")[0]
        if not route.startswith(APP):
            return None
        route = route[len(APP):]
        with self.lock:
            self._advance()
            if method == "GET" and route == "catalog":
                out = []
                for e in self.CATALOG:
                    c = dict(e)
                    c["installed"] = self._installed(e)
                    out.append(c)
                return 200, {"entries": out}
            if method == "GET" and route == "downloads":
                return 200, {"downloads": [dict(d) for d in self.items]}
            if method == "POST" and route == "downloads":
                try:
                    req = json.loads(body or b"{}")
                except ValueError:
                    return 400, {"error": "body is not JSON"}
                url = str(req.get("url") or "")
                if not re.match(r"^https?://", url):
                    return 400, {"error": "url must start with http:// or https://"}
                for d in self.items:
                    if d["url"] == url and d["state"] in ("queued", "running"):
                        return 409, {"error": "already downloading"}
                dest = req.get("dest") or urllib.parse.unquote(url.split("?")[0].split("/")[-1]) or "download.bin"
                total = 8 * GB if "big" in url else 400 * MB
                for e in self.CATALOG:
                    for f in e["files"]:
                        if f["url"] == url:
                            total = f["bytes"]
                item = {"id": str(self.next_id), "url": url, "dest": dest, "bytes": 0, "total": total,
                        "state": "queued", "error": "", "bytes_per_second": 0, "catalog": req.get("catalog") or ""}
                self.next_id += 1
                if self.storage is not None and total > self.storage.free():
                    item["state"] = "failed"
                    item["error"] = "Not enough space on the console: it needs %.1f GB, %.1f GB are free." % (
                        total / 1e9, self.storage.free() / 1e9)
                self.items.append(item)
                self._advance()
                return 202, dict(item)
            if method == "DELETE" and route.startswith("downloads/"):
                did = route.split("/", 1)[1]
                for d in self.items:
                    if d["id"] == did:
                        if d["state"] in ("queued", "running"):
                            d["state"] = "cancelled"
                            d["bytes_per_second"] = 0
                        return 200, {"ok": True}
                return 404, {"error": "no such download"}
            if method == "POST" and route == "hf/search":
                try:
                    q = str(json.loads(body or b"{}").get("q") or "")
                except ValueError:
                    return 400, {"error": "body is not JSON"}
                if "err" in q:
                    return 502, {"error": "Hugging Face is not reachable from the console"}
                if "none" in q:
                    return 200, {"results": []}
                many = [{"path": "variant-%d/model-Q%d_0.gguf" % (i, i), "bytes": (1 + i) * 700 * MB} for i in range(9)]
                return 200, {"results": [
                    {"repo": "stabilityai/stable-diffusion-xl-base-1.0", "downloads": 1840211, "likes": 6500,
                     "files": [{"path": "README.md", "bytes": 9000},
                               {"path": "sd_xl_base_1.0.safetensors", "bytes": 6938078334},
                               {"path": "vae/diffusion_pytorch_model.safetensors", "bytes": 334643276}]},
                    {"repo": "someone/%s-gguf" % (re.sub(r"[^A-Za-z0-9_.-]+", "-", q)[:24] or "model"),
                     "downloads": 12400, "likes": 88,
                     "files": [{"path": "config.json", "bytes": 800}] + many},
                    {"repo": "nobody/tiny-docs", "downloads": 3, "likes": 0,
                     "files": [{"path": "README.md", "bytes": 100}]},
                ]}
        return 404, {"error": "not found"}


class MockModels:
    """In-memory stand-in for GET /models, POST /models/active and DELETE /models/<id>."""

    DIR = "/data/PS5SD/models/"
    EXT = (".gguf", ".safetensors", ".ckpt", ".sft")
    FILES = {
        "stable-diffusion-xl-base-1.0-Q8_0.gguf": 5040957760,
        "vae/sdxl_vae_fp16_fix.safetensors": 334641162,
        "raehoshiIllustXL_v110.safetensors": 6938063182,
        "flux3-klein-int8.safetensors": 3510 * MB,
        "flux3/text_encoder.safetensors": 2500 * MB,
        "sd_turbo-f16-q8_0.gguf": 2023745376,
        "sdxs-512-tinySDdistilled_Q8_0.gguf": 682847200,
    }
    SDXL_DEFAULTS = {"steps": "25", "cfg": "6", "width": "1024", "height": "1024", "sampler": "euler_a"}

    def __init__(self, downloads=None):
        self.lock = threading.Lock()
        self.downloads = downloads
        self.files = dict(self.FILES)
        self.presets = [
            {"id": "sdxl-base", "name": "SDXL Base 1.0 (Q8_0)", "defaults": self.SDXL_DEFAULTS,
             "files": ["stable-diffusion-xl-base-1.0-Q8_0.gguf", "vae/sdxl_vae_fp16_fix.safetensors"]},
            {"id": "sdxl-fast", "name": "SDXL Base 1.0 (Q8_0), fast", "defaults": dict(self.SDXL_DEFAULTS, steps="12"),
             "files": ["stable-diffusion-xl-base-1.0-Q8_0.gguf", "vae/sdxl_vae_fp16_fix.safetensors"]},
            {"id": "illustxl", "name": "Raehoshi IllustXL 1.1 (Q8_0)", "defaults": dict(self.SDXL_DEFAULTS, steps="28"),
             "files": ["raehoshiIllustXL_v110.safetensors", "vae/sdxl_vae_fp16_fix.safetensors"]},
            # one file is missing on purpose: a preset that is incomplete
            {"id": "flux-klein", "name": "FLUX.3 Image Klein (int8)",
             "defaults": {"steps": "16", "cfg": "1", "width": "1024", "height": "1024", "sampler": "euler"},
             "files": ["flux3-klein-int8.safetensors", "flux3/text_encoder.safetensors", "flux3/ae.safetensors"]},
            # a saved preset: settings on its base, whose files it shares
            {"id": "illustxl-quick", "name": "IllustXL quick", "base": "illustxl", "defaults": dict(self.SDXL_DEFAULTS, steps="14"),
             "files": ["raehoshiIllustXL_v110.safetensors", "vae/sdxl_vae_fp16_fix.safetensors"]},
        ]
        self.active = "sdxl-base"
        self.loaded = "sdxl-base"
        self.until = 0.0
        self.standby = False

    def _entries(self):
        out = []
        for p in self.presets:
            out.append(dict(p, kind="preset"))
        for path in sorted(self.files):
            if "/" not in path and path.lower().endswith(self.EXT):
                out.append({"id": path, "name": path.rsplit(".", 1)[0], "kind": "file", "files": [path], "defaults": {}})
        return out

    def _find(self, mid):
        for e in self._entries():
            if e["id"] == mid:
                return e
        return None

    def _json(self):
        if self.until and time.time() >= self.until:
            self.until = 0.0
            self.loaded = self.active
            self.standby = False
        models = []
        for e in self._entries():
            models.append({"id": e["id"], "name": e["name"], "kind": e["kind"], "defaults": e["defaults"],
                           "base": e.get("base", ""), "bytes": sum(self.files[f] for f in e["files"] if f in self.files),
                           "files": [self.DIR + f for f in e["files"]],
                           "missing": [self.DIR + f for f in e["files"] if f not in self.files]})
        return {"active": self.active, "loaded": "" if self.until else self.loaded,
                "switching": bool(self.until), "standby": self.standby and not self.until, "models": models}

    def _sync_catalog(self, mid, gone):
        if self.downloads is None:
            return
        with self.downloads.lock:
            for e in self.downloads.CATALOG:
                if e.get("preset") == mid:
                    e["installed"] = False
            self.downloads.items = [d for d in self.downloads.items
                                    if not (d["state"] == "done" and d["dest"] in gone)]

    def handle(self, method, path, body):
        route = path.split("?")[0]
        if route.startswith(APP + "presets"):
            return self._presets(method, route[len(APP + "presets"):], body)
        if not route.startswith(APP + "models"):
            return None
        rest = route[len(APP + "models"):]
        with self.lock:
            if method == "GET" and rest == "":
                return 200, self._json()
            if method == "POST" and rest == "/active":
                try:
                    mid = str(json.loads(body or b"{}").get("id") or "")
                except ValueError:
                    mid = ""
                if not mid:
                    return 400, {"error": "body needs an id"}
                e = self._find(mid)
                if e is None or any(f not in self.files for f in e["files"]):
                    return 404, {"error": "unknown model, or files missing: " + mid}
                cur = self._json()
                if cur["switching"]:
                    return 409, {"error": "a model switch is already running"}
                if cur["loaded"] == mid:
                    return 200, cur
                self.active = mid
                self.until = time.time() + 3
                return 202, self._json()
            if method == "DELETE" and rest.startswith("/"):
                mid = urllib.parse.unquote(rest[1:])
                e = self._find(mid)
                if e is None:
                    return 404, {"error": "unknown model: " + mid}
                cur = self._json()
                if cur["switching"]:
                    return 409, {"error": "a model switch is running"}
                if mid in (self.active, cur["loaded"]):
                    return 409, {"error": e["name"] + " is in use; switch to another model first"}
                if e["kind"] == "file":
                    for p in self.presets:
                        if e["files"][0] in p["files"]:
                            return 409, {"error": e["name"] + " is part of preset " + p["id"] + "; delete that preset instead"}
                busy = set(self._find(self.active)["files"]) if self._find(self.active) else set()
                others = set()
                for p in self.presets:
                    if p["id"] != mid and p.get("base") != mid:
                        others.update(p["files"])
                deleted, kept = [], []
                for f in e["files"]:
                    if f not in self.files:
                        continue
                    if f in busy or f in others:
                        kept.append(self.DIR + f)
                    else:
                        del self.files[f]
                        deleted.append(self.DIR + f)
                if e["kind"] == "preset":
                    # its saved presets go with it
                    self.presets = [p for p in self.presets if p["id"] != mid and p.get("base") != mid]
                self._sync_catalog(mid, [d[len(self.DIR):] for d in deleted])
                out = self._json()
                out["deleted"] = deleted
                out["kept"] = kept
                return 200, out
        return 404, {"error": "not found"}

    # Options whose next line is a model file, as is_model_path_option in ps5/app/models.cpp.
    MODEL_OPTIONS = ("-m", "--model", "--diffusion-model", "--high-noise-diffusion-model", "--vae", "--taesd", "--tae",
                     "--clip_l", "--clip_g", "--clip_vision", "--t5xxl", "--llm", "--llm_vision", "--control-net",
                     "--upscale-model", "--photo-maker", "--audio-vae", "--tokenizer")

    def _text(self, p):
        """The preset's file: its own text once written, else one made up from its fields."""
        if "text" in p:
            return p["text"]
        out = "# name: " + p["name"] + "\n"
        if p.get("base"):
            out += "# base: " + p["base"] + "\n"
        if p["defaults"]:
            out += "# defaults: " + " ".join(k + "=" + v for k, v in p["defaults"].items()) + "\n"
        for i, f in enumerate(p["files"]):
            out += ("-m" if i == 0 else "--vae" if "vae" in f or f.endswith("ae.safetensors") else "--llm") + "\n" + f + "\n"
        return out + "--vae-tiling\n"

    def _parse(self, p, text):
        """The fields of a preset's text as parse_preset reads them; False when it names no model file."""
        name, base, defaults, files, prev = p["id"], "", {}, [], ""
        for line in text.split("\n"):
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                key, colon, value = line[1:].partition(":")
                if not colon:
                    continue
                key, value = key.strip().lower(), value.strip()
                if key == "name" and value:
                    name = value
                elif key == "base":
                    base = value
                elif key == "defaults":
                    defaults = dict(w.split("=", 1) for w in value.split() if "=" in w)
                continue
            if prev in self.MODEL_OPTIONS:
                files.append(line[len(self.DIR):] if line.startswith(self.DIR) else line)
            prev = line
        if not files:
            return False
        p.update(name=name, base=base, defaults=defaults, files=files, text=text)
        return True

    # Saved presets: POST /presets {"name", "base", "defaults"} and DELETE /presets/<id>; a preset's text:
    # GET /presets/<id> and POST /presets/<id> {"text"} or {"defaults"} (only these).
    def _presets(self, method, rest, body):
        with self.lock:
            if rest.startswith("/") and method in ("GET", "POST"):
                pid = urllib.parse.unquote(rest[1:])
                p = next((p for p in self.presets if p["id"] == pid), None)
                if p is None:
                    return 404, {"error": "no preset " + pid}
                if method == "GET":
                    return 200, {"id": pid, "text": self._text(p)}
                try:
                    j = json.loads(body or b"{}")
                except ValueError:
                    j = {}
                if isinstance(j.get("defaults"), dict) and "text" not in j:
                    # Update preset: only the "# defaults:" line changes
                    p["defaults"] = {str(k): str(v) for k, v in j["defaults"].items()}
                    if "text" in p:
                        rest = [l for l in p.pop("text").split("\n") if not re.match(r"\s*#\s*defaults\s*:", l, re.I)]
                        p["text"] = self._text(dict(p, files=[])).split("--vae-tiling")[0] + "\n".join(
                            l for l in rest if not re.match(r"\s*#\s*(name|base)\s*:", l, re.I))
                    return 200, self._json()
                if not isinstance(j.get("text"), str):
                    return 400, {"error": "the body must be {\"text\": \"...\"} or {\"defaults\": {...}}"}
                if len(j["text"].encode()) > 64 * 1024:
                    return 400, {"error": "a preset is at most 64 KB"}
                if not self._parse(p, j["text"]):
                    return 400, {"error": "the preset names no model (-m, --diffusion-model, ...)"}
                return 200, self._json()
            if method == "POST" and rest == "":
                try:
                    j = json.loads(body or b"{}")
                except ValueError:
                    j = {}
                base = self._find(str(j.get("base") or self.loaded))
                if base is None or not j.get("name"):
                    return 400, {"error": "the body must be {\"name\": \"...\", \"base\": \"<model id>\", \"defaults\": {...}}"}
                root = base.get("base") or base["id"]
                pid = re.sub(r"[^a-z0-9]+", "-", str(j["name"]).lower()).strip("-") or "preset"
                while self._find(pid) is not None:
                    pid += "-2"
                self.presets.append({"id": pid, "name": str(j["name"]), "base": root,
                                     "defaults": dict(j.get("defaults") or {}), "files": list(base["files"])})
                out = self._json()
                out["id"] = pid
                return 201, out
            if method == "DELETE" and rest.startswith("/"):
                pid = urllib.parse.unquote(rest[1:])
                e = self._find(pid)
                if e is None:
                    return 404, {"error": "no preset " + pid}
                if not e.get("base"):
                    return 400, {"error": pid + " is a model, not a saved preset"}
                cur = self._json()
                if pid in (self.active, cur["loaded"]):
                    return 409, {"error": e["name"] + " is in use; switch to another preset first"}
                self.presets = [p for p in self.presets if p["id"] != pid]
                return 200, self._json()
        return None


class MockLoras:
    """In-memory stand-in for GET /loras, DELETE /loras/<path>, the LoRA info, lookup and preview routes, the
    tokens route (GET and POST tokens; the token is kept here and never sent back), and sd-server's sampler
    and scheduler lists. A lookup of a name with "mystery" in it answers not_found, one with "offline" a 502
    (Civitai cannot be reached), one with "slow" takes two seconds; the others find an entry."""

    SAMPLERS = ["euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2", "ipndm", "lcm"]
    SCHEDULERS = ["discrete", "karras", "exponential", "ays", "sgm_uniform", "simple"]
    # path: (bytes, family); "" is a LoRA whose family the server cannot tell
    FILES = {
        "add-detail-xl.safetensors": (228 * MB, "SDXL"),
        "sdxl-lightning-8step.safetensors": (394 * MB, "SDXL"),
        "styles/ink-sketch.safetensors": (171 * MB, "SDXL"),
        "styles/watercolor_v2.gguf": (85 * MB, "SDXL"),
        "styles/slow-oil-paint.safetensors": (140 * MB, "SDXL"),
        "styles/offline-neon.safetensors": (96 * MB, "SDXL"),
        "styles/anime/flat-colors.safetensors": (57 * MB, "SDXL"),
        "pixel-art-xl.safetensors": (166 * MB, "SDXL"),
        "flux/realism-lora.safetensors": (22 * MB, "FLUX.1"),
        "flux/slow-anime-flux.safetensors": (180 * MB, "FLUX.1"),
        "sd15/epi_noiseoffset2.safetensors": (81 * MB, "SD 1.5"),
        "mystery.safetensors": (12 * MB, ""),
    }
    # the sidecars that exist from the start; the key names are the app's answers of GET loras/info
    INFO = {
        "styles/ink-sketch.safetensors": {
            "trigger": "inksketch, monochrome", "weight": 0.8, "base": "SDXL 1.0", "preview": True, "preview_nsfw_level": 1,
            "description": "Black ink on white paper, loose lines.\n\nWorks best at strength 0.6 to 1.",
            "notes": "Good with the Illustrious models too.", "looked_up": "2026-11-20T18:30:00Z", "lookup": "found",
            "civitai": {"model_id": 1, "version_id": 11, "model": "Ink Sketch", "version": "v1.0", "base_model": "SDXL 1.0",
                        "url": "https://civitai.com/models/1?modelVersionId=11"},
            "images": [{"nsfw_level": 1}, {"nsfw_level": 2}, {"nsfw_level": 8}]},
        "add-detail-xl.safetensors": {
            "trigger": "detailed", "base": "SDXL 1.0", "preview": True, "preview_nsfw_level": 8, "lookup": "found",
            "description": "Adds fine detail. Negative strength takes detail away.", "looked_up": "2026-11-19T09:00:00Z",
            "hf": {"repo": "example/add-detail-xl", "url": "https://huggingface.co/example/add-detail-xl"},
            "images": [{"nsfw_level": 8}, {"nsfw_level": 1}]},
        "sdxl-lightning-8step.safetensors": {"notes": "From ByteDance. No trigger words.", "lookup": "not_found",
                                             "looked_up": "2026-11-18T12:00:00Z"},
        "styles/anime/flat-colors.safetensors": {"trigger": "flat color", "weight": 1.2, "base": "SDXL 1.0"},
    }

    def __init__(self):
        self.lock = threading.Lock()
        self.files = dict(self.FILES)
        self.info = {k: dict(v) for k, v in self.INFO.items()}
        self.tokens = {"civitai": "", "huggingface": ""}

    @staticmethod
    def _stem(path):
        return path.rsplit("/", 1)[-1].rsplit(".", 1)[0]

    def _entry(self, path, b, fam):
        e = {"name": self._stem(path), "path": path, "bytes": b, "family": fam}
        i = self.info.get(path)
        if i is not None:
            e["info"] = True
            for k in ("trigger", "weight", "base", "preview", "lookup"):
                if i.get(k):
                    e[k] = i[k]
            if i.get("preview_nsfw_level") is not None and i.get("preview"):
                e["preview_nsfw_level"] = i["preview_nsfw_level"]
        return e

    def _json(self):
        return {"loras": [self._entry(path, b, fam) for path, (b, fam) in sorted(self.files.items())]}

    def _info(self, path):
        out = {"path": path}
        out.update(self.info.get(path, {}))
        if out.get("preview"):
            out["images"] = [dict(im, url=APP + "loras/preview/" + urllib.parse.quote(path)) for im in out.get("images", [])]
        return out

    def _png(self, path):
        h = sum(ord(c) for c in path)
        return make_png(160, 160, ((h * 37) % 200 + 30, (h * 91) % 200 + 30, (h * 53) % 200 + 30))

    def _lookup(self, path):
        stem = self._stem(path)
        if "mystery" in stem:
            self.info.setdefault(path, {})["lookup"] = "not_found"
            return 404, {"error": "Civitai does not know this file", "code": "not_found"}
        if "offline" in stem:
            return 502, {"error": "cannot reach civitai.com"}
        if "slow" in stem:
            time.sleep(2)
        i = self.info.setdefault(path, {})
        words = [w for w in re.split(r"[-_ ]+", stem) if w]
        i.setdefault("trigger", " ".join(words[:2]) + " style")
        i.setdefault("description", "Mock description of " + stem + ".\n\nFound by the mock lookup.")
        i.setdefault("base", "SDXL 1.0" if self.files[path][1] == "SDXL" else (self.files[path][1] or "Unknown"))
        if "flux" in path:
            i["hf"] = {"repo": "example/" + stem, "url": "https://huggingface.co/example/" + stem}
        else:
            i["civitai"] = {"model_id": 5, "version_id": 55, "model": stem, "version": "v1", "base_model": i["base"],
                            "url": "https://civitai.com/models/5?modelVersionId=55"}
        i["images"] = [{"nsfw_level": 1}, {"nsfw_level": 1}, {"nsfw_level": 4}]
        i["preview"] = True
        i["preview_nsfw_level"] = 1
        i["looked_up"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        i["lookup"] = "found"
        return 200, self._info(path)

    def _tokens(self, method, body):
        if method == "POST":
            try:
                data = json.loads(body or b"null")
            except ValueError:
                data = None
            if not isinstance(data, dict):
                return 400, {"error": "the body is not an object"}
            for k, v in data.items():
                if k not in self.tokens or not isinstance(v, str) or len(v) > 200 or not re.match(r"^[A-Za-z0-9_.\-]*$", v):
                    return 400, {"error": "a token has letters, digits and -_. only, at most 200"}
            self.tokens.update(data)
        return 200, {k: bool(v) for k, v in self.tokens.items()}

    def handle(self, method, path, body):
        route = path.split("?")[0]
        if method == "GET" and route == "/sdapi/v1/samplers":
            return 200, [{"name": n, "aliases": [n], "options": {}} for n in self.SAMPLERS]
        if method == "GET" and route == "/sdapi/v1/schedulers":
            return 200, [{"name": n, "label": n} for n in self.SCHEDULERS]
        if route == APP + "tokens" and method in ("GET", "POST"):
            with self.lock:
                return self._tokens(method, body)
        if not route.startswith(APP + "loras"):
            return None
        rest = route[len(APP + "loras"):]
        with self.lock:
            if method == "GET" and rest == "":
                return 200, self._json()
            m = re.match(r"^/(info|lookup|preview)/(.+)$", rest)
            if m:
                lora = urllib.parse.unquote(m.group(2))
                if lora not in self.files:
                    return 404, {"error": {"code": "no_lora", "message": "no LoRA " + lora}}
                if m.group(1) == "info" and method == "GET":
                    return 200, self._info(lora)
                if m.group(1) == "info" and method == "POST":
                    try:
                        data = json.loads(body or b"null")
                    except ValueError:
                        data = None
                    ok = isinstance(data, dict) and all(
                        (k in ("trigger", "notes", "description") and isinstance(v, str)) or
                        (k == "weight" and (v is None or (isinstance(v, (int, float)) and not isinstance(v, bool))))
                        for k, v in data.items())
                    if not ok:
                        return 400, {"error": "the body is not an object of trigger, weight, notes, description"}
                    i = self.info.setdefault(lora, {})
                    for k, v in data.items():
                        if v in ("", None, 0):
                            i.pop(k, None)
                        else:
                            i[k] = v
                    return 200, self._info(lora)
                if m.group(1) == "lookup" and method == "POST":
                    return self._lookup(lora)
                if m.group(1) == "preview" and method == "GET":
                    if not self.info.get(lora, {}).get("preview"):
                        return 404, {"error": "no preview"}
                    return "raw", "image/png", self._png(lora)
            if method == "DELETE" and rest.startswith("/"):
                lora = urllib.parse.unquote(rest[1:])
                if lora not in self.files:
                    return 404, {"error": "no LoRA " + lora}
                del self.files[lora]
                self.info.pop(lora, None)
                return 200, self._json()
        return None


ERROR_CODES = {400: "invalid_request", 401: "unauthorized", 403: "forbidden", 404: "not_found", 409: "conflict",
               413: "payload_too_large", 429: "rate_limited", 500: "server_error", 502: "bad_gateway",
               503: "unavailable", 507: "storage_full"}


class MockStorage:
    """In-memory stand-in for the storage routes: the console's space, two USB drives, the gallery copy
    and the usb_copy setting. The copy advances with the clock, so a poll sees it move."""

    CONSOLE = {"path": "/data", "free": 6400 * MB}
    USED = {"models": 512 * GB, "loras": 4200 * MB, "gallery": 3100 * MB, "upscalers": 200 * MB}
    USB = [{"id": "usb0", "path": "/mnt/usb0", "free": 91 * GB},
           {"id": "usb1", "path": "/mnt/usb1", "free": 800 * MB}]

    def __init__(self, gallery_names):
        self.lock = threading.Lock()
        self.gallery_names = gallery_names   # callable: the names of the gallery, newest first
        self.usb_copy = ""
        self.job = None

    def free(self):
        return self.CONSOLE["free"]

    def _state(self):
        j = self.job
        if j is None:
            return {"state": "idle", "to": "", "done": 0, "total": 0, "skipped": 0, "bytes": 0, "error": ""}
        now = time.time()
        n = min(j["total"], int((now - j["t0"]) * j["rate"]))
        state, error = "copying", ""
        if j["fail_at"] is not None and n >= j["fail_at"]:
            n, state, error = j["fail_at"], "failed", "The drive is full (no space left on %s)." % j["to"]
        elif n >= j["total"]:
            state = "done"
        j["state"] = state
        skipped = n // 5 if j["whole"] else 0   # done counts the skipped files too
        done = n
        return {"state": state, "to": j["to"], "done": done, "total": j["total"], "skipped": skipped,
                "bytes": (done - skipped) * 2500 * 1000, "error": error}

    def handle(self, method, path, body):
        """Returns (status, object) or None when the path is not one of these routes."""
        route = path.split("?")[0]
        if not route.startswith(APP):
            return None
        route = route[len(APP):]
        with self.lock:
            if method == "GET" and route == "storage":
                return 200, {"console": dict(self.CONSOLE), "used": dict(self.USED), "usb": [dict(u) for u in self.USB]}
            if method == "GET" and route == "gallery/copy":
                return 200, self._state()
            if method == "POST" and route == "gallery/copy":
                try:
                    req = json.loads(body or b"{}")
                except ValueError:
                    return 400, {"error": "body is not JSON"}
                drive = str(req.get("to") or "")
                if drive not in [u["id"] for u in self.USB]:
                    return 400, {"error": "no such drive: %s" % drive}
                names = req.get("names")
                have = self.gallery_names()
                if names is not None:
                    if not isinstance(names, list) or not names or any(not isinstance(x, str) for x in names):
                        return 400, {"error": "names must be a list of file names"}
                    bad = [x for x in names if x not in have]
                    if bad:
                        return 400, {"error": "not in the gallery: %s" % bad[0]}
                if self.job is not None and self._state()["state"] == "copying":
                    return 409, {"error": "a copy is already running"}
                total = len(names) if names else len(have)
                # about 8 seconds for a whole gallery; usb1 is full at 60 percent
                self.job = {"to": drive, "total": total, "t0": time.time(), "rate": max(total / 8.0, 1.5),
                            "whole": not names, "fail_at": int(total * 0.6) if drive == "usb1" and total > 1 else None}
                return 202, self._state()
            if route == "settings":
                return None
        return None


class MockPrompts:
    """--mock-prompts: the prompt history of the app in memory (prompt_history.hpp): newest first, at most 50."""

    MAX = 50

    def __init__(self):
        self.lock = threading.Lock()
        self.items = []

    def handle(self, method, path, body):
        """Returns (status, object), or None for another route."""
        route = urllib.parse.urlparse(path).path
        m = re.match(r"^/ps5sd/v1/app/prompts(?:/(.*))?$", route)
        if not m:
            return None
        rest = m.group(1)
        with self.lock:
            if rest is None and method == "GET":
                return 200, {"prompts": list(self.items)}
            if rest is None and method == "POST":
                try:
                    j = json.loads(body or b"")
                except ValueError:
                    j = None
                if not isinstance(j, dict) or not isinstance(j.get("prompt"), str) \
                        or not isinstance(j.get("negative", ""), str) or not isinstance(j.get("model", ""), str):
                    return 400, {"error": {"code": "invalid_request", "message": "the body must be {\"prompt\": ...}"}}
                p, n, mo = j["prompt"].strip(), j.get("negative", "").strip(), j.get("model", "").strip()
                if not p:
                    return 400, {"error": {"code": "invalid_request", "message": "the prompt is empty"}}
                if len(p.encode()) > 8000 or len(n.encode()) > 8000 or len(mo.encode()) > 200:
                    return 400, {"error": {"code": "invalid_request", "message": "a text is too long"}}
                nxt = max([e["id"] for e in self.items] + [0]) + 1
                for i, e in enumerate(self.items):
                    if e["prompt"] == p and e["negative"] == n and e["model"] == mo:
                        if i == 0:
                            return 201, e
                        nxt = e["id"]
                        del self.items[i]
                        break
                entry = {"id": nxt, "prompt": p, "negative": n, "model": mo, "time": int(time.time())}
                self.items.insert(0, entry)
                del self.items[self.MAX:]
                return 201, entry
            if rest is None and method == "DELETE":
                n = len(self.items)
                self.items = []
                return 200, {"deleted": n}
            if rest is not None and method == "DELETE":
                if not rest.isdigit():
                    return 400, {"error": {"code": "invalid_request", "message": "not a prompt id: " + rest}}
                before = len(self.items)
                self.items = [e for e in self.items if e["id"] != int(rest)]
                if len(self.items) == before:
                    return 404, {"error": {"code": "not_found", "message": "no prompt " + rest}}
                return 200, {"deleted": int(rest)}
        return None


class MockPromptBooks:
    """--mock-promptbooks: the prompt collections of the app in memory (prompt_books.hpp), with its rules: a name
    of 1 to 40 letters, digits, - and _ (unique without regard to case, "lora" reserved), entries of 1 to 8000
    bytes, at most 200 collections and 2000 entries in one."""

    def __init__(self):
        self.lock = threading.Lock()
        self.books = []   # [{"name", "time", "entries": [{"id", "prompt", "negative"}], "_next"}]

    @staticmethod
    def err(code, status, msg):
        return status, {"error": {"code": code, "message": msg}}

    def _find(self, name):
        for b in self.books:
            if b["name"].lower() == name.lower():
                return b
        return None

    @staticmethod
    def _out(b):
        return {"name": b["name"], "time": b["time"], "entries": [dict(e) for e in b["entries"]]}

    def _name_error(self, name):
        if not isinstance(name, str) or not re.match(r"^[A-Za-z0-9_-]{1,40}$", name):
            return self.err("bad_request", 400, "a name has 1 to 40 letters, digits, - and _")
        if name.lower() == "lora":
            return self.err("bad_request", 400, "lora is reserved for the trigger words of the LoRAs")
        return None

    def _entry(self, j, need_prompt=True):
        """(prompt, negative) trimmed, or an error tuple."""
        if not isinstance(j, dict):
            return self.err("bad_request", 400, "the body must be an object")
        for k in ("prompt", "negative"):
            if k in j and not isinstance(j[k], str):
                return self.err("bad_request", 400, k + " must be a string")
        p = j.get("prompt", "").strip() if "prompt" in j else None
        n = j.get("negative", "").strip() if "negative" in j else None
        if need_prompt and not p:
            return self.err("bad_request", 400, "the prompt is empty")
        if p is not None and "prompt" in j and not p:
            return self.err("bad_request", 400, "the prompt is empty")
        if (p and len(p.encode()) > 8000) or (n and len(n.encode()) > 8000):
            return self.err("bad_request", 400, "a text is too long")
        return p, n

    def _entries(self, lst):
        if not isinstance(lst, list):
            return self.err("bad_request", 400, "entries must be a list")
        if len(lst) > 2000:
            return self.err("too_many", 400, "a collection holds at most 2000 entries")
        out = []
        for i, e in enumerate(lst):
            r = self._entry(e)
            if r[0] == 400 and isinstance(r[1], dict):
                return r
            out.append({"id": i + 1, "prompt": r[0], "negative": r[1] or ""})
        return out

    def handle(self, method, path, body):
        """Returns (status, object), or None for another route."""
        route = urllib.parse.urlparse(path).path
        m = re.match(r"^/ps5sd/v1/app/promptbooks(?:/([^/]*))?(?:/entries(?:/([^/]*))?)?$", route)
        if not m:
            return None
        name = urllib.parse.unquote(m.group(1)) if m.group(1) is not None else None
        eid = m.group(2)
        entries = "/entries" in route
        j = None
        if method == "POST":
            try:
                j = json.loads(body or b"")
            except ValueError:
                j = None
        with self.lock:
            if name is None:
                if method == "GET":
                    return 200, {"books": [self._out(b) for b in self.books]}
                if method == "POST":
                    if not isinstance(j, dict):
                        return self.err("bad_request", 400, "the body must be an object")
                    bad = self._name_error(j.get("name"))
                    if bad:
                        return bad
                    if self._find(j["name"]):
                        return self.err("conflict", 409, "a collection with this name is there")
                    if len(self.books) >= 200:
                        return self.err("too_many", 400, "at most 200 collections")
                    es = self._entries(j.get("entries", []))
                    if isinstance(es, tuple):
                        return es
                    b = {"name": j["name"], "time": int(time.time()), "entries": es}
                    self.books.append(b)
                    return 201, self._out(b)
                return None
            b = self._find(name)
            if not entries:
                if method == "GET":
                    return (200, self._out(b)) if b else self.err("not_found", 404, "no collection " + name)
                if method == "DELETE":
                    if not b:
                        return self.err("not_found", 404, "no collection " + name)
                    self.books.remove(b)
                    return 200, {"deleted": b["name"]}
                if method == "POST":
                    if not b:
                        return self.err("not_found", 404, "no collection " + name)
                    if not isinstance(j, dict):
                        return self.err("bad_request", 400, "the body must be an object")
                    new = j.get("name")
                    if "name" in j:
                        bad = self._name_error(new)
                        if bad:
                            return bad
                        other = self._find(new)
                        if other and other is not b:
                            return self.err("conflict", 409, "a collection with this name is there")
                    if "entries" in j:
                        es = self._entries(j["entries"])
                        if isinstance(es, tuple):
                            return es
                        b["entries"] = es
                    if "name" in j:
                        b["name"] = new
                    b["time"] = int(time.time())
                    return 200, self._out(b)
                return None
            # entries
            if eid is None and method == "POST":
                r = self._entry(j)
                if r[0] == 400 and isinstance(r[1], dict):
                    return r
                p, n = r[0], r[1] or ""
                bad = None if b else self._name_error(name)
                if bad:
                    return bad
                if not b:
                    if len(self.books) >= 200:
                        return self.err("too_many", 400, "at most 200 collections")
                    b = {"name": name, "time": int(time.time()), "entries": []}
                    self.books.append(b)
                for e in b["entries"]:
                    if e["prompt"] == p and e["negative"] == n:
                        return 200, dict(e)
                if len(b["entries"]) >= 2000:
                    return self.err("too_many", 400, "a collection holds at most 2000 entries")
                e = {"id": max([x["id"] for x in b["entries"]] + [b.get("last", 0)]) + 1, "prompt": p, "negative": n}
                b["last"] = e["id"]
                b["entries"].append(e)
                b["time"] = int(time.time())
                return 201, dict(e)
            if eid is not None and eid.isdigit() and method in ("POST", "DELETE"):
                e = None
                for x in (b["entries"] if b else []):
                    if x["id"] == int(eid):
                        e = x
                if not e:
                    return self.err("not_found", 404, "no entry " + eid)
                if method == "DELETE":
                    b["entries"].remove(e)
                    b["time"] = int(time.time())
                    return 200, {"deleted": int(eid)}
                r = self._entry(j, need_prompt=False)
                if r[0] == 400 and isinstance(r[1], dict):
                    return r
                if r[0] is not None:
                    e["prompt"] = r[0]
                if r[1] is not None:
                    e["negative"] = r[1]
                b["time"] = int(time.time())
                return 200, dict(e)
        return None


def make_png(w, h, rgb):
    """A solid-colour PNG of w x h (RGB, 8 bit)."""
    raw = (b"\x00" + bytes(rgb) * w) * h

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))


class MockGenerate:
    """--mock-generate: img_gen jobs that finish half a second after they were posted, with a solid PNG of the
    requested size. The bodies it got are kept for GET /__mock/requests."""

    def __init__(self):
        self.lock = threading.Lock()
        self.jobs = {}
        self.requests = []
        self.next_id = 1

    @staticmethod
    def colour(prompt):
        p = prompt.lower()
        for word, rgb in (("red", (220, 40, 40)), ("green", (40, 180, 70)), ("blue", (50, 90, 220))):
            if word in p:
                return rgb
        d = hashlib.md5(p.encode()).digest()
        return (d[0], d[1], d[2])

    def handle(self, method, path, body):
        """Returns (status, object) or None."""
        route = urllib.parse.urlparse(path).path
        if route == "/__mock/requests":
            with self.lock:
                if method == "DELETE":
                    self.requests = []
                    return 200, {"requests": []}
                return 200, {"requests": list(self.requests)}
        if method == "GET" and route == "/sdcpp/v1/capabilities":
            return 200, {"supported_modes": ["img_gen"], "features": {"cancel_generating": True},
                         "limits": {"max_repeat": 16}, "upscalers": []}
        if method == "POST" and route in ("/sdcpp/v1/img_gen", "/sdapi/v1/txt2img"):
            try:
                j = json.loads(body or b"{}")
            except ValueError:
                return 400, {"error": "bad json"}
            def shown(v):
                # a long text (a picture) as its size; a PNG as its pixels too
                if not (isinstance(v, str) and len(v) > 200):
                    return v
                try:
                    raw = base64.b64decode(v.split(",", 1)[1] if v.startswith("data:") else v)
                    if raw[:8] == b"\x89PNG\r\n\x1a\n":
                        return "<PNG %dx%d>" % struct.unpack(">II", raw[16:24])
                except (ValueError, IndexError, struct.error):
                    pass
                return "<%d chars>" % len(v)
            cut = {k: shown(v) for k, v in j.items()}
            if isinstance(j.get("ref_images"), list):
                cut["ref_images"] = [shown(x) for x in j["ref_images"]]
            with self.lock:
                self.requests.append({"route": route, "body": cut})
                jid = "mock-%d" % self.next_id
                self.next_id += 1
                self.jobs[jid] = {"t": time.time(), "body": j}
            return 202, {"id": jid, "status": "queued"}
        m = re.match(r"^/sdcpp/v1/jobs/(mock-\d+)(/cancel)?$", route)
        if m:
            with self.lock:
                job = self.jobs.get(m.group(1))
            if not job:
                return 404, {"error": "no such job"}
            if m.group(2):
                return 200, {"id": m.group(1), "status": "cancelled"}
            if time.time() - job["t"] < 0.5:
                return 200, {"id": m.group(1), "status": "generating", "kind": "img_gen",
                             "preview": {"step": 1, "total_steps": 4, "pass": 1}}
            b = job["body"]
            w, h = int(b.get("width") or 512), int(b.get("height") or 512)
            png = make_png(w, h, self.colour(str(b.get("prompt", ""))))
            return 200, {"id": m.group(1), "status": "completed", "kind": "img_gen", "prompt": b.get("prompt", ""),
                         "result": {"images": [{"b64_json": base64.b64encode(png).decode()}]}}
        return None


class MockGallery:
    """--mock-gallery N: a gallery of N images in memory, newest first, paged as the server pages it
    (limit 1 to 1000, default 100, offset, X-Total-Count). Every image and thumbnail is the title's icon."""

    def __init__(self, count, favourites=True):
        self.lock = threading.Lock()
        self.favourites = favourites
        self.labels = {}   # {name: {"title", "tags", "fav"}}, as the app keeps them
        now = int(time.time())
        self.items = [{"name": "mock_%05d.png" % i, "created": now - i * 60, "size": 1500000, "type": "image",
                       "prompt": "mock image %d" % i} for i in range(count)]

    def names(self):
        with self.lock:
            return [x["name"] for x in self.items]

    def _labels(self, body):
        """POST gallery/labels as the app answers it (gallery_labels.cpp): the name and tags, or only the star."""
        try:
            j = json.loads(body or b"")
        except ValueError:
            j = None
        if not isinstance(j, dict) or not isinstance(j.get("name"), str):
            return 400, {"error": {"code": "invalid_request", "message": "the body must be {\"name\": ...}"}}, {}
        name = j["name"]
        with self.lock:
            if not any(x["name"] == name for x in self.items):
                return 404, {"error": {"code": "not_found", "message": "no file " + name + " in the gallery"}}, {}
            old = self.labels.get(name, {"title": "", "tags": [], "fav": False})
            title, tags, fav = old["title"], old["tags"], old["fav"]
            if "title" in j or "tags" in j or "fav" not in j:
                title = str(j.get("title", "")).strip()
                tags = [str(t).strip().lstrip("#").strip() for t in j.get("tags", []) if str(t).strip()]
            if "fav" in j and self.favourites:
                fav = bool(j["fav"])
            entry = {"title": title, "tags": tags}
            if fav:
                entry["fav"] = True
            if title or tags or fav:
                self.labels[name] = entry
            else:
                self.labels.pop(name, None)
            out = {"name": name, "title": title, "tags": tags}
            if self.favourites:
                out["fav"] = fav
            return 200, out, {}

    def handle(self, method, path, body=None):
        """Returns (status, object, headers), ("png", None, None) for a picture, or None for another route."""
        u = urllib.parse.urlparse(path)
        q = urllib.parse.parse_qs(u.query)
        route = u.path
        if method == "GET" and route == APP + "gallery/labels":
            with self.lock:
                out = {"labels": dict(self.labels)}
            if self.favourites:
                out["favourites"] = True
            return 200, out, {}
        if method == "POST" and route == APP + "gallery/labels":
            return self._labels(body)
        if route == "/ps5sd/v1/gallery" and method == "GET":
            def number(key, default, lo, hi):
                try:
                    return max(lo, min(hi, int(q.get(key, [default])[0])))
                except ValueError:
                    return default
            limit, offset = number("limit", 100, 1, 1000), number("offset", 0, 0, 1 << 30)
            with self.lock:
                items = list(self.items)
            if q.get("temporary", [""])[0] == "1":
                items = []
            kind = q.get("type", [""])[0]
            if kind:
                items = [x for x in items if x["type"] == kind]
            if q.get("order", [""])[0] == "oldest":
                items.reverse()
            page = [{"name": x["name"], "url": "/ps5sd/v1/gallery/" + x["name"],
                     "thumb": "/ps5sd/v1/gallery/thumb/" + x["name"], "size": x["size"], "created": x["created"],
                     "type": x["type"], "prompt": x["prompt"]} for x in items[offset:offset + limit]]
            return 200, page, {"X-Total-Count": str(len(items))}
        m = re.match(r"^/ps5sd/v1/gallery/(info/|thumb/)?(mock_\d+\.png)$", route)
        if not m:
            return None
        with self.lock:
            there = any(x["name"] == m.group(2) for x in self.items)
            if method == "DELETE" and not m.group(1) and there:
                self.items = [x for x in self.items if x["name"] != m.group(2)]
                return 200, {"deleted": m.group(2)}, {}
        if not there:
            return 404, {"error": "not found"}, {}
        if method == "GET" and m.group(1) == "info/":
            # what made it, as 1.1.0 writes it, so the viewer shows its info and Use all
            n = int(re.sub(r"\D", "", m.group(2)))
            return 200, {"name": m.group(2), "type": "image/png", "prompt": "mock image %d" % n,
                         "parameters": "mock image %d\nSteps: 20" % n,
                         "sdcpp": {"schema": "sdcpp.generation/v1", "prompt": "mock image %d" % n, "negative_prompt": "",
                                   "width": 1024, "height": 1024, "seed": n,
                                   "sample_params": {"sample_steps": 20, "sample_method": "euler", "guidance": {"txt_cfg": 6}},
                                   "models": {"model": "mock.safetensors"}}}, {}
        if method == "GET":
            return "png", None, None
        return None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    target = None
    mock = None
    models = None
    loras = None
    storage = None
    gallery = None
    prompts = None
    books = None
    generate = None
    read_only = False

    def log_message(self, fmt, *args):
        pass

    def _page(self):
        body = bundle()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _proxy(self, body):
        t = urllib.parse.urlparse(self.target)
        try:
            conn = http.client.HTTPConnection(t.hostname, t.port or 80, timeout=600)
            headers = {k: v for k, v in self.headers.items()
                       if k.lower() not in ("host", "connection", "accept-encoding")}
            conn.request(self.command, self.path, body=body, headers=headers)
            r = conn.getresponse()
            if (r.getheader("Content-Type") or "").startswith("text/event-stream"):
                self._stream(r)
                return
            data = r.read()
        except Exception as e:  # server down: behave like a dropped connection
            msg = ("proxy error: %s" % e).encode()
            self.send_response(502)
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)
            return
        self.send_response(r.status)
        for k, v in r.getheaders():
            if k.lower() in ("transfer-encoding", "connection", "content-length", "keep-alive"):
                continue
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _stream(self, r):
        # GET /ps5sd/v1/events: passed on as it comes, until either side closes it
        self.send_response(r.status)
        self.send_header("Content-Type", r.getheader("Content-Type"))
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            while True:
                chunk = r.read1(65536)
                if not chunk:
                    break
                self.wfile.write(chunk)
                self.wfile.flush()
        except OSError:
            pass

    def _json(self, code, obj, headers=None):
        # Every error of the API is {"error": {"code", "message"}}; the mocks write {"error": "text"}.
        if code >= 400 and isinstance(obj, dict) and isinstance(obj.get("error"), str):
            kind = "standby" if obj.get("standby") else ERROR_CODES.get(code, "http_%d" % code)
            obj = {"error": {"code": kind, "message": obj["error"]}}
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    @classmethod
    def _target_json(cls, path):
        """GET path from the console: (status, object), (0, None) when it does not answer."""
        t = urllib.parse.urlparse(cls.target)
        try:
            conn = http.client.HTTPConnection(t.hostname, t.port or 80, timeout=10)
            conn.request("GET", path, headers={"X-PS5SD-Request": "1"})
            r = conn.getresponse()
            return r.status, json.loads(r.read() or b"null")
        except Exception:
            return 0, None

    @classmethod
    def _gallery_names(cls):
        if cls.gallery is not None:
            return cls.gallery.names()
        code, data = cls._target_json("/ps5sd/v1/gallery?limit=1000")
        if code == 200 and isinstance(data, list) and data:
            return [x["name"] for x in data if isinstance(x, dict) and "name" in x]
        return ["mock_%03d.png" % i for i in range(40)]

    def _file(self, path, ctype):
        data = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _storage(self, body):
        """--mock-storage: True when the request was answered here, else None."""
        route = self.path.split("?")[0]
        if self.command == "GET" and route == APP + "icon.png":
            self._file(os.path.join(HERE, "..", "ps5", "app", "sce_sys", "icon0.png"), "image/png")
            return True
        if self.command == "GET" and route == APP + "manifest.webmanifest":
            icon = {"src": APP + "icon.png", "sizes": "512x512", "type": "image/png"}
            data = json.dumps({"name": "PS5SD", "short_name": "PS5SD", "start_url": "/", "display": "standalone",
                               "background_color": "#000000", "theme_color": "#000000", "icons": [icon]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/manifest+json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return True
        if route == APP + "settings":
            st = self.storage
            if self.command == "POST":
                try:
                    req = json.loads(body or b"{}")
                except ValueError:
                    return None
                if not isinstance(req, dict) or list(req.keys()) != ["usb_copy"]:
                    return None
                v = req["usb_copy"]
                if v != "" and v not in [u["id"] for u in st.USB]:
                    self._json(400, {"error": "no such drive: %s" % v})
                    return True
                st.usb_copy = v
            elif self.command != "GET":
                return None
            code, data = self._target_json(APP + "settings")
            data = data if code == 200 and isinstance(data, dict) else {}
            data["usb_copy"] = st.usb_copy
            self._json(200, data)
            return True
        res = self.storage.handle(self.command, self.path, body)
        if res is not None:
            self._json(res[0], res[1])
            return True
        return None

    def _dispatch(self):
        if self.command == "GET" and self.path.split("?")[0] in ("/", "/index.html"):
            self._page()
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        res = None
        if self.gallery is not None:
            g = self.gallery.handle(self.command, self.path, body)
            if g is not None and g[0] == "png":
                self._file(os.path.join(HERE, "..", "ps5", "app", "sce_sys", "icon0.png"), "image/png")
                return
            if g is not None:
                self._json(g[0], g[1], g[2])
                return
        if self.generate is not None:
            g = self.generate.handle(self.command, self.path, body)
            if g is not None:
                self._json(g[0], g[1])
                return
        if self.prompts is not None and re.match(r"^/ps5sd/v1/app/prompts(/|$)", self.path):
            res = self.prompts.handle(self.command, self.path, body)
            if res is not None:
                self._json(res[0], res[1])
                return
        if self.books is not None and re.match(r"^/ps5sd/v1/app/promptbooks(/|$)", self.path):
            res = self.books.handle(self.command, self.path, body)
            if res is not None:
                self._json(res[0], res[1])
                return
        if self.storage is not None and self._storage(body):
            return
        if self.models is not None and self.path.split("?")[0] == APP + "info":
            src = open(VERSION_HPP, encoding="utf-8").read()
            version = re.search(r'PS5SD_VERSION "([^"]*)"', src).group(1)
            self._json(200, {"name": "PS5SD", "version": version, "sd_cpp": {"version": "mock", "commit": "mock"}})
            return
        if self.models is not None and self.models.standby and not self.path.startswith("/ps5sd/"):
            with self.models.lock:
                standby = self.models._json()["standby"]
            if standby:
                self._json(503, {"error": "no model is loaded; pick or download one in the web UI, tab Models",
                                 "standby": True})
                return
        doc = re.match(r"^/ps5sd/v1/app/docs/(MODELS|API)\.md$", self.path)
        if self.models is not None and self.command == "GET" and doc:
            data = open(os.path.join(DOCS, doc.group(1) + ".md"), "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/markdown; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.models is not None and re.match(r"^/ps5sd/v1/app/(models|presets)(/|\?|$)", self.path):
            res = self.models.handle(self.command, self.path, body)
        elif self.loras is not None and re.match(r"^(/ps5sd/v1/app/(loras|tokens)|/sdapi/v1/(samplers|schedulers))(/|\?|$)", self.path):
            res = self.loras.handle(self.command, self.path, body)
            if res is not None and res[0] == "raw":
                data = res[2]
                self.send_response(200)
                self.send_header("Content-Type", res[1])
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
        elif self.mock is not None and re.match(r"^/ps5sd/v1/app/(catalog|downloads|hf/)", self.path):
            res = self.mock.handle(self.command, self.path, body)
        if res is not None:
            self._json(res[0], res[1])
            return
        if self.read_only and self.command not in ("GET", "HEAD", "OPTIONS"):
            self._json(403, {"error": "webui-dev.py --read-only: " + self.command + " is not sent to the console"})
            return
        self._proxy(body)

    do_GET = do_POST = do_PUT = do_DELETE = do_OPTIONS = _dispatch


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--target", default="http://10.0.0.101:7860")
    ap.add_argument("--mock-downloads", action="store_true",
                    help="answer catalog, downloads and Hugging Face search here (simulated)")
    ap.add_argument("--mock-models", action="store_true",
                    help="answer the model library (list, switch, delete; saved presets: save, delete; preset text),"
                         " the LoRAs (list, delete), the sampler and scheduler lists and the docs here;"
                         " implied by --mock-downloads")
    ap.add_argument("--read-only", action="store_true",
                    help="send only GET to the console: no job, cancel, delete or setting reaches it (the mocks still answer)")
    ap.add_argument("--mock-storage", action="store_true",
                    help="answer the storage and USB routes (storage, gallery/copy, the usb_copy setting, icon.png,"
                         " manifest.webmanifest) here, even with --read-only")
    ap.add_argument("--mock-gallery", type=int, metavar="N",
                    help="answer the gallery's list, files, info and labels here with N mock images (paged as the"
                         " server pages them: at most 1000 an answer)")
    ap.add_argument("--no-favourites", action="store_true",
                    help="with --mock-gallery: answer the labels route as an app from before 1.6.0 (no favourites)")
    ap.add_argument("--mock-prompts", action="store_true",
                    help="answer the prompt history (GET, POST, DELETE /ps5sd/v1/app/prompts) here; implied by --mock-models")
    ap.add_argument("--mock-promptbooks", action="store_true",
                    help="answer the prompt collections (/ps5sd/v1/app/promptbooks) here; implied by --mock-models")
    ap.add_argument("--mock-generate", action="store_true",
                    help="answer POST /sdcpp/v1/img_gen and GET /sdcpp/v1/jobs/<id> here with solid-colour PNGs; "
                         "GET /__mock/requests shows what was posted")
    ap.add_argument("--mock-standby", action="store_true",
                    help="start without a model, as sd-server --standby does; implies --mock-downloads")
    a = ap.parse_args()
    a.mock_downloads = a.mock_downloads or a.mock_standby
    Handler.target = a.target
    Handler.read_only = a.read_only
    if a.mock_gallery is not None:
        Handler.gallery = MockGallery(max(0, a.mock_gallery), not a.no_favourites)
    if a.mock_storage:
        Handler.storage = MockStorage(lambda: Handler._gallery_names())
    if a.mock_downloads:
        Handler.mock = MockDownloads(Handler.storage)
    if a.mock_downloads or a.mock_models:
        Handler.models = MockModels(Handler.mock)
        Handler.loras = MockLoras()
    if a.mock_prompts or Handler.models:
        Handler.prompts = MockPrompts()
    if a.mock_promptbooks or Handler.models:
        Handler.books = MockPromptBooks()
    if a.mock_generate:
        Handler.generate = MockGenerate()
    if a.mock_standby:
        Handler.models.standby = True
        Handler.models.active = Handler.models.loaded = ""
    print("serving %s on http://127.0.0.1:%d -> %s%s" % (SRC, a.port, a.target,
                                                      (" (mock downloads)" if a.mock_downloads else "") + (" (mock models)" if Handler.models else "") + (" (mock storage)" if Handler.storage else "") + (" (mock gallery)" if Handler.gallery else "")))
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()
