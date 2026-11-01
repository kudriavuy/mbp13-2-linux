#!/bin/bash
# Build the Touch ID packages into out/, then: sudo ../install.sh touchid
#   libfprint-t1bridge, fprintd-t1bridge  T1Bridge's own PKGBUILDs (unchanged)
#   t1bridge-touchid                      T1Bridge's Touch ID services only, with
#                                         our patch 0001 (no recovery, no USB cycles,
#                                         no automatic EFI import)
#   ./build.sh               on the Mac itself (Arch): makepkg as your user;
#                            installs libfprint-t1bridge on the way (fprintd builds
#                            against it; it replaces libfprint)
#   ./build.sh --container   in a fresh archlinux:latest container (docker/podman)
set -euo pipefail
cd "$(dirname "$0")"
exec ../lib/pkgbuild.sh ${1:+"$1"} out libfprint-t1bridge fprintd-t1bridge .
