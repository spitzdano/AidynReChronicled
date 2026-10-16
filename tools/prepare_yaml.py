"""prepare_yaml.py: split/aidyn.us.yaml = our options (tools/yaml_options.txt) + decomp/aidyn_chronicles.us.yaml's segments.

The decomp yaml targets an older splat, so the segments get these fixes
(everything else is copied verbatim; decomp/ is never edited):
- a missing comma: [0xBA790, "asm"os/lookatref]
- boundaries listed in tools/yaml_fixes.txt are moved (H4: a boundary inside a function)
- two subsegments sharing a name write the same file; later ones get a "_2" suffix
"""
import re
from collections import Counter
from pathlib import Path

segs = Path("decomp/aidyn_chronicles.us.yaml").read_text()
segs = segs[segs.index("\nsegments:"):]
fixes = []

def comma(m):
    fixes.append(f"comma: {m.group(0)}")
    return f'{m.group(1)},{m.group(2)}'
segs = re.sub(r'(\[0x[0-9A-Fa-f]+, *"asm")([A-Za-z_/])', comma, segs)

for line in Path("tools/yaml_fixes.txt").read_text().splitlines():
    line = line.split("   #")[0].strip()
    if not line or line.startswith("#"):
        continue
    if "=>" in line:
        old, new = [x.strip() for x in line.split("=>", 1)]
        if segs.count(old) != 1:
            raise SystemExit(f"prepare_yaml.py: replacement {old!r} matched {segs.count(old)} times (want 1)")
        segs = segs.replace(old, new.replace("\\n", "\n"))
        fixes.append(f"replace {old[:30]}")
        continue
    old, new = [x.strip() for x in line.split("->")]
    pat = re.compile(r"(\[)" + re.escape(old) + r"(,)", re.I)
    segs, n = pat.subn(lambda m: m.group(1) + new + m.group(2), segs)
    if n != 1:
        raise SystemExit(f"prepare_yaml.py: yaml_fixes entry {old} matched {n} times (want 1)")
    fixes.append(f"boundary {old}->{new}")

seen = Counter()
def uniq(m):
    key = (m.group(2), m.group(3).strip())
    if key[0] != "asm" or not key[1]:
        return m.group(0)
    seen[key] += 1
    if seen[key] == 1:
        return m.group(0)
    fixes.append(f"duplicate {key[1]} at {m.group(1)}")
    return f'{m.group(0).rstrip("]")}_{seen[key]}]'.replace(f"{key[1]}_{seen[key]}]", f"{key[1]}_{seen[key]}]")
segs = re.sub(r'^\s*- \[(0x[0-9A-Fa-f]+), *"?(\w+)"? *, *([^\]\n,]*)\]', uniq, segs, flags=re.M)

Path("split/aidyn.us.yaml").write_text(Path("tools/yaml_options.txt").read_text().rstrip("\n") + "\n" + segs)
print("prepare_yaml.py:", "; ".join(fixes) or "no fixes")
