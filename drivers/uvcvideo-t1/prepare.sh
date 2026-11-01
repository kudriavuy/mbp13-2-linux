#!/bin/bash
# DKMS PRE_BUILD for uvcvideo-t1: fetch this kernel's own drivers/media/usb/uvc
# (HTTPS only, git.kernel.org stable tree, cached in /var/cache/mbp-uvcvideo-t1)
# and apply patches/ into build/. Any failure stops the build, so DKMS installs
# nothing and the stock uvcvideo stays.
#   prepare.sh <kernelver>      e.g. 7.2.11-arch1-1 -> v7.2.11, 6.18.57-2-lts -> v6.18.57
set -euo pipefail
cd "$(dirname "$0")"
kver=${1:?kernel version}
v=$(sed -E 's/^([0-9]+\.[0-9]+)(\.[0-9]+)?.*/\1\2/' <<<"$kver")
v=${v%.0}
CACHE=${UVC_T1_CACHE:-/var/cache/mbp-uvcvideo-t1}/v$v
BASE=https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/drivers/media/usb/uvc

fail() {
	echo "uvcvideo-t1: ERROR: $*"
	echo "uvcvideo-t1: the camera stays on the stock uvcvideo for $kver (no H.264 in T1 configuration 2)."
	echo "uvcvideo-t1: rebuild later: sudo dkms install uvcvideo-t1/0.1 -k $kver"
	exit 1
}

get() {
	curl --proto '=https' --tlsv1.2 -fsS --retry 2 --max-time 60 -o "$CACHE.tmp/$1" "$BASE/$1?h=v$v" ||
		fail "could not download $1 for v$v from git.kernel.org (no network?)"
}

if [[ ! -e $CACHE/.complete ]]; then
	rm -rf "$CACHE.tmp"; mkdir -p "$CACHE.tmp"
	get Makefile
	# the objects of uvcvideo-objs (uvcvideo.o itself is the module)
	for f in $(grep -oE '\buvc_[a-z0-9_]+\.o' "$CACHE.tmp/Makefile" | sed 's/\.o$/.c/' | sort -u) uvcvideo.h; do
		get "$f"
	done
	touch "$CACHE.tmp/.complete"
	rm -rf "$CACHE" && mv "$CACHE.tmp" "$CACHE"
fi

rm -rf build && mkdir build
cp "$CACHE"/* build/
for p in patches/0*.patch; do
	patch -d build -p5 -s --no-backup-if-mismatch < "$p" || fail "$(basename "$p") does not apply to v$v"
done
echo "uvcvideo-t1: v$v uvc sources with $(ls patches/0*.patch | wc -l) patches ready"
