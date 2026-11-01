# Internal speakers: protection, crossover, EQ (MacBookPro13,2 only)

The four speakers (2 tweeters, 2 woofers, 4x SSM3515 behind the CS8409)
are reachable only through the PipeWire sink "MacBook Speakers", a
filter-chain graph that always protects them.

**MacBookPro13,2 only.** `install.sh audio` and `mbp-audio gen` refuse any
other model: the protection parameters (crossover, ceilings, thermal model)
belong to these speakers and amps, and wrong ones can damage others.

> **Listen carefully.** After removing the macOS leveler and BuzzKill a
> static +11 dB make-up gain was added. This graph was **not tested on
> hardware**. The protection (limiter at -3 dBFS, thermal model) runs, but
> start at a low volume and raise it step by step. The module is installed
> only explicitly (`sudo ./install.sh audio`) and only on a MacBookPro13,2.

## Install

    sudo ./install.sh sound          # the codec driver (DKMS), then reboot
    sudo ./install.sh audio
    sudo /usr/local/lib/mbp-audio/get-layout.sh <layout54.xml.zlib>   # see below
    systemctl --user restart wireplumber pipewire pipewire-pulse

Until the speaker data is there the graph is not written and the speakers
stay **silent**, never unprotected.

### Speaker data from your own macOS

The static EQ, the crossover and the thermal parameters are Apple's numbers
from macOS (AppleHDA layout 54). They are not shipped here: `get-layout.sh`
reads them from **this Mac's** macOS, read only, and keeps the result in
`/usr/local/lib/mbp-audio/layout54.json`.

- Simplest: in macOS copy the file to a USB stick,
  `cp /System/Library/Extensions/AppleHDA.kext/Contents/Resources/layout54.xml.zlib /Volumes/<stick>/`,
  then in Linux `sudo /usr/local/lib/mbp-audio/get-layout.sh /path/to/layout54.xml.zlib`.
- Or `sudo /usr/local/lib/mbp-audio/get-layout.sh --mount`: finds the APFS
  partition and reads it with `apfs-fuse` (read-only driver, AUR
  `apfs-fuse-git`). Not tested on hardware (the author no longer has the Mac).

Tested with macOS 12.7.6 (Monterey).

## Use

    mbp-audio status                 # nodes, EQ, guard temperatures, gain reduction
    mbp-audio eq on|off              # static EQ + make-up gain, live
    mbp-audio bass 0|2|3             # bass boost under 250 Hz (dB), live
    mbp-audio dac                    # amp DAC volume, thermal model voltage
    sudo mbp-audio dac 0             # amp DAC volume in dB (-3 = driver default)

Undo: `sudo ./install.sh audio-remove` (then the speakers get the plain stereo
signal again: keep the volume low, or remove `sound` too).

## The graph

    in -> high-pass (macOS EQ0) -> volume (macOS volume law)
       -> EQ3 -> EQ5 -> make-up gain                      [EQ layer]
       -> bass boost (optional)
       -> crossover 2.6 kHz -> per-speaker filters (macOS EQ8 / EQ9)
       -> tweeters: tweeter ceiling ; woofers: bass ceiling
       -> mbp_speakerguard: thermal model + look-ahead limiter (-3 dBFS) -> amps

- `acp/` - ALSA card profile set: `speakers` (hw:0, 4 channels, one per
  amp; unavailable with headphones plugged) and `headphones` (front:0,
  stereo). WirePlumber switches between them on the jack.
- `wireplumber/` - selects the profile set, loads the graph for the speakers
  node and hides the raw 4-channel node from all clients (class
  `Audio/Sink/Internal`, volume locked at 1.0): nothing can play to the amps
  past the protection.
- `mkgraph.py` - builds the graph from `layout54.json` and `speaker-map`
  (channel order, checked on the hardware).
- `extract_layout54.py` - reads the layout XML: static filters and thermal
  parameters only.
- `speakerguard/` - our LADSPA plugins (`make test` runs offline checks):
  `mbp_speakerguard` (thermal model + limiter, the last stage),
  `mbp_basslimit` / `mbp_treblelimit` (side-chain ceilings), `mbp_volume`
  (volume law), `mbp_vbass` (psychoacoustic bass, not used by the graph).

## Rules the graph keeps

- Always: high-pass, crossover, per-speaker filters, ceilings, guard.
- Ceilings: never more than +3 dB (bass) / +3 dB (tweeters) over what the
  static macOS EQ gives a 0 dBFS sine, at every frequency, EQ on or off.
- The graph runs at 44.1 kHz, the only rate of the amps (`node.force-rate`;
  `pipewire/` allows the rate): nothing resamples after the limiter.
- No IV sense on the SSM3515: the thermal model is an estimate from the
  signal (as in macOS), with the macOS parameters, the voltage scaled with
  the DAC volume.

## What is not included

**The macOS-like dynamic processing (leveler, loudness, two-band compressor,
BuzzKill) is not included.** Without it a static make-up gain stands in:
quiet passages are quieter than in macOS, loud ones are held by the ceilings
and the limiter. Before release the graph was checked offline against a
digital model of the macOS chain (pink noise, stepped sines, music at
30/50/70/100 %, EQ on and off): in every third octave it stays at least 2 dB
under macOS. The exception is the optional bass boost (`mbp-audio bass 2|3`,
off by default): it is meant to be louder than macOS in the bass, by up to
its gain (the ceilings, the thermal model and the limiter still bound it).
It was **not** listened to on the hardware in this form.
