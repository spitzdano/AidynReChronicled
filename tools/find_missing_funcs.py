"""find_missing_funcs.py: code addresses stored in data (vtables, callback tables) that are not function starts in the ELF.

N64Recomp turns `jalr` into a runtime lookup by address, so a function nobody calls directly and splat never
found has no entry and the game aborts with "Failed to find function at 0x..." (H3). This scans the code
segment's data for words that point into code and reports those that are not at a known function start,
marking the ones that sit right after a `jr $ra` + delay slot (very likely real function starts).

    find_missing_funcs.py            report; `> tools/symbol_fixes_auto.txt` to record (read by prepare_symbols.py)
Output lines are symbol_fixes.txt-ready (size filled in up to the next known start or `jr ra` end).
"""
import re
import struct
import subprocess
import sys

CODE_ROM, CODE_VRAM, CODE_END_ROM = 0x1000, 0x80000400, 0xE7220
rom = open("baserom.z64", "rb").read()
word = lambda o: struct.unpack(">I", rom[o:o + 4])[0]
vram = lambda a: a - CODE_VRAM + CODE_ROM

starts = {}
for line in subprocess.check_output(["mips-linux-gnu-nm", "-n", "build/aidyn.us.elf"], text=True).splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tTWwAa" and not p[2].endswith(".NON_MATCHING") and not p[2].startswith((".L", "_binary", "code_", "romMain_")):
        a = int(p[0], 16)
        if CODE_VRAM <= a < CODE_VRAM + (CODE_END_ROM - CODE_ROM):
            starts[a] = p[2]

# Data words: everything in the code segment that isn't code is found by scanning all of it; code words that
# happen to look like an address are rare (aligned 0x8000xxxx-0x800Exxxx), and we filter by plausibility below.
found = {}
for off in range(CODE_ROM, CODE_END_ROM, 4):
    w = word(off)
    if CODE_VRAM <= w < CODE_VRAM + (CODE_END_ROM - CODE_ROM) and w % 4 == 0 and w not in starts:
        found.setdefault(w, []).append(off)

sorted_starts = sorted(starts)
def next_start(a):
    import bisect
    i = bisect.bisect_right(sorted_starts, a)
    return sorted_starts[i] if i < len(sorted_starts) else CODE_VRAM + (CODE_END_ROM - CODE_ROM)

import bisect

def containing(a):
    i = bisect.bisect_right(sorted_starts, a) - 1
    return sorted_starts[i] if i >= 0 else None

def is_code_ptr(w):
    return CODE_VRAM <= w < CODE_VRAM + (CODE_END_ROM - CODE_ROM) and w % 4 == 0

def in_jump_table(a, off):
    """True if the pointer at ROM `off` belongs to a switch's jump table: its run of consecutive code pointers
    (zeros allowed) has other entries inside the same function as `a`. Vtables point at different functions."""
    run = [off]
    for step in (-4, 4):
        o = off + step
        while CODE_ROM <= o < CODE_END_ROM and (is_code_ptr(word(o)) or word(o) == 0):
            if word(o):
                run.append(o)
            o += step
    ca = containing(a)
    return any(word(o) != a and containing(word(o)) == ca for o in run)

cands = []
EXC = (0xB8750, 0xB91A0)  # os/exception: raw handler code, stubbed; its words are not function pointers
for a, refs in sorted(found.items()):
    if EXC[0] <= vram(a) < EXC[1]:
        continue
    o = vram(a)
    prev = word(o - 8), word(o - 4)
    after_ret = prev[0] == 0x03E00008 or (word(o - 4) == 0 and word(o - 12) == 0x03E00008)
    # must not be a jump-table entry inside a function: those are in .rodata and reached via a lui/lw pair, so
    # require the preceding instruction to end a function.
    if not after_ret:
        continue
    if all(in_jump_table(a, r) for r in refs):
        continue
    cands.append((a, refs))
cand_addrs = sorted(a for a, _ in cands)
def end_of(a):
    import bisect
    i = bisect.bisect_right(cand_addrs, a)
    return min(next_start(a), cand_addrs[i] if i < len(cand_addrs) else 1 << 40)
cands = [(a, refs, end_of(a) - a) for a, refs in cands]
import os
AUTO = "tools/symbol_fixes_auto.txt"
lines = {}
if os.path.exists(AUTO):  # keep earlier findings: once recorded, a function is a symbol start and stops being "missing"
    for l in open(AUTO):
        m = re.match(r"func_([0-9A-F]{8}) =", l)
        if m:
            lines[int(m.group(1), 16)] = l.rstrip("\n")
for a, refs, size in cands:
    lines[a] = f"func_{a:08X} = 0x{a:08X}; // type:func size:0x{size:X}  (from {len(refs)} data word(s), first at ROM 0x{refs[0]:X})"
new = [a for a, _, _ in cands if a not in starts]
out = ["// Functions referenced only from data (vtables, callback tables): found by tools/find_missing_funcs.py (H3). Do not hand-edit."]
out += [lines[a] for a in sorted(lines)]
if "--update" in sys.argv:
    open(AUTO, "w").write("\n".join(out) + "\n")
    print(f"find_missing_funcs.py: {len(lines)} recorded in {AUTO} ({len(cands)} found this run)")
else:
    print("\n".join(out))
