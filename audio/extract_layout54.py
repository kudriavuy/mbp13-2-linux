#!/usr/bin/env python3
"""Pull the speaker data of the macOS AppleHDA layout 54 (MacBookPro13,2)
into the JSON file mbp-audio builds its graph from:

    extract_layout54.py <layout54.xml or layout54.xml.zlib> <out.json>

The file comes from this Mac's own macOS, read only (see audio/README.md):
/System/Library/Extensions/AppleHDA.kext/Contents/Resources/layout54.xml.zlib
The output holds Apple's numbers: keep it on this machine, never in git.

Taken: the static filters (EQ0 high-pass, EQ3, EQ5, the per-speaker filters
of EQ8/EQ9 with the 2.6 kHz crossover) and the thermal parameters of
DspThermalSpeakerProtection4ch. Filter keys 6/7/8 = float freq Hz / Q /
gain dB, key 5 = type (0 low-pass, 1 high-pass, 4 peaking, 6 shelf), key 2
= channel; both channels must carry the same filters.
"""
import json
import plistlib
import re
import struct
import sys

TYPES = {0: "lowpass", 1: "highpass", 4: "peaking", 6: "highshelf"}


def as_float(i):
    return struct.unpack("<f", struct.pack("<i", i))[0]


def load(path):
    raw = open(path, "rb").read()
    if path.endswith(".zlib"):
        import zlib
        raw = zlib.decompress(raw)
    raw = re.sub(rb"<!--.*?-->", b"", raw, flags=re.S)
    return plistlib.loads(b'<?xml version="1.0"?><plist version="1.0">' + raw + b"</plist>")


def dsp_functions(doc):
    sp = doc["PathMapRef"][0]["IntSpeaker"]["SignalProcessing"][0]["SoftwareDSP"]
    return {f["FunctionInfo"]["DspFuncInstance"]: f for f in sp.values()}


def filters(fn):
    per_ch = {}
    for f in fn["ParameterInfo"]["Filter"]:
        t = f["5"]
        if t not in TYPES:
            sys.exit(f"unknown filter type {t}")
        per_ch.setdefault(f["2"], []).append({
            "band": f["3"], "apple_type": t, "type": TYPES[t],
            "freq": round(as_float(f["6"]), 3), "q": round(as_float(f["7"]), 4),
            "gain": round(as_float(f["8"]), 3)})
    chans = sorted(per_ch)
    strip = lambda l: [{k: v for k, v in b.items() if k != "band"} for b in l]
    if any(strip(per_ch[c]) != strip(per_ch[chans[0]]) for c in chans):
        sys.exit("channels differ, this script assumes identical filters per channel")
    return per_ch[chans[0]]


def params(fn):
    return {int(k): as_float(v) if abs(v) > 1000 else v for k, v in fn["ParameterInfo"].items()}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    doc = load(sys.argv[1])
    fn = dsp_functions(doc)
    want = {0: "DspEqualization32", 1: "DspMozartCompressor", 2: "DspLoudness",
            3: "DspEqualization32", 4: "DspMozartCompressorDualBand",
            5: "DspEqualization32",
            6: "DspGainStage", 7: "Dsp2To4Splitter", 8: "DspEqualization32",
            9: "DspEqualization32", 10: "DspDelay", 11: "DspBuzzKill", 12: "DspControlFreak",
            13: "DspControlFreak", 14: "DspThermalSpeakerProtection4ch"}
    for i, name in want.items():
        if fn[i]["FunctionInfo"]["DspFuncName"] != name:
            sys.exit(f"function {i} is {fn[i]['FunctionInfo']['DspFuncName']}, expected {name}")
    # EQ8 feeds the delay and thermal inputs 0/1, EQ9 the BuzzKill and inputs 2/3
    pb = lambda i: {p["SourceFuncInstance"] for p in fn[i]["PatchbayInfo"].values()}
    assert pb(10) == {8} and pb(8) == {7} and pb(9) == {7}
    th = params(fn[14])

    def spk(base):
        v = [th[base + k] for k in range(11)]
        return {"vfs": v[0], "re": v[1], "rth_c": v[2], "tau_c": v[3], "rth_m": v[4],
                "tau_m": v[5], "t_amb": v[6], "t_lim": v[7], "unknown": v[8:10],
                "headroom": v[10]}

    out = {
        "source": "AppleHDA layout54.xml (macOS), IntSpeaker",
        "highpass": filters(fn[0]),
        "eq3": filters(fn[3]),
        "eq5": filters(fn[5]),
        "tweeter": filters(fn[8]),
        "woofer": filters(fn[9]),
        "thermal": {"tweeter": spk(3), "woofer": spk(25)},
    }
    if spk(3) != spk(14) or spk(25) != spk(36):
        sys.exit("thermal parameters differ between left and right")
    with open(sys.argv[2], "w") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {sys.argv[2]}: highpass {len(out['highpass'])}, eq3 {len(out['eq3'])}, eq5 {len(out['eq5'])}, "
          f"tweeter {len(out['tweeter'])}, woofer {len(out['woofer'])} biquads")


if __name__ == "__main__":
    main()
