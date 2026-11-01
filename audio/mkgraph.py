#!/usr/bin/env python3
"""Build the PipeWire filter-chain graph for the internal speakers of the
MacBookPro13,2.

    mkgraph.py <layout54.json> <map> <eq on|off> [meta.json]  > speakers.conf

<layout54.json>: the speaker data of this Mac's own macOS (static EQ,
crossover, thermal parameters), made by extract_layout54.py; it is not
shipped. <map>: the hardware channel position of the left tweeter, right
tweeter, left woofer and right woofer, e.g. "RL,FR,FL,RR" (speaker-map).

Two layers, one graph:

  protection (always): the high-pass of the macOS chain, the crossover
      (tweeters never get the bass), the bass ceiling on the woofers and the
      tweeter ceiling (mbp_basslimit / mbp_treblelimit), and
      mbp_speakerguard (thermal model + look-ahead limiter) right before the
      amps;
  eq (toggled by 'mbp-audio eq'): the static macOS EQ (EQ3, EQ5) and a
      static make-up gain (EQ off: a -2 dB trim instead); the per-speaker
      filters stay on in both modes.

The sink volume is mbp_volume (a volume law measured in macOS), right after
the high-pass, before everything that limits.

Not included: the dynamic stages of the macOS chain (leveler, loudness,
two-band compressor, BuzzKill). Without them the speakers play quieter than
in macOS, never louder: every stage here is static or only lowers the level.
"""
import json
import math
import os
import sys

GUARD = "/usr/local/lib/ladspa/mbp_speakerguard.so"
SPEAKER_RATE = 44100    # the only rate of the speaker amps (driver)

# Format of the filter data in layout54.json (AppleHDA layout XML): RBJ
# biquads whose Q is defined by alpha = sin(w) sinh(1 / (2 Q)); types 5/6
# are low/high shelves with the slope S = 0.7071, types 0/1/4 low-pass,
# high-pass, peaking. PipeWire's bq_* use alpha = sin(w) / (2 Q), so each
# filter gets the Q that gives the same alpha (checked against the response
# measured with a microphone on the hardware).
APPLE_SHELF_S = 0.7071
APPLE_SHELVES = {5: "lowshelf", 6: "highshelf"}

# Volume law (mbp_volume): dB at 0, 5, ... 100 % of the slider, linear in
# between. Measured in macOS 12.7.6 on a MacBookPro13,2
# (tools/macos/vollaw.sh): the output level control steps 0..127 =
# -63.5..0 dB against the slider.
MACOS_VOLUME_STEPS = [0, 28, 40, 49, 57, 64, 70, 75, 80, 85, 90, 94, 98, 102, 106, 110, 114, 117, 120, 124, 127]
VOLUME_LAW_DB = [-63.5 + 0.5 * k for k in MACOS_VOLUME_STEPS]

# Bass boost ('mbp-audio bass 0|2|3'): a low shelf under BASS_BOOST_FREQ,
# off by default, before the crossover: the ceilings and the guard see it.
BASS_BOOST_FREQ = 250.0
BASS_BOOST_Q = 0.7071
BASS_BOOST_MAX_DB = 3.0

# Bass ceiling for the woofers (basslimit.h): the side-chain is the woofer
# signal through the inverse of the static peaking filters up to
# BASS_MAX_HZ, so a 0 dB ceiling = never more bass than the static macOS EQ
# gives a 0 dBFS sine, at every frequency, in both EQ modes. Raised by
# BASS_RAISE_DB (room for the bass boost), minus BASS_MARGIN_DB (the
# side-chain low-pass reads up to 1.7 dB low at 400 Hz). The gain acts
# below BASS_SPLIT only, so the woofer mids do not pump with the bass.
BASS_MAX_HZ = 1000.0
BASS_DETECT = 600.0
BASS_RAISE_DB = 5.0
BASS_MARGIN_DB = 2.0
BASS_SPLIT = 1000.0     # LR8: at 400 Hz the rest is -64 dB
LR8_ALLPASS_Q = (0.54119610, 1.30656296)  # Butterworth 4th order: the LR8 sum
BASS_LOOKAHEAD_MS = 20.0  # longer than a 50 Hz period: no gain ripple within a cycle
BASS_RELEASE_MS = 300.0

# Make-up gain (EQ on only), after the static EQ, before the ceilings and
# the guard. The dynamic stages of macOS (leveler, two-band compressor, gain
# stage) lift quiet passages by up to ~+28 dB; without them a static gain
# stands in, chosen so that our output stays at least 2 dB under a model of
# the macOS chain in every third octave (pink noise, stepped sines, music,
# 30-100 %): loud passages are held by the ceilings and the limiter. The
# limit is the bass of loud stepped sines at 100 % (63 Hz: -2.3 dB at 11 dB,
# +0.3 dB at 14 dB). EQ off: no make-up, the per-speaker filters stay
# (MBP_MAKEUP_DB overrides, for tests).
MAKEUP_DB = 11.0
# EQ off: the static EQ is bypassed (its cuts too), so the level is trimmed
EQ_OFF_TRIM_DB = -2.0

# Tweeter ceiling: the same plugin on the whole tweeter signal, side-chain
# through the inverse static EQ and crossover. With the EQ on the tweeters
# get exactly the static EQ (0 dB over it); the ceiling only acts with the
# EQ off or on rounding.
TREBLE_RAISE_DB = 3.0

# Thermal model: peak volts at the amp output for a 0 dBFS sample at the
# driver's default DAC volume of the amps (-3 dB): the macOS value from the
# layout. Another DAC volume (snd_hda_codec_cs8409 ssm_dac_vol, 'mbp-audio
# dac'; MBP_DAC_DB in the environment) scales it the same way; above 0 dB
# the amps gain digitally, so the limiter goes down by as much.
DAC_DEFAULT_DB = -3.0
GUARD_CEILING_DB = -3.0


def env_db(name, default, lo, hi):
    v = float(os.environ.get(name, default))
    if not lo <= v <= hi:
        sys.exit("%s %s: outside %g..%g dB" % (name, v, lo, hi))
    return v


def dac_db():
    return env_db("MBP_DAC_DB", DAC_DEFAULT_DB, -3.0, 18.0)


def bass_boost_db():
    return env_db("MBP_BASS_DB", 0.0, 0.0, BASS_BOOST_MAX_DB)


def makeup_db():
    return env_db("MBP_MAKEUP_DB", MAKEUP_DB, 0.0, 30.0)


def vfs(th):
    v = max(th["tweeter"]["vfs"], th["woofer"]["vfs"])
    return round(v * 10 ** ((dac_db() - DAC_DEFAULT_DB) / 20), 2)


def guard_ceiling():
    return GUARD_CEILING_DB - max(0.0, dac_db())


def apple_rbj(f):
    """A layout54.json filter as PipeWire's builtin biquad with the same response."""
    f = dict(f)
    t = f.get("apple_type")
    if t in APPLE_SHELVES:
        a = 10 ** (f["gain"] / 40)
        f["type"] = APPLE_SHELVES[t]
        f["q"] = round(1 / math.sqrt((a + 1 / a) * (1 / APPLE_SHELF_S - 1) + 2), 6)
    elif t is not None:
        f["q"] = round(1 / (2 * math.sinh(1 / (2 * f["q"]))), 6)
    return f


GAIN_TYPES = ("peaking", "lowshelf", "highshelf")


def peq(name, filters):
    """One param_eq node: all biquads of a stage, both channels (In/Out 1 = left, 2 = right)."""
    return {"type": "builtin", "label": "param_eq", "name": name,
            "config": {"filters": [{"type": "bq_" + f["type"], "freq": f["freq"], "q": f["q"],
                                    "gain": f["gain"] if f["type"] in GAIN_TYPES else 0.0}
                                   for f in filters]}}


def build(d, hw_map, eq_on):
    """Stages, each one param_eq for both channels; at every EQ stage a mixer per
    channel picks the EQ'd signal (Gain 1) or the bypass (Gain 2), so the EQ is
    switched live without touching the protection stages."""
    d = dict(d)
    for key in ("highpass", "eq3", "eq5", "tweeter", "woofer"):
        d[key] = [apple_rbj(f) for f in d[key]]
    pk_eq = lambda key: [f for f in d[key] if f["type"] in GAIN_TYPES]
    xo = lambda key: [f for f in d[key] if f["type"] not in GAIN_TYPES]
    th = d["thermal"]
    vol = {f"Law {i}": v for i, v in enumerate(VOLUME_LAW_DB)}
    hp = d["highpass"]
    nodes = [{"type": "builtin", "label": "copy", "name": "in_l"},
             {"type": "builtin", "label": "copy", "name": "in_r"},
             peq("hp", hp),
             {"type": "ladspa", "name": "vol", "plugin": GUARD, "label": "mbp_volume", "control": vol},
             peq("eq3", pk_eq("eq3")),
             peq("eq5", pk_eq("eq5")),
             peq("xtw", xo("tweeter")), peq("xwf", xo("woofer")),
             peq("tweq", pk_eq("tweeter")), peq("wfeq", pk_eq("woofer")),
             # the phase the bass ceiling's LR8 split gives the woofers, on the tweeters
             peq("twap", [{"type": "allpass", "freq": BASS_SPLIT, "q": q} for q in LR8_ALLPASS_Q]),
             # side-chain of the bass ceiling: inverse static peaking EQ (bass part)
             peq("wdet", [dict(f, gain=-f["gain"]) for f in pk_eq("eq3") + pk_eq("eq5") + pk_eq("woofer")
                          if f["freq"] <= BASS_MAX_HZ]),
             {"type": "ladspa", "name": "bass", "plugin": GUARD, "label": "mbp_basslimit",
              "control": {"Ceiling": BASS_RAISE_DB - BASS_MARGIN_DB, "Split": BASS_SPLIT,
                          "Detect": BASS_DETECT, "Lookahead": BASS_LOOKAHEAD_MS,
                          "Release": BASS_RELEASE_MS,
                          "HP1 freq": hp[0]["freq"], "HP1 Q": hp[0]["q"],
                          "HP2 freq": hp[1]["freq"], "HP2 Q": hp[1]["q"],
                          "Inverse floor": 15.0}},
             # side-chain of the tweeter ceiling: inverse of the whole static peaking EQ
             peq("tdet", [dict(f, gain=-f["gain"]) for f in pk_eq("eq3") + pk_eq("eq5") + pk_eq("tweeter")]),
             {"type": "ladspa", "name": "treble", "plugin": GUARD, "label": "mbp_treblelimit",
              "control": {"Ceiling": TREBLE_RAISE_DB, "Split": 0.0, "Detect": 0.0,
                          "Lookahead": BASS_LOOKAHEAD_MS, "Release": BASS_RELEASE_MS,
                          "HP1 freq": xo("tweeter")[0]["freq"], "HP1 Q": xo("tweeter")[0]["q"],
                          "HP2 freq": xo("tweeter")[1]["freq"], "HP2 Q": xo("tweeter")[1]["q"],
                          "Inverse floor": 500.0}}]
    links, toggles = [], []

    def mixer(name, eq_src, bypass_src, on_gain=1.0, off_gain=1.0):
        nodes.append({"type": "builtin", "label": "mixer", "name": name,
                      "control": {"Gain 1": on_gain if eq_on else 0.0, "Gain 2": 0.0 if eq_on else off_gain}})
        links.append({"output": eq_src, "input": f"{name}:In 1"})
        links.append({"output": bypass_src, "input": f"{name}:In 2"})
        toggles.append([f"{name}:Gain 1", on_gain, 0.0])
        toggles.append([f"{name}:Gain 2", 0.0, off_gain])

    for k, side in ((1, "l"), (2, "r")):
        c = side.upper()
        links.append({"output": f"in_{side}:Out", "input": f"hp:In {k}"})
        links.append({"output": f"hp:Out {k}", "input": f"vol:In {c}"})
        links.append({"output": f"vol:Out {c}", "input": f"eq3:In {k}"})
        links.append({"output": f"eq3:Out {k}", "input": f"eq5:In {k}"})
        mixer(f"premix_{side}", f"eq5:Out {k}", f"vol:Out {c}", round(10 ** (makeup_db() / 20), 6),
              round(10 ** (EQ_OFF_TRIM_DB / 20), 6))
        nodes.append({"type": "builtin", "label": "bq_lowshelf", "name": f"bassb_{side}",
                      "control": {"Freq": BASS_BOOST_FREQ, "Q": BASS_BOOST_Q, "Gain": bass_boost_db()}})
        links.append({"output": f"premix_{side}:Out", "input": f"bassb_{side}:In"})
        for x in ("xtw", "xwf"):
            links.append({"output": f"bassb_{side}:Out", "input": f"{x}:In {k}"})
        links.append({"output": f"xtw:Out {k}", "input": f"tweq:In {k}"})
        links.append({"output": f"xwf:Out {k}", "input": f"wfeq:In {k}"})
        # the per-speaker filters stay in both modes (they protect the drivers)
        mixer(f"twmix_{side}", f"tweq:Out {k}", f"tweq:Out {k}")
        mixer(f"wfmix_{side}", f"wfeq:Out {k}", f"wfeq:Out {k}")
        links.append({"output": f"twmix_{side}:Out", "input": f"twap:In {k}"})
        links.append({"output": f"twap:Out {k}", "input": f"treble:In {c}"})
        links.append({"output": f"twap:Out {k}", "input": f"tdet:In {k}"})
        links.append({"output": f"tdet:Out {k}", "input": f"treble:Side {c}"})
        links.append({"output": f"treble:Out {c}", "input": f"guard:In {k - 1}"})
        links.append({"output": f"wfmix_{side}:Out", "input": f"bass:In {c}"})
        links.append({"output": f"wfmix_{side}:Out", "input": f"wdet:In {k}"})
        links.append({"output": f"wdet:Out {k}", "input": f"bass:Side {c}"})
        links.append({"output": f"bass:Out {c}", "input": f"guard:In {k + 1}"})

    t, w = th["tweeter"], th["woofer"]
    nodes.append({"type": "ladspa", "name": "guard", "plugin": GUARD, "label": "mbp_speakerguard",
                  "control": {
                      "A Re": t["re"], "A Rth coil": t["rth_c"], "A Tau coil": t["tau_c"],
                      "A Rth magnet": t["rth_m"], "A Tau magnet": t["tau_m"], "A Tlimit": t["t_lim"],
                      "B Re": w["re"], "B Rth coil": w["rth_c"], "B Tau coil": w["tau_c"],
                      "B Rth magnet": w["rth_m"], "B Tau magnet": w["tau_m"], "B Tlimit": w["t_lim"],
                      "Vfs": vfs(th), "Tambient": max(t["t_amb"], w["t_amb"]),
                      "Headroom": max(t["headroom"], w["headroom"]), "Ceiling": guard_ceiling()}})

    graph = {
        "node.description": "MacBook Speakers",
        "media.name": "MacBook Speakers",
        "filter.graph": {
            "nodes": nodes,
            "links": links,
            "inputs": ["in_l:In", "in_r:In"],
            "outputs": ["guard:Out 0", "guard:Out 1", "guard:Out 2", "guard:Out 3"],
            # the sink volume drives these controls instead of scaling the input;
            # "linear": the control gets PipeWire's linear volume
            "capture.volumes": [
                {"control": "vol:Volume L", "min": 0.0, "max": 1.0, "scale": "linear"},
                {"control": "vol:Volume R", "min": 0.0, "max": 1.0, "scale": "linear"},
            ],
        },
        "capture.props": {
            "node.name": "mbp_speakers",
            "media.class": "Audio/Sink",
            "audio.channels": 2,
            "audio.position": ["FL", "FR"],
            "device.icon-name": "audio-speakers",
            "priority.session": 1010,
            # the amps run at 44.1 kHz only: keep the whole graph there, so
            # nothing resamples after the limiter
            "node.force-rate": SPEAKER_RATE,
        },
        "playback.props": {
            "node.name": "mbp_speakers.out",
            "node.passive": True,
            "target.object": "alsa_output.pci-0000_00_1f.3.speakers",
            "node.dont-fallback": True,
            "node.dont-reconnect": True,
            "stream.dont-remix": True,
            "node.force-rate": SPEAKER_RATE,
            "audio.channels": 4,
            "audio.position": hw_map,
        },
    }
    return graph, {"eq": eq_on, "toggles": toggles}


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    d = json.load(open(sys.argv[1]))
    hw_map = sys.argv[2].split(",")
    if len(hw_map) != 4 or len(set(hw_map)) != 4:
        sys.exit("map: four distinct positions, e.g. FL,FR,RL,RR")
    graph, meta = build(d, hw_map, sys.argv[3] == "on")
    json.dump(graph, sys.stdout, indent=1)
    print()
    if len(sys.argv) > 4:
        json.dump(meta, open(sys.argv[4], "w"), indent=1)


if __name__ == "__main__":
    main()
