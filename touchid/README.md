# Touch ID (T1) for sudo and the KDE lock screen

Uses [t1bridge](https://github.com/standardagents/t1bridge)'s Touch ID
services: the fingerprint is captured and matched inside the T1's Secure
Enclave; Linux only gets "match / no match". Fingerprints enrolled in macOS
are not used; you enrol new ones in Linux (up to 3). macOS keeps its own.

Needs T1 configuration 2 (`install.sh t1stack`) and the T1 network link
(`install.sh t1ncm`). With a firewall, allow TCP 61500 **in on `t1ncm0` only**
(the Secure Enclave's storage service on the host).

    touchid/build.sh                   # as your user; or --container
    sudo ./install.sh touchid
    sudo ./touchid/import-fdrdata.sh   # sensor calibration of THIS Mac, read only
    fprintd-enroll                     # in the desktop session (not over SSH)

`import-fdrdata.sh` mounts the Apple EFI partition read-only, reads
`EFI/APPLE/EMBEDDEDOS/FDRData` and hands it to `t1bridge machine-data import
--from`, which keeps it in root-only `/var/lib/t1bridge`. Nothing is written
to the partition or to the T1. `FDRData` and `/var/lib/t1bridge` are private
to your machine: never share them.

PAM (by hand, keep the password as fallback): first line of `/etc/pam.d/sudo`

    auth sufficient pam_fprintd.so max-tries=3 timeout=10

The KDE lock screen uses the stock `/usr/lib/pam.d/kde-fingerprint` as soon as
a finger is enrolled. SDDM and polkit stay on the password.

`mbp-touchid.sleep` restarts t1bridge's keybag relay right after a resume (it
fails on every resume; without the hook fingerprint checks were refused for up
to a minute). Known limitation: going to sleep within ~3 s of a wake while the
lock screen checks a finger leaves only the password until the next lock.

Our patch `0001` makes t1bridge refuse `machine-data recover --online`
(contacts Apple and resets the T1), `validate usb-cycle` / `usb-live-loss`
(reconfigure the T1 on USB) and `machine-data import` without `--from`
(discovers and mounts EFI partitions itself).

Undo: `sudo ./install.sh touchid-remove`, take `pam_fprintd` out of the PAM
files you changed; delete `/var/lib/t1bridge` to forget everything.
