# Third-party code and data

This repository combines our own code with code from other projects. Each
part keeps its own license; the texts are in [`LICENSES/`](LICENSES/) and next to
the code. Upstream history is not included here: each entry names the
upstream commit the code was taken from.

**Nothing from Apple is included**: no firmware, no EFI data, no DSDT/ACPI
tables, no colour profiles, no macOS screenshots, no numbers from macOS
files. What the scripts need from Apple (the speaker layout data, the Touch
ID sensor calibration `FDRData`, the Wi-Fi chip's SROM) they read, read-only,
from the user's own machine at install time, and keep there.

## Kernel modules (`drivers/`)

| Directory | Origin | Upstream base | License |
|---|---|---|---|
| `snd_hda_macbookpro/` | [davidjo/snd_hda_macbookpro](https://github.com/davidjo/snd_hda_macbookpro) (CS8409 driver for Apple Macs, based on the Linux `snd-hda-codec-cs8409`) | `89b22ff90b86468b186706861dd18663562defa7` (2026-09-06), plus our changes: DKMS builds for `$kernelver`, cached `sound/hda` tree, re-setup after S3, SSM3515 DAC volume parameter, jack state after resume, internal microphone levels | GPL-2.0 (`LICENSE`) |
| | internal-mic patches `0001-internal-mic-gain-from-mixer`, `0002-internal-mic-settle-before-unmute` by Olof Dahlbom, [olofd/omarchy-macbookpro-a1706](https://github.com/olofd/omarchy-macbookpro-a1706) `snd-hda-macbookpro/` | `c9c74dd576e6c330dd578eb5da86def7065c48cc`, folded into `patch_cirrus/` | MIT ([`LICENSES/MIT-olofd.txt`](LICENSES/MIT-olofd.txt)), combined under GPL-2.0 |
| `appletb-t1/` | `hid-appletb-kbd`, `hid-appletb-bl` from Linux 7.2.9 (Ronald Tschalär, Kerem Karabay, Aditya Garg), `hid-appletb-kbd` patched for the T1; `apple-ibridge-pm` ours | Linux v7.2.9 | GPL-2.0 |
| `mbp-t1-display/` | `appletbdrm` from Linux 7.2.9 / 6.18.56 (Kerem Karabay) with the T1 series of [standardagents/t1bridge](https://github.com/standardagents/t1bridge) `kernel/appletbdrm-t1/upstream/0001-0003` (Andrew Boyd, T1Bridge contributors); resume fix, ZLP fix and `hid-t1-touchbar` ours (digitizer layout after T1Bridge `digitizer.rs`) | Linux v7.2.9, v6.18.56; t1bridge `81cbdf81026a16e02f0bea74735c6b029a8ffae2` (v0.1.12) | GPL-2.0 (T1Bridge parts: MIT or GPL-2.0-only) |
| `uvcvideo-t1/` | each kernel's own `drivers/media/usb/uvc`, downloaded from git.kernel.org at build time (not stored here); patch `0001` by Andrew Boyd (t1bridge `kernel/uvcvideo-t1/upstream`), rebased; patch `0002` ours | t1bridge `81cbdf8` | GPL-2.0-or-later (uvcvideo); patch 0001 MIT or GPL-2.0-only |
| `apple-t1-ncm/` | `apple_t1_ncm.c` and the `.link` file from t1bridge `kernel/apple-t1-ncm/` (T1Bridge contributors), unchanged except the interface name; DKMS packaging ours | t1bridge `81cbdf8` | MIT or GPL-2.0-only (`LICENSE`) |
| `tb-ar-shim/` | `tb_ar_shim.c` from olofd/omarchy-macbookpro-a1706 `alpine-ridge-sleep/` (Olof Dahlbom), based on Andrew Clunis' [orospakr/linux-apple-alpine-ridge-suspend](https://github.com/orospakr/linux-apple-alpine-ridge-suspend); unchanged | olofd `c9c74dd` | GPL-2.0 |
| `macbook12-spi-driver/` (optional) | [roadrunner2/macbook12-spi-driver](https://github.com/roadrunner2/macbook12-spi-driver) by Ronald Tschalär and contributors, via the `touchbar-driver-hid-driver` branch (fork LastExile16); our changes: `applespi` dropped (in the kernel now), ports to current kernel APIs | `ddfbc7733542b8474a0e8f593aba91e06542be4f` (2019-08-26) | GPL-2.0 (`LICENSE`) |

## Packages built from upstream sources (`tiny-dfr/`, `touchid/`)

The PKGBUILDs download the upstream source at a fixed commit and apply the
patches kept here. Each patch has a `From:` line (its author) and says what
it changes.

| Package | Upstream | Commit | License |
|---|---|---|---|
| `tiny-dfr` | [AsahiLinux/tiny-dfr](https://github.com/AsahiLinux/tiny-dfr) (WhatAmISupposedToPutHere and contributors) | `285faff8b8cae893e9babd673b904929dd6f7f9c` | MIT ([`LICENSES/MIT-tiny-dfr.txt`](LICENSES/MIT-tiny-dfr.txt)); its Material icons Apache-2.0 ([`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt)). Our patches 0001-0021: MIT, as upstream |
| `t1bridge-touchid` | [standardagents/t1bridge](https://github.com/standardagents/t1bridge) (T1Bridge contributors): only the Touch ID services | `81cbdf81026a16e02f0bea74735c6b029a8ffae2` (v0.1.12) | MIT ([`LICENSES/MIT-T1Bridge.txt`](LICENSES/MIT-T1Bridge.txt), `touchid/T1Bridge-LICENSE`). Our patch 0001: MIT |
| `libfprint-t1bridge`, `fprintd-t1bridge` | t1bridge's own PKGBUILDs and patches, unchanged (`packaging/arch/`), building [libfprint](https://gitlab.freedesktop.org/libfprint/libfprint) and [fprintd](https://gitlab.freedesktop.org/libfprint/fprintd) | t1bridge `81cbdf8` | libfprint LGPL-2.1-or-later, fprintd GPL-2.0-or-later; packaging MIT |

## Data and artwork

| What | Where | Source | License |
|---|---|---|---|
| Lucide icons (49 SVG) | `tiny-dfr/icons/lucide/` | [lucide-static](https://lucide.dev) 0.460.0 | ISC ([`LICENSES/ISC-Lucide.txt`](LICENSES/ISC-Lucide.txt)) |
| Emoji list `share/tiny-dfr/emoji.txt` | created by patch 0019 | generated from Unicode `emoji-test.txt` 17.0 (`tiny-dfr/tools/emoji-list.py`) | [Unicode License v3](https://www.unicode.org/license.txt) |
| BCM43602 SROM variable tables | `wifi/srom_tables.py` | generated by `wifi/gen_srom_table.py` from Broadcom's `bcmsrom_tbl.h` / `bcmsrom_fmt.h` (bcmdhd, e.g. Fuchsia `third_party/bcmdhd`) | GPL-2.0 (Broadcom's dual-licensed bcmdhd, GPL option) |

Fonts are not shipped, only depended on: Inter (`inter-font`, SIL OFL 1.1),
Noto Color Emoji (`noto-fonts-emoji`, SIL OFL 1.1), Noto Sans.

## Trademarks

Apple, MacBook, macOS, Touch Bar, Touch ID and FaceTime are trademarks of
Apple Inc. This project is not affiliated with or endorsed by Apple.
