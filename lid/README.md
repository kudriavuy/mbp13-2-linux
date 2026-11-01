# Lid wake (`mbp-lid-wake.sleep`)

A sleep begun with the lid **open** (`systemctl suspend`, the KDE menu)
ended about 3 s later, in T1 configuration 1 and 2 alike: the lid wake
(`LID0`, `_PRW` S4; `_L6F` reads the lid GPIO, `PCRR 0xAD`) fires on the
"open" level, not on the opening. Signature in the GPE counters: +1
`gpe6F`, EC `0x06` 00→19, as when the lid is opened. Tested 2026-10-21/22
(7.2.11, battery, hands off): SPI keyboard/trackpad wake off — still woke;
`LID0` wake off — slept, woken by the power button and by the space bar.

The hook turns the lid wake off for a sleep begun with the lid open and back
on after; a sleep begun with the lid closed is not touched (opening wakes).
In a sleep begun open, closing and reopening the lid does not wake the Mac:
the power button or a key does.

    sudo ./install.sh lidwake          install
    sudo ./install.sh lidwake-remove   remove (and turn LID0 wake back on)

Journal tag: `mbp-lid-wake`. Not explained yet: on 2026-10-19 a sleep begun
open from the KDE menu held 1 h 53 min.
