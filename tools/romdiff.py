"""romdiff.py <yaml> <base.z64> <built.z64>: say where two ROMs first differ.

Prints the first differing offset, the top-level splat segment and the
subsegment that contain it, and how many bytes differ in total.
"""

import sys
from pathlib import Path

import yaml


def start_of(seg):
    if isinstance(seg, list):
        return seg[0] if seg and isinstance(seg[0], int) else None
    return seg.get("start")


def label(seg) -> str:
    if isinstance(seg, list):
        parts = [str(x) for x in seg[1:]]
        return " ".join(parts) if parts else "(end marker)"
    return f"{seg.get('name', '?')} ({seg.get('type', '?')})"


def containing(segs, off):
    best = None
    for s in segs or []:
        st = start_of(s)
        if isinstance(st, int) and st <= off:
            best = s
    return best


def main() -> None:
    cfg = yaml.safe_load(Path(sys.argv[1]).read_text())
    a, b = Path(sys.argv[2]).read_bytes(), Path(sys.argv[3]).read_bytes()
    if len(a) != len(b):
        print(f"sizes differ: base {len(a):#x}, built {len(b):#x}")
    n = min(len(a), len(b))
    diffs = []  # compared in blocks: a byte-by-byte loop over 32 MiB is slow
    for blk in range(0, n, 0x10000):
        x, y = a[blk:min(n, blk + 0x10000)], b[blk:min(n, blk + 0x10000)]
        if x != y:
            diffs += [blk + i for i in range(len(x)) if x[i] != y[i]]
    if not diffs:
        print("contents match up to the shorter length")
        return
    off = diffs[0]
    seg = containing(cfg.get("segments"), off)
    where = label(seg) if seg is not None else "before the first segment"
    sub = containing(seg.get("subsegments"), off) if isinstance(seg, dict) else None
    if sub is not None:
        where += f" / subsegment at {start_of(sub):#x}: {label(sub)}"
    print(f"MISMATCH: first difference at ROM {off:#x} (base {a[off]:02x}, built {b[off]:02x})")
    print(f"  in {where}")
    print(f"  {len(diffs)} differing bytes in total")


if __name__ == "__main__":
    main()
