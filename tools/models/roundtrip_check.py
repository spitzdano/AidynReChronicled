#!/usr/bin/env python3
"""roundtrip_check.py: borg2_mesh.py's check: each mesh exported as an OBJ and built again must draw the same, display
list by display list: the same triangles (positions, winding), texture coordinates and colours exactly, normals within
one step of rounding.

    tools/models/roundtrip_check.py <asset index>... [--rom baserom.z64]
    tools/models/roundtrip_check.py --model <Borg5 index>   every mesh of a model (e.g. 0x25cb, Alaron)
"""
import argparse
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
import borg2_mesh  # noqa: E402
import romassets  # noqa: E402


def model_meshes(rom_path, index):
    d = romassets.Rom(str(rom_path)).data(index)
    count = struct.unpack(">i", d[8:12])[0]
    at = struct.unpack(">I", d[0x2C:0x30])[0]
    return list(struct.unpack(f">{count}i", d[at:at + 4 * count]))


def lists_of(blob):
    """Per display list, its triangles as (position, s, t, last four bytes) per corner, rotated to a fixed start."""
    h = borg2_mesh.HEADER.unpack(blob[:0x50])
    verts = [borg2_mesh.VTX.unpack(blob[h[13] + 16 * i:h[13] + 16 * i + 16]) for i in range(h[14])]
    out = []
    for li in range(h[1]):
        k = struct.unpack(">I", blob[h[11] + 4 * li:h[11] + 4 * li + 4])[0]
        cache, tris = [0] * 32, []
        while True:
            w0, w1 = struct.unpack(">II", blob[k:k + 8])
            k += 8
            op = w0 >> 24
            if op == 0x01 and w1 >> 24 == 0x0A:
                num, end = (w0 >> 12) & 0xFF, (w0 & 0xFF) >> 1
                for j in range(num):
                    cache[end - num + j] = (w1 & 0xFFFFFF) // 16 + j
            elif op == 0x05:
                c = [verts[cache[(w0 >> s & 0xFF) // 2]] for s in (16, 8, 0)]
                c = [(v[:3], v[4], v[5], v[6:10]) for v in c]
                r = min(range(3), key=lambda i: c[i][0])
                tris.append(tuple(c[r:] + c[:r]))
            elif op == 0xDF:
                break
        out.append(sorted(tris))
    return out


def check(index, rom):
    original = romassets.Rom(str(rom)).data(index)
    with tempfile.TemporaryDirectory() as t:
        obj = Path(t) / "mesh.obj"
        obj.write_text(borg2_mesh.export(index, rom))
        blob, _, _ = borg2_mesh.build(index, obj, rom, False)
    a, b = lists_of(original), lists_of(blob)
    if len(a) != len(b):
        return f"{len(b)} display lists, the original {len(a)}"
    for li, (ta, tb) in enumerate(zip(a, b)):
        if len(ta) != len(tb):
            return f"list {li}: {len(tb)} triangles, the original {len(ta)}"
        for x, y in zip(ta, tb):
            for (pa, sa, ta_, na), (pb, sb, tb_, nb) in zip(x, y):
                if pa != pb:
                    return f"list {li}: a triangle moved or turned"
                if (sa, ta_) != (sb, tb_):
                    return f"list {li}: texture coordinates {(sa, ta_)} became {(sb, tb_)}"
                if max(abs(p - q) for p, q in zip(na, nb)) > 1:
                    return f"list {li}: normal or colour {na} became {nb}"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("index", nargs="*", type=lambda s: int(s, 0))
    ap.add_argument("--model", type=lambda s: int(s, 0))
    ap.add_argument("--rom", type=Path, default=Path(__file__).resolve().parents[2] / "baserom.z64")
    args = ap.parse_args()
    meshes = list(args.index) + (model_meshes(args.rom, args.model) if args.model is not None else [])
    failed = 0
    for i in meshes:
        problem = check(i, args.rom)
        print(f"{i:#06x}: {'same' if problem is None else problem}")
        failed += problem is not None
    print(f"roundtrip_check.py: {len(meshes) - failed} of {len(meshes)} the same")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
