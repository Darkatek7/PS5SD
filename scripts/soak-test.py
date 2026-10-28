#!/usr/bin/env python3
"""A long mixed load on the console, to find what a short test does not: a crash, a leak, a stuck job.

    python3 scripts/soak-test.py [--minutes 60]

Round after round, until the time is up: a switch to each model of the list, images on it (one with a LoRA
on FLUX.2 Klein 9B), a short video on Wan 2.1, three jobs queued at once, one job cancelled while it runs.
Meanwhile a catalog download runs (SDXS-512, 0.7 GB, only when the console does not have its file; deleted
at the end only then), an event stream stays open and
GET /ps5sd/v1/health is asked once a second. At the end: every failure, how the CPU heap and the pool went
(the app's "memory after a request" lines), and whether the title answered all the time. The model the
console had is loaded again at the end. Exit status 1 when anything failed that should not have.
"""
import argparse
import json
import os
import re
import threading
import time
import urllib.error
import urllib.request

HOST = os.environ.get("PS5_HOST", "10.0.0.101")
API = "http://%s:7860" % HOST
LOG = "ftp://%s:2121/data/PS5SD/app.log" % HOST
PROMPT = "a lighthouse on a cliff at dusk, waves, warm light in the windows"


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def call(method, path, body=None, timeout=1800):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read() or b"null")
        except ValueError:
            return e.code, None
    except (OSError, ValueError) as e:
        return 0, {"error": {"code": "no_answer", "message": str(e)}}


stop = False
health = {"ok": 0, "fail": 0, "longest_gap_s": 0.0}
stream = {"events": 0, "ends": 0}
failures = []


def probe():
    last_ok = time.time()
    while not stop:
        try:
            urllib.request.urlopen(API + "/ps5sd/v1/health", timeout=3).read()
            health["ok"] += 1
            last_ok = time.time()
        except Exception:  # noqa: BLE001 - every kind of no answer counts
            health["fail"] += 1
            health["longest_gap_s"] = max(health["longest_gap_s"], time.time() - last_ok)
        time.sleep(1)


def events():
    while not stop:
        try:
            with urllib.request.urlopen(API + "/ps5sd/v1/events", timeout=120) as r:
                for line in r:
                    if line.startswith(b"event:"):
                        stream["events"] += 1
                    if stop:
                        return
        except Exception:  # noqa: BLE001
            pass
        stream["ends"] += 1
        time.sleep(2)


def wait_job(jid, label, expect=("completed",)):
    t0 = time.time()
    while True:
        st, j = call("GET", "/sdcpp/v1/jobs/" + jid)
        if st != 200:
            failures.append("%s: job %s answered %s" % (label, jid, st))
            return None
        if j["status"] in ("completed", "failed", "cancelled"):
            break
        time.sleep(2)
    took = time.time() - t0
    err = j.get("error") or {}
    log("  %s: %s in %.0f s%s" % (label, j["status"], took, " (%s)" % err.get("code") if err else ""))
    if j["status"] not in expect:
        failures.append("%s: %s %s" % (label, j["status"], err.get("message", "")[:160]))
    return j


def job(path, body, label, expect=("completed",)):
    st, d = call("POST", path, body)
    if st != 202:
        failures.append("%s: refused %s %s" % (label, st, d))
        return None
    return wait_job(d["id"], label, expect)


def switch(model):
    t0 = time.time()
    st, d = call("POST", "/sdapi/v1/options", {"sd_model_checkpoint": model})
    log("switch to %s: %s in %.0f s" % (model, st, time.time() - t0))
    if st != 200:
        failures.append("switch to %s: %s %s" % (model, st, d))
    return st == 200


def memory_lines():
    text = urllib.request.urlopen(LOG, timeout=60).read().decode("utf-8", "replace")
    out = []
    for l in text.splitlines():
        m = re.match(r"ps5sd: memory after a request: pool free ([\d.]+) .*the largest ([\d.]+)\), cpu heap ([\d.]+) GiB "
                     r"\(peak ([\d.]+)\)", l)
        if m:
            out.append(tuple(float(x) for x in m.groups()))
    return out, [l for l in text.splitlines() if "ggml ran out" in l or "GGML_ASSERT" in l or "terminate" in l]


def round_(n):
    img = {"prompt": PROMPT, "seed": n, "temporary": True}
    if switch("z-image-turbo"):
        job("/sdcpp/v1/img_gen", dict(img, width=1024, height=1024), "Z-Image 1024")
    if switch("sdxl-base"):
        # three at once: the queue
        ids = []
        for i in range(3):
            st, d = call("POST", "/sdcpp/v1/img_gen", dict(img, seed=n * 10 + i, width=1024, height=1024))
            if st == 202:
                ids.append(d["id"])
            else:
                failures.append("SDXL queue %d: refused %s" % (i, st))
        for i, jid in enumerate(ids):
            wait_job(jid, "SDXL 1024, queued %d of 3" % (i + 1))
        # one cancelled while it runs
        st, d = call("POST", "/sdcpp/v1/img_gen", dict(img, width=1024, height=1024))
        if st == 202:
            time.sleep(8)
            call("POST", "/sdcpp/v1/jobs/%s/cancel" % d["id"])
            wait_job(d["id"], "SDXL 1024, cancelled", expect=("cancelled", "completed"))
    if switch("flux2-klein-9b"):
        job("/sdcpp/v1/img_gen", dict(img, width=1024, height=1024, sample_params={"sample_steps": 4},
                                      lora=[{"path": "lenovo_flux_klein9b.safetensors", "multiplier": 1.0}]),
            "Klein 9B + LoRA 1024, 4 steps")
    if switch("wan21-t2v-1.3b"):
        job("/sdcpp/v1/vid_gen", {"prompt": "waves at a lighthouse, evening", "width": 832, "height": 480,
                                  "video_frames": 9, "fps": 16, "seed": n, "temporary": True,
                                  "sample_params": {"sample_steps": 8}}, "Wan 2.1 video, 9 frames, 8 steps")


def main():
    global stop
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--minutes", type=float, default=60)
    a = ap.parse_args()
    st, d = call("GET", "/ps5sd/v1/app/models")
    if st != 200:
        raise SystemExit("soak-test: the console does not answer (%s)" % st)
    start_model = d.get("active") or "z-image-turbo"
    threading.Thread(target=probe, daemon=True).start()
    threading.Thread(target=events, daemon=True).start()
    # only a download that really runs is a test, and only what it fetched is deleted at the end
    st, d = call("POST", "/ps5sd/v1/app/downloads", {"catalog": "sdxs-512"})
    downloading = st == 202
    log("download sdxs-512: %s%s" % (st, "" if downloading else " (its file is there already: no download, nothing deleted)"))
    t_end = time.time() + a.minutes * 60
    n = 0
    while time.time() < t_end:
        n += 1
        log("== round %d" % n)
        round_(n)
    switch(start_model)
    if downloading:
        st, d = call("GET", "/ps5sd/v1/app/downloads")
        dl = [x for x in (d or {}).get("downloads", []) if x.get("catalog") == "sdxs-512"]
        log("download: %s" % ([(x["state"], x["error"]) for x in dl]))
        if not dl or dl[-1]["state"] != "done":
            failures.append("the download: %s" % dl)
        st, _ = call("DELETE", "/ps5sd/v1/app/models/sdxs-512")
        log("sdxs-512 deleted: %s" % st)
    stop = True
    time.sleep(2)
    mem, bad = memory_lines()
    print()
    print("rounds: %d, minutes: %.0f" % (n, a.minutes))
    print("health: %d answered, %d not, the longest gap %.0f s" % (health["ok"], health["fail"], health["longest_gap_s"]))
    print("event stream: %d events, %d ends" % (stream["events"], stream["ends"]))
    if mem:
        heaps = [m[2] for m in mem]
        print("memory after %d requests: CPU heap first %.2f, last %.2f, the highest %.2f GiB; peak %.2f GiB; "
              "pool free %.2f to %.2f GiB" % (len(mem), heaps[0], heaps[-1], max(heaps), max(m[3] for m in mem),
                                               min(m[0] for m in mem), max(m[0] for m in mem)))
    for l in bad:
        print("log:", l[:200])
    print("failures: %d" % len(failures))
    for f in failures:
        print("  -", f)
    return 1 if failures or health["fail"] > 0 or bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
