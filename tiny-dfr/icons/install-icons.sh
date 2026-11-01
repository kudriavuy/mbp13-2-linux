#!/bin/bash
# Put the Touch Bar icons into <dir> (/etc/tiny-dfr, where tiny-dfr looks
# first): own drawings as they are, Lucide (ISC, lucide-static 0.460.0) as
# lucide-<name>.svg drawn white instead of currentColor (librsvg: black).
#   install-icons.sh <dir>      install-icons.sh --remove <dir>
set -euo pipefail
cd "$(dirname "$0")"
if [[ ${1:-} == --remove ]]; then
	rm -f "$2"/mbp-*.svg "$2"/lucide-*.svg "$2"/LICENSE.lucide
	exit 0
fi
d=$1
install -d -m755 "$d"
install -m644 own/*.svg "$d"/
for f in lucide/*.svg; do
	sed 's/currentColor/#ffffff/g' "$f" > "$d/lucide-$(basename "$f")"
	chmod 644 "$d/lucide-$(basename "$f")"
done
install -m644 lucide/LICENSE "$d"/LICENSE.lucide
