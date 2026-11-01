#!/bin/bash
# Undo install.sh: DKMS module out (the stock uvcvideo is used again), hook
# out. The source cache in /var/cache/mbp-uvcvideo-t1 stays.
#   sudo ./remove.sh
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
cd "$(dirname "$0")"
N=uvcvideo-t1 V=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)
dkms status "$N/$V" | grep -q . && dkms remove "$N/$V" --all
rm -rf "/usr/src/$N-$V" /usr/local/bin/mbp-uvcvideo-t1-check /etc/pacman.d/hooks/92-mbp-uvcvideo-t1-check.hook
echo "uvcvideo now: $(modinfo -n uvcvideo)"
if [[ $(cat /sys/module/uvcvideo/refcnt 2>/dev/null) == 0 ]]; then
	modprobe -r uvcvideo && modprobe uvcvideo && echo "stock uvcvideo loaded"
else
	echo "uvcvideo in use: the stock one loads at the next reboot"
fi
