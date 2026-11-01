# appletb-t1: in-kernel Touch Bar drivers with T1 (MacBookPro13,2) support

Out-of-tree build of `hid-appletb-kbd` and `hid-appletb-bl` from Linux 7.2.9;
`hid-appletb-kbd` is patched for the T1 iBridge (`05ac:8600`). `hid-appletb-bl`
is the unmodified upstream file (T2 only), built so both come from one package.
`apple-ibridge-pm` (new) binds the T1's ACPI node `APP7777` and tells the T1
the host is awake (`SOCW(1)`) at probe and after every system resume.

## T1 differences handled (all verified on a MacBookPro13,2, 2026-10-15)
- Touch Bar mode (usage 0x00ff0004, output report 2 of the keyboard interface)
  is accepted only as a **vendor** request to the device, not a class SET_REPORT.
- Keyboard/trackpad are SPI (`applespi`), so the Fn/activity input handler also
  matches `BUS_SPI` "Apple SPI …" devices.
- The T1 really autosuspends: PM callbacks must not call `hid_hw_power()`
  (deadlock), and autosuspend must not switch the bar off (no remote wakeup).
- Display brightness is feature report 2 on the *other* HID interface (the ALS
  one). `hid-appletb-kbd` registers `appletb_backlight` and sends it with a
  USB control request, leaving the interface (and the ALS) to
  `hid-sensor-hub` + `hid-sensor-als`. Values: 1 on / 2 dim / 4 off.
- Sleep: without the ACPI `ASOC.SOCW(1)` wake notification the T1 often drops
  off USB after a sleep longer than ~40 s and re-enumerates with the Touch Bar
  dead until reboot. `SOCW(0)` before sleep (`suspend_notify=1`) wakes the
  machine up immediately, so it is off by default.

## Verified on hardware
modes 0–3 via sysfs, Fn toggle, brightness on/dim/off, ALS (`iio:device0`)
still working, no hang after USB autosuspend, mode/brightness restored after S3,
Touch Bar alive after lid-closed sleeps up to 32 min with `apple-ibridge-pm`.

## Install (Arch)
    sudo cp -r . /usr/src/appletb-t1-0.2
    sudo dkms install appletb-t1/0.2 -k <kernel>     # for each installed kernel
