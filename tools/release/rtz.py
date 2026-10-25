#!/usr/bin/env python3
"""rtz.py: a texture pack folder as one .rtz (RT64's zipped pack), the way releases ship them.

    python3 -I tools/release/rtz.py <pack folder> <out.rtz> [--without <pack folder>...]

--without: leave out every picture whose RT64 hash one of these packs also has (they come later in settings.json's
texture_packs, so theirs is the one drawn): the HD upscale packs (hd-ui, hd-world) ship this way, only what the redrawn
art doesn't replace yet, a few tens of MB of their 1.3 GB. rt64.json is written without those entries; the pack's
other files (hd-ui's aidyn-labels.txt and aidyn-text-widths.txt) stay.

The files are stored, not compressed (they're PNG and DDS already), in a fixed order, so the same pack gives the same
file. One file loads in a fraction of the time 40,000 do, on Windows above all, and unpacks and copies faster.
"""
import json
import os
import sys
import zipfile
from pathlib import Path


def hashes(pack):
    """Every RT64 hash a pack replaces."""
    return {t["hashes"]["rt64"] for t in json.loads((pack / "rt64.json").read_text())["textures"]}


def main():
    args = sys.argv[1:]
    later = []
    if "--without" in args:
        at = args.index("--without")
        args, later = args[:at], [Path(a) for a in args[at + 1:]]
    if len(args) != 2:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    src, out = Path(args[0]), Path(args[1])
    for pack in [src] + later:
        if not (pack / "rt64.json").is_file():
            sys.exit(f"rtz.py: {pack} isn't a texture pack (no rt64.json)")
    db = json.loads((src / "rt64.json").read_text())
    dropped = set()
    if later:
        covered = set().union(*(hashes(p) for p in later))
        kept = [t for t in db["textures"] if t["hashes"]["rt64"] not in covered]
        dropped = {t["path"] for t in db["textures"]} - {t["path"] for t in kept}
        db["textures"] = kept
    tmp = out.with_name(out.name + ".tmp")
    count = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as z:
        for root, dirs, files in os.walk(src, followlinks=True):
            dirs.sort()
            for f in sorted(files):
                full = Path(root) / f
                name = full.relative_to(src).as_posix()
                if name in dropped:
                    continue
                info = zipfile.ZipInfo(name, date_time=(2001, 1, 1, 0, 0, 0))
                info.external_attr = 0o644 << 16
                if later and name == "rt64.json":
                    z.writestr(info, json.dumps(db, indent=1) + "\n", zipfile.ZIP_STORED)
                else:
                    with open(full, "rb") as data:
                        z.writestr(info, data.read(), zipfile.ZIP_STORED)
                count += 1
    os.replace(tmp, out)
    print(f"rtz.py: {out} ({count} files, {out.stat().st_size // (1 << 20)} MB)")


if __name__ == "__main__":
    main()
