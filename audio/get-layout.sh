#!/bin/bash
# Speaker data for the graph: the static EQ, crossover and thermal parameters
# of macOS's AppleHDA layout 54, taken from THIS Mac's own macOS, read only.
# Nothing of Apple's is shipped with this repo.
#
#   sudo get-layout.sh <layout54.xml.zlib>   a copy made in macOS:
#        cp /System/Library/Extensions/AppleHDA.kext/Contents/Resources/layout54.xml.zlib <usb stick>
#   sudo get-layout.sh --mount               find the macOS APFS partition and read it
#                                            with apfs-fuse (read-only driver; AUR
#                                            apfs-fuse-git), then unmount
#
# Writes /usr/local/lib/mbp-audio/layout54.json and regenerates the graph.
set -euo pipefail
LIB=/usr/local/lib/mbp-audio
REL=System/Library/Extensions/AppleHDA.kext/Contents/Resources/layout54.xml.zlib
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
[[ $(cat /sys/class/dmi/id/product_name 2>/dev/null) == MacBookPro13,2 ]] ||
	{ echo "not a MacBookPro13,2: these speaker parameters are not for this machine"; exit 1; }
[[ $# -eq 1 ]] || { sed -n '2,13p' "$0"; exit 1; }

src=$1 mnt='' tmp=''
cleanup() {
	if [[ -n $mnt ]]; then fusermount -u "$mnt" 2>/dev/null || umount "$mnt" 2>/dev/null || true; rmdir "$mnt"; fi
	if [[ -n $tmp ]]; then rm -f "$tmp"; fi
}
trap cleanup EXIT

if [[ $src == --mount ]]; then
	command -v apfs-fuse >/dev/null || { echo "apfs-fuse missing (AUR: apfs-fuse-git), or copy the file from macOS"; exit 1; }
	part=$(lsblk -rno PATH,PARTTYPE | awk 'tolower($2) == "7c3457ef-0000-11aa-aa11-00306543ecac" {print $1; exit}')
	[[ -n $part ]] || { echo "no APFS partition found"; exit 1; }
	mnt=$(mktemp -d /run/mbp-macos.XXXXXX)
	# apfs-fuse is read-only; the macOS system volume is the first one
	apfs-fuse -o ro,allow_other "$part" "$mnt"
	src=$(find "$mnt" -maxdepth 4 -path "*/$REL" -print -quit 2>/dev/null || true)
	[[ -z $src ]] && for f in "$mnt"/*/"$REL" "$mnt"/root/"$REL"; do [[ -e $f ]] && src=$f && break; done
	[[ -n $src && -e $src ]] || { echo "layout54.xml.zlib not found on $part"; exit 1; }
fi
[[ -r $src ]] || { echo "cannot read $src"; exit 1; }
tmp=$(mktemp)
python3 -I "$LIB/extract_layout54.py" "$src" "$tmp"
install -m644 "$tmp" "$LIB/layout54.json"
echo "installed $LIB/layout54.json (Apple's numbers from this Mac: keep them here, do not share)"
owner=$(stat -c %U /var/lib/mbp-audio)
sudo -u "$owner" /usr/local/bin/mbp-audio gen
echo "as $owner run: systemctl --user restart wireplumber pipewire pipewire-pulse"
