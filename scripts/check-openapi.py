#!/usr/bin/env python3
"""Checks ps5/app/openapi.json against the routes the code registers.

    python3 scripts/check-openapi.py [--server DIR] [--list]

The routes come from the sources:
  - sd-server's (DIR, default third_party/stable-diffusion.cpp/examples/server, with
    patches/sdcpp applied): svr.Get/Post/Delete/Put("<path>" or R"(<regex>)"), and
    the paths of a `for (const char* route : {...})` loop around one;
  - the app's routes under /ps5sd/v1/app/ (ps5/app/main.cpp, sd_app_request and
    sd_app_upload; ps5/app/downloader.cpp, downloader_route): `m == "GET" && p ==
    "models"`, and a prefix such as `p.rfind("presets/", 0) == 0` with its methods.
    compat/* are the server's own routes asking the app; they are not routes.

Every route must be in the spec with its method, and every operation of the spec
must be a route. A regex or prefix route matches the spec's paths that have a
{parameter} where it has its pattern. --list prints the routes and checks nothing.
Exit status 1 when they differ.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METHODS = ("GET", "POST", "PUT", "DELETE")


def server_routes(directory):
    routes = []  # (method, kind, path): kind "exact" or "regex"
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".cpp"):
            continue
        text = open(os.path.join(directory, name), encoding="utf-8").read()
        for m in re.finditer(r'svr\.(Get|Post|Delete|Put)\(\s*(R"\((.*?)\)"|"([^"]*)")', text):
            method = m.group(1).upper()
            if m.group(3) is not None:
                routes.append((method, "regex", m.group(3)))
            else:
                routes.append((method, "exact", m.group(4)))
        for m in re.finditer(r'for \(const char\* (\w+) : \{([^}]*)\}\)\s*\{\s*svr\.(Get|Post|Delete|Put)\(\1\b', text):
            for path in re.findall(r'"([^"]+)"', m.group(2)):
                routes.append((m.group(3).upper(), "exact", path))
    # a catch-all such as ".*" (none since 0078) is not a route of the API, nor are the server's
    # routes that hand /ps5sd/v1/app/* to the app (its own routes are listed from its sources)
    return [r for r in routes if r[2] != ".*" and not r[2].startswith("/ps5sd/v1/app/")]


def app_routes(files):
    routes = []
    for path in files:
        text = open(path, encoding="utf-8").read()
        # one condition: m == "GET" && p == "x", or (p == "a" || p == "b")
        for cond in re.finditer(r'(?:m|method) == "(GET|POST|PUT|DELETE)" && \(?((?:(?:p|path) == "[^"]+"(?: \|\| )?)+)\)?',
                                text):
            for sub in re.findall(r'(?:p|path) == "([^"]+)"', cond.group(2)):
                routes.append((cond.group(1), "exact", sub))
        # a prefix with its methods: (m == "GET" || m == "POST") && p.rfind("x/", 0) == 0, either order
        for line in text.splitlines():
            prefix = re.search(r'(?:p|path)\.rfind\("([^"]+/)", 0\) (==|!=) 0', line)
            if not prefix or " if (" not in line:
                continue
            methods = re.findall(r'(?:m|method) == "(GET|POST|PUT|DELETE)"', line)
            if prefix.group(1) == "uploads/":
                methods = ["PUT"]  # sd_app_upload, which the server calls for PUT only
            for method in methods:
                routes.append((method, "prefix", prefix.group(1)))
    out = []
    for method, kind, sub in routes:
        if sub.startswith("compat/"):
            continue
        out.append((method, kind, "/ps5sd/v1/app/" + sub))
    return out


def spec_operations(spec):
    ops = []
    for path, item in spec.get("paths", {}).items():
        for method in item:
            if method.upper() in METHODS:
                ops.append((method.upper(), path))
    return ops


def matches(route, op):
    method, kind, pattern = route
    op_method, op_path = op
    if method != op_method:
        return False
    if kind == "exact":
        return op_path == pattern
    if kind == "prefix":
        return op_path.startswith(pattern) and "{" in op_path[len(pattern):]
    # a regex route: its groups are the spec's {parameters}
    as_spec = re.sub(r"\([^()]*(?:\([^()]*\)[^()]*)*\)", "{}", pattern)
    return re.fullmatch(re.escape(as_spec).replace(r"\{\}", r"\{[^/{}]+\}"), op_path) is not None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--server", default=os.path.join(ROOT, "third_party", "stable-diffusion.cpp", "examples", "server"))
    ap.add_argument("--spec", default=os.path.join(ROOT, "ps5", "app", "openapi.json"))
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(os.path.join(a.server, "events.cpp")):
        sys.exit("check-openapi: %s is not patched (no events.cpp); apply patches/sdcpp first" % a.server)
    routes = server_routes(a.server) + app_routes(
        [os.path.join(ROOT, "ps5", "app", "main.cpp"), os.path.join(ROOT, "ps5", "app", "downloader.cpp")])
    routes = sorted(set(routes), key=lambda r: (r[2], r[0]))
    if a.list:
        for method, kind, path in routes:
            print("%-6s %-6s %s" % (method, kind, path))
        return 0
    spec = json.load(open(a.spec, encoding="utf-8"))
    ops = spec_operations(spec)
    missing = [r for r in routes if not any(matches(r, op) for op in ops)]
    extra = [op for op in ops if not any(matches(r, op) for r in routes)]
    for method, kind, path in missing:
        print("check-openapi: not in the spec: %s %s (%s)" % (method, path, kind))
    for method, path in extra:
        print("check-openapi: in the spec, not a route: %s %s" % (method, path))
    if missing or extra:
        return 1
    print("check-openapi: %d routes, %d operations: they match" % (len(routes), len(ops)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
