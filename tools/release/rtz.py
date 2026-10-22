#!/usr/bin/env python3
"""rtz.py: a texture pack folder as one .rtz (RT64's zipped pack), the way releases ship them.

    python3 -I tools/release/rtz.py <pack folder> <out.rtz>

The files are stored, not compressed (they're PNG and DDS already), in a fixed order, so the same pack gives the same
file. One file loads in a fraction of the time 40,000 do, on Windows above all, and unpacks and copies faster.
"""
import os
import sys
import zipfile
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (src / "rt64.json").is_file():
        sys.exit(f"rtz.py: {src} isn't a texture pack (no rt64.json)")
    tmp = out.with_name(out.name + ".tmp")
    count = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as z:
        for root, dirs, files in os.walk(src, followlinks=True):
            dirs.sort()
            for f in sorted(files):
                full = Path(root) / f
                info = zipfile.ZipInfo(full.relative_to(src).as_posix(), date_time=(2001, 1, 1, 0, 0, 0))
                info.external_attr = 0o644 << 16
                with open(full, "rb") as data:
                    z.writestr(info, data.read(), zipfile.ZIP_STORED)
                count += 1
    os.replace(tmp, out)
    print(f"rtz.py: {out} ({count} files, {out.stat().st_size // (1 << 20)} MB)")


if __name__ == "__main__":
    main()
