# mbp-t1-display: Touch Bar display and touch for the T1 (MacBookPro13,2)

`appletbdrm` from the kernel's own tree (7.2.9 for 7.x, 6.18.56 for 6.x) with
the T1 series of [T1Bridge](https://github.com/standardagents/t1bridge)
(`kernel/appletbdrm-t1/upstream/0001-0003`, v0.1.12, commit `81cbdf8`,
by Andrew Boyd and the T1Bridge contributors) applied unchanged, plus one
change of ours: after probe and after every resume the panel is set awake
(HID feature report 3 = `03 02 f4 01` on interface 6), undoing the park the
driver sends at suspend (T1Bridge issues #47 and #49 found this).
GPL-2.0-only like the upstream driver.

`hid-t1-touchbar` (ours, stage 4): the digitizer on HID interface 2 as a
multitouch input device "Apple Inc. iBridge Touch Bar" (slot layout after
T1Bridge's `digitizer.rs`, MIT), and `appletb_backlight` = panel power
(0 parked; 1..84 / 85..169 / 170..255 the T1's brightness levels 3 / 2 / 1,
about 10 % / 15 % / full, report 2; tiny-dfr's dimmed value 1 dims). It checks the parsed descriptor (Digitizer /
Touch Pad application on interface 2) before any I/O, so in configuration 1
`hid-appletb-kbd` and `hid-sensor-hub` keep their interfaces.

Both only bind while the T1 is in USB configuration 2 (display interface 3);
in configuration 1, our default, nothing changes.

    sudo cp -r . /usr/src/mbp-t1-display-0.4
    sudo dkms install mbp-t1-display/0.4 -k <kernel>     # each kernel
    sudo install -m644 90-mbp-t1-display.rules /etc/udev/rules.d/
    make -f /dev/null t1-testpattern LDLIBS="$(pkg-config --libs libdrm)" CFLAGS="$(pkg-config --cflags libdrm)"

Undo: `sudo dkms remove mbp-t1-display/0.4 --all`, remove the rule.

`t1-fpsbench [seconds [px ...]]` times frames of the given widths (stop
tiny-dfr first). On 7.2.11: full bar 98 fps (10 ms), 900 px 196, 400 px 322.
Before 0.4, a request of a multiple of 512 bytes (68 of the 2170 widths,
e.g. 130 px) got no zero-length packet: the T1 waited, every frame after it
timed out (1 s each) until the transfer happened to end short.
