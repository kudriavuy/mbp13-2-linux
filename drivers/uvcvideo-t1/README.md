# uvcvideo-t1: the T1 camera in USB configuration 2 (MacBookPro13,2)

In configuration 2 (the one macOS uses, needed for our own Touch Bar stack)
the T1 FaceTime camera offers only H.264, which the stock `uvcvideo` does not
parse ("No streaming interface found"). This DKMS module is **each kernel's own**
`drivers/media/usb/uvc` with two patches:

- `0001` — parse UVC 1.5 H.264 format/frame descriptors (Andrew Boyd,
  [t1bridge](https://github.com/standardagents/t1bridge) `kernel/uvcvideo-t1/upstream`),
  rebased: only the context of the first `uvc_driver.c` hunk changed.
- `0002` — no USB autosuspend from the camera probe for 05ac:8600 **in
  configuration 2 only**; configuration 1 keeps the upstream behaviour
  (t1bridge's own 0002 sets it for every configuration).

`prepare.sh` (DKMS `PRE_BUILD`) fetches the sources for the exact kernel
(`7.2.11-arch1-1` → tag `v7.2.11`) over HTTPS from git.kernel.org and caches them
in `/var/cache/mbp-uvcvideo-t1/`. If the download, a patch or the build fails,
DKMS installs nothing and the stock module stays; the pacman hook
`92-mbp-uvcvideo-t1-check` then says so, with the command to rebuild.

    sudo ./install.sh     # all installed kernels; the module is used from the next reboot
    sudo ./remove.sh      # back to the stock uvcvideo

DKMS moves the stock `kernel/drivers/media/usb/uvc/uvcvideo.ko.zst` of each
kernel it installs for into `/var/lib/dkms/uvcvideo-t1/original_module/<kernel>/`
and puts it back on `dkms remove` (as for our other DKMS modules). For a new
kernel where the build fails nothing is moved, so its stock module stays.

## Licence

GPL-2.0-or-later, like the Linux uvcvideo driver it patches (t1bridge ships
its uvcvideo override under the same licence). The patches keep their authors.
