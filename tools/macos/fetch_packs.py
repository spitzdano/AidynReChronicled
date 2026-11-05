#!/usr/bin/env python3
"""fetch_packs.py: the texture and sample packs for the macOS app, from our own newest release.

The packs are the same on every platform and the art behind them is in private repositories, so CI doesn't build them:
it takes the files the in-game updater would (the newest release's update.json, platform windows-x86_64, the entries under
texture-packs/ and sample-packs/), checks each against its BLAKE2b-256 hash, and writes them under DEST.

    fetch_packs.py DEST [--tag vX.Y.Z]
"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.request
from pathlib import Path

REPO = "spitzdano/AidynReChronicled"


def get(url, tries=4):
    for n in range(tries):
        try:
            headers = {"User-Agent": "aidyn-macos-package"}
            token = os.environ.get("GITHUB_TOKEN")
            if token and url.startswith("https://api.github.com/"):  # the API's rate limit is per address: CI's is shared; never sent to asset hosts
                headers["Authorization"] = f"Bearer {token}"
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001
            if n == tries - 1:
                raise
            print(f"  retry {url.rsplit('/', 1)[-1]}: {e}", file=sys.stderr)
            time.sleep(3 * (n + 1))


def newest_manifest(tag):
    releases = json.loads(get(f"https://api.github.com/repos/{REPO}/releases?per_page=30"))
    best = None
    for r in releases:
        m = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", r["tag_name"])
        if r["draft"] or r["prerelease"] or not m:
            continue
        if tag and r["tag_name"] != tag:
            continue
        url = next((a["browser_download_url"] for a in r["assets"] if a["name"] == "update.json"), None)
        key = tuple(int(x) for x in m.groups())
        if url and (best is None or key > best[0]):
            best = (key, r["tag_name"], url)
    if best is None:
        sys.exit("fetch_packs.py: no release with an update.json")
    return best[1], json.loads(get(best[2]))


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    dest = Path(args[0])
    tag = args[args.index("--tag") + 1] if "--tag" in args else None
    tag, manifest = newest_manifest(tag)
    files = [f for f in manifest["platforms"]["windows-x86_64"]["files"]
             if f["path"].startswith(("texture-packs/", "sample-packs/"))]
    print(f"fetch_packs.py: {tag}: {len(files)} files, {sum(f['size'] for f in files) / 1e6:.0f} MB")
    for f in files:
        out = dest / f["path"]
        if out.exists() and out.stat().st_size == f["size"] and hashlib.blake2b(out.read_bytes(), digest_size=32).hexdigest() == f["blake2b"]:
            continue
        data = get(f["url"])
        if len(data) != f["size"] or hashlib.blake2b(data, digest_size=32).hexdigest() != f["blake2b"]:
            sys.exit(f"fetch_packs.py: {f['path']} doesn't match the manifest's hash")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    print("fetch_packs.py: all files match their hashes")


main()
