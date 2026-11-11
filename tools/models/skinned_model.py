#!/usr/bin/env python3
"""skinned_model.py: a skinned character (a glTF/GLB with a skeleton and animations) in place of one of the game's
rigid models, for the port's skinned-model path (src/game/patches.cpp, "Skinned models"). A test for now.

    tools/models/skinned_model.py <model.glb> <out folder> [--mesh "outfit plain"] [--rom baserom.z64]

The game's characters are rigid: a Borg5 model draws each body part (a Borg2 mesh, in the model's space) with its own
matrix. A skinned mesh bends at the joints instead, which the game's formats can't hold, so the port draws it itself:
one of the model's meshes (the torso) carries the whole skinned mesh (its vertices rewritten every frame), the other meshes are
emptied, and the GLB's animations, one per game animation (named "borg6 <index>", as Aidyn Chronicles Enhanced
exports them), stand in for the game's. Written to the out folder, for AIDYN_ASSET_DIR:

    <model>-<mesh>.bin    the carrier (the first mesh: every triangle in one display list, each vertex's flag the
                          skinned vertex it is, a second vertex buffer after it) and the emptied meshes
    <model>.skin          what the port skins with: the vertices (rest position and normal, four joints and weights)
                          and, per animation and frame, each joint's matrix, from the mesh's rest to the model's space

The GLB's root node is the model's root transform (transform 2, which the game moves the character by); its animation
channels are the game's own, so a joint's matrix leaves that transform out (the game applies its own), and the carrier
is drawn with the root's matrix (the port copies it over the carrier's). Units and axes are the game's: Y up, feet at
0, metres, as the game's models are in model space. Textures: the carrier keeps the original's texture for now.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hdui"))
import borg2_mesh  # noqa: E402
import romassets  # noqa: E402

MODEL = 0x25CB          # Alaron's body
HEAD = 0x25AA           # his head, hung on a locator: emptied (the skinned mesh has its own)
CARRIER = 0x203D        # the torso, drawn with transform 6: carries the skinned mesh
CARRIER_LIST = 4        # the carrier's display list that keeps a texture (0x1FF9, loaded before it)
FPS = 60


def read_glb(path):
    b = Path(path).read_bytes()
    n = struct.unpack("<I", b[12:16])[0]
    g = json.loads(b[20:20 + n])
    return g, b[20 + n + 8:]


def accessor(g, binary, i):
    a = g["accessors"][i]
    v = g["bufferViews"][a["bufferView"]]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    dt = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[
        a["componentType"]]
    size = np.dtype(dt).itemsize * comps
    stride = v.get("byteStride", size)
    start = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    rows = [np.frombuffer(binary, dt, comps, start + k * stride) for k in range(a["count"])]
    out = np.array(rows, dtype=np.float64 if dt == np.float32 else np.int64)
    if a.get("normalized") and dt != np.float32:
        out = out / np.iinfo(dt).max
    return out


def trs_matrix(t, r, s):
    x, y, z, w = r
    rot = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                    [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                    [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = rot * np.asarray(s)
    m[:3, 3] = t
    return m


def sample(times, values, t, path):
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    k = int(np.searchsorted(times, t)) - 1
    f = (t - times[k]) / (times[k + 1] - times[k])
    a, b = values[k], values[k + 1]
    if path == "rotation":
        if np.dot(a, b) < 0:
            b = -b
        q = a * (1 - f) + b * f
        return q / np.linalg.norm(q)
    return a * (1 - f) + b * f


def empty_mesh(rom, index):
    """The mesh with every display list a bare G_ENDDL (the game skips those) and no vertices."""
    h, _, materials, _ = borg2_mesh.read_template(rom, index)
    out = bytearray(0x50)
    off_box = len(out)
    out += bytes(16 * 8)
    offs = []
    for _ in materials:
        offs.append(len(out))
        out += struct.pack(">II", 0xDF000000, 0)
    off_table = len(out)
    out += b"".join(struct.pack(">I", o) for o in offs)
    off_mats = len(out)
    out += b"".join(materials)
    while len(out) % 8:
        out += b"\0"
    head = list(h)
    head[11], head[12], head[13], head[14], head[15], head[16] = off_table, off_box, off_box, 0, 0, off_mats
    head[17] = head[18] = head[19] = 0
    out[0:0x50] = borg2_mesh.HEADER.pack(*head)
    return bytes(out)


def carrier_mesh(rom, index, pos, nrm, uv, tris, tex_size):
    """The carrier: every triangle in CARRIER_LIST, in batches of at most 32 vertices; each vertex's flag (Vtx +6) the
    skinned vertex it is. A second vertex buffer, the same, follows the blob (the port fills one while the other may
    still be drawn)."""
    h, _, materials, _ = borg2_mesh.read_template(rom, index)
    unit = 16 / h[2]  # a vertex's units per metre: the game draws v * scale / 16 (BorgAnimDrawSceneRaw, B5INST_MATRIX)

    def vtx(i):
        p = [int(round(c * unit)) for c in pos[i]]
        n = [max(-127, min(127, int(round(c * 127)))) for c in nrm[i]]
        # S10.5 texels, doubled: the game draws model textures at half scale (gSPTexture 0x8000, BorgAnimDrawSceneRaw).
        s, t = (int(round(uv[i][0] * tex_size[0] * 64)), int(round(uv[i][1] * tex_size[1] * 64)))
        return borg2_mesh.VTX.pack(*p, i, max(-32768, min(32767, s)), max(-32768, min(32767, t)), *n, 255)

    vtx_blob, cmds, batch, slots, pending = bytearray(), [], [], {}, []

    def flush():
        if not batch:
            return
        first = len(vtx_blob) // 16
        for i in batch:
            vtx_blob.extend(vtx(i))
        cmds.append((0x01000000 | len(batch) << 12 | len(batch) << 1, 0x0A000000 + first * 16))
        for a, b, c in pending:
            cmds.append((0x05000000 | a * 2 << 16 | b * 2 << 8 | c * 2, 0))
        batch.clear(), slots.clear(), pending.clear()

    for tri in tris:
        new = [i for i in dict.fromkeys(tri) if i not in slots]
        if len(batch) + len(new) > 32:
            flush()
            new = list(dict.fromkeys(tri))
        for i in new:
            slots[i] = len(batch)
            batch.append(i)
        pending.append(tuple(slots[i] for i in tri))
    flush()

    # A box that holds any pose (G_CULLDL skips the list only when all of it is off screen): 1.2 m around, -0.3..2.3 up.
    r, lo, hi = int(1.2 * unit), int(-0.3 * unit), int(2.3 * unit)
    box = b"".join(borg2_mesh.VTX.pack(x, y, z, 0, 0, 0, 0, 0, 0, 0) for x, y, z in
                   [(-r, hi, -r), (r, hi, -r), (r, hi, r), (-r, hi, r), (-r, lo, -r), (r, lo, -r), (r, lo, r),
                    (-r, lo, r)])
    out = bytearray(0x50)
    off_box = len(out)
    out += box
    off_vtx = len(out)
    out += vtx_blob
    offs = []
    for li in range(len(materials)):
        offs.append(len(out))
        if li == CARRIER_LIST:
            out += struct.pack(">II", 0x01008010, 0x0E000000)  # G_VTX the box
            out += struct.pack(">II", 0x0300000E, 0)           # G_CULLDL 0..7
            for w0, w1 in cmds:
                out += struct.pack(">II", w0, w1)
        out += struct.pack(">II", 0xDF000000, 0)
    off_table = len(out)
    out += b"".join(struct.pack(">I", o) for o in offs)
    off_mats = len(out)
    out += b"".join(materials)
    while len(out) % 8:
        out += b"\0"
    off_vtx2 = len(out)
    out += vtx_blob
    head = list(h)
    head[11], head[12], head[13], head[14], head[15], head[16] = (off_table, off_box, off_vtx, len(vtx_blob) // 16, 0,
                                                                  off_mats)
    head[17] = head[18] = head[19] = 0
    out[0:0x50] = borg2_mesh.HEADER.pack(*head)
    return bytes(out), off_vtx, off_vtx2, len(vtx_blob) // 16


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("glb", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--mesh", default="outfit plain", help="the GLB mesh (node) to use")
    ap.add_argument("--rom", type=Path, default=Path(__file__).resolve().parents[2] / "baserom.z64")
    args = ap.parse_args()
    g, binary = read_glb(args.glb)
    nodes = g["nodes"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    node = next(i for i, n in enumerate(nodes) if n.get("name") == args.mesh and "mesh" in n)
    skin = g["skins"][nodes[node]["skin"]]
    joints = skin["joints"]
    ibm = accessor(g, binary, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)  # column-major in glTF
    root = 0
    while root in parent:
        root = parent[root]

    prim = g["meshes"][nodes[node]["mesh"]]["primitives"]
    if len(prim) != 1:
        raise SystemExit("skinned_model.py: one primitive per mesh expected")
    at = prim[0]["attributes"]
    pos = accessor(g, binary, at["POSITION"])
    nrm = accessor(g, binary, at["NORMAL"])
    uv = accessor(g, binary, at["TEXCOORD_0"])
    jnt = accessor(g, binary, at["JOINTS_0"]).astype(int)
    wgt = accessor(g, binary, at["WEIGHTS_0"])
    wgt = wgt / wgt.sum(axis=1, keepdims=True)
    idx = accessor(g, binary, prim[0]["indices"]).astype(int).reshape(-1, 3)

    rom = romassets.Rom(str(args.rom))
    d = rom.data(MODEL)
    count = struct.unpack(">i", d[8:12])[0]
    at_b2 = struct.unpack(">I", d[0x2C:0x30])[0]
    meshes = list(struct.unpack(f">{count}i", d[at_b2:at_b2 + 4 * count]))
    carrier, slot = CARRIER, meshes.index(CARRIER)
    tex_size = (32, 32)  # 0x1FF9, the texture loaded before the carrier's list
    blob, off_vtx, off_vtx2, nvtx = carrier_mesh(args.rom, carrier, pos, nrm, uv, [tuple(t) for t in idx], tex_size)

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f"{MODEL:04x}-{carrier:04x}.bin").write_bytes(blob)
    for m in meshes:
        if m == carrier:
            continue
        (args.out / f"{MODEL:04x}-{m:04x}.bin").write_bytes(empty_mesh(args.rom, m))
    hd = rom.data(HEAD)
    hcount = struct.unpack(">i", hd[8:12])[0]
    hat = struct.unpack(">I", hd[0x2C:0x30])[0]
    for m in struct.unpack(f">{hcount}i", hd[hat:hat + 4 * hcount]):
        (args.out / f"{HEAD:04x}-{m:04x}.bin").write_bytes(empty_mesh(args.rom, m))

    # Each animation's frames: every joint's matrix from the mesh's rest to the model's space without the root
    # transform's own motion (the game applies it): inverse(root's global) x joint's global x inverse bind matrix.
    channels_of = []
    for a in g["animations"]:
        name = a.get("name", "")
        if not name.startswith("borg6 "):
            continue
        chans = []
        end = 0.0
        for c in a["channels"]:
            s = a["samplers"][c["sampler"]]
            t = accessor(g, binary, s["input"])[:, 0]
            v = accessor(g, binary, s["output"])
            chans.append((c["target"]["node"], c["target"]["path"], t, v))
            end = max(end, float(t[-1]))
        channels_of.append((int(name.split()[1]), chans, int(round(end * FPS)) + 1))

    rest = {i: (np.array(n.get("translation", [0, 0, 0]), float), np.array(n.get("rotation", [0, 0, 0, 1]), float),
                np.array(n.get("scale", [1, 1, 1]), float)) for i, n in enumerate(nodes)}

    def globals_at(chans, t):
        trs = {i: list(v) for i, v in rest.items()}
        for n, path, times, vals in chans:
            k = {"translation": 0, "rotation": 1, "scale": 2}.get(path)
            if k is not None:
                trs[n][k] = sample(times, vals, t, path)
        cache = {}

        def glob(i):
            if i not in cache:
                local = trs_matrix(*trs[i])
                cache[i] = glob(parent[i]) @ local if i in parent else local
            return cache[i]
        return glob

    anims = bytearray()
    for borg6, chans, frames in channels_of:
        anims += struct.pack("<II", borg6, frames)
        for f in range(frames):
            glob = globals_at(chans, f / FPS)
            inv_root = np.linalg.inv(glob(root))
            for j, jn in enumerate(joints):
                k = inv_root @ glob(jn) @ ibm[j]
                anims += struct.pack("<12f", *k[:3, :].reshape(-1))

    verts = bytearray()
    for i in range(len(pos)):
        verts += struct.pack("<6f4B4f", *pos[i], *nrm[i], *jnt[i], *wgt[i])
    head = struct.pack("<4s9I", b"AKS1", 1, len(pos), len(joints), len(channels_of), slot, off_vtx, off_vtx2, nvtx,
                       len(blob))
    (args.out / f"{MODEL:04x}.skin").write_bytes(head + verts + anims)
    print(f"skinned_model.py: {args.mesh}: {len(pos)} vertices, {len(idx)} triangles, {len(joints)} joints, "
          f"{len(channels_of)} animations; carrier {MODEL:04x}-{carrier:04x}.bin ({nvtx} vertices, {len(blob)} bytes), "
          f"{len(meshes) - 1} body meshes and the head's emptied")


if __name__ == "__main__":
    main()
