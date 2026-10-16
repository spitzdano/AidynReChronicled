"""prepare_symbols.py: split/symbol_addrs.txt = decomp/symbol_addrs.us.txt made usable by splat + GNU as.

- C++ names (Class::method, Class::~Class, odd characters) become C identifiers:
  "::~" -> "__D_", "::" -> "__", anything else outside [A-Za-z0-9_] -> "_".
- Newer splat refuses two symbols with one name at different addresses (upstream
  has a few: constructor pairs, libultra buffers, a DCP_START typo). The later
  ones get _2, _3 ...
- Type vec3f (not known to splat) becomes Vec3f, a custom type splat allows.
- Names listed in tools/symbol_renames.txt are renamed (see that file).
- Names listed in tools/symbol_drops.txt are removed (bogus symbols inside other functions).
- Names that are GNU ld script keywords (VERSION, ...) or C++ keywords (new, delete, ...: the
  generated function table is compiled as C++) get a trailing _.
Addresses are untouched. decomp/ is never edited.
"""
import re
from collections import Counter
from pathlib import Path

src = Path("decomp/symbol_addrs.us.txt").read_text().splitlines()
LD_KEYWORDS = {"ABSOLUTE", "ADDR", "ALIGN", "ALIGNOF", "ASSERT", "AT", "BLOCK", "BYTE", "CONSTANT", "CREATE_OBJECT_SYMBOLS",
               "DATA_SEGMENT_ALIGN", "DEFINED", "ENTRY", "EXTERN", "FILL", "FLOAT", "FORCE_COMMON_ALLOCATION", "GROUP",
               "HIDDEN", "INCLUDE", "INFO", "INHIBIT_COMMON_ALLOCATION", "INPUT", "KEEP", "LENGTH", "LOADADDR", "LONG",
               "MAP", "MAX", "MEMORY", "MIN", "NOCROSSREFS", "NOLOAD", "ORIGIN", "OUTPUT", "OUTPUT_ARCH", "OUTPUT_FORMAT",
               "OVERLAY", "PHDRS", "PROVIDE", "PROVIDE_HIDDEN", "QUAD", "SEARCH_DIR", "SECTIONS", "SHORT", "SIZEOF",
               "SORT", "SQUAD", "STARTUP", "SUBALIGN", "TARGET", "VERSION", "VERSIONK"}
renames = dict(l.split()[:2] for l in Path('tools/symbol_renames.txt').read_text().splitlines() if l.split('#')[0].strip())
drops = {l.split('#')[0].strip() for l in Path('tools/symbol_drops.txt').read_text().splitlines() if l.split('#')[0].strip()}
fixes = Path("tools/symbol_fixes.txt").read_text().splitlines()
auto = Path("tools/symbol_fixes_auto.txt").read_text().splitlines()
overridden = {"osInvalDcache", "func_800C7F60"} | {m.group(1) for l in fixes if (m := re.match(r"^(\w+)\s*=", l))}
CXX_KEYWORDS = {"new", "delete", "class", "this", "operator", "template", "typename", "namespace", "private", "public",
                "protected", "virtual", "friend", "try", "catch", "throw", "and", "or", "not", "xor", "bool", "true", "false",
                "union", "auto", "register", "default", "export", "explicit", "inline", "mutable", "using", "typeid",
                "signed", "unsigned", "main"}
LD_KEYWORDS |= CXX_KEYWORDS - {"main"}
seen, out, renamed = Counter(), [], []
for line in src:
    m = re.match(r"^(\s*)([^\s=]+)(\s*=.*)$", line)
    if m:
        name = m.group(2).replace("::~", "__D_").replace("::", "__")
        name = re.sub(r"[^A-Za-z0-9_]", "_", name)
        if name in LD_KEYWORDS:
            name += "_"
        seen[name] += 1
        if seen[name] > 1:
            name = f"{name}_{seen[name]}"
            renamed.append(name)
        name = renames.get(name, name)
        if name in drops:
            continue
        if name in overridden:
            continue  # redefined in tools/symbol_fixes.txt
        line = f"{m.group(1)}{name}{m.group(3)}".replace("type:vec3f", "type:Vec3f")
    out.append(line)
out += fixes
out += Path("tools/symbol_fixes_auto.txt").read_text().splitlines()  # from find_missing_funcs.py (H3)
Path("split/symbol_addrs.txt").write_text("\n".join(out) + "\n")
print(f"prepare_symbols.py: {len(src)} lines, renamed {len(renamed)} duplicate(s): {', '.join(renamed)}")
