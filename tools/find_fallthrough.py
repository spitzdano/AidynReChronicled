"""find_fallthrough.py: functions in split/asm that end without a jump/return, i.e. fall into the next function.

N64Recomp compiles each function on its own: code that falls off the end just returns, skipping the rest of the
real function (including its epilogue, so the caller's stack pointer ends up wrong). A fall-through means a bogus symbol or a
yaml file boundary sits inside a function: fix it with tools/symbol_drops.txt or tools/yaml_fixes.txt.
Exit status 1 if any are found (apart from `romMain`, which ends in the libultra `break`).
"""
import glob
import re
import sys

term = re.compile(r"^(jr|j|b)\s")
bad = []
for f in sorted(glob.glob("split/asm/**/*.s", recursive=True)):
    if "exception" in f:
        continue
    cur, ins = None, []
    for line in open(f):
        m = re.match(r"glabel (\S+)", line)
        if m:
            cur, ins = m.group(1), []
            continue
        if line.startswith("endlabel"):
            if cur and len(ins) >= 2 and cur != "romMain" and not term.match(ins[-2]):
                bad.append((f.split("asm/")[-1], cur, ins[-2], ins[-1]))
            cur = None
            continue
        if cur and "/*" in line:
            s = re.sub(r"/\*.*?\*/", "", line).strip()
            if s and not s.startswith("."):
                ins.append(s)
for b in bad:
    print("falls through:", *b)
print(f"find_fallthrough.py: {len(bad)} function(s) fall through")
sys.exit(1 if bad else 0)
