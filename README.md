# MacBook Pro 13" 2016 with Touch Bar (MacBookPro13,2, A1706) on Arch Linux

Drivers, fixes and a Touch Bar stack for the 2016 13-inch MacBook Pro with
Touch Bar and the Apple T1 chip, for Arch Linux with KDE Plasma 6.
[Українською](README.uk.md).

> **Status: no longer maintained on hardware.** The author sold this Mac and
> cannot test anything any more. Everything marked as tested was tested on
> one MacBookPro13,2 in October 2026, with linux 7.2.11 and linux-lts 6.18.57
> (the sources were build-checked later on 7.2.11 and 6.18.60). Pull requests
> and reports from other machines are welcome, especially from MacBookPro13,3,
> 14,2 and 14,3 (the other T1 models).

## Models

| Model | Status |
|---|---|
| MacBookPro13,2 (13", 2016, four Thunderbolt 3 ports, Touch Bar, A1706) | tested |
| MacBookPro13,3, 14,2, 14,3 (other T1 Macs) | **not tested**; drivers may work, model-specific settings are refused |
| T2 Macs (2018+), Apple silicon | not for these: see [t2linux](https://wiki.t2linux.org/) and [Asahi Linux](https://asahilinux.org/) |

`install.sh` reads the model from DMI. The speaker protection and EQ, the
Wi-Fi NVRAM recipe and the power quirks are installed **only on a
MacBookPro13,2**: on any other model it installs just the driver and warns,
because speaker parameters of another Mac can damage the speakers. Modules for
the Touch Bar, the camera and Touch ID first check that the T1 is on USB and
not in Recovery Mode.

## What works (on the tested MacBookPro13,2)

| Part | State | Module(s) |
|---|---|---|
| Speakers | four speakers with crossover, macOS's static EQ, a volume law measured in macOS, thermal model and limiter; quieter than macOS, never louder (see [audio](audio/README.md)) | `sound`, `audio` |
| Headphones, microphone | headset jack, internal microphone at sane levels, both after sleep | `sound` |
| Touch Bar, configuration 1 | the T1 draws Esc, F1-F12 and media keys itself; F-keys per program, brightness, dimming | `appletb`, `touchbar` |
| Touch Bar, configuration 2 | our own stack: the T1 as a display (`appletbdrm`) and touch surface, [tiny-dfr](https://github.com/AsahiLinux/tiny-dfr) with patches (Control Strip, sliders, key sets per program, now playing, ...), a fallback renderer if tiny-dfr fails, a boot entry back to configuration 1 | `t1display`, `tinydfr`, `t1stack` |
| Camera | configuration 1: stock `uvcvideo` (MJPEG 720p); configuration 2: H.264 through a virtual camera (`v4l2loopback`, VA-API decode), 30 fps in browsers | `uvcvideo`, `camera` |
| Touch ID | sudo and the KDE lock screen by fingerprint (password stays as fallback), through [t1bridge](https://github.com/standardagents/t1bridge) | `t1ncm`, `touchid` |
| Sleep (S3) | deep sleep on battery and on the charger; Touch Bar, sound, USB-C and Wi-Fi after resume | `base`, `appletb`, `thunderbolt`, `wifi`, `lidwake` |
| Wi-Fi (BCM43602) | 2.4 and 5 GHz at macOS-like signal levels, with a board NVRAM generated from the chip's own SROM | `wifi`, `base` |
| Colour profile | the panel's own ICC profile from your macOS (nothing shipped): [docs/color-profile.md](docs/color-profile.md) | — |
| Keyboard, trackpad, keyboard light, Bluetooth, ambient light sensor, screen brightness | stock kernel | — |

## Requirements

- Arch Linux, kernels `linux` and `linux-lts` with headers, DKMS:
  `sudo pacman -S --needed base-devel git dkms linux-headers linux-lts-headers wget patch`
- KDE Plasma 6 (the Touch Bar pieces use KWin and KDE services; the drivers do not).
- **Keep macOS installed** (a small APFS partition is enough). The T1 loads
  its firmware from the Apple EFI partition that macOS maintains; without it
  the T1 stays in Recovery Mode (`lsusb`: `05ac:1281`) and there is no Touch
  Bar, camera or Touch ID. macOS also holds the data some modules read from
  your machine: the speaker layout and the Touch ID sensor calibration.
  Nothing here writes T1, EFI or SMC firmware.
- Boot Linux from the Mac's boot picker (Option key), e.g. with systemd-boot;
  leave macOS as the default in NVRAM.

## Install

Everything is a module of [`install.sh`](install.sh); install only what you
need, in this order. Every module has `<module>-remove`.

    ./install.sh check                    # model, T1 state, kernels
    ./install.sh --help
    ./install.sh --dry-run sound audio    # what would change, nothing changes
    sudo ./install.sh base sound          # then reboot
    sudo ./install.sh audio               # + the speaker data: audio/README.md
    sudo ./install.sh wifi && sudo mbp-wifi nvram
    sudo ./install.sh thunderbolt lidwake pacman
    sudo ./install.sh appletb touchbar    # Touch Bar in configuration 1

Own Touch Bar stack (configuration 2) and Touch ID:

    sudo pacman -S --needed v4l2loopback-dkms
    sudo ./install.sh t1display uvcvideo t1ncm
    tiny-dfr/build.sh                     # as your user (or --container)
    sudo ./install.sh tinydfr camera t1stack     # then reboot
    touchid/build.sh                      # as your user (or --container)
    sudo ./install.sh touchid             # then touchid/README.md

| Module | What | Undo |
|---|---|---|
| `sound` | CS8409 driver (DKMS); fetches `sound/hda` of your kernel version from kernel.org once | `sound-remove` |
| `audio` | speaker graph (PipeWire filter-chain, WirePlumber rules), `mbp-audio` | `audio-remove` |
| `appletb` | Touch Bar driver for configuration 1, T1 sleep notifications | `appletb-remove` |
| `spi-legacy` | optional alternative to `appletb` (macbook12-spi-driver, with the ambient light sensor driver); never both | `spi-legacy-remove` |
| `t1display`, `uvcvideo`, `t1ncm` | drivers for configuration 2: display and touch, H.264 camera, T1 network link | `…-remove` |
| `thunderbolt` | USB-C/Thunderbolt through sleep (`tb_ar_shim`) | `thunderbolt-remove` |
| `base` | NVMe/Thunderbolt D3cold off (fast resume), brcmfmac and T1 options | `base-remove` |
| `wifi` | `mbp-wifi` (NVRAM from SROM), Wi-Fi sleep hook | `wifi-remove` (removes the NVRAM too) |
| `lidwake` | a sleep started with the lid open does not end at once | `lidwake-remove` |
| `touchbar` | Touch Bar timeouts, F-keys per program (KWin script) | `touchbar-remove` |
| `tinydfr`, `camera`, `t1stack` | the configuration-2 stack | `…-remove` |
| `touchid` | Touch ID services, t1bridge's libfprint/fprintd builds | `touchid-remove` |
| `pacman` | test-build the DKMS modules before every kernel update | `pacman-remove` |

Optional: `tiny-dfr/kde-shortcuts.sh apply` (as your user) binds the Touch
Bar's Mission Control / Launchpad keys to the KWin Overview and the
application launcher; `restore` undoes it.
[`tiny-dfr/examples/config-author.toml`](tiny-dfr/examples/config-author.toml)
is the author's full Touch Bar configuration, as an example.

## Known limitations

- **Untested in this form.** The repository was cleaned up for publishing
  after the author no longer had the Mac. The packaging, `install.sh`, the
  audio graph without macOS's dynamic stages and the default Touch Bar
  configuration were checked by building in a clean Arch container and
  offline, not on hardware.
- **Sound:** the macOS-like dynamic processing (leveler, loudness, two-band
  compressor, BuzzKill) is not included. After removing the leveler and
  BuzzKill a static **+11 dB** make-up gain was added; quiet passages are
  noticeably quieter than in macOS. **This was not tested on hardware.** The
  protection (limiter at -3 dBFS, thermal model) runs, but listen carefully:
  **start at a low volume**. The `audio` module is never installed
  implicitly: only `sudo ./install.sh audio`, and only on a MacBookPro13,2.
- **Touch Bar configuration 2:** switching the T1 to configuration 2 stops
  its built-in renderer until Linux reboots; going back to configuration 1
  without a reboot shows no keys (they still work). That is why the stack
  decides once at boot and keeps a fallback renderer.
- **Touch Bar "like macOS"** (tiny-dfr patches 0004-0021: look, Control
  Strip, sliders, animations, panels, emoji, tabs, settings window):
  **work in progress and experimental**, animations especially; no tests.
- **Touch Bar brightness** has three levels on the T1.
- The Touch Bar settings window (`mbp-touchbar-settings`) speaks Ukrainian
  only; translations welcome.
- **Touch ID:** sudo and the lock screen only (SDDM and polkit ask for the
  password). Sleeping within ~3 s of a wake while the lock screen checks a
  finger leaves only the password until the next lock.
- **Sleep on the charger:** rare self-wakes remain (about 1 in 5 sleeps; the
  Mac goes back to sleep by itself).
- Not handled here: no hardware VP9/AV1 decoding (Skylake); the keys next to
  1 and Z are swapped on ISO keyboards.

## Safety

- No code writes firmware of the T1, EFI or SMC. Of the T1's ACPI methods
  only `SOCW` (sleep/wake notification) is called, never the DFU or reset
  methods. t1bridge's recovery, USB-cycle and automatic EFI import commands
  are refused by our patch (`touchid/0001-…`).
- The Wi-Fi SROM is only read (the READ opcode, asserted in `read_srom.py`);
  the Apple EFI partition is mounted read-only to read `FDRData`.
- The speaker protection always runs: if its data is missing the speakers
  stay silent rather than unprotected.

## Licenses

Our own code: see [LICENSE](LICENSE). Third-party code keeps its license:
GPL-2.0 for the kernel modules and the forked drivers, MIT for tiny-dfr and
the T1Bridge code (MIT or GPL-2.0-only for its kernel parts), ISC for the
Lucide icons, Apache-2.0 for tiny-dfr's Material icons. Upstream commits and
authors: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Built on the work of davidjo/snd_hda_macbookpro,
roadrunner2/macbook12-spi-driver, AsahiLinux/tiny-dfr,
standardagents/t1bridge, olofd/omarchy-macbookpro-a1706,
orospakr/linux-apple-alpine-ridge-suspend, Dunedan/mbp-2016-linux and the
Linux kernel's Apple Touch Bar drivers. Thank you.
