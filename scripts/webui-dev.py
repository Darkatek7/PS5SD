#!/usr/bin/env python3
"""Dev helper: serve the web UI at / and forward every other path to a PS5SD server.

    python scripts/webui-dev.py [--port 8787] [--target http://10.0.0.101:7860] [--mock-downloads] [--mock-models]

The page is put together from ps5/webui/src on every request (as scripts/bundle-webui.sh does
for the build), so an edit shows after a reload.

--mock-downloads answers the download routes of the app (catalog, downloads, Hugging Face
search) in this process, with simulated progress, so the Models tab can be tried before the
console has them. It also turns on --mock-models: the model library (list, switch, delete;
saved presets: save, delete; a preset's text: read, write) is then an in-memory fake too and the console is not
asked about models at all, so a delete or a switch cannot touch it. The LoRAs (list, delete; a few
fixed files, some in subfolders, with a family) and sd-server's sampler and scheduler lists are fakes
too, so the Generate tab and the preset editor work without a console. It also serves docs/MODELS.md
and docs/API.md from this checkout for the Help tab. --mock-models alone fakes only these, not the downloads. Everything else is still forwarded. Without the flag the real console answers
(an old app answers 404 and the page hides the download parts). Words in a URL steer the mock:
"fail" makes the download fail at 30 percent, "big" makes it 8 GB, "slow" makes it slow.
In a search, "err" gives an error and "none" gives no results.

--mock-standby starts like a fresh install: no model is loaded, the library answers
"standby": true, and every route outside /ps5sd/ answers 503 as sd-server --standby does,
until a switch (Use) has loaded a model. It implies --mock-downloads.
"""
import argparse
import http.client
import json
import os
import re
import threading
import time
import urllib.parse
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

    def __init__(self):
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
    """In-memory stand-in for GET /loras and DELETE /loras/<path>, and sd-server's sampler and scheduler lists."""

    SAMPLERS = ["euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2", "ipndm", "lcm"]
    SCHEDULERS = ["discrete", "karras", "exponential", "ays", "sgm_uniform", "simple"]
    # path: (bytes, family); "" is a LoRA whose family the server cannot tell
    FILES = {
        "add-detail-xl.safetensors": (228 * MB, "SDXL"),
        "sdxl-lightning-8step.safetensors": (394 * MB, "SDXL"),
        "styles/ink-sketch.safetensors": (171 * MB, "SDXL"),
        "styles/watercolor_v2.gguf": (85 * MB, "SDXL"),
        "styles/anime/flat-colors.safetensors": (57 * MB, "SDXL"),
        "flux/realism-lora.safetensors": (22 * MB, "FLUX.1"),
        "sd15/epi_noiseoffset2.safetensors": (81 * MB, "SD 1.5"),
        "mystery.safetensors": (12 * MB, ""),
    }

    def __init__(self):
        self.lock = threading.Lock()
        self.files = dict(self.FILES)

    def _json(self):
        return {"loras": [{"name": path.rsplit("/", 1)[-1].rsplit(".", 1)[0], "path": path, "bytes": b, "family": fam}
                          for path, (b, fam) in sorted(self.files.items())]}

    def handle(self, method, path, body):
        route = path.split("?")[0]
        if method == "GET" and route == "/sdapi/v1/samplers":
            return 200, [{"name": n, "aliases": [n], "options": {}} for n in self.SAMPLERS]
        if method == "GET" and route == "/sdapi/v1/schedulers":
            return 200, [{"name": n, "label": n} for n in self.SCHEDULERS]
        if not route.startswith(APP + "loras"):
            return None
        rest = route[len(APP + "loras"):]
        with self.lock:
            if method == "GET" and rest == "":
                return 200, self._json()
            if method == "DELETE" and rest.startswith("/"):
                lora = urllib.parse.unquote(rest[1:])
                if lora not in self.files:
                    return 404, {"error": "no LoRA " + lora}
                del self.files[lora]
                return 200, self._json()
        return None


ERROR_CODES = {400: "invalid_request", 401: "unauthorized", 403: "forbidden", 404: "not_found", 409: "conflict",
               413: "payload_too_large", 429: "rate_limited", 500: "server_error", 502: "bad_gateway",
               503: "unavailable", 507: "storage_full"}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    target = None
    mock = None
    models = None
    loras = None
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

    def _json(self, code, obj):
        # Every error of the API is {"error": {"code", "message"}}; the mocks write {"error": "text"}.
        if code >= 400 and isinstance(obj, dict) and isinstance(obj.get("error"), str):
            kind = "standby" if obj.get("standby") else ERROR_CODES.get(code, "http_%d" % code)
            obj = {"error": {"code": kind, "message": obj["error"]}}
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _dispatch(self):
        if self.command == "GET" and self.path.split("?")[0] in ("/", "/index.html"):
            self._page()
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        res = None
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
        elif self.loras is not None and re.match(r"^(/ps5sd/v1/app/loras|/sdapi/v1/(samplers|schedulers))(/|\?|$)", self.path):
            res = self.loras.handle(self.command, self.path, body)
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
    ap.add_argument("--mock-standby", action="store_true",
                    help="start without a model, as sd-server --standby does; implies --mock-downloads")
    a = ap.parse_args()
    a.mock_downloads = a.mock_downloads or a.mock_standby
    Handler.target = a.target
    Handler.read_only = a.read_only
    if a.mock_downloads:
        Handler.mock = MockDownloads()
    if a.mock_downloads or a.mock_models:
        Handler.models = MockModels(Handler.mock)
        Handler.loras = MockLoras()
    if a.mock_standby:
        Handler.models.standby = True
        Handler.models.active = Handler.models.loaded = ""
    print("serving %s on http://127.0.0.1:%d -> %s%s" % (SRC, a.port, a.target,
                                                      (" (mock downloads)" if a.mock_downloads else "") + (" (mock models)" if Handler.models else "")))
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()
