"""romassets.py: read Aidyn's "Borg" assets straight from the ROM (images only, for the HD UI pack).

The game keeps every asset in one archive: a listing at ROM 0x1F89C10 (u32 count, u32 pad, then 16-byte entries
{s16 type, s16 compression, s32 compressed size, s32 size, s32 offset}) and the data at ROM 0xE7220
(decomp: n64Borg/borgmain.cpp getBorgItem; symbols borg_listings, borg_files). Compression 2 ("LZB") is UCL's
NRV2B with an 8-bit bit buffer (decomp lzb.cpp), 1 is LZO1X (text only), 0 is none. A type-8 (image) asset
decompresses to a Borg8Data {u16 format, width, height, pad; u32 palette, pixels}, the offsets counted from its start.
"""
import re
import struct
from pathlib import Path

LISTING = 0x1F89C10
FILES = 0xE7220
IMAGE = 8


def nrv2b(src, out_size):
    out = bytearray()
    pos = 0
    bb = 0

    def bit():
        nonlocal bb, pos
        bb = (bb << 1) & 0x1FF
        if bb & 0xFF == 0:
            bb = src[pos] * 2 + 1
            pos += 1
        return bb >> 8 & 1

    last_off = 1
    while True:
        while bit():
            out.append(src[pos])
            pos += 1
        m_off = 1
        while True:
            m_off = m_off * 2 + bit()
            if bit():
                break
        if m_off == 2:
            m_off = last_off
        else:
            m_off = (m_off - 3) * 256 + src[pos]
            pos += 1
            if m_off == 0xFFFFFFFF or m_off == -1:
                break
            m_off += 1
            last_off = m_off
        m_len = bit() * 2
        m_len += bit()
        if m_len == 0:
            m_len = 1
            while True:
                m_len = m_len * 2 + bit()
                if bit():
                    break
            m_len += 2
        m_len += m_off > 0xD00
        start = len(out) - m_off
        for i in range(m_len + 1):
            out.append(out[start + i])
    if len(out) != out_size:
        raise ValueError(f"nrv2b: {len(out)} bytes, expected {out_size}")
    return bytes(out)


class Rom:
    def __init__(self, path):
        self.rom = Path(path).read_bytes()
        self.count = struct.unpack(">I", self.rom[LISTING:LISTING + 4])[0]

    def entry(self, index):
        o = LISTING + 8 + index * 16
        return struct.unpack(">hhiii", self.rom[o:o + 16])  # type, compression, compressed, size, offset

    def data(self, index):
        kind, comp, csize, size, offset = self.entry(index)
        raw = self.rom[FILES + offset:FILES + offset + csize]
        if comp == 0:
            return raw[:size]
        if comp == 2:
            return nrv2b(raw, size)
        raise ValueError(f"asset {index}: compression {comp} not supported")

    def image(self, index):
        """Returns (format, width, height, pixel bytes, palette bytes or None), as Borg8Format / N64 layout."""
        kind = self.entry(index)[0]
        if kind != IMAGE:
            raise ValueError(f"asset {index} is type {kind}, not an image")
        d = self.data(index)
        fmt, w, h, _, pal, pix = struct.unpack(">HHHHII", d[:16])
        bpp = {1: 32, 2: 16, 3: 16, 4: 8, 5: 8, 6: 8, 7: 4, 8: 4, 9: 4}[fmt]
        pixels = d[pix:pix + w * h * bpp // 8]
        palette = None
        if fmt in (4, 7):
            palette = d[pal:pal + (256 if fmt == 4 else 16) * 2]
        return fmt, w, h, pixels, palette


# Pictures the decomp leaves unnamed that have redrawn art (art/), named here for it: two portraits (a skull-masked figure
# and a hooded one) and the halves of the trainer page's "Level Up Rituals" title.
UNNAMED = {0x117: "Portrait117", 0x170: "Portrait170", 0xE8: "TitleLevelUp", 0xEA: "TitleRituals"}


def borg8_names(decomp):
    """BORG8_* names from the decomp's include/Borgindecies.h, and UNNAMED's: {index: name}."""
    names = dict(UNNAMED)
    for line in (Path(decomp) / "include/Borgindecies.h").read_text().splitlines():
        m = re.match(r"#define\s+BORG8_(\w+)\s+(0[xX][0-9a-fA-F]+)", line)  # some have two spaces (PortraitBecan),
        # some 0X (the compass's)
        if m:
            names[int(m.group(2), 16)] = m.group(1)
    return names
