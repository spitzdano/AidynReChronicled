#!/usr/bin/env python3
"""steam_shortcut.py: add Aidyn ReChronicled to Steam as a non-Steam game, with its library art. Runs on the handheld.

    python3 steam_shortcut.py [--dry-run]

Steam must be closed (in Desktop Mode: Steam menu > Exit; the script says so if it isn't). For every Steam account on
the machine it:
  1. reads userdata/<account>/config/shortcuts.vdf (Steam's binary key-value format). It writes nothing unless
     reading and re-writing the file gives back exactly the same bytes, so a format it doesn't understand is never
     damaged. A copy is kept as shortcuts.vdf.before-aidyn.
  2. adds the shortcut, or updates the one it added before (found by its Exe): name "Aidyn ReChronicled", the launcher
     aidyn-rechronicled.sh next to this script, icon.png next to it, no launch options.
  3. copies the art in steam-art/ (made from the ROM by tools/ally/steam_art.py, so a release has none) into
     userdata/<account>/config/grid, named by the shortcut's app id the way Steam looks for it: <id>p.png (library
     capsule), <id>.png (wide capsule), <id>_hero.png, <id>_logo.png. Missing pictures are skipped.
Steam picks it all up when it starts again (switching to Game Mode starts it).
"""
import binascii
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = "Aidyn ReChronicled"
LAUNCHER = HERE / "aidyn-rechronicled.sh"
ART = HERE / "steam-art"
STEAM_ROOTS = [Path.home() / ".local/share/Steam", Path.home() / ".steam/steam",
               Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam"]

# Binary VDF: a type byte, a NUL-terminated key, then the value. 0x00 map (until 0x08), 0x01 string, 0x02 int32,
# 0x03 float32, 0x07 uint64, 0x0A int64.
MAP, STR, INT, FLT, U64, I64, END = 0, 1, 2, 3, 7, 10, 8
FIXED = {INT: 4, FLT: 4, U64: 8, I64: 8}


def parse(data):
    pos = 0

    def cstr():
        nonlocal pos
        end = data.index(b"\0", pos)
        s = data[pos:end]
        pos = end + 1
        return s

    def items():
        nonlocal pos
        out = []
        while True:
            t = data[pos]
            pos += 1
            if t == END:
                return out
            key = cstr()
            if t == MAP:
                out.append([t, key, items()])
            elif t == STR:
                out.append([t, key, cstr()])
            elif t in FIXED:
                out.append([t, key, data[pos:pos + FIXED[t]]])
                pos += FIXED[t]
            else:
                raise ValueError(f"unknown value type {t:#x} at byte {pos - 1}")

    root = items()
    if pos != len(data):
        raise ValueError(f"{len(data) - pos} bytes after the end")
    return root


def dump(items):
    out = bytearray()
    for t, key, value in items:
        out += bytes([t]) + key + b"\0"
        if t == MAP:
            out += dump(value)
        elif t == STR:
            out += value + b"\0"
        else:
            out += value
    out.append(END)
    return bytes(out)


def get(items, key):
    for item in items:
        if item[1] == key:
            return item
    return None


def shortcut_entry(appid, exe, start_dir, icon):
    s = lambda k, v: [STR, k.encode(), v.encode()]
    i = lambda k, v: [INT, k.encode(), struct.pack("<i", v)]
    return [i("appid", appid), s("AppName", NAME), s("Exe", f'"{exe}"'), s("StartDir", f'"{start_dir}"'),
            s("icon", str(icon)), s("ShortcutPath", ""), s("LaunchOptions", ""), i("IsHidden", 0),
            i("AllowDesktopConfig", 1), i("AllowOverlay", 1), i("OpenVR", 0), i("Devkit", 0), s("DevkitGameID", ""),
            i("DevkitOverrideAppID", 0), i("LastPlayTime", 0), s("FlatpakAppID", ""), s("sortas", ""),
            [MAP, b"tags", []]]


def steam_running():
    try:
        out = subprocess.run(["pgrep", "-x", "steam"], capture_output=True, text=True).stdout.strip()
    except FileNotFoundError:
        return False
    return bool(out)


def main():
    dry = "--dry-run" in sys.argv
    if not LAUNCHER.exists():
        sys.exit(f"steam_shortcut.py: {LAUNCHER} is missing")
    root = next((r for r in STEAM_ROOTS if (r / "userdata").is_dir()), None)
    if root is None:
        sys.exit("steam_shortcut.py: no Steam found (start Steam once and log in, then run this again)")
    accounts = [d for d in (root / "userdata").iterdir() if d.is_dir() and d.name.isdigit() and d.name != "0"]
    if not accounts:
        sys.exit(f"steam_shortcut.py: no Steam account in {root / 'userdata'} (log in to Steam once first)")
    if steam_running() and not dry:
        sys.exit("steam_shortcut.py: Steam is running. Close it first (Steam menu > Exit, or: steam -shutdown), "
                 "then run this again.")

    exe = LAUNCHER.resolve()
    icon = HERE / "icon.png"
    # Steam's own id for a shortcut: CRC32 of exe + name, top bit set. Signed in shortcuts.vdf, unsigned in grid/.
    appid_u = (binascii.crc32((f'"{exe}"' + NAME).encode()) & 0xFFFFFFFF) | 0x80000000
    appid_s = struct.unpack("<i", struct.pack("<I", appid_u))[0]

    for account in accounts:
        cfg = account / "config"
        vdf = cfg / "shortcuts.vdf"
        if vdf.exists():
            data = vdf.read_bytes()
            try:
                tree = parse(data)
            except (ValueError, IndexError) as e:
                sys.exit(f"steam_shortcut.py: can't read {vdf} ({e}); nothing changed. Add the game by hand "
                         "(HANDOFF.md, 'By hand').")
            if dump(tree) != data:
                sys.exit(f"steam_shortcut.py: {vdf} doesn't round-trip; nothing changed. Add the game by hand "
                         "(HANDOFF.md, 'By hand').")
        else:
            data, tree = None, [[MAP, b"shortcuts", []]]
        shortcuts = get(tree, b"shortcuts")
        if shortcuts is None or shortcuts[0] != MAP:
            sys.exit(f"steam_shortcut.py: {vdf} has no shortcuts list; nothing changed.")
        entries = shortcuts[2]
        mine = None
        for e in entries:
            ex = get(e[2], b"Exe") or get(e[2], b"exe")
            if ex and b"aidyn-rechronicled.sh" in ex[2]:
                mine = e
                break
        entry = shortcut_entry(appid_s, exe, exe.parent, icon)
        if mine:
            old_id = get(mine[2], b"appid")
            if old_id:  # keep the id Steam already knows (play time, controller layout, art)
                appid_s = struct.unpack("<i", old_id[2])[0]
                appid_u = appid_s & 0xFFFFFFFF
                entry[0][2] = old_id[2]
            mine[2] = entry
            action = "updated"
        else:
            entries.append([MAP, str(len(entries)).encode(), entry])
            action = "added"
        new = dump(tree)
        print(f"{account.name}: shortcut {action} (app id {appid_u}), art -> {cfg / 'grid'}")
        if dry:
            continue
        if data is not None and not (cfg / "shortcuts.vdf.before-aidyn").exists():
            shutil.copy2(vdf, cfg / "shortcuts.vdf.before-aidyn")
        cfg.mkdir(parents=True, exist_ok=True)
        tmp = vdf.with_suffix(".vdf.tmp")
        tmp.write_bytes(new)
        os.replace(tmp, vdf)
        grid = cfg / "grid"
        grid.mkdir(exist_ok=True)
        for src, dst in (("capsule.png", f"{appid_u}p.png"), ("wide.png", f"{appid_u}.png"),
                         ("hero.png", f"{appid_u}_hero.png"), ("logo.png", f"{appid_u}_logo.png")):
            if (ART / src).exists():
                shutil.copyfile(ART / src, grid / dst)
    print("Done. Start Steam (or switch to Game Mode): Aidyn ReChronicled is in the Library under Non-Steam.")


if __name__ == "__main__":
    main()
