"""splat_paths.py <yaml>: print, as shell assignments, the paths build_elf.sh needs.

Uses splat's own option parser, so paths resolve exactly as splat resolves them.
Every path is printed relative to the current directory (the project root).
"""

import os
import shlex
import sys
from pathlib import Path

from splat.util import conf, options


def rel(p) -> str:
    return os.path.relpath(Path(p).resolve(), Path.cwd())


def main() -> None:
    conf.load([Path(sys.argv[1])])
    o = options.opts
    if o.elf_path is None:
        sys.exit(f"{sys.argv[1]}: set options.elf_path")
    out = {
        "LD_SCRIPT": o.ld_script_path,
        "BUILD_PATH": o.build_path,
        "ELF": o.elf_path,
        "UNDEF_FUNCS": o.undefined_funcs_auto_path,
        "UNDEF_SYMS": o.undefined_syms_auto_path,
        "TARGET": o.target_path,
    }
    for k, v in out.items():
        print(f"{k}={shlex.quote(rel(v))}")


if __name__ == "__main__":
    main()
