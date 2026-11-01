#!/bin/bash
# Touch ID needs this Mac's sensor calibration (FDRData), which macOS keeps
# on the Apple EFI system partition in EFI/APPLE/EMBEDDEDOS/. This mounts
# that partition READ ONLY, hands the file to 't1bridge machine-data import
# --from' (it copies it to root-only /var/lib/t1bridge after checking it
# against the sensor over the T1 link), and unmounts. Nothing is written to
# the partition or to the T1.
#
#   sudo ./import-fdrdata.sh              find the partition (FAT ESP with EFI/APPLE)
#   sudo ./import-fdrdata.sh /dev/nvme0n1p1
#   sudo ./import-fdrdata.sh --file <FDRData>   a copy you made yourself
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
command -v t1bridge >/dev/null || { echo "t1bridge missing: ./install.sh touchid first"; exit 1; }
REL=EFI/APPLE/EMBEDDEDOS/FDRData
mnt=
cleanup() { if [[ -n $mnt ]]; then umount "$mnt" 2>/dev/null || true; rmdir "$mnt"; fi; }
trap cleanup EXIT

if [[ ${1:-} == --file ]]; then
	f=$(realpath "${2:?file}")
else
	parts=${1:-$(lsblk -rno PATH,PARTTYPE,FSTYPE | awk 'tolower($2) == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" && $3 == "vfat" {print $1}')}
	for p in $parts; do
		mnt=$(mktemp -d /run/mbp-efi.XXXXXX)
		if mount -o ro,nodev,nosuid,noexec "$p" "$mnt" 2>/dev/null && [[ -e $mnt/$REL ]]; then
			echo "found $REL on $p (mounted read-only)"
			f=$mnt/$REL
			break
		fi
		umount "$mnt" 2>/dev/null || true; rmdir "$mnt"; mnt=
	done
	[[ -n ${f:-} ]] || { echo "no partition with $REL (is macOS still installed? It keeps the T1 firmware too)"; exit 1; }
fi
t1bridge machine-data import --from "$f"
echo "imported; next: fprintd-enroll in the desktop session"
