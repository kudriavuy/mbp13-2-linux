#!/usr/bin/env python3
"""thirds.py <dir> <a> <b> [<a> <b> ...]: third-octave levels of
recordings made by rec.sh (dB, the room floor of the same take subtracted
in power; bands within 3 dB of the floor are marked), and b - a per band.
Names like lin_30 / mac_30."""
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch

THIRDS = [63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
          2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]


def bands(path, skip=2.0, dur=16.0):
    r, d = wavfile.read(path)
    d = d.astype(float) / 32768
    e = np.abs(d)
    i0 = 0
    if "floor" in path:
        d = d[int(r * 1.0):]          # the mic's click at the start of a take
    else:
        # start of the noise: first 50 ms block 10 dB over the first 0.5 s
        blk = int(r * 0.05)
        lv = [np.sqrt(np.mean(d[k:k + blk] ** 2)) for k in range(0, len(d) - blk, blk)]
        ref = np.mean(lv[:10])
        i0 = next(k for k, x in enumerate(lv) if x > ref * 3.16) * blk
        d = d[i0 + int(skip * r): i0 + int((skip + dur) * r)]
    f, p = welch(d, r, nperseg=8192)
    return np.array([p[(f >= fc / 2 ** (1 / 6)) & (f < fc * 2 ** (1 / 6))].sum() for fc in THIRDS])


def level(dirn, name):
    """per band: level with the floor subtracted (dB), signal over floor (dB)"""
    side, v = name.split("_")
    s = bands(f"{dirn}/{name}.wav")
    fl = bands(f"{dirn}/floor_{side}_{v}.wav")
    return 10 * np.log10(np.maximum(s - fl, 1e-14)), 10 * np.log10(s / fl)


# Rule (user, 2026-10-18; the room was never fully quiet): a band less than
# 10 dB over the floor of its take is "under the noise" and not used; 10-20
# dB is used with the floor subtracted and marked "~".
SNR_MIN, SNR_OK = 10.0, 20.0


def main():
    dirn = sys.argv[1]
    pairs = list(zip(sys.argv[2::2], sys.argv[3::2]))
    res = [(a, b, level(dirn, a), level(dirn, b)) for a, b in pairs]
    print("Hz     " + "".join(f"{b + '-' + a:>17}{'S/N a/b':>12}" for a, b, _, _ in res))
    use = []
    for k, fc in enumerate(THIRDS):
        line = f"{fc:<6} "
        row = []
        for _, _, (da, na), (db, nb) in res:
            n = min(na[k], nb[k])
            if n < SNR_MIN:
                line += f"{'под шумом':>17}".replace("под", "під")
                row.append(False)
            else:
                line += f"{db[k] - da[k]:+16.2f}{'~' if n < SNR_OK else ' '}"
                row.append(True)
            line += f"{na[k]:6.1f}/{nb[k]:<5.1f}"
        use.append(row)
        print(line)
    use = np.array(use)
    tot = "total  "
    for j, (_, _, (da, _), (db, _)) in enumerate(res):
        m = use[:, j]
        pa, pb = (10 ** (da[m] / 10)).sum(), (10 ** (db[m] / 10)).sum()
        tot += f"{10 * np.log10(pb / pa):+16.2f} ({m.sum()} bands)  "
    print(tot)
    print("~ = 10-20 dB over the floor (floor subtracted); 'під шумом' = < 10 dB, not used; S/N = signal over floor per take")


if __name__ == "__main__":
    main()
