#!/bin/bash
# Build the tiny-dfr package (PKGBUILD here: upstream tiny-dfr at a fixed
# commit + our patches 0001-0021) into out/, then: sudo ../install.sh tinydfr
#   ./build.sh               on the Mac itself (Arch): makepkg, as your user
#   ./build.sh --container   in a fresh archlinux:latest container (docker/podman)
set -euo pipefail
cd "$(dirname "$0")"
exec ../lib/pkgbuild.sh ${1:+"$1"} out .
