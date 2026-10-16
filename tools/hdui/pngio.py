"""Minimal PNG read/write (RGBA8, no third-party modules) and N64 Borg8 image decoding for tools/hdui."""
import struct
import zlib
from pathlib import Path


def write_png(path, w, h, rgba):
    """RGBA8 in; written as RGB when every pixel is opaque. With numpy, each row gets the PNG filter that suits it
    (as encoders do: least sum of absolute differences), which makes painted pictures about half the size."""
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is None:
        ctype = 6
        raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    else:
        a = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4)
        ch, ctype = (3, 2) if (a[..., 3] == 255).all() else (4, 6)
        x = a[..., :ch].reshape(h, w * ch).astype(np.int16)
        up = np.vstack([np.zeros((1, w * ch), np.int16), x[:-1]])
        left = np.hstack([np.zeros((h, ch), np.int16), x[:, :-ch]])
        ul = np.hstack([np.zeros((h, ch), np.int16), up[:, :-ch]])
        p = left + up - ul
        pa, pb, pc = np.abs(p - left), np.abs(p - up), np.abs(p - ul)
        paeth = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, ul))
        filtered = np.stack([x, x - left, x - up, x - ((left + up) >> 1), x - paeth]).astype(np.uint8)  # mod 256
        cost = np.abs(filtered.astype(np.int8).astype(np.int16)).sum(axis=2)  # (5, h)
        best = cost.argmin(axis=0)
        rows = np.empty((h, 1 + w * ch), np.uint8)
        rows[:, 0] = best
        rows[:, 1:] = filtered[best, np.arange(h)]
        raw = rows.tobytes()
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_png(path):
    """Returns (w, h, rgba bytearray). Handles 8-bit RGB/RGBA/gray/gray+alpha, all filter types, no interlace."""
    d = Path(path).read_bytes()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", path
    i, idat, w = 8, b"", 0
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]
        t = d[i + 4:i + 8]
        body = d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and interlace == 0, f"{path}: unsupported PNG (depth {depth}, interlace {interlace})"
        elif t == b"IDAT":
            idat += body
        i += 12 + n
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(w * h * ch)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1: line[x] = (line[x] + a) & 0xFF
            elif f == 2: line[x] = (line[x] + b) & 0xFF
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if ch == 4:
        return w, h, out
    rgba = bytearray(w * h * 4)
    for k in range(w * h):
        if ch == 3: rgba[k * 4:k * 4 + 4] = bytes((out[k * 3], out[k * 3 + 1], out[k * 3 + 2], 255))
        elif ch == 1: rgba[k * 4:k * 4 + 4] = bytes((out[k], out[k], out[k], 255))
        else: rgba[k * 4:k * 4 + 4] = bytes((out[k * 2], out[k * 2], out[k * 2], out[k * 2 + 1]))
    return w, h, rgba


def rgba16(v):
    r, g, b = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if v & 1 else 0)


# Borg8Format: 1 RGBA32, 2 RGBA16, 3 IA16, 4 CI8, 5 IA8, 6 I8, 7 CI4, 8 IA4, 9 I4. Colours decode the way RT64 does
# (TextureDecoder.hlsli), so a decoded image matches the dumped textures pixel for pixel.
BPP = {1: 32, 2: 16, 3: 16, 4: 8, 5: 8, 6: 8, 7: 4, 8: 4, 9: 4}


def decode_borg8(fmt, w, h, data, pal=None):
    out = bytearray(w * h * 4)
    palette = [rgba16((pal[i * 2] << 8) | pal[i * 2 + 1]) for i in range(len(pal) // 2)] if pal else []
    for k in range(w * h):
        if fmt == 1: c = tuple(data[k * 4:k * 4 + 4])
        elif fmt == 2: c = rgba16((data[k * 2] << 8) | data[k * 2 + 1])
        elif fmt == 3: c = (data[k * 2],) * 3 + (data[k * 2 + 1],)
        elif fmt in (4, 5, 6):
            v = data[k]
            if fmt == 4: c = palette[v] if v < len(palette) else (0, 0, 0, 0)
            elif fmt == 5: c = ((v >> 4) * 17,) * 3 + ((v & 15) * 17,)
            else: c = (v, v, v, v)
        else:
            v = (data[k >> 1] >> (0 if k & 1 else 4)) & 15
            if fmt == 7: c = palette[v] if v < len(palette) else (0, 0, 0, 0)
            elif fmt == 8:
                i = v & 0b1110
                i = ((i << 4) | (i << 1) | (i >> 2)) & 0xFF
                c = (i, i, i, 255 if v & 1 else 0)
            else: c = ((v << 4) | v,) * 4
        out[k * 4:k * 4 + 4] = bytes(c)
    return out


def _cubic(t):
    # Catmull-Rom weights for the 4 taps around a sample at fraction t
    t2, t3 = t * t, t * t * t
    return (-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2)


def upscale_glyph(w, h, rgba, scale, lo=0.4, hi=0.6):
    """Smooth font upscale: bicubic on every channel, then the alpha edge pulled tight with a smoothstep(lo, hi)
    around 0.5, which puts a stroke's edge on its texel boundary: strokes keep the original thickness, diagonals get
    smooth. Outside the cell counts as transparent (colour repeated), as it is in the font sheet, so a stroke on the
    cell's top or left edge is shaped like one on the bottom or right. (Repeating the edge texels outward instead
    made those strokes end in a flat slice that looked cut off.) Returns (W, H, rgba)."""
    W, H = w * scale, h * scale

    def px(x, y, c):
        if 0 <= x < w and 0 <= y < h:
            return rgba[(y * w + x) * 4 + c]
        return 0 if c == 3 else rgba[(min(max(y, 0), h - 1) * w + min(max(x, 0), w - 1)) * 4 + c]

    out = bytearray(W * H * 4)
    for Y in range(H):
        fy = (Y + 0.5) / scale - 0.5
        iy = int(fy // 1)
        wy = _cubic(fy - iy)
        for X in range(W):
            fx = (X + 0.5) / scale - 0.5
            ix = int(fx // 1)
            wx = _cubic(fx - ix)
            acc = [0.0, 0.0, 0.0, 0.0]
            for j in range(4):
                for i in range(4):
                    wgt = wx[i] * wy[j]
                    sx, sy = ix - 1 + i, iy - 1 + j
                    a = px(sx, sy, 3)
                    acc[3] += wgt * a
                    for c in range(3):
                        acc[c] += wgt * px(sx, sy, c)
            a = min(max(acc[3] / 255.0, 0.0), 1.0)
            t = min(max((a - lo) / (hi - lo), 0.0), 1.0)
            a = t * t * (3 - 2 * t)
            o = (Y * W + X) * 4
            out[o:o + 4] = bytes([min(max(int(acc[0] + 0.5), 0), 255), min(max(int(acc[1] + 0.5), 0), 255),
                                  min(max(int(acc[2] + 0.5), 0), 255), int(a * 255 + 0.5)])
    return W, H, out


def upscale_bicubic(w, h, rgba, scale):
    """Plain Catmull-Rom bicubic upscale (edge texels repeated). Returns (W, H, rgba)."""
    W, H = w * scale, h * scale
    out = bytearray(W * H * 4)
    for Y in range(H):
        fy = (Y + 0.5) / scale - 0.5
        iy = int(fy // 1)
        wy = _cubic(fy - iy)
        for X in range(W):
            fx = (X + 0.5) / scale - 0.5
            ix = int(fx // 1)
            wx = _cubic(fx - ix)
            acc = [0.0, 0.0, 0.0, 0.0]
            for j in range(4):
                sy = min(max(iy - 1 + j, 0), h - 1)
                for i in range(4):
                    sx = min(max(ix - 1 + i, 0), w - 1)
                    wgt = wx[i] * wy[j]
                    o = (sy * w + sx) * 4
                    for c in range(4):
                        acc[c] += wgt * rgba[o + c]
            o = (Y * W + X) * 4
            out[o:o + 4] = bytes(min(max(int(v + 0.5), 0), 255) for v in acc)
    return W, H, out


def _blur(W, H, chan, sigma):
    """Separable Gaussian blur of a list of floats (W*H), edges repeated."""
    import math
    r = max(1, int(sigma * 3 + 0.5))
    k = [math.exp(-(i * i) / (2 * sigma * sigma)) for i in range(-r, r + 1)]
    s = sum(k)
    k = [v / s for v in k]
    tmp = [0.0] * (W * H)
    for y in range(H):
        row = y * W
        for x in range(W):
            tmp[row + x] = sum(k[i] * chan[row + min(max(x + i - r, 0), W - 1)] for i in range(2 * r + 1))
    out = [0.0] * (W * H)
    for y in range(H):
        for x in range(W):
            out[y * W + x] = sum(k[i] * tmp[min(max(y + i - r, 0), H - 1) * W + x] for i in range(2 * r + 1))
    return out


def restore_colours(ai_rgba, ref_rgba, W, H, sigma):
    """Keep an AI upscale's fine detail but make its colours agree with a faithful (bicubic) upscale at every scale
    above `sigma` pixels: out = ai + blur(ref - ai). Small features the AI repainted (a one-texel blue iris that came
    out white) get their original colour back; edges stay sharp. Alpha is taken from the reference as is."""
    out = bytearray(W * H * 4)
    for c in range(3):
        diff = [float(ref_rgba[k * 4 + c]) - float(ai_rgba[k * 4 + c]) for k in range(W * H)]
        fix = _blur(W, H, diff, sigma)
        for k in range(W * H):
            out[k * 4 + c] = min(max(int(ai_rgba[k * 4 + c] + fix[k] + 0.5), 0), 255)
    for k in range(W * H):
        out[k * 4 + 3] = ref_rgba[k * 4 + 3]
    return out


def bleed(w, h, rgba, passes=8):
    """Gives fully transparent texels the colour of their opaque neighbours (alpha untouched), so an upscaler doesn't
    smear the black stored under transparency into the image's edges (a dark fringe around icons)."""
    import numpy as np
    img = np.frombuffer(bytes(rgba), np.uint8).reshape(h, w, 4).astype(np.float32)
    known = img[..., 3] > 0
    if known.all() or not known.any():
        return bytearray(rgba)
    rgb = img[..., :3] * known[..., None]
    for _ in range(passes):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx or dy:
                    acc += np.roll(np.roll(rgb * known[..., None], dy, 0), dx, 1)
                    cnt += np.roll(np.roll(known.astype(np.float32), dy, 0), dx, 1)
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    rgb[~known] = img[..., :3][known].mean(axis=0) if known.any() else 0
    out = img.copy()
    out[..., :3] = rgb
    return bytearray(out.round().astype(np.uint8).tobytes())
