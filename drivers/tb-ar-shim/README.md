# tb_ar_shim: Thunderbolt / USB-C (USB 3) through sleep

`tb_ar_shim.c` is taken unchanged from
[olofd/omarchy-macbookpro-a1706](https://github.com/olofd/omarchy-macbookpro-a1706)
`alpine-ridge-sleep/` at commit `c9c74dd` (module version 1.0.0-a1706.5;
based on Andrew Clunis' module, orospakr/linux-apple-alpine-ridge-suspend;
GPL-2.0). Only the packaging (dkms.conf, install.sh) is ours.

Problem on this Mac (2026-10-18, journal): at the first S3 sleep pciehp
removes both Alpine Ridge xHCIs (06:00.0, 7c:00.0: the USB 3 side of the
USB-C ports); after the wake the Thunderbolt bridges do not answer
(`device inaccessible`, `tb_cfg_read -108`) until a reboot. USB 2.0 on the
same ports goes through the PCH xHCI and keeps working.

What the module does (read before installing): a PM domain on the two
upstream bridges 8086:1578 (03:00.0, 79:00.0), only where Apple's ACPI
`DSB0.NHI0.SXFP` exists under them:
- suspend_late: save the PCI config of the bridge and its subtree;
- suspend_noirq (S3 only): `NHI0.SXFP(0)`, which in our SSDT5 only drives
  the chip's power GPIOs low (0x02060001/0x02060000, 0x02060003/0x02060002);
- resume_noirq: the root port's `UGIO` (releases those GPIOs), `_PS0` of the
  bridge if it does not answer within 1 s, then restore the config; a chip
  that still does not answer is marked gone at once (no minute-long waits).
No T1/ASOC (FDFU, DFU1, DFU2, FRST, SOCW), no EC/SMC access anywhere in the
module or in the methods it calls.

Install: `sudo ./install.sh thunderbolt` (DKMS for every installed kernel,
loads it now: the two bridges are re-bound, so nothing should be plugged
into USB-C but the charger). Check: `journalctl -k -b | grep tb_ar_shim`.
Undo: `sudo rmmod tb_ar_shim` (at once), `sudo ./install.sh thunderbolt-remove`
(DKMS package gone).
