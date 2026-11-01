#!/bin/bash
# KDE shortcuts for the tiny-dfr media layer (run as the desktop user):
# Mission Control (KEY_SCALE = XF86LaunchA, "Launch (A)") also toggles the
# KWin Overview, Launchpad (KEY_DASHBOARD = XF86LaunchB, "Launch (B)") also
# opens the application launcher. Existing shortcuts stay.
#   ./kde-shortcuts.sh apply     (first copy: ~/.config/kglobalshortcutsrc.bak-tiny-dfr)
#   ./kde-shortcuts.sh restore   put that copy back
# Takes effect at the next login (KWin reads it then).
set -euo pipefail
F=~/.config/kglobalshortcutsrc
B=$F.bak-tiny-dfr

add() {	# group key shortcut: add it to the active shortcuts of group/key
	local cur active rest
	cur=$(kreadconfig6 --file kglobalshortcutsrc --group "$1" --key "$2")
	[[ -n $cur ]] || { echo "no [$1] $2 in $F" >&2; return 1; }
	active=${cur%%,*}; rest=${cur#*,}
	[[ $'\t'$active$'\t' == *$'\t'$3$'\t'* ]] && { echo "[$1] $2 already has $3"; return; }
	kwriteconfig6 --file kglobalshortcutsrc --group "$1" --key "$2" "$active"$'\t'"$3,$rest"
	echo "[$1] $2: $active + $3"
}

case ${1:-} in
apply)
	[[ -e $B ]] || cp -p "$F" "$B"
	add kwin Overview "Launch (A)"
	add plasmashell "activate application launcher" "Launch (B)"
	echo "log out and in to use them" ;;
restore)
	[[ -e $B ]] || { echo "no $B"; exit 1; }
	cp -p "$B" "$F" && echo "restored $F from $B (log out and in)" ;;
*) echo "usage: $0 apply|restore"; exit 2 ;;
esac
