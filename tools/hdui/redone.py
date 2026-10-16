"""redone.py: the redrawn portraits and icons in art/ (portraits/<group>/Portrait*.png, items/IconItem*.png,
slots/IconSlot*.png; see art/README.md), which the packs use in place of upscaling the ROM's pictures, and the redrawn
world textures in art/textures (tex<ROM index>.png, in export_textures.py's folders).

A picture is found by its Borg8 name (the file name), a texture by the number in its name, whatever folder it is in.
It is used at SCALE pixels per texel (8: 320x320 for a 40x40 portrait, 128x128 for a 16x16 icon, 256x256 for a 32x32
texture); one of any other size of the same shape is resized to that with Lanczos.
"""
import subprocess
import tempfile
from pathlib import Path

from pngio import read_png

PROJECT = Path(__file__).resolve().parents[2]
ART = PROJECT / "art"
SCALE = 8


def collect(root=ART):
    """{Borg8 name: path} of every redrawn picture (not the textures or the button icons)."""
    if not Path(root).exists():
        return {}
    return {p.stem: p for p in sorted(Path(root).rglob("*.png"))
            if not {"textures", "buttons"} & set(p.relative_to(root).parts)}


def collect_textures(root=ART / "textures"):
    """{Borg1 index: path} of every redrawn world texture."""
    import re
    out = {}
    for p in sorted(Path(root).rglob("tex*.png")) if Path(root).exists() else []:
        m = re.fullmatch(r"tex(\d+)", p.stem)
        if m:
            out[int(m.group(1))] = p
    return out


def load(path, w, h):
    """The picture at SCALE pixels per texel of a w x h image: (W, H, rgba)."""
    W, H = w * SCALE, h * SCALE
    pw, ph, rgba = read_png(path)
    if (pw, ph) == (W, H) and len(rgba) == W * H * 4:
        return W, H, rgba
    if pw * h != ph * w:
        raise ValueError(f"{path}: {pw}x{ph} is not the shape of the {w}x{h} original")
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "r.png"
        subprocess.run(["magick", str(path), "-filter", "Lanczos", "-resize", f"{W}x{H}!", f"PNG32:{out}"], check=True)
        return read_png(out)
