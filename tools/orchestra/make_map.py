#!/usr/bin/env python3
"""make_map.py: the orchestral music's instrument map (#12), from the ROM: which orchestral instrument plays each of
the music's samples.

    tools/orchestra/make_map.py [--rom ROM] [--out FILE]   (default: <data folder>/orchestra/orchestra-map.json)

The game's music is DCM tracks (Borg12 with 4+ channels and a long command stream: 83 of the ROM's 476), each with a
handful of its own instruments (Borg11 PCM samples, 44.1 kHz). For every sample a music track plays this measures its
pitch (YIN), whether the track loops it, how many different notes the track plays on it and how bright it is, and
guesses an orchestral instrument (General MIDI program; percussion as a drum note on the orchestral kit). The hand
choices in overrides.json (by Borg11 index) win over the guesses: fitted against the original parts (the research
tools: each sample's part rendered from the game's own log and compared with every orchestral candidate for tone,
articulation and level), "gain" the level that matches the original part, "game": true a sample left to the game.

The map is keyed as src/main/orchestra.cpp keys a sample it meets in the game: "<samples>:<flag>:<FNV-1a of its first
256 bytes>", the sample's own data, so it needs no index at runtime. Entries: program (or drum), the sample's
fundamental f0 (Hz at 44.1 kHz playback) and an octave shift. It's made from the player's own ROM and stays in their
data folder, like the manual: nothing of the game's goes into the repository.
"""
import argparse
import collections
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools/hdui"))
import dcm  # noqa: E402
import romassets  # noqa: E402

DATA = Path.home() / ".local/share/AidynReChronicled"


def key(sample):
    h = 0x811C9DC5
    for b in sample["pcm"][:256]:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return f"{sample['samples']}:{sample['flag']}:{h:08x}"


def pcm_float(sample):
    if sample["flag"] & 8:
        return np.frombuffer(sample["pcm"], ">i2").astype(np.float64) / 32768
    return np.frombuffer(sample["pcm"], np.int8).astype(np.float64) / 128


def yin(pcm):
    """(f0, clarity): YIN's first dip under 0.15 of the cumulative-mean-normalised difference, 30..1500 Hz."""
    seg = pcm[min(2000, len(pcm) // 4):][:4096]
    seg = seg - seg.mean()
    W, lo, hi = 2048, int(44100 / 1500), int(44100 / 30)
    if len(seg) < W + hi:
        seg = np.pad(seg, (0, W + hi - len(seg)))
    d = np.array([((seg[:W] - seg[t:t + W]) ** 2).sum() for t in range(hi + 1)])
    cm = d.copy()
    cm[1:] = d[1:] * np.arange(1, hi + 1) / np.maximum(np.cumsum(d[1:]), 1e-12)
    cm[0] = 1
    dips = [t for t in range(lo, hi) if cm[t] < 0.15 and cm[t] <= cm[t - 1] and cm[t] <= cm[t + 1]]
    lag = dips[0] if dips else lo + int(np.argmin(cm[lo:hi]))
    return 44100 / lag, float(1 - cm[lag])


def guess(sample, pitches, looped):
    pcm = pcm_float(sample)
    f0, clarity = yin(pcm)
    n = min(len(pcm), 16384)
    spec = np.abs(np.fft.rfft(pcm[:n]))
    centroid = float((spec * np.fft.rfftfreq(n, 1 / 44100)).sum() / (spec.sum() + 1e-12))
    entry = {"f0": round(f0, 1), "clarity": round(clarity, 2), "centroid": round(centroid), "notes": len(pitches)}
    if (len(pitches) <= 2 and clarity < 0.85) or (len(pitches) == 1 and not looped and len(pcm) < 30000):
        entry.update(drum=36 if centroid < 900 else 38 if centroid < 2500 else 42 if len(pcm) < 12000 else 49,
                     what="percussion")
        return entry
    notes = [69 + 12 * math.log2(f0 * p / 44100 / 440) for p in pitches.elements()]
    med, shift = float(np.median(notes)), 0
    while med + shift < 36:
        shift += 12
    while med + shift > 90:
        shift -= 12
    med += shift
    if looped:
        prog, what = (43, "contrabass") if med < 48 else (42, "cellos") if med < 55 else \
            (48, "strings") if med < 72 else (40, "violins")
    else:
        prog, what = (45, "pizzicato") if med < 55 else (46, "harp") if med < 76 else (73, "flute")
    entry.update(program=prog, shift=shift, what=what)
    return entry


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", type=Path, default=HERE.parents[1] / "baserom.z64")
    ap.add_argument("--out", type=Path, default=DATA / "orchestra/orchestra-map.json")
    args = ap.parse_args()
    rom = romassets.Rom(args.rom)
    overrides = json.loads((HERE / "overrides.json").read_text()) if (HERE / "overrides.json").exists() else {}
    uses = collections.defaultdict(lambda: [collections.Counter(), False])
    tracks = 0
    for i in range(rom.count):
        if rom.entry(i)[0] != 12:
            continue
        try:
            b = dcm.borg12(rom, i)
        except Exception:
            continue
        if b["chans"] < 4 or b["size"] < 3000:
            continue  # a sound effect
        tracks += 1
        events, _ = dcm.events(b)
        for _, _, kind, v in events:
            if kind == "note" and v["inst"] < len(b["insts"]):
                u = uses[b["insts"][v["inst"]]]
                u[0][v["pitch"]] += 1
                u[1] |= b["table"][v["inst"]][6] != 0
    out = {}
    for index, (pitches, looped) in sorted(uses.items()):
        sample = dcm.borg11(rom, index)
        entry = guess(sample, pitches, looped)
        entry["borg11"] = hex(index)
        hand = overrides.get(hex(index), {})
        if hand.get("game"):
            continue  # left to the game: an effect in the music (wind, a whoosh), no instrument plays it
        if "program" in hand:
            entry.pop("drum", None)
        if "drum" in hand:
            entry.pop("program", None)
            entry.pop("shift", None)
        entry.update(hand)
        out[key(sample)] = entry
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=1) + "\n")
    kinds = collections.Counter(e["what"] for e in out.values())
    print(f"make_map.py: {tracks} music tracks, {len(out)} samples -> {args.out} ({dict(kinds)})")


if __name__ == "__main__":
    main()
