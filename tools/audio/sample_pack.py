#!/usr/bin/env python3
"""sample_pack.py: the restored-samples pack (#12) from AudioSR's output, for src/game/samples.cpp.

    sample_pack.py <export dir> <restored dir> [--out DIR]   (default: <data folder>/sample-packs/rechronicled-samples)

<export dir> is tools/audio's export (each sample the music plays as a WAV at its median playback rate, and
meta.json); <restored dir> AudioSR's 48 kHz versions of them. Each restored sample is brought back to its original's
exact length (the game's loop points and timing are in its frames), whole: AudioSR redraws the full sound, so the
8-bit samples' hiss goes as well as the lost treble coming back (picked by ear over keeping the original under its
cutoff and adding only the treble, which was too faint to hear). Matched to the original's level over the band the
original has (AudioSR normalises its output), peaks over full scale soft-limited, as 16-bit mono:
<samples>-<flag>-<FNV-1a of the original's first 256 bytes>.wav, the key the game computes from the sample it plays.
Made from the player's own ROM like the manual: nothing of the game's goes into the repository.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent / "hdui"))
import dcm  # noqa: E402
import romassets  # noqa: E402

DATA = Path.home() / ".local/share/AidynReChronicled"


def key(sample):
    h = 0x811C9DC5
    for b in sample["pcm"][:256]:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return f"{sample['samples']}-{sample['flag']}-{h:08x}"


def pcm_float(sample):
    if sample["flag"] & 8:
        return np.frombuffer(sample["pcm"], ">i2").astype(np.float64) / 32768
    return np.frombuffer(sample["pcm"], np.int8).astype(np.float64) / 128


def fft_resample(y, n):
    Y = np.fft.rfft(y)
    m = n // 2 + 1
    Y = Y[:m] if len(Y) >= m else np.pad(Y, (0, m - len(Y)))
    return np.fft.irfft(Y, n) * n / len(y)


def mend_loop(r, start, end):
    """A loop the game plays from end back to start (end exclusive) made seamless again: the restoration redraws the
    sound, and the sample at the loop's end no longer runs on into the one at its start. The loop's last stretch is
    crossfaded into what comes just before its start (what the end has to turn into); with no room before the start,
    the jump is eased out over that stretch instead."""
    n = min(2048, (end - start) // 4)
    if n < 8:
        return r
    w = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
    if start >= n:
        r[end - n:end] = r[end - n:end] * (1 - w) + r[start - n:start] * w
    else:
        jump = (2 * r[end - 1] - r[end - 2]) - r[start]  # where the end heads, against where the loop goes
        r[end - n:end] -= jump * w
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("export", type=Path)
    ap.add_argument("restored", type=Path)
    ap.add_argument("--rom", type=Path, default=HERE.parents[1] / "baserom.z64")
    ap.add_argument("--out", type=Path, default=DATA / "sample-packs/rechronicled-samples")
    args = ap.parse_args()
    rom = romassets.Rom(args.rom)
    meta = json.loads((args.export / "meta.json").read_text())
    args.out.mkdir(parents=True, exist_ok=True)
    for old in args.out.glob("*.wav"):
        old.unlink()
    peaks = []
    for name, m in sorted(meta.items()):
        if not (args.restored / f"{name}.wav").exists():
            continue  # not restored (AudioSR leaves out samples with nothing above 1 kHz): the game's own plays
        s = dcm.borg11(rom, int(m["borg11"], 16))
        x = pcm_float(s)
        y, _ = sf.read(args.restored / f"{name}.wav")
        r = fft_resample(y, len(x))
        X = np.abs(np.fft.rfft(x)) ** 2
        smooth = np.convolve(X, np.ones(64) / 64, "same")
        top = int(np.nonzero(10 * np.log10(smooth / smooth.max() + 1e-15) > -50)[0].max())
        # The whole restored sample (spitzdano's pick of the A/B: the 8-bit hiss goes as well as the treble coming
        # back), at the original's level over the band the original has.
        R = np.abs(np.fft.rfft(r)) ** 2
        r *= np.sqrt(X[:top].sum() / max(R[:top].sum(), 1e-20))
        if m.get("loop") and 0 <= m["loop"][0] < m["loop"][1] <= len(r):
            r = mend_loop(r, *m["loop"])
        peak = float(np.abs(r).max())
        if peak > 0.999:  # restored transients above full scale: soft-limit those few samples, keep the level
            over = np.abs(r) > 0.9
            r[over] = np.sign(r[over]) * (0.9 + 0.099 * np.tanh((np.abs(r[over]) - 0.9) / 0.099))
        peaks.append(peak)
        sf.write(args.out / f"{key(s)}.wav", r.astype(np.float32), m["rate"], subtype="PCM_16")
    (args.out / "pack.json").write_text(json.dumps({"name": "rechronicled-samples", "samples": len(peaks),
                                                    "made_with": "AudioSR (basic model, 50 steps)"}, indent=1) + "\n")
    print(f"sample_pack.py: {len(peaks)} of {len(meta)} samples -> {args.out} ({sum(p > 0.999 for p in peaks)} soft-limited)")


if __name__ == "__main__":
    main()
