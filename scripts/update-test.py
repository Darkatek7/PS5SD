#!/usr/bin/env python3
"""Does an update keep everything? Installs each old release on the console, makes state with it
through its API, updates to the new build, and checks that the state is still there.

    python3 scripts/update-test.py --releases DIR --new build/app-ps5/title/PPSA99576 [--only 0.9.5,...]

DIR holds <version>/PPSA99576.zip for every release to test (gh release download v<version> -p
PPSA99576.zip -D DIR/<version>). Needs the console with ftpsrv, elfldr and klogsrv, and
scripts/ps5sd-app.sh (deploy, launch, close). The app's own files in /data/PS5SD (settings, presets,
model.txt, server-args.txt, env.txt, the API key, the download queue, the gallery's names and tags)
are saved first, to DIR/saved-state.json, and put back at the end; models and images are not touched. A run
that was stopped leaves that file, and the next run puts it back first (--restore only does that). The
model during the test is sdxl-base, which every release since 0.5.0 loads.

What each old release makes, where it has the route (a route it lacks is skipped and not checked):
  settings      tv_images false; image_metadata false
  preset        a saved preset of sdxl-base, "Update test", steps 7, cfg 5
  label         a name and a tag on the newest gallery image
  default       that preset as the default of the SDXL file (default_presets)
  active        a switch to that preset (model.txt)
  key           the API key turned on, last; the new build must take the same key
"""
import argparse
import base64
import ftplib
import io
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile

HOST = os.environ.get("PS5_HOST", "10.0.0.101")
API = "http://%s:7860" % HOST
DATA = "/data/PS5SD"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TITLE = os.path.join(ROOT, "build", "app-ps5", "title", "PPSA99576")
SAVED = ["model.txt", "server-args.txt", "env.txt", "settings.json", "api-key.txt", "downloads.json",
         "outputs/.labels.json"]
SAVED_DIRS = ["presets", "presets-backup"]
SDXL_FILE = "stable-diffusion-xl-base-1.0-Q8_0.gguf"
# On Windows "bash" is WSL's; ps5sd-app.sh runs in Git Bash.
GIT_BASH = os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "Git", "bin", "bash.exe")
BASH = os.environ.get("PS5SD_BASH") or (GIT_BASH if os.name == "nt" and os.path.exists(GIT_BASH) else "bash")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ---- the console's files, over ftpsrv
def ftp():
    f = ftplib.FTP()
    f.connect(HOST, 2121, timeout=60)
    f.login()
    return f


def ftp_get(f, path):
    buf = io.BytesIO()
    try:
        f.retrbinary("RETR " + path, buf.write)
    except ftplib.error_perm:
        return None
    return buf.getvalue()


def ftp_put(f, path, data):
    f.storbinary("STOR " + path, io.BytesIO(data))


def ftp_delete(f, path):
    try:
        f.delete(path)
    except ftplib.error_perm:
        pass


def ftp_names(f, d):
    try:
        return [n.rsplit("/", 1)[-1] for n in f.nlst(d) if not n.endswith("/.") and not n.endswith("/..")]
    except ftplib.error_perm:
        return []


def save_state():
    f = ftp()
    state = {"files": {p: ftp_get(f, "%s/%s" % (DATA, p)) for p in SAVED}, "dirs": {}}
    for d in SAVED_DIRS:
        state["dirs"][d] = {n: ftp_get(f, "%s/%s/%s" % (DATA, d, n)) for n in ftp_names(f, "%s/%s" % (DATA, d))
                            if n not in (".", "..")}
    f.quit()
    return state


def write_saved(state, path):
    enc = {"files": {p: (base64.b64encode(d).decode() if d is not None else None) for p, d in state["files"].items()},
           "dirs": {k: {n: base64.b64encode(d).decode() for n, d in v.items() if d is not None}
                    for k, v in state["dirs"].items()}}
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(enc, f)
    os.replace(path + ".tmp", path)


def read_saved(path):
    enc = json.load(open(path, encoding="utf-8"))
    return {"files": {p: (base64.b64decode(d) if d is not None else None) for p, d in enc["files"].items()},
            "dirs": {k: {n: base64.b64decode(d) for n, d in v.items()} for k, v in enc["dirs"].items()}}


def put_state(state, model=None):
    """The saved files back (a file that was not there is deleted); model: model.txt instead."""
    f = ftp()
    for p, data in state["files"].items():
        if p == "model.txt" and model is not None:
            ftp_put(f, "%s/%s" % (DATA, p), (model + "\n").encode())
        elif data is None:
            ftp_delete(f, "%s/%s" % (DATA, p))
        else:
            ftp_put(f, "%s/%s" % (DATA, p), data)
    for d, files in state["dirs"].items():
        for n in ftp_names(f, "%s/%s" % (DATA, d)):
            if n not in files and n not in (".", ".."):
                ftp_delete(f, "%s/%s/%s" % (DATA, d, n))
        for n, data in files.items():
            if data is not None:
                ftp_put(f, "%s/%s/%s" % (DATA, d, n), data)
    f.quit()


# ---- the app
def app(*args, timeout=900):
    script = os.path.join(ROOT, "scripts", "ps5sd-app.sh").replace(os.sep, "/")  # bash on Windows: no backslashes
    p = subprocess.Popen([BASH, script] + list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         start_new_session=os.name != "nt",
                         creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0)
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
        else:
            os.killpg(p.pid, 9)
        return 1, ["ps5sd-app.sh %s: no end after %d s" % (" ".join(args), timeout)]
    return p.returncode, out.strip().splitlines()[-1:] or [""]


def install(source):
    """source: a release zip, or a title folder."""
    shutil.rmtree(TITLE, ignore_errors=True)
    if source.endswith(".zip"):
        zipfile.ZipFile(source).extractall(os.path.dirname(TITLE))
    else:
        shutil.copytree(source, TITLE)
    rc, last = app("deploy")
    if rc != 0:
        raise RuntimeError("deploy: %s" % last)


def call(method, path, body=None, key=None, timeout=120):
    headers = {"Content-Type": "application/json", "X-PS5SD-Request": "1"}
    if key:
        headers["X-API-Key"] = key
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read() or b"null")
        except ValueError:
            return e.code, None
    except (OSError, ValueError):
        return 0, None


def start(key=None, model_wanted="sdxl-base", timeout=240):
    """Launches the title and waits until it has a model loaded (or reports standby)."""
    rc, last = app("launch")
    if rc != 0:
        raise RuntimeError("launch: %s" % last)
    t0 = time.time()
    while time.time() - t0 < timeout:
        st, d = call("GET", "/ps5sd/v1/app/models", key=key, timeout=5)
        if st == 200 and d and not d.get("switching") and (d.get("loaded") or d.get("standby")):
            return d
        time.sleep(2)
    raise RuntimeError("the title did not come up in %d s" % timeout)


def stop():
    rc, last = app("close", timeout=180)
    time.sleep(3)
    return last


def version(key=None):
    st, d = call("GET", "/ps5sd/v1/app/info", key=key)
    return d.get("version") if st == 200 and d else "?"


# ---- one release
def make_state():
    made = {}
    # a setting counts once the release says it has it (an older one answers 200 and drops a field it does not know)
    for k in ("tv_images", "image_metadata"):
        st, _ = call("POST", "/ps5sd/v1/app/settings", {k: False})
        st2, now = call("GET", "/ps5sd/v1/app/settings")
        if st == 200 and st2 == 200 and (now or {}).get(k) is False:
            made[k] = False
    st, d = call("POST", "/ps5sd/v1/app/presets",
                 {"name": "Update test", "base": "sdxl-base", "defaults": {"steps": "7", "cfg": "5"}})
    if st in (200, 201) and d and d.get("id"):
        made["preset"] = d["id"]
    st, g = call("GET", "/ps5sd/v1/gallery?limit=1")
    if st == 200 and isinstance(g, list) and g:
        name = g[0]["name"]
        st, _ = call("POST", "/ps5sd/v1/app/gallery/labels", {"name": name, "title": "update test", "tags": ["update-test"]})
        if st == 200:
            made["label"] = name
    if "preset" in made:
        st, _ = call("POST", "/ps5sd/v1/app/settings", {"default_presets": {SDXL_FILE: made["preset"]}})
        st2, s = call("GET", "/ps5sd/v1/app/settings")
        if st == 200 and st2 == 200 and (s or {}).get("default_presets", {}).get(SDXL_FILE) == made["preset"]:
            made["default"] = made["preset"]
        st, _ = call("POST", "/ps5sd/v1/app/models/active", {"id": made["preset"]})
        if st in (200, 202):
            for _ in range(120):
                st, d = call("GET", "/ps5sd/v1/app/models", timeout=5)
                if st == 200 and d and not d.get("switching") and d.get("loaded") == made["preset"]:
                    made["active"] = made["preset"]
                    break
                time.sleep(2)
    st, d = call("POST", "/ps5sd/v1/app/auth/enable", {})
    if st == 200 and d and d.get("key"):
        made["key"] = d["key"]
    return made


def check_state(made):
    key = made.get("key")
    problems = []
    st, s = call("GET", "/ps5sd/v1/app/settings", key=key)
    if st != 200:
        return ["settings: %s (the key %s)" % (st, "kept?" if key else "none")]
    for k in ("tv_images", "image_metadata"):
        if k in made and s.get(k) != made[k]:
            problems.append("settings.%s is %s, not %s" % (k, s.get(k), made[k]))
    if "default" in made and (s.get("default_presets") or {}).get(SDXL_FILE) != made["default"]:
        problems.append("default_presets: %s" % s.get("default_presets"))
    if "preset" in made:
        st, p = call("GET", "/ps5sd/v1/app/presets/" + made["preset"], key=key)
        if st != 200 or "steps=7" not in (p or {}).get("text", ""):
            problems.append("preset %s: %s %s" % (made["preset"], st, (p or {}).get("text", "")[:80]))
    if "active" in made:
        st, d = call("GET", "/ps5sd/v1/app/models", key=key)
        if st != 200 or d.get("active") != made["active"] or d.get("loaded") != made["active"]:
            problems.append("active %s, loaded %s, not %s" % ((d or {}).get("active"), (d or {}).get("loaded"), made["active"]))
    if "label" in made:
        st, l = call("GET", "/ps5sd/v1/app/gallery/labels", key=key)
        got = ((l or {}).get("labels") or {}).get(made["label"])
        if st != 200 or not got or got.get("title") != "update test" or "update-test" not in got.get("tags", []):
            problems.append("label of %s: %s" % (made["label"], got))
    if key:
        st, _ = call("GET", "/ps5sd/v1/app/settings")
        if st != 401:
            problems.append("without the key: %s, not 401" % st)
        st, a = call("GET", "/ps5sd/v1/app/auth", key=key)
        if st != 200 or not (a or {}).get("enabled") or (a or {}).get("key") != key:
            problems.append("the API key: %s %s" % (st, a))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--releases", required=True)
    ap.add_argument("--new", required=True, help="the new build's title folder (a copy is taken)")
    ap.add_argument("--only", default="")
    ap.add_argument("--restore", action="store_true", help="only put back the files a stopped run saved")
    a = ap.parse_args()
    new_copy = os.path.join(a.releases, "new-PPSA99576")
    shutil.rmtree(new_copy, ignore_errors=True)
    shutil.copytree(a.new, new_copy)
    versions = sorted((v for v in os.listdir(a.releases) if os.path.exists(os.path.join(a.releases, v, "PPSA99576.zip"))),
                      key=lambda v: [int(x) for x in v.split(".")])
    if a.only:
        versions = [v for v in versions if v in a.only.split(",")]
    saved_path = os.path.join(a.releases, "saved-state.json")
    stop()
    if os.path.exists(saved_path):
        log("a run before was stopped: putting its saved files back first")
        put_state(read_saved(saved_path))
    if a.restore:
        if os.path.exists(saved_path):
            install(new_copy)
            start()
            os.remove(saved_path)
        return 0
    log("saving the app's files")
    saved = save_state()
    write_saved(saved, saved_path)
    results = []
    try:
        for v in versions:
            log("== %s" % v)
            try:
                put_state(saved, model="sdxl-base")
                install(os.path.join(a.releases, v, "PPSA99576.zip"))
                start()
                got = version()
                if got != v:
                    raise RuntimeError("the console runs %s, not %s" % (got, v))
                made = make_state()
                log("%s (says %s) made: %s" % (v, got, ", ".join(k for k in made if k != "key") + (", key" if "key" in made else "")))
                stop()
                install(new_copy)
                start(key=made.get("key"))
                new = version(made.get("key"))
                problems = check_state(made)
                stop()
                results.append((v, sorted(made), problems))
                log("%s -> %s: %s" % (v, new, "kept everything" if not problems else "; ".join(problems)))
            except Exception as e:  # noqa: BLE001 - a release that fails is a result, the run goes on
                results.append((v, [], ["error: %s" % e]))
                log("%s: error: %s" % (v, e))
                stop()
    finally:
        log("putting the app's files back")
        put_state(saved)
        install(new_copy)
        start(model_wanted=None)
        os.remove(saved_path)
        log("the new build runs again with the saved files")
    print()
    print("| From | Made | Result |")
    print("|---|---|---|")
    for v, made, problems in results:
        print("| %s | %s | %s |" % (v, " ".join(m for m in made), "kept" if not problems else "; ".join(problems)))
    return 1 if any(p for _, _, p in results) else 0


if __name__ == "__main__":
    sys.exit(main())
