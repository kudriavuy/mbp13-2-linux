#!/bin/bash
# Build PKGBUILD directories of this repo, in order, into <out>/.
#
#   lib/pkgbuild.sh [--container] <out> <dir> [<dir> ...]
#
# Native (default, on Arch): makepkg in a temporary copy, as you (not root).
# A package that a later one builds against is installed with sudo pacman -U
# (e.g. libfprint-t1bridge before fprintd-t1bridge), and it says so first.
# --container: in a fresh archlinux:latest container (docker or podman),
# nothing on this machine changes; the result is built against the
# container's (current Arch) libraries, so update the target system first.
#
# A <dir> named *-t1bridge is built in T1Bridge's tree layout
# (packaging/arch/<name>, LICENSE three levels up), as its PKGBUILD expects.
set -euo pipefail
container=0
[[ ${1:-} == --container ]] && { container=1; shift; }
out=$(realpath -m "${1:?out dir}"); shift
[[ $# -gt 0 ]] || { sed -n '2,15p' "$0"; exit 1; }
mkdir -p "$out"
dirs=()
for d in "$@"; do dirs+=("$(realpath "$d")"); done

# stage <dir> <work>: copy a PKGBUILD dir to where makepkg runs; prints that place
stage() {
	local d=$1 w=$2 n; n=$(basename "$d")
	if [[ $n == *-t1bridge ]]; then
		mkdir -p "$w/t1bridge/packaging/arch"
		cp -r "$d" "$w/t1bridge/packaging/arch/$n"
		cp "$(dirname "$d")/T1Bridge-LICENSE" "$w/t1bridge/LICENSE"
		echo "$w/t1bridge/packaging/arch/$n"
	else
		cp -r "$d" "$w/$n"; rm -rf "$w/$n/out"
		echo "$w/$n"
	fi
}

if (( ! container )); then
	[[ $EUID -ne 0 ]] || { echo "run as your user, not root (makepkg refuses root)"; exit 1; }
	command -v makepkg >/dev/null || { echo "makepkg missing: this is not Arch; use --container"; exit 1; }
	w=$(mktemp -d "${TMPDIR:-/tmp}/mbp-pkg.XXXXXX"); trap 'rm -rf "$w"' EXIT
	last=${dirs[-1]}
	for d in "${dirs[@]}"; do
		p=$(stage "$d" "$w")
		(cd "$p" && makepkg -sf --noconfirm)
		for f in "$p"/*.pkg.tar.zst; do [[ $f == *-debug-* ]] || cp "$f" "$out/"; done
		if [[ $d != "$last" ]]; then
			echo "==> installing $(basename "$d") now: the next package builds against it"
			for f in "$p"/*.pkg.tar.zst; do
				[[ $f == *-debug-* ]] || sudo pacman -U --noconfirm "$f"
			done
		fi
	done
	ls -l "$out"
	exit 0
fi

eng=$(command -v podman || command -v docker || true)
[[ -n $eng ]] || { echo "neither podman nor docker found"; exit 1; }
w=$(mktemp -d "${TMPDIR:-/tmp}/mbp-pkg.XXXXXX"); trap 'rm -rf "$w"' EXIT
rel=()
for d in "${dirs[@]}"; do p=$(stage "$d" "$w"); rel+=("${p#"$w"/}"); done
"$eng" run --rm -i -v "$w:/w:ro" -v "$out:/out" -e HOST_UID="$(id -u):$(id -g)" \
	archlinux:latest bash -s "${rel[@]}" <<'IN'
set -euo pipefail
pacman -Syu --noconfirm --needed base-devel git >/dev/null
useradd -m builder
cp -r /w /home/builder/w && chown -R builder: /home/builder/w
built=()
for r in "$@"; do
	d=/home/builder/w/$r
	# depends/makedepends/checkdepends, minus what was just built here
	deps=$(cd "$d" && bash -c 'source PKGBUILD; echo "${depends[@]}" "${makedepends[@]}" "${checkdepends[@]:-}"')
	for b in "${built[@]:-}"; do deps=$(tr ' ' '\n' <<< "$deps" | grep -v "^$b\([<>=].*\)\?$" | tr '\n' ' '); done
	pacman -S --noconfirm --needed $deps >/dev/null
	su builder -c "export PATH=\$PATH:/usr/bin/core_perl; cd $d && makepkg -f --noconfirm" > "/home/builder/$(basename "$r").log" 2>&1 ||
		{ tail -40 "/home/builder/$(basename "$r").log"; exit 1; }
	for f in "$d"/*.pkg.tar.zst; do
		case $f in *-debug-*) continue ;; esac
		cp "$f" /out/
		pacman -U --noconfirm "$f" >/dev/null
	done
	built+=("$(basename "$r")")
done
chown "$HOST_UID" /out/*
ls -l /out
IN
