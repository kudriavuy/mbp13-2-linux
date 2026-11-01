# Shared checks for install.sh and the helper scripts (sourced, not run).
# shellcheck shell=bash

TESTED_MODEL=MacBookPro13,2

model() { cat /sys/class/dmi/id/product_name 2>/dev/null || echo unknown; }

say()  { echo "==> $*"; }
warn() { echo "WARNING: $*" >&2; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# DRY=1: print what would change instead of changing it. The commands that
# change the system are shadowed by functions that only print; checks that
# would stop the run only warn.
DRY=${DRY:-0}
dry() { printf '  [dry-run]'; printf ' %q' "$@"; echo; }
dry_shims() {
	local c
	for c in install cp rm ln mv chmod chgrp chown rmdir mkdir systemctl udevadm pacman dkms \
		modprobe rmmod depmod make sudo systemd-tmpfiles mount umount dbus-send journalctl; do
		eval "$c() { dry $c \"\$@\"; }"
	done
}
# fail: stop, or in a dry run only say so
fail() { if (( DRY )); then warn "$* (dry run: continuing)"; else die "$*"; fi; }

# write <file> <content>: a file in /etc, /sys or /proc
write() { if (( DRY )); then dry write "$1" "$2"; else printf '%s\n' "$2" > "$1"; fi; }

need_root() { (( DRY )) || [[ $EUID -eq 0 ]] || die "run with sudo (or --dry-run to see what would happen)"; }

# Settings tuned on the tested model only (speaker protection and EQ, Wi-Fi
# NVRAM recipe, power and sleep quirks): refused anywhere else.
require_tested_model() {
	local what=$1
	[[ $(model) == "$TESTED_MODEL" ]] && return 0
	fail "$what is tuned for a $TESTED_MODEL; this is a $(model). Not installed."
}

# Drivers: allowed on other Macs with a warning (untested there).
warn_other_model() {
	[[ $(model) == "$TESTED_MODEL" ]] && return 0
	warn "only tested on a $TESTED_MODEL, this is a $(model): $1 is untested here. Reports welcome."
}

# T1 (iBridge) state from the USB device list, no lsusb needed:
#   ok        05ac:8600 iBridge (firmware loaded)
#   recovery  05ac:1281 Apple Mobile Device [Recovery Mode]: no T1 firmware
#   absent    no T1 at all (not a Touch Bar Mac with T1, or it fell off USB)
t1_state() {
	local d v p
	for d in /sys/bus/usb/devices/*; do
		[[ -r $d/idVendor ]] || continue
		v=$(<"$d/idVendor"); p=$(<"$d/idProduct")
		[[ $v == 05ac ]] || continue
		case $p in
		8600) echo ok; return ;;
		1281) echo recovery; return ;;
		esac
	done
	echo absent
}

require_t1() {
	case $(t1_state) in
	ok) return 0 ;;
	recovery) fail "the T1 is in Recovery Mode (05ac:1281): it has no firmware, so the Touch Bar, camera and Touch ID cannot work. Boot macOS once (or reinstall macOS without erasing) to restore it. Nothing here writes T1 firmware." ;;
	*) fail "no T1 (iBridge 05ac:8600) on USB: $1 needs a Touch Bar Mac with a T1 chip" ;;
	esac
}

# The user who ran sudo (owner of build trees and of the desktop session)
# shellcheck disable=SC2034  # used by the scripts that source this file
OWNER=${SUDO_USER:-${USER:-root}}
