#!/usr/bin/env python3
"""rom_guard.py ROM DIR: fails if anything under DIR holds the ROM, whole or in part.

Looks for the ROM's first 64 bytes (its header: the title, the cartridge ID) and for four evenly spaced 64-byte slices of the
rest, in every file. A match anywhere means a ROM, or a piece of one, is about to be packaged.
"""
import mmap
import sys
from pathlib import Path


def main():
    rom = Path(sys.argv[1]).read_bytes()
    needles = [rom[:64]] + [rom[o:o + 64] for o in (len(rom) // 5, 2 * len(rom) // 5, 3 * len(rom) // 5, 4 * len(rom) // 5)]
    checked = 0
    for p in sorted(Path(sys.argv[2]).rglob("*")):
        if not p.is_file() or p.is_symlink() or p.stat().st_size == 0:
            continue
        with open(p, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as m:
            for n in needles:
                if m.find(n) >= 0:
                    sys.exit(f"rom_guard.py: {p} contains part of the ROM")
        checked += 1
    print(f"rom_guard.py: {checked} files checked, no ROM in them")


main()
