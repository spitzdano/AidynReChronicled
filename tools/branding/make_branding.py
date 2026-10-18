#!/usr/bin/env python3
"""make_branding.py: the Aidyn ReChronicled additions to the title screen, drawn from fonts (no game data).

  "Rechronicled"           under THE FIRST MAGE on the title card: Marcellus SC (small caps), copper like the logo's
                           CHRONICLES, dark outline, soft shadow
  "Enhanced by SPITZDANO"  bottom right: "Enhanced by" like the game's white text; SPITZDANO in the Neon Horizon chrome
                           type (Kanit Black Italic, sky-white > cyan > horizon line > magenta > pink, light scan
                           stripes, white rim, magenta glow)
  the version list           bottom left: Original, Aidyn Patch, Aidyn Plus (each a picture the game tints), "not
                           found", the running one's marker and the X / Y icons for switching
  the fast-forward marker, the controls cards' lines ([button] what it does), the LB / RB page prompts and the
  treasure screen's button hints (word [button])

Each is rendered at 8 pixels per N64 texel. Outputs:
  src/game/branding_images.h       the N64-size RGBA16 copies the game draws (src/game/patches.cpp, "Title screen").
                                    Generated: re-run this script (then rebuild the game), never edit it.
  build/branding/<name>@8x.png     the full-resolution versions
  <data>/texture-packs/rechronicled  a small RT64 pack replacing each RGBA16 copy (as the game loads it, hashed by
                                    build/cmake/tmemhash) with its full-resolution version; settings.json loads it by
                                    default

  python3 -I tools/branding/make_branding.py
"""
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools/hdui"))
from pngio import write_png  # noqa: E402

SS = 8  # output pixels per texel
MARCELLUS = PROJECT / "tools/hdui/fonts/MarcellusSC-Regular.ttf"
KANIT = HERE / "fonts/Kanit-BlackItalic.ttf"
HEADER = PROJECT / "src/game/branding_images.h"
OUT = PROJECT / "build/branding"
PACK = Path.home() / ".local/share/AidynReChronicled/texture-packs/rechronicled"

# The chrome fill (themes/neon-horizon/live/shaders/chrome.frag): position from cap top (0) to baseline (1).
CHROME = [(0.00, "#f2fdff"), (0.32, "#8af7f9"), (0.48, "#2de2e6"), (0.50, "#2a0f4e"), (0.53, "#ff3ec8"),
          (0.80, "#ff8ae2"), (1.00, "#fff4fd")]
COPPER = [(0.00, "#ffe2ac"), (0.40, "#e39352"), (0.72, "#ad471d"), (1.00, "#6c240d")]


def rgb(hex_):
    return np.array([int(hex_[i:i + 2], 16) for i in (1, 3, 5)], np.float32) / 255.0


def gradient(stops, f):
    """stops [(pos, colour)] sampled at f (any shape) -> (..., 3)."""
    f = np.clip(f, 0.0, 1.0)
    out = np.zeros(f.shape + (3,), np.float32)
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        t = np.clip((f - p0) / max(p1 - p0, 1e-6), 0.0, 1.0)[..., None]
        inside = ((f >= p0) & (f <= p1))[..., None]
        out = np.where(inside, rgb(c0) * (1 - t) + rgb(c1) * t, out)
    return out


def text_mask(text, font, size, w, h, x, baseline):
    """Coverage 0..1 of `text` drawn with its baseline at y = baseline, starting at x."""
    cmd = ["magick", "-size", f"{w}x{h}", "xc:black", "-fill", "white", "-font", str(font), "-pointsize", str(size),
           "-annotate", f"+{x}+{baseline}", text, "-colorspace", "gray", "-depth", "8", "gray:-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(h, w).astype(np.float32) / 255.0


def ink_width(text, font, size):
    m = text_mask(text, font, size, 4000, int(size * 2), 0, int(size * 1.4))
    cols = np.nonzero(m.max(axis=0) > 0.05)[0]
    return int(cols.max()) + 1 if len(cols) else 0


def cap_rows(font, size, baseline, h):
    """(cap top, baseline) rows of an H at this size."""
    m = text_mask("H", font, size, int(size * 2), h, 0, baseline)
    rows = np.nonzero(m.max(axis=1) > 0.5)[0]
    return int(rows.min()), int(rows.max()) + 1


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= r * r:
                out = np.maximum(out, np.roll(np.roll(m, dy, 0), dx, 1))
    return out


def blur(m, r):
    for axis in (0, 1):
        for _ in range(3):
            c = np.cumsum(np.pad(m, [(r + 1, r) if a == axis else (0, 0) for a in (0, 1)], mode="edge"), axis=axis)
            m = (np.take(c, range(2 * r + 1, c.shape[axis]), axis=axis) - np.take(c, range(0, c.shape[axis] - 2 * r - 1), axis=axis)) / (2 * r + 1)
    return m


def over(dst, colour, alpha):
    """Premultiplied RGBA dst; colour (3,) or (..., 3); alpha (...)."""
    a = np.clip(alpha, 0.0, 1.0)[..., None]
    dst[..., :3] = colour * a + dst[..., :3] * (1 - a)
    dst[..., 3:] = a + dst[..., 3:] * (1 - a)


def shift(m, dx, dy):
    return np.roll(np.roll(m, dy, 0), dx, 1)


def rechronicled(w, h):
    W, H = w * SS, h * SS
    size = 128
    while ink_width("Rechronicled", MARCELLUS, size) > W - 4 * SS and size > 20:
        size -= 2
    baseline = int(H * 0.80)
    width = ink_width("Rechronicled", MARCELLUS, size)
    m = text_mask("Rechronicled", MARCELLUS, size, W, H, (W - width) // 2, baseline)
    top, base = cap_rows(MARCELLUS, size, baseline, H)
    img = np.zeros((H, W, 4), np.float32)
    edge = dilate(m, 6)
    over(img, rgb("#000000"), 0.55 * blur(shift(edge, 5, 7), 6))
    over(img, rgb("#1e0d06"), edge)
    f = (np.arange(H, dtype=np.float32)[:, None] - top) / max(base - top, 1)
    over(img, gradient(COPPER, np.broadcast_to(f, (H, W))), m)
    over(img, rgb("#fff0c8"), 0.45 * np.clip(m - shift(m, 0, 3), 0, 1))  # light top edge, like the logo's bevel
    return img


def credit(w, h):
    W, H = w * SS, h * SS
    small, big, gap = 60, 104, 3 * SS
    baseline = int(H * 0.80)
    w1, w2 = ink_width("Enhanced by", MARCELLUS, small), ink_width("SPITZDANO", KANIT, big)
    while w1 + gap + w2 > W - 4 * SS:
        small, big = small - 2, big - 3
        w1, w2 = ink_width("Enhanced by", MARCELLUS, small), ink_width("SPITZDANO", KANIT, big)
    x2 = W - 2 * SS - w2
    x1 = x2 - gap - w1
    m1 = text_mask("Enhanced by", MARCELLUS, small, W, H, x1, baseline)
    m2 = text_mask("SPITZDANO", KANIT, big, W, H, x2, baseline)
    img = np.zeros((H, W, 4), np.float32)
    # Enhanced by: light, dark outline and shadow, like the game's PRESS START.
    e1 = dilate(m1, 4)
    over(img, rgb("#000000"), 0.5 * blur(shift(e1, 4, 5), 4))
    over(img, rgb("#120c08"), e1)
    over(img, rgb("#efe6d4"), m1)
    # SPITZDANO: magenta glow, white rim, chrome fill with light scan stripes.
    top, base = cap_rows(KANIT, big, baseline, H)
    over(img, rgb("#ff3ec8"), 0.8 * blur(dilate(m2, 5), 9))
    over(img, rgb("#fff4fd"), 0.95 * dilate(m2, 3))
    ys = np.arange(H, dtype=np.float32)[:, None]
    f = np.broadcast_to((ys - top) / max(base - top, 1), (H, W))
    fill = gradient(CHROME, f)
    stripe = (((ys - top) % 10) < 2).astype(np.float32)[..., None] * 0.35
    fill = fill * (1 - stripe) + stripe
    over(img, fill, m2)
    return img


def version_label(text, size=64):
    """The title screen's version list (bottom left, src/game/patches.cpp "Title screen"): a version's name, or the
    "not found" note, in light letters with a dark edge, left-aligned. The game tints it: bright for the one running,
    dimmer for one X or Y switches to, grey for one whose ROM isn't stored."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        baseline = int(H * 0.78)
        if SS // 2 + ink_width(text, MARCELLUS, size) > W - SS // 2:
            sys.exit(f"make_branding.py: the version label {text!r} is wider than {w} texels")
        m = text_mask(text, MARCELLUS, size, W, H, SS // 2, baseline)
        e = dilate(m, 4)
        over(img, rgb("#000000"), 0.5 * blur(shift(e, 4, 5), 4))
        over(img, rgb("#120c08"), e)
        over(img, rgb("#f6e7c1"), m)
        return img
    return draw


def version_mark(w, h):
    """The running version's marker in that list: a copper chevron with a dark edge, like the fast-forward marker's."""
    W, H = w * SS, h * SS
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    pad, half = 2.0 * SS, (H - 5 * SS) / 2.0
    tri = W - 2 * pad
    m = ((xs >= pad) & (xs <= pad + tri) & (np.abs(ys - H / 2.0) <= half * (1 - (xs - pad) / tri))).astype(np.float32)
    img = np.zeros((H, W, 4), np.float32)
    edge = dilate(m, 5)
    over(img, rgb("#000000"), 0.55 * blur(shift(edge, 4, 5), 5))
    over(img, rgb("#1e0d06"), edge)
    f = np.broadcast_to((ys - (H / 2.0 - half)) / max(2 * half, 1), (H, W))
    over(img, gradient(COPPER, f), m)
    return img


def version_button(button):
    """X or Y in that list, beside a version it switches to: the button icon, 10 texels, centred in the line."""
    def draw(w, h):
        import buttons
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        px = 10 * SS
        ic = np.frombuffer(bytes(buttons.icon_for(button, 10, 10, SS)), np.uint8).reshape(px, px, 4).astype(np.float32) / 255.0
        y0 = (H - px) // 2
        over(img[y0:y0 + px, 0:px], ic[..., :3], ic[..., 3])
        return img
    return draw


def fast_forward(w, h):
    """The fast-forward marker (top right while it is on): two copper chevrons with a dark edge."""
    W, H = w * SS, h * SS
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    m = np.zeros((H, W), np.float32)
    pad, half = 1.5 * SS, (H - 3 * SS) / 2.0
    tri = (W - 2 * pad) / 2.0
    for k in range(2):
        x0 = pad + k * tri
        inside = (xs >= x0) & (xs <= x0 + tri) & (np.abs(ys - H / 2.0) <= half * (1 - (xs - x0) / tri))
        m = np.maximum(m, inside.astype(np.float32))
    img = np.zeros((H, W, 4), np.float32)
    edge = dilate(m, 5)
    over(img, rgb("#000000"), 0.55 * blur(shift(edge, 4, 5), 5))
    over(img, rgb("#1e0d06"), edge)
    f = np.broadcast_to((ys - (H / 2.0 - half)) / max(2 * half, 1), (H, W))
    over(img, gradient(COPPER, f), m)
    return img


# Button prompts. A controls card (src/game/patches.cpp, "Controls card") is a dark panel with lines of
# [button] what it does; each line is one picture, so the field and combat cards share them. Buttons are named as the
# N64 buttons the port maps them to (tools/hdui/buttons.py: A B CD=X CU=Y CL=View CR=RT START=Menu Z=LT L=LB R=RB)
# or LS, RS, L3, R3 for the sticks; "+" between two means together.
ROW_W, ROW_H = 84, 7


def icon(button, h):
    """(w texels, premultiplied float rgba at SS) of one button icon, h texels square (bumpers and triggers too: their
    shape is as wide as a round button, so a wider box would only shift them right)."""
    import buttons
    w = h
    px = np.frombuffer(bytes(buttons.icon_for(button, w, h, SS)), np.uint8).reshape(h * SS, w * SS, 4)
    px = px.astype(np.float32) / 255.0
    px[..., :3] *= px[..., 3:]
    return w, px


def text_line(img, x, text, size, baseline, colour="#efe6d4"):
    m = text_mask(text, MARCELLUS, size, img.shape[1], img.shape[0], x, baseline)
    e = dilate(m, 3)
    over(img, rgb("#000000"), 0.45 * blur(shift(e, 3, 4), 3))
    over(img, rgb("#120c08"), e)
    over(img, rgb(colour), m)
    return ink_width(text, MARCELLUS, size)


def control_row(*pairs):
    """One line of a controls card: (buttons, label) pairs, e.g. (("A",), "Talk, open"), (("R", "+", "LS"), "Camera")."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        size = 40  # Marcellus SC: capitals about 3.6 texels tall, like the game's small text
        baseline = int(H * 0.76)
        x = SS // 2
        for n, (buttons_, label) in enumerate(pairs):
            if n:
                x += 4 * SS
            for b in buttons_:
                if b == "+":
                    x += SS // 2
                    x += text_line(img, x, "+", size, baseline) + SS
                    continue
                bw, px = icon(b, h)
                region = img[:, x:x + bw * SS]
                over(region, px[..., :3] / np.maximum(px[..., 3:], 1e-6), px[..., 3])
                x += bw * SS + SS // 2
            x += SS // 2
            x += text_line(img, x, label, size, baseline)
        if x > W:
            sys.exit(f"make_branding.py: a controls line is {x / SS:.0f} texels wide, more than {w}")
        return img
    return draw


def panel(w, h):
    """The controls card's background: plain black (the game draws it see-through)."""
    img = np.zeros((h * SS, w * SS, 4), np.float32)
    img[..., 3] = 1.0
    return img


def page_prompt(button):
    """LB / RB at the ends of a pause page's arrow (the arrow is 8 texels tall)."""
    def draw(w, h):
        img = np.zeros((h * SS, w * SS, 4), np.float32)
        bw, px = icon(button, h)
        x0 = (w - bw) * SS // 2
        over(img[:, x0:x0 + bw * SS], px[..., :3] / np.maximum(px[..., 3:], 1e-6), px[..., 3])
        return img
    return draw


def menu_hint(label, button, light=False):
    """A button hint on a menu's parchment, like the inventory page's "DETAILS [X]": the word in the pages' brown ink
    (capitals 7 texels tall, as DETAILS), then the button icon (11 texels, as its X). Left-aligned; the game measures
    the ink to space a row of them (src/game/patches.cpp, "Treasure hints"). light: for the combat menus' dark see-through
    box, light letters with a dark edge like the controls card's."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        size = 78  # Marcellus SC capitals about 7 texels tall
        baseline = int(H - 2 * SS)
        if light:
            text_line(img, SS // 2, label, size, baseline, "#f2e6c8")
        else:
            m = dilate(text_mask(label, MARCELLUS, size, W, H, SS // 2, baseline), 2)
            over(img, rgb("#945a4a"), m)
        x = SS // 2 + ink_width(label, MARCELLUS, size) + 2 * SS
        bw, px = icon(button, h)
        if x + bw * SS > W:
            sys.exit(f"make_branding.py: the {label!r} hint is wider than {w} texels")
        over(img[:, x:x + bw * SS], px[..., :3] / np.maximum(px[..., 3:], 1e-6), px[..., 3])
        return img
    return draw


HINTS = [("hint_take", "Take", "A"), ("hint_putback", "Put back", "A"), ("hint_leave", "Leave", "B"),
         ("hint_switch", "Switch", "DPAD"), ("hint_equip", "Equip", "A"), ("hint_unequip", "Unequip", "A"),
         ("hint_use", "Use", "A"), ("hint_lists", "Lists", "DPAD"), ("hint_train", "Train", "A"),
         ("hint_buy", "Buy", "A"), ("hint_sell", "Sell", "A"), ("hint_appraise", "Appraise", "CU")]

CONTROLS = [
    ("ctl_walk", (("LS",), "Walk")),
    ("ctl_camera", (("RS",), "Camera"), (("R3",), "Behind the party")),
    ("ctl_talk", (("A",), "Talk, open, search")),
    ("ctl_party", (("B",), "Party")),
    ("ctl_sneak", (("Z", "+", "LS"), "Sneak")),
    ("ctl_ff", (("L3",), "Fast-forward")),
    ("ctl_menu", (("START",), "Pause menu"), (("CL",), "Hide this")),
    ("ctl_move", (("LS",), "Move")),
    ("ctl_attack", (("A",), "Attack")),
    ("ctl_magic", (("CD",), "Spells"), (("CU",), "Items")),
    ("ctl_endturn", (("Z",), "or"), (("L",), "End turn")),
    ("ctl_combatcam", (("R", "+", "LS"), "Camera")),
    ("ctl_hide", (("CL",), "Hide this")),
]

def turn_label(text, size):
    """The combat turn order's round badge (src/game/patches.cpp, "Turn order"): light letters with a dark edge, like
    the controls card's, left-aligned (the game measures the ink to centre them)."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        baseline = H - int(1.5 * SS)
        x = SS // 2
        if x + ink_width(text, MARCELLUS, size) > W:
            sys.exit(f"make_branding.py: the turn label {text!r} is wider than {w} texels")
        text_line(img, x, text, size, baseline, "#f2e6c8")
        return img
    return draw


def art_off(w, h):
    """Top right while F2 has the redrawn art pack off: what is showing, and the key."""
    img = np.zeros((h * SS, w * SS, 4), np.float32)
    size, baseline = 40, int(h * SS * 0.8)
    text = "Redrawn art off  ·  F2"
    x = w * SS - SS // 2 - ink_width(text, MARCELLUS, size)
    text_line(img, x, text, size, baseline, "#f6e7c1")
    return img


IMAGES = [("rechronicled", 96, 16, rechronicled), ("credit", 140, 14, credit),
          ("ver_original", 50, 12, version_label("Original")), ("ver_patch", 50, 12, version_label("Aidyn Patch")),
          ("fast_forward", 20, 12, fast_forward), ("ctl_panel", 4, 4, panel),
          ("prompt_lb", 11, 8, page_prompt("L")), ("prompt_rb", 11, 8, page_prompt("R")),
          ("art_off", 64, 8, art_off)]
IMAGES += [(name, ROW_W, ROW_H, control_row(*pairs)) for name, *pairs in CONTROLS]
IMAGES += [(name, 60, 11, menu_hint(label, button)) for name, label, button in HINTS]
# The combat menus held open with Y (items) or X (spells and skills): choosing, and letting go to use.
IMAGES += [("chint_choose", 60, 11, menu_hint("Choose", "DPAD", True)),
           ("chint_use_items", 90, 11, menu_hint("Release to use", "CU", True)),
           ("chint_use_spells", 90, 11, menu_hint("Release to use", "CD", True))]
# The combat turn order: "Round" over the round's number (capitals 4 and 9 texels tall), and "+" for "+N".
IMAGES += [("turn_round", 20, 7, turn_label("Round", 44))]
IMAGES += [(f"turn_d{d}", 12, 12, turn_label(str(d), 100)) for d in range(10)]
IMAGES += [("turn_plus", 10, 12, turn_label("+", 100))]
# The title screen's version list (bottom left): Aidyn Plus's name, "not found", the running one's marker, X and Y.
IMAGES += [("ver_plus", 50, 12, version_label("Aidyn Plus")), ("ver_missing", 34, 12, version_label("not found", 52)),
           ("ver_mark", 10, 12, version_mark), ("ver_x", 10, 12, version_button("CD")), ("ver_y", 10, 12, version_button("CU"))]
# The title screen's version number (bottom right, above the credit): one picture per character, put together by the
# game from its build's version (aidyn_version.h: "v0.4.0", or "v0.4.0 dev" for a build after the tag).
IMAGES += [("verno_v", 6, 9, version_label("v", 44)), ("verno_dot", 4, 9, version_label(".", 44)),
           ("verno_dev", 14, 9, version_label("dev", 44))]
IMAGES += [(f"verno_{d}", 6, 9, version_label(str(d), 44)) for d in range(10)]
# The inventory page's party switch: View and RT beside the party's portraits (previous / next member).
IMAGES += [("party_prev", 11, 11, page_prompt("CL")), ("party_next", 11, 11, page_prompt("CR"))]


def to_rgba8(img):
    a = img[..., 3:]
    colour = np.where(a > 0, img[..., :3] / np.maximum(a, 1e-6), 0)
    return np.clip(np.concatenate([colour, a], -1) * 255 + 0.5, 0, 255).astype(np.uint8)


def to_rgba16(img, w, h):
    """Box-filtered to w x h, then RGBA 5551 (alpha on at half coverage); big-endian words."""
    small = img.reshape(h, SS, w, SS, 4).mean(axis=(1, 3))
    a = small[..., 3]
    colour = np.where(a[..., None] > 0, small[..., :3] / np.maximum(a, 1e-6)[..., None], 0)
    q = np.clip(colour * 31 + 0.5, 0, 31).astype(np.uint16)
    return (q[..., 0] << 11) | (q[..., 1] << 6) | (q[..., 2] << 1) | (a >= 0.5).astype(np.uint16)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/branding/make_branding.py (the title screen's Aidyn ReChronicled additions, drawn from",
             "// the fonts in tools/branding/fonts and tools/hdui/fonts). Do not edit: re-run the script.",
             "#pragma once", "#include <cstdint>", "", "namespace aidyn::branding {"]
    table = []
    for name, w, h, draw in IMAGES:
        img = draw(w, h)
        write_png(OUT / f"{name}@8x.png", w * SS, h * SS, to_rgba8(img).tobytes())
        words = to_rgba16(img, w, h).flatten()
        ident = "k" + "".join(p.capitalize() for p in name.split("_"))
        lines.append(f"    // {name}: {w}x{h} RGBA16")
        lines.append(f"    inline constexpr uint16_t {ident}[{w * h}] = {{")
        for i in range(0, len(words), 16):
            lines.append("        " + ", ".join(f"0x{v:04X}" for v in words[i:i + 16]) + ",")
        lines.append("    };")
        table.append(f'        {{ "{name}", {w}, {h}, {ident} }},')
        print(f"make_branding.py: {name} {w}x{h} -> {OUT / (name + '@8x.png')}")
    lines += ["", "    struct Image {", "        const char* name;", "        uint16_t w, h;", "        const uint16_t* rgba16;",
              "    };", "    inline constexpr Image kImages[] = {"] + table + ["    };", "}", ""]
    HEADER.write_text("\n".join(lines))
    print(f"make_branding.py: wrote {HEADER}")
    write_pack()


def write_pack():
    """The rechronicled pack: each image as the game loads it (one RGBA16 LoadTile at TMEM 0, see patches.cpp)."""
    import json
    import shutil
    import bigstrips
    import portraits
    tool = PROJECT / "build/cmake/tmemhash"
    if not tool.exists():
        sys.exit("build/cmake/tmemhash missing: cmake --build build/cmake --target tmemhash")
    images = header_images()
    jobs = []
    for name, (w, h, data) in images.items():
        tmem = bigstrips.tmem_strip(2, w, h, data, None, 0, h)  # Borg8 format 2 = RGBA16
        jobs.append((tmem, 0, 2, bigstrips.line_words(2, w), w, h, 0))
    tmp = PACK.with_name(PACK.name + ".new")
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    textures = []
    for (name, _), h_ in zip(images.items(), portraits.hashes(tool, jobs)):
        shutil.copy(OUT / f"{name}@8x.png", tmp / f"{h_}.png")
        textures.append({"path": f"{h_}.png", "hashes": {"rt64": h_}, "operation": "preload"})
    db = {"configuration": {"configurationVersion": 3, "autoPath": "rt64", "defaultOperation": "stream",
                            "defaultShift": "none", "hashVersion": 5},
          "textures": textures, "operationFilters": [], "shiftFilters": [], "extraFiles": []}
    (tmp / "rt64.json").write_text(json.dumps(db, indent=1) + "\n")
    shutil.rmtree(PACK, ignore_errors=True)
    tmp.rename(PACK)
    print(f"make_branding.py: {len(textures)} replacements in {PACK}")


def header_images(path=HEADER):
    """{name: (w, h, big-endian RGBA16 bytes)} as the game has them (for build_pack.py)."""
    text = path.read_text()
    out = {}
    for name, w, h, ident in re.findall(r'\{ "(\w+)", (\d+), (\d+), (\w+) \}', text):
        body = re.search(rf"{ident}\[\d+\] = \{{(.*?)\}};", text, re.S).group(1)
        words = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{4})", body)]
        out[name] = (int(w), int(h), b"".join(v.to_bytes(2, "big") for v in words))
    return out


if __name__ == "__main__":
    main()
