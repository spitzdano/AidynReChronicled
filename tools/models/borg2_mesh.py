#!/usr/bin/env python3
"""borg2_mesh.py: a new mesh for one of the game's models, as a replacement for its Borg2 asset (vertices and display
lists), from a Wavefront OBJ file.

    tools/models/borg2_mesh.py build <asset index> <model.obj> <out.bin> [--rom baserom.z64] [--fit-uv]
    tools/models/borg2_mesh.py export <asset index> <out.obj> [--rom baserom.z64]

The game draws a model (a Borg5: a list of drawing steps) from meshes (Borg2) and textures (Borg1). A Borg2 is one
relocatable blob (decomp include/borg/borg2.h; offsets made pointers on load by borg2_func_a):

    +0x00  Borg2Data: alpha, display list count, scale, position, rotation, ..., the offsets of the display lists'
           table, the bounding box's vertices (segment E), the mesh's vertices (segment A), their count, and the
           material table (Borg2Struct: flags, tint per display list)
    then   8 bounding-box vertices, the mesh's vertices (N64 Vtx: x y z, flag, s t in S10.5, normal x y z as signed
           bytes, alpha), each display list, the display lists' table, the material table

Each display list (F3DEX2): G_VTX the box from segment E, G_CULLDL 0..7 (skipped when off screen), then batches of
G_VTX up to 32 vertices from segment A and G_TRI1 triangles, then G_ENDDL. This writes the same, with the mesh's
vertices taken from the OBJ (its v / vt / vn and faces, polygons fanned into triangles) in the original's units. What
the model still expects of the mesh is kept from the original asset (from the ROM): the number of display lists (an
OBJ with one usemtl group per list, in order; one list takes every face), each list's material entry, scale, position
and rotation. Triangles keep the OBJ's winding: counter-clockwise seen from the side that shows, as in Blender's and
every other tool's OBJ files, and as the game's own meshes are (all 22 of the Small Shield's turn counter-clockwise
about their normals). Texture coordinates: an OBJ's 0..1 (v up) are the area of its texture each display list's own
coordinates cover in the original (each list draws with its own texture), which is what export writes, so a mesh
exported and built again is the same. --fit-uv maps each vertex flat (x across, y down) into that area instead, for a
test model without its own texture coordinates. export writes a mesh as an OBJ in its own units (its bone's space, for
a character's part), one usemtl group per display list.

A vertex's last four bytes are its normal (x y z as signed bytes, then 255) for a display list the game lights, and its
colour (r g b a) for one with flag 0x8 in its material entry (the decompilation calls it B2S_Lighting, but the meshes
with it set carry colours, the Kite Shield's browns and tans, and those without carry normals): there the OBJ's vertex
colours (v x y z r g b) are used, or, without them, shading baked from each face's direction (light from above and in
front) in the original's average colour, so the texture keeps its tone.

The game loads the blob in place of its own when it is in the assets folder the port reads (src/game/patches.cpp,
"Asset files": <hex index>.bin).
"""
import argparse
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
import romassets  # noqa: E402

HEADER = struct.Struct(">f i f 3f 3f I I I I I I I I I I I")  # Borg2Data, 0x50 bytes
VTX = struct.Struct(">hhhHhhbbbB")


def read_template(rom_path, index):
    rom = romassets.Rom(str(rom_path))
    kind = rom.entry(index)[0]
    if kind != 2:
        raise SystemExit(f"borg2_mesh.py: asset {index:#x} is type {kind}, not a Borg2 mesh")
    d = rom.data(index)
    h = HEADER.unpack(d[:0x50])
    n, vlist, vlist2, count, mats = h[1], h[12], h[13], h[14], h[16]
    verts = [VTX.unpack(d[vlist2 + 16 * i:vlist2 + 16 * i + 16]) for i in range(count)]
    materials = [d[mats + 20 * i:mats + 20 * i + 20] for i in range(n)]
    lists = [struct.unpack(">I", d[h[11] + 4 * i:h[11] + 4 * i + 4])[0] for i in range(n)]
    # Each display list's triangles (vertex cache indices resolved to vertices).
    tris = []
    for off in lists:
        tris.append([])
        cache = [0] * 32
        k = off
        while True:
            w0, w1 = struct.unpack(">II", d[k:k + 8])
            k += 8
            op = w0 >> 24
            if op == 0x01:  # G_VTX
                num, end = (w0 >> 12) & 0xFF, (w0 & 0xFF) >> 1
                if w1 >> 24 == 0x0A:
                    first = (w1 & 0xFFFFFF) // 16
                    for j in range(num):
                        cache[end - num + j] = first + j
            elif op == 0x05:  # G_TRI1
                tris[-1].append(tuple(cache[(w0 >> s & 0xFF) // 2] for s in (16, 8, 0)))
            elif op == 0xDF:
                break
    return h, verts, materials, tris


def uv_boxes(verts, tris):
    """Per display list, the area (s_lo, s_hi, t_lo, t_hi) of its texture its vertices' coordinates cover."""
    every = (min(v[4] for v in verts), max(v[4] for v in verts), min(v[5] for v in verts), max(v[5] for v in verts))
    out = []
    for t in tris:
        used = {i for tri in t for i in tri}
        if not used:
            out.append(every)
            continue
        out.append((min(verts[i][4] for i in used), max(verts[i][4] for i in used),
                    min(verts[i][5] for i in used), max(verts[i][5] for i in used)))
    return out


def export(index, rom):
    """The mesh as OBJ text: every vertex (once per display list using it, with that list's texture coordinates),
    its normal (or, for a coloured list, the colour after the position), one usemtl group per list."""
    h, verts, materials, tris = read_template(rom, index)
    boxes = uv_boxes(verts, tris)
    lines, faces = [f"# asset {index:#x}, exported by borg2_mesh.py"], []
    n = 0
    for li, t in enumerate(tris):
        coloured = bool(struct.unpack(">I", materials[li][:4])[0] & 0x8)
        s_lo, s_hi, t_lo, t_hi = boxes[li]
        local = {}
        for i in sorted({i for tri in t for i in tri}):
            v = verts[i]
            n += 1
            local[i] = n
            if coloured:
                lines.append(f"v {v[0]} {v[1]} {v[2]} " + " ".join(f"{(c & 0xFF) / 255:.4f}" for c in v[6:9]))
            else:
                lines.append(f"v {v[0]} {v[1]} {v[2]}")
            lines.append(f"vt {(v[4] - s_lo) / ((s_hi - s_lo) or 1):.6f} {1 - (v[5] - t_lo) / ((t_hi - t_lo) or 1):.6f}")
            nv = [c / 127 for c in v[6:9]] if not coloured else [0.0, 1.0, 0.0]
            lines.append(f"vn {nv[0]:.5f} {nv[1]:.5f} {nv[2]:.5f}")
        faces.append(f"usemtl list{li}")
        for tri in t:
            faces.append("f " + " ".join(f"{local[i]}/{local[i]}/{local[i]}" for i in tri))
    return "\n".join(lines + faces) + "\n"


def read_obj(path):
    """(positions, colours, uvs, normals, groups): colours per position (None without them), groups a list of faces,
    each face a list of (v, vt, vn) indices (0-based, vt / vn None when absent), one group per usemtl (or one for all)."""
    pos, col, uv, nrm, groups = [], [], [], [], [[]]
    named = False  # a usemtl seen: from then on every group counts, empty or not (a mesh can have an empty list)
    for line in Path(path).read_text().splitlines():
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0] == "v":
            pos.append(tuple(float(x) for x in p[1:4]))
            col.append(tuple(float(x) for x in p[4:7]) if len(p) >= 7 else None)
        elif p[0] == "vt":
            uv.append(tuple(float(x) for x in p[1:3]))
        elif p[0] == "vn":
            nrm.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == "usemtl":
            if named or groups[-1]:
                groups.append([])
            named = True
        elif p[0] == "f":
            face = []
            for c in p[1:]:
                ix = (c.split("/") + ["", ""])[:3]
                face.append(tuple(int(x) - 1 if x else None for x in ix))
            groups[-1].append(face)
    return pos, col, uv, nrm, groups if named else [g for g in groups if g]


def normalize(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]


def build(index, obj, rom, fit_uv):
    h, tverts, materials, ttris = read_template(rom, index)
    boxes = uv_boxes(tverts, ttris)
    pos, col, uv, nrm, groups = read_obj(obj)
    if len(groups) != len(materials):
        raise SystemExit(f"borg2_mesh.py: the OBJ has {len(groups)} usemtl group(s), the original {len(materials)} "
                         "display list(s)")
    x_lo, x_hi = min(p[0] for p in pos), max(p[0] for p in pos)
    y_lo, y_hi = min(p[1] for p in pos), max(p[1] for p in pos)

    def face_normal(f):
        a, b, c = (pos[f[i][0]] for i in range(3))
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        return normalize([u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]])

    light = normalize([-0.3, 0.8, 0.5])
    # The original's average vertex colour (for its coloured display lists), which tints its texture.
    tone = [sum((v[6 + i] & 0xFF) for v in tverts) / len(tverts) / 255 for i in range(3)]

    def vertex(corner, fn, coloured, box):
        s_lo, s_hi, t_lo, t_hi = box
        p = pos[corner[0]]
        n = normalize(nrm[corner[2]]) if corner[2] is not None and nrm else fn
        if fit_uv or corner[1] is None or not uv:
            s = s_lo + (p[0] - x_lo) / ((x_hi - x_lo) or 1) * (s_hi - s_lo)
            t = t_lo + (y_hi - p[1]) / ((y_hi - y_lo) or 1) * (t_hi - t_lo)
        else:  # OBJ coordinates are 0..1, v up: into the area the original covers
            u_, v_ = uv[corner[1]]
            s = s_lo + u_ * (s_hi - s_lo)
            t = t_lo + (1 - v_) * (t_hi - t_lo)
        if coloured:
            c = col[corner[0]]
            if c is None:  # baked: half lit everywhere, the rest from the light's angle
                shade = 0.55 + 0.45 * max(0.0, sum(n[i] * light[i] for i in range(3)))
                c = tuple(shade * tone[i] / 0.775 for i in range(3))  # 0.775: the shade's middle
            rgb = [max(0, min(255, int(round(x * 255)))) for x in c]
            return VTX.pack(*(int(round(x)) for x in p), 0, int(round(s)), int(round(t)),
                            *(x - 256 if x > 127 else x for x in rgb), 255)
        return VTX.pack(*(int(round(x)) for x in p), 0, int(round(s)), int(round(t)),
                        *(max(-127, min(127, int(round(x * 127)))) for x in n), 255)

    # Triangles per group, polygons fanned, in the OBJ's winding.
    lists = []
    for g in groups:
        tris = []
        for f in g:
            for k in range(1, len(f) - 1):
                tri = [f[0], f[k], f[k + 1]]
                tris.append((tri, face_normal(tri)))
        lists.append(tris)

    # Batches of at most 32 distinct vertices, each batch's vertices together in the vertex list.
    vtx_blob, dl_cmds = bytearray(), []
    for li, tris in enumerate(lists):
        coloured = bool(struct.unpack(">I", materials[li][:4])[0] & 0x8)
        cmds, batch, keys = [], [], {}

        def flush():
            if not batch:
                return
            first = len(vtx_blob) // 16
            for v in batch:
                vtx_blob.extend(v)
            n = len(batch)
            cmds.append((0x01000000 | n << 12 | n << 1, 0x0A000000 + first * 16))
            for a, b, c in pending:
                cmds.append((0x05000000 | a * 2 << 16 | b * 2 << 8 | c * 2, 0))
            batch.clear(), keys.clear(), pending.clear()

        pending = []
        for tri, fn in tris:
            need = [vertex(c, fn, coloured, boxes[li]) for c in tri]
            new = [v for v in dict.fromkeys(need) if v not in keys]
            if len(batch) + len(new) > 32:
                flush()
                new = list(dict.fromkeys(need))
            for v in new:
                keys[v] = len(batch)
                batch.append(v)
            pending.append(tuple(keys[v] for v in need))
        flush()
        dl_cmds.append(cmds)

    # The bounding box (segment E): its 8 corners, ordered as the game's are.
    xs = [p[0] for p in pos]
    ys = [p[1] for p in pos]
    zs = [p[2] for p in pos]
    x0, x1, y0, y1, z0, z1 = (int(math.floor(min(xs))), int(math.ceil(max(xs))), int(math.floor(min(ys))),
                              int(math.ceil(max(ys))), int(math.floor(min(zs))), int(math.ceil(max(zs))))
    box = b"".join(VTX.pack(x, y, z, 0, 0, 0, 0, 0, 0, 0) for x, y, z in
                   [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1),
                    (x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)])

    out = bytearray(0x50)
    off_box = len(out)
    out += box
    off_vtx = len(out)
    out += vtx_blob
    dl_offs = []
    for cmds in dl_cmds:
        dl_offs.append(len(out))
        if not cmds:  # an empty list is a bare G_ENDDL, as the game's are: it skips those
            out += struct.pack(">II", 0xDF000000, 0)
            continue
        out += struct.pack(">II", 0x01008010, 0x0E000000)  # G_VTX the box: 8 into slots 0..7
        out += struct.pack(">II", 0x0300000E, 0)           # G_CULLDL 0..7
        for w0, w1 in cmds:
            out += struct.pack(">II", w0, w1)
        out += struct.pack(">II", 0xDF000000, 0)           # G_ENDDL
    off_table = len(out)
    out += b"".join(struct.pack(">I", o) for o in dl_offs)
    off_mats = len(out)
    out += b"".join(materials)
    while len(out) % 8:
        out += b"\0"
    head = list(h)
    head[1] = len(dl_offs)
    head[11], head[12], head[13], head[14], head[15], head[16] = off_table, off_box, off_vtx, len(vtx_blob) // 16, 0, off_mats
    head[17] = head[18] = head[19] = 0
    out[0:0x50] = HEADER.pack(*head)
    tri_count = sum(len(t) for t in lists)
    return bytes(out), len(vtx_blob) // 16, tri_count


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="mode", required=True)
    rom_default = Path(__file__).resolve().parents[2] / "baserom.z64"
    b = sub.add_parser("build", help="a mesh from an OBJ, to replace the asset")
    b.add_argument("index", type=lambda s: int(s, 0), help="the Borg2 asset it replaces (e.g. 0x3533)")
    b.add_argument("obj", type=Path)
    b.add_argument("out", type=Path)
    b.add_argument("--rom", type=Path, default=rom_default)
    b.add_argument("--fit-uv", action="store_true", help="map the texture flat across the mesh (x across, y down)")
    e = sub.add_parser("export", help="the asset's mesh as an OBJ")
    e.add_argument("index", type=lambda s: int(s, 0))
    e.add_argument("obj", type=Path)
    e.add_argument("--rom", type=Path, default=rom_default)
    args = ap.parse_args()
    if args.mode == "export":
        args.obj.write_text(export(args.index, args.rom))
        print(f"borg2_mesh.py: {args.obj}")
        return
    blob, verts, tris = build(args.index, args.obj, args.rom, args.fit_uv)
    args.out.write_bytes(blob)
    print(f"borg2_mesh.py: {args.out} ({len(blob)} bytes): {verts} vertices, {tris} triangles")


if __name__ == "__main__":
    main()
