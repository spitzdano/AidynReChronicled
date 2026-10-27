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
sys.path.insert(0, str(HERE))
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
        if KEYS["on"]:
            KEYS["slots"].append((0, slot_width(button), button))
            return img
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


# Keyboard variants. Every picture with a button icon in it also has a "_k" twin whose icons are blank slots (wide enough
# for a key's name), and a table of where the slots are (header: kKeyed); while the keyboard or mouse was used last the
# game draws the twin and a key cap or mouse picture in each slot (src/game/patches.cpp, "Keys on the prompts").
KEYS = {"on": False, "slots": []}
SLOTS = {}  # name_k -> (icon height, [(x, w, button)])
SLOT_W = {"LS": 24, "DPAD": 15, "L3": 15, "R3": 13, "RS": 13}


def slot_width(button):
    return SLOT_W.get(button, 14)


def keyed(name, draw):
    """The twin's drawing: the same, with the icons left blank and their places noted."""
    def run(w, h):
        KEYS["on"], KEYS["slots"] = True, []
        try:
            img = draw(w, h)
        finally:
            KEYS["on"] = False
        SLOTS[name + "_k"] = (h, list(KEYS["slots"]))
        return img
    return run


def icon(button, h):
    """(w texels, premultiplied float rgba at SS) of one button icon, h texels square (bumpers and triggers too: their
    shape is as wide as a round button, so a wider box would only shift them right)."""
    import buttons
    if KEYS["on"]:
        return slot_width(button), None
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
                if px is None:
                    KEYS["slots"].append((x // SS, bw, b))
                else:
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
        if px is None:
            KEYS["slots"].append((x0 // SS, bw, button))
        else:
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
        if px is None:
            KEYS["slots"].append((x // SS, bw, button))
        else:
            over(img[:, x:x + bw * SS], px[..., :3] / np.maximum(px[..., 3:], 1e-6), px[..., 3])
        return img
    return draw


HINTS = [("hint_take", "Take", "A"), ("hint_putback", "Put back", "A"), ("hint_leave", "Leave", "B"),
         ("hint_switch", "Switch", "DPAD"), ("hint_equip", "Equip", "A"), ("hint_unequip", "Unequip", "A"),
         ("hint_use", "Use", "A"), ("hint_lists", "Lists", "DPAD"), ("hint_train", "Train", "A"),
         ("hint_buy", "Buy", "A"), ("hint_sell", "Sell", "A"), ("hint_appraise", "Appraise", "CU"),
         ("hint_open", "Open", "A"), ("hint_scroll", "Scroll", "DPAD"), ("hint_close", "Close", "B")]

CONTROLS = [
    ("ctl_walk", (("LS",), "Walk")),
    ("ctl_camera", (("RS",), "Camera"), (("R3",), "Behind the party")),
    ("ctl_talk", (("A",), "Talk, open, search")),
    ("ctl_party", (("B",), "Party")),
    ("ctl_sneak", (("Z", "+", "LS"), "Sneak")),
    ("ctl_ff", (("L3",), "Fast-forward")),
    ("ctl_quicksave", (("CL",), "Quicksave")),
    ("ctl_menu", (("START",), "Pause menu"), (("L",), "Hide this")),
    ("ctl_move", (("LS",), "Move")),
    ("ctl_attack", (("A",), "Attack")),
    ("ctl_magic", (("CD",), "Spells"), (("CU",), "Items")),
    ("ctl_endturn", (("Z",), "End turn")),
    ("ctl_combatcam", (("R", "+", "LS"), "Camera")),
    ("ctl_hide", (("L",), "Hide this")),
]

TURN_INK = [(0.0, "#fff6e0"), (0.55, "#f1dcb2"), (1.0, "#c9a066")]


def turn_label(text, caps):
    """The combat turn order's words (src/game/patches.cpp, "Turn order"): ivory serif capitals `caps` texels tall,
    lit from above, with a soft dark shadow, as in spitzdano's concept of the strip; left-aligned (the game measures the ink
    to centre them on the plates)."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        baseline = H - int(0.8 * SS)
        size = 20
        while True:
            top, base = cap_rows(MARCELLUS, size, baseline, H)
            if base - top >= caps * SS or size > 400:
                break
            size += 1
        x = SS // 2
        if x + ink_width(text, MARCELLUS, size) + SS // 2 > W:
            sys.exit(f"make_branding.py: the turn label {text!r} is wider than {w} texels")
        m = text_mask(text, MARCELLUS, size, W, H, x, baseline)
        over(img, rgb("#000000"), 0.8 * blur(shift(dilate(m, 2), SS // 5, SS // 4), 3))
        f = (np.arange(H, dtype=np.float32)[:, None] - top) / max(base - top, 1)
        over(img, gradient(TURN_INK, np.broadcast_to(f, (H, W))), m)
        return img
    return draw


def page_word(text, size):
    """A word for the pause menu's ReChronicled page (src/game/patches.cpp, "ReChronicled page"), written on the page's
    parchment like the game's menu list: white letters as heavy as its HD letters (capitals 7 texels tall, as DETAILS),
    left-aligned, tinted by the game with the list's own colours (red-brown; the highlight pulsing brown to purple)."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        baseline = int(H - 2 * SS)
        if SS // 2 + ink_width(text, MARCELLUS, size) + 2 > W:
            sys.exit(f"make_branding.py: the page word {text!r} is wider than {w} texels")
        m = dilate(text_mask(text, MARCELLUS, size, W, H, SS // 2, baseline), 2)
        over(img, rgb("#ffffff"), m)
        return img
    return draw


def page_word_image(name, text, size=78, h=11):
    """(name, w, h, draw) with w fitted to the word."""
    return (name, int(ink_width(text, MARCELLUS, size) / SS) + 2, h, page_word(text, size))


PAGE_TITLE = [(0.0, "#fde2ae"), (0.3, "#f3a95a"), (0.7, "#c8702e"), (1.0, "#6a3014")]


TITLE_SIZE = 150  # the page titles' letters: capitals about 13 texels tall, as the game's own titles


def page_title(text, size, above=4.5):
    """A page title like the pause pages' own (MENU, OPTIONS): copper letters lit from above, a dark edge and a soft
    purple glow; the baseline `above` texels over the bottom. Centred; drawn untinted."""
    def draw(w, h):
        W, H = w * SS, h * SS
        img = np.zeros((H, W, 4), np.float32)
        baseline = int(H - above * SS)
        width = ink_width(text, MARCELLUS, size)
        if width + 8 * SS > W:
            sys.exit(f"make_branding.py: the page title {text!r} is wider than {w} texels")
        m = text_mask(text, MARCELLUS, size, W, H, (W - width) // 2, baseline)
        top, base = cap_rows(MARCELLUS, size, baseline, H)
        over(img, rgb("#7d55e8"), 0.9 * blur(dilate(m, 9), 7))
        over(img, rgb("#3a1a10"), dilate(m, 3))
        f = (np.arange(H, dtype=np.float32)[:, None] - top) / max(base - top, 1)
        over(img, gradient(PAGE_TITLE, np.broadcast_to(f, (H, W))), m)
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
IMAGES += [("turn_round", 13, 5, turn_label("Round", 2.6))]
IMAGES += [(f"turn_d{d}", 10, 10, turn_label(str(d), 7.6)) for d in range(10)]
IMAGES += [("turn_plus", 10, 10, turn_label("+", 7.6))]
# The strip's pieces, cut from spitzdano's concept (turn_order.py): its ends, the round plates, the portrait slots (bronze,
# the current fighter's gold, a target's silver) and the divider. The game lines them up by kTurnPieces (header).
import turn_order  # noqa: E402
TURN_PIECES, TURN_PIECE_LINES = turn_order.images(SS)
IMAGES += TURN_PIECES
# The title screen's version list (bottom left): Aidyn Plus's name, "not found", the running one's marker, X and Y.
IMAGES += [("ver_plus", 50, 12, version_label("Aidyn Plus")), ("ver_missing", 34, 12, version_label("not found", 52)),
           ("ver_mark", 10, 12, version_mark), ("ver_x", 10, 12, version_button("CD")), ("ver_y", 10, 12, version_button("CU"))]
# The title screen's version number (bottom right, above the credit): one picture per character, put together by the
# game from its build's version (aidyn_version.h: "v0.4.0", or "v0.4.0 dev" for a build after the tag).
IMAGES += [("verno_v", 6, 9, version_label("v", 44)), ("verno_dot", 4, 9, version_label(".", 44)),
           ("verno_dev", 14, 9, version_label("dev", 44)), ("verno_beta", 17, 9, version_label("beta", 44))]
IMAGES += [(f"verno_{d}", 6, 9, version_label(str(d), 44)) for d in range(10)]
# The pause menu's ReChronicled page: its title, each row's name and the values (tinted by the game), and the hints.
RC_WORDS = [("rc_widescreen", "Widescreen"), ("rc_textures", "Textures"), ("rc_vsync", "VSync"), ("rc_turn_order", "Turn order"),
            ("rc_combat_log", "Combat log"), ("rc_fps", "FPS counter"),
            ("rc_controls_card", "Controls card"), ("rc_button_prompts", "Button prompts"),
            ("rc_hud_edges", "HUD at the screen edges"),
            ("rc_draw_distance", "Draw distance"), ("rc_smooth_loops", "Smooth walk cycles"),
            ("rc_stick_camera", "Right-stick camera"), ("rc_invert_x", "Invert camera turning"),
            ("rc_invert_y", "Invert camera tilt"),
            ("rc_on", "On"), ("rc_off", "Off"), ("rc_dd_1", "Original"), ("rc_dd_15", "1.5x"), ("rc_dd_2", "2x"),
            ("rc_dd_25", "2.5x"), ("rc_dd_3", "3x"), ("rc_walk_speed", "Walk speed"), ("rc_report", "Report a problem"), ("rc_controls", "Controls"), ("rc_ws_125", "1.25x"),
            ("rc_ws_175", "1.75x"), ("rc_window", "Window"), ("rc_windowed", "Windowed"), ("rc_borderless", "Borderless"),
            ("rc_sound", "Sound"), ("rc_enhanced", "Enhanced"),
            ("rc_combat_speed", "Combat speed"), ("rc_enemy_turns", "Enemy turns"), ("rc_x4", "4x"),
            ("rc_instruments", "Instruments"), ("rc_restored", "Restored"),
            ("rc_anti_aliasing", "Anti-aliasing"), ("rc_aa_low", "Low"), ("rc_aa_medium", "Medium"), ("rc_aa_high", "High")]
# The title is wider than one load of TMEM (4 KB) allows: two halves, drawn side by side.
RC_TITLE_W = (int(ink_width("ReChronicled", MARCELLUS, TITLE_SIZE) / SS) + 11) // 2 * 2


def half(draw, w, h, right):
    def part(pw, ph):
        return draw(w, h)[:, (w // 2) * SS:] if right else draw(w, h)[:, :(w // 2) * SS]
    return part


IMAGES += [("rc_title_display", int(ink_width("Display", MARCELLUS, TITLE_SIZE) / SS) + 10, 25,
            page_title("Display", TITLE_SIZE, 5))]
IMAGES += [("rc_title_l", RC_TITLE_W // 2, 25, half(page_title("ReChronicled", TITLE_SIZE, 5), RC_TITLE_W, 25, False)),
           ("rc_title_r", RC_TITLE_W // 2, 25, half(page_title("ReChronicled", TITLE_SIZE, 5), RC_TITLE_W, 25, True))]
IMAGES += [page_word_image(name, text) for name, text in RC_WORDS]
IMAGES += [("rc_hint_change", 60, 11, menu_hint("Change", "A")), ("rc_hint_back", 60, 11, menu_hint("Back", "B"))]
# A conversation's hint (src/game/patches.cpp, "Conversation hint"): A moves it on, or picks the highlighted answer.
IMAGES += [("dlg_continue", 64, 11, menu_hint("Continue", "A", light=True)), ("dlg_choose", 64, 11, menu_hint("Choose", "A", light=True))]
# The inventory page's party switch: LT and RT beside the party's portraits (previous / next member; the port makes LT the
# game's C-left in the pause menu, src/main/input.cpp).
IMAGES += [("party_prev", 11, 11, page_prompt("Z")), ("party_next", 11, 11, page_prompt("CR"))]
# The combat columns' portrait frames from spitzdano's art (hud_frames.py): gold for the party, red and silver for enemies,
# and the soft band the game's colour for a slot (in reach, targeted) shows as.
import hud_frames  # noqa: E402
# The title menu's panel and the achievement toast, in the turn order bar's style (tools/branding/title_panel.py).
import title_panel  # noqa: E402
IMAGES += title_panel.IMAGES
IMAGES += [("frame_party", hud_frames.FRAME_W, hud_frames.FRAME_W, hud_frames.frame_party),
           ("frame_enemy", hud_frames.FRAME_W, hud_frames.FRAME_W, hud_frames.frame_enemy),
           ("frame_glow", hud_frames.GLOW_W, hud_frames.GLOW_W, hud_frames.frame_glow)]


# The pause pages' and menus' titles (the game's own pictures, 32-bit, in its decorative capitals), redrawn like the
# ReChronicled page's title: written into art/titles at 8x their size, where tools/hdui/art_pack.py takes them.
TITLES = [("TitleMenui", "Menu", 60, 24), ("TitleOptions", "Options", 86, 24), ("TitleTheater", "Theater", 87, 24),
          ("TitlePause", "Pause", 62, 24), ("TitleEnterName", "Enter Name", 125, 24),
          ("LoadGameTitle", "Load Game", 114, 24), ("SaveGameTitle", "Save Game", 111, 24)]
TITLES_DIR = PROJECT / "art/titles"


def write_titles():
    TITLES_DIR.mkdir(parents=True, exist_ok=True)
    for name, text, w, h in TITLES:
        size = TITLE_SIZE
        while ink_width(text, MARCELLUS, size) + 8 * SS > w * SS:
            size -= 4
        img = page_title(text, size)(w, h)
        write_png(TITLES_DIR / f"{name}.png", w * SS, h * SS, to_rgba8(img).tobytes())
        print(f"make_branding.py: {name} ({text!r}, size {size}) -> {TITLES_DIR / (name + '.png')}")


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


# Placeholder key caps and mouse pictures for the keyboard twins (spitzdano will have better art made; the game draws a
# cap as its two ends and a middle stretched to the key's name, then the name on it, and a mouse picture as it is).
def _rrect_sdf(xs, ys, cx, cy, hw, hh, r):
    qx = np.abs(xs - cx) - (hw - r)
    qy = np.abs(ys - cy) - (hh - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def _paint(img, mask, colour, alpha=1.0):
    over(img, rgb(colour), mask * alpha)


def key_cap(piece):
    """One piece of a key cap, cut from a 12 x 12 cap: slate with a brass rim."""
    cols = {"l": (0, 4), "m": (5, 7), "r": (8, 12)}[piece]

    def draw(w, h):
        W, H = 12 * SS, 12 * SS
        ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) / SS + 0.5 / SS
        d = _rrect_sdf(xs, ys, 6.0, 6.0, 5.9, 5.9, 2.6)
        inside = np.clip(0.5 - d * SS, 0, 1)
        rim = np.clip(0.5 - (np.abs(d + 0.45) - 0.45) * SS, 0, 1)
        img = np.zeros((H, W, 4), np.float32)
        _paint(img, inside, "#262a31")
        _paint(img, inside * np.clip((4.0 - ys) / 3.0, 0, 1), "#4a505b", 0.55)
        _paint(img, rim, "#d6c08a")
        return img[:, cols[0] * SS:cols[1] * SS]
    return draw


MOUSE_GOLD = "#f0bd2c"


def mouse_icon(which):
    """A mouse seen from above with one thing lit (a button, the wheel, a side button, a wheel step) or its movement."""
    def draw(w, h):
        W, H = w * SS, h * SS
        ys, xs = np.mgrid[0:H, 0:W].astype(np.float32) / SS + 0.5 / SS
        sx, sy = w / 12.0, h / 12.0
        xs, ys = xs / sx, ys / sy  # work in a 12 x 12 box
        d = _rrect_sdf(xs, ys, 6.0, 6.0, 3.1, 5.6, 3.0)
        inside = np.clip(0.5 - d * SS * sx, 0, 1)
        rim = np.clip(0.5 - (np.abs(d + 0.4) - 0.4) * SS * sx, 0, 1)
        img = np.zeros((H, W, 4), np.float32)
        _paint(img, inside, "#262a31")
        top = (ys < 5.2).astype(np.float32)
        if which == "left":
            _paint(img, inside * top * (xs < 5.8), MOUSE_GOLD)
        elif which == "right":
            _paint(img, inside * top * (xs > 6.2), MOUSE_GOLD)
        if which in ("middle", "wheelup", "wheeldown"):
            _paint(img, ((np.abs(xs - 6.0) < 0.8) & (ys > 1.2) & (ys < 4.2)).astype(np.float32), MOUSE_GOLD)
        if which == "back":
            _paint(img, ((xs > 1.6) & (xs < 3.2) & (ys > 5.4) & (ys < 7.6)).astype(np.float32), MOUSE_GOLD)
        if which == "forward":
            _paint(img, ((xs > 1.6) & (xs < 3.2) & (ys > 2.6) & (ys < 4.8)).astype(np.float32), MOUSE_GOLD)
        _paint(img, rim * (0.0 < 1), "#d6c08a")
        # the dividers
        _paint(img, ((np.abs(ys - 5.2) < 0.3) & (np.abs(xs - 6.0) < 2.9)).astype(np.float32), "#d6c08a", 0.8)
        _paint(img, ((np.abs(xs - 6.0) < 0.3) & (ys < 5.2) & (ys > 0.6)).astype(np.float32), "#d6c08a", 0.8)
        if which == "wheelup":
            tri = ((ys > 1.6) & (ys < 4.0) & (xs > 9.2) & (np.abs(xs - 10.6) < (ys - 1.6) * 0.6)).astype(np.float32)
            _paint(img, tri, MOUSE_GOLD)
        if which == "wheeldown":
            tri = ((ys > 6.4) & (ys < 8.8) & (xs > 9.2) & (np.abs(xs - 10.6) < (8.8 - ys) * 0.6)).astype(np.float32)
            _paint(img, tri, MOUSE_GOLD)
        if which == "move":
            for flip in (1, -1):
                cx = 6.0 + flip * 5.1
                tri = ((np.abs(ys - 6.0) < (1.3 - np.abs(xs - cx) * 0.9)) & (np.abs(xs - cx) < 1.4)).astype(np.float32)
                _paint(img, tri, MOUSE_GOLD)
        return img
    return draw


# Codex's painted ones (art/hud/source/keys: eleven PNGs at 8 pixels a unit, from the brief in ~/Work/aidyn-keys-brief) when
# the art submodule has them; the drawings above otherwise.
KEYS_SOURCE = PROJECT / "art/hud/source/keys"
MOUSE_NAMES = ("left", "right", "middle", "back", "forward", "wheelup", "wheeldown", "move")


def key_piece(name, flip=False, pad=0):
    """A painted piece, as it is (or mirrored), with `pad` transparent texels added at its right: the renderer draws a
    picture one unit wider than its texels cover and repeats the last column over it, which would show as a stub."""
    def draw(w, h):
        from pngio import read_png
        pw, ph, rgba = read_png(KEYS_SOURCE / f"{name}.png")
        a = np.frombuffer(bytes(rgba), np.uint8).reshape(ph, pw, 4).astype(np.float32) / 255
        a[..., :3] *= a[..., 3:]  # premultiplied
        if flip:
            a = a[:, ::-1]
        a = np.pad(a, ((0, 0), (0, pad * SS), (0, 0)))
        if a.shape[:2] != (h * SS, w * SS):
            sys.exit(f"make_branding.py: {name}.png is {pw}x{ph}, not {(w - pad) * SS}x{h * SS}")
        return a.copy()
    return draw


# The caps' pieces and the mice carry 2 transparent texels at the right (KEY_PAD): src/game/patches.cpp, draw_button_cap,
# draws them that much wider.
KEY_PAD = 2
if (KEYS_SOURCE / "key_cap_l.png").exists():
    IMAGES += [("key_cap_l", 4 + KEY_PAD, 12, key_piece("key_cap_l", pad=KEY_PAD)),
               ("key_cap_m", 2 + KEY_PAD, 12, key_piece("key_cap_m", pad=KEY_PAD)),
               ("key_cap_r", 4 + KEY_PAD, 12, key_piece("key_cap_l", flip=True, pad=KEY_PAD))]
    IMAGES += [(f"key_mouse_{n}", 12 + KEY_PAD, 12, key_piece(f"key_mouse_{n}", pad=KEY_PAD)) for n in MOUSE_NAMES]
else:  # the drawings: padded the same way
    def padded(draw, pad=KEY_PAD):
        return lambda w, h: np.pad(draw(w - pad, h), ((0, 0), (0, pad * SS), (0, 0)))
    IMAGES += [("key_cap_l", 4 + KEY_PAD, 12, padded(key_cap("l"))), ("key_cap_m", 2 + KEY_PAD, 12, padded(key_cap("m"))),
               ("key_cap_r", 4 + KEY_PAD, 12, padded(key_cap("r")))]
    IMAGES += [(f"key_mouse_{n}", 12 + KEY_PAD, 12, padded(mouse_icon(n))) for n in MOUSE_NAMES]

# The twins: every picture with a button icon in it, with the icons left as blank slots (keyed above).
ICON_BUTTONS = {"prompt_lb": ["L"], "prompt_rb": ["R"], "party_prev": ["Z"], "party_next": ["CR"], "ver_x": ["CD"],
                "ver_y": ["CU"], "rc_hint_change": ["A"], "rc_hint_back": ["B"], "dlg_continue": ["A"], "dlg_choose": ["A"],
                "chint_choose": ["DPAD"], "chint_use_items": ["CU"], "chint_use_spells": ["CD"]}
ICON_BUTTONS.update({name: [button] for name, _, button in HINTS})
ICON_BUTTONS.update({name: [b for pairs_ in pairs for b in pairs_[0] if b != "+"] for name, *pairs in CONTROLS})
_by_name = {n: (w, h, d) for n, w, h, d in IMAGES}
for _name, _buttons in ICON_BUTTONS.items():
    _w, _h, _d = _by_name[_name]
    # (Twins that would be the same picture once their icons are blank are one texel wider for the second of the pair: no
    # two share a texture's hash.)
    _wk = slot_width(_buttons[0]) if _name in ("prompt_lb", "prompt_rb", "party_prev", "party_next", "ver_x", "ver_y") \
        else _w + sum(slot_width(b) for b in _buttons)
    _wk += 1 if _name in ("prompt_rb", "party_next", "ver_y", "chint_use_spells") else 0
    IMAGES.append((_name + "_k", _wk, _h, keyed(_name, _d)))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for path in hud_frames.write_art():
        print(f"make_branding.py: {path}")
    lines = ["// Generated by tools/branding/make_branding.py (the title screen's Aidyn ReChronicled additions, drawn from",
             "// the fonts in tools/branding/fonts and tools/hdui/fonts). Do not edit: re-run the script.",
             "#pragma once", "#include <cstdint>", "", "namespace aidyn::branding {"]
    table = []
    for name, w, h, draw in IMAGES:
        if (w * 2 + 7) // 8 * 8 * h > 4096:
            sys.exit(f"make_branding.py: {name} ({w}x{h}) is more than one load of TMEM (4 KB): split it")
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
    keyed_lines = []
    for name_k, (kh, slots) in SLOTS.items():
        cells = ", ".join(f'{{ {x}, {sw}, "{b}" }}' for x, sw, b in slots)
        keyed_lines.append(f'        {{ "{name_k}", {kh}, {len(slots)}, {{ {cells} }} }},')
    lines += ["", "    struct Image {", "        const char* name;", "        uint16_t w, h;", "        const uint16_t* rgba16;",
              "    };", "    inline constexpr Image kImages[] = {"] + table + ["    };", "",
              "    // Each keyboard twin's slots: where (texels from its left), how wide, and the button it stands for.",
              "    struct KeySlot {", "        uint16_t x, w;", "        const char* button;", "    };",
              "    struct KeyedImage {", "        const char* name;", "        uint16_t h;", "        uint8_t n;", "        KeySlot slots[4];",
              "    };", "    inline constexpr KeyedImage kKeyed[] = {"] + keyed_lines + ["    };", "",
              "    // The turn order strip's pieces (tools/branding/turn_order.py), in texels: the bar's middle row and a",
              "    // slot's portrait window (left, top, right, bottom; 0 for the others).",
              "    struct TurnPiece {", "        const char* name;", "        float bar_mid, x0, y0, x1, y1;", "    };",
              "    inline constexpr TurnPiece kTurnPieces[] = {"] + TURN_PIECE_LINES + ["    };", "}", ""]
    HEADER.write_text("\n".join(lines))
    print(f"make_branding.py: wrote {HEADER}")
    write_pack()
    write_titles()


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
    hashes = portraits.hashes(tool, jobs)
    seen = {}
    for name, h_ in zip(images, hashes):
        if h_ in seen:  # two pictures with the same pixels would be one texture to the renderer, and it stops on that
            sys.exit(f"make_branding.py: {name} and {seen[h_]} are the same picture (hash {h_}): make them differ")
        seen[h_] = name
    for (name, _), h_ in zip(images.items(), hashes):
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
