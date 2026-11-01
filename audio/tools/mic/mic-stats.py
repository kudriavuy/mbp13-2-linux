#!/usr/bin/python3
"""Statistics of mic-a.sh recordings (s32 stereo WAV, 3 s quiet, then speech):
click at the start (peak of the first 0.15 s), noise floor (0.5-2.5 s, RMS),
speech (4-11 s: peak, RMS of the loudest 50 ms blocks = voice, samples at or
near full scale), and voice over noise.
    mic-stats.py <wav>..."""
import sys
import wave

import numpy as np


def db(x):
    return 20 * np.log10(max(x, 1e-12))


def stats(path):
    with wave.open(path) as w:
        rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    full = float(2 ** (8 * width - 1))
    x = np.frombuffer(raw, dtype={2: "<i2", 4: "<i4"}[width]).astype(np.float64).reshape(-1, ch)[:, 0] / full
    seg = lambda a, b: x[int(a * rate):int(b * rate)]
    click = np.abs(seg(0, 0.15)).max()
    noise = np.sqrt(np.mean(seg(0.5, 2.5) ** 2))
    speech = seg(4, 11)
    blocks = speech[: len(speech) // 2205 * 2205].reshape(-1, 2205)
    brms = np.sqrt(np.mean(blocks ** 2, axis=1))
    voiced = brms[brms > np.percentile(brms, 70)]
    voice = np.sqrt(np.mean(voiced ** 2))
    clipped = int(np.sum(np.abs(speech) > 0.999))
    return click, noise, np.abs(speech).max(), voice, clipped


print(f"{'file':14} {'click dBFS':>10} {'noise dBFS':>10} {'peak dBFS':>9} {'voice dBFS':>10} {'voice/noise dB':>14} {'clipped':>7}")
for p in sys.argv[1:]:
    c, n, pk, v, cl = stats(p)
    print(f"{p.split('/')[-1]:14} {db(c):10.1f} {db(n):10.1f} {db(pk):9.1f} {db(v):10.1f} {db(v) - db(n):14.1f} {cl:7d}")
