#!/bin/bash
# Install uvcvideo-t1 as DKMS for every installed kernel, and the pacman
# hook that warns when it is missing for a kernel. The stock uvcvideo stays
# for any kernel where the build fails.
#   sudo ./install.sh        (remove.sh undoes it)
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
cd "$(dirname "$0")"
N=uvcvideo-t1 V=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)
S=/usr/src/$N-$V
dkms status "$N/$V" | grep -q . && dkms remove "$N/$V" --all
rm -rf "$S" && mkdir -p "$S"
cp -r dkms.conf Makefile prepare.sh patches "$S"/
dkms add "$N/$V"
for k in /usr/lib/modules/*/build; do
	k=$(basename "$(dirname "$k")")
	dkms install "$N/$V" -k "$k" || echo "WARNING: $N not built for $k: the stock uvcvideo stays there"
done
install -Dm755 mbp-uvcvideo-t1-check /usr/local/bin/mbp-uvcvideo-t1-check
install -Dm644 92-mbp-uvcvideo-t1-check.hook /etc/pacman.d/hooks/92-mbp-uvcvideo-t1-check.hook
dkms status "$N"
echo "uvcvideo now: $(modinfo -n uvcvideo)"
echo "the loaded module changes at the next reboot (or: modprobe -r uvcvideo && modprobe uvcvideo with no camera in use)"
