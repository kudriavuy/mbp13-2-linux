#!/bin/bash
# Installs the MacBookPro13,2 (A1706) support for Arch Linux, one module at a
# time. Every module has a matching <module>-remove. See README.md.
#
#   sudo ./install.sh <module> [<module> ...]
#   ./install.sh --dry-run <module>...   what would change (no root needed)
#   ./install.sh check                   model, T1 state, kernels, DKMS state
#   ./install.sh --help
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck source=lib/common.sh
. lib/common.sh

usage() {
	cat <<'EOF'
usage: sudo ./install.sh [--dry-run] <module> [<module> ...]
       ./install.sh check | --help

Kernel modules (DKMS, built for every installed kernel: linux and linux-lts):
  sound          CS8409 codec driver (snd_hda_macbookpro): speakers, headphones,
                 microphone, sound after sleep
  appletb        Touch Bar in T1 configuration 1 (function/media keys drawn by
                 the T1 itself) + T1 sleep/wake notifications (apple-ibridge-pm)
  spi-legacy     OPTIONAL alternative to appletb (macbook12-spi-driver, with the
                 ambient light sensor); never together with appletb
  t1display      Touch Bar display + touch in T1 configuration 2 (appletbdrm
                 with T1 support, hid-t1-touchbar)
  uvcvideo       camera in T1 configuration 2 (H.264), stock uvcvideo elsewhere
  t1ncm          network link to the T1 (Touch ID needs it)
  thunderbolt    tb_ar_shim: USB-C/Thunderbolt keep working through sleep

System setup:
  base           power and sleep quirks of the MacBookPro13,2 (modprobe.d,
                 tmpfiles.d, udev)                              [13,2 only]
  audio          speaker protection, crossover, EQ, volume law ("MacBook
                 Speakers"); needs 'sound' and the layout data   [13,2 only]
  wifi           BCM43602 board NVRAM from the chip's own SROM (mbp-wifi) and
                 the sleep hook                                  [13,2 only]
  lidwake        sleep begun with the lid open does not wake at once
  touchbar       Touch Bar settings for configuration 1 (timeouts, F-keys per
                 program, KWin script)
  tinydfr        tiny-dfr with our patches (build it first: tiny-dfr/build.sh)
  camera         virtual camera for T1 configuration 2 (needs uvcvideo,
                 v4l2loopback-dkms)
  t1stack        own Touch Bar stack: switch the T1 to configuration 2 at boot
                 when everything is there, fallback renderer, boot entries
                 "Touch Bar config 1" (needs t1display, tinydfr, camera)
  touchid        Touch ID services, libfprint/fprintd (build first:
                 touchid/build.sh); needs t1ncm and t1stack
  pacman         pacman hook: test-build the DKMS modules before a kernel update

Every module: <module>-remove undoes it.

Options:
  --dry-run, -n  print what would change, change nothing
  --help, -h     this text

Speaker settings (audio) are refused on any other model: wrong protection
parameters can damage speakers. Touch Bar, camera and Touch ID modules check
that the T1 is present and not in Recovery Mode.
EOF
}

kernels() {
	local b
	for b in /usr/lib/modules/*/build; do
		[[ -d $b ]] && basename "$(dirname "$b")"
	done
	return 0
}

dkms_ver() { sed -n 's/^PACKAGE_VERSION="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$1/dkms.conf"; }
dkms_pkg() { sed -n 's/^PACKAGE_NAME="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$1/dkms.conf"; }
dkms_has() { [[ -n $(command dkms status "$1" 2>/dev/null) ]]; }

# Copy a DKMS tree to /usr/src and build + install it for every kernel.
# 'dkms install --force' does not rebuild, so build first.
dkms_put() {
	local dir=$1 n v k ok=0
	n=$(dkms_pkg "$dir"); v=$(dkms_ver "$dir")
	command -v dkms >/dev/null || (( DRY )) || die "dkms missing: sudo pacman -S dkms linux-headers linux-lts-headers"
	[[ -n $(kernels) ]] || fail "no kernel headers in /usr/lib/modules/*/build (pacman -S linux-headers linux-lts-headers)"
	dkms remove "$n/$v" --all >/dev/null 2>&1 || true
	rm -rf "/usr/src/$n-$v"
	mkdir -p "/usr/src/$n-$v"
	cp -r "$dir"/. "/usr/src/$n-$v/"
	dkms add "$n/$v"
	for k in $(kernels); do
		if dkms build "$n/$v" -k "$k" --force && dkms install "$n/$v" -k "$k" --force; then
			ok=1
		else
			warn "$n/$v not built for $k (the stock driver stays there)"
		fi
	done
	(( ok || DRY )) || die "$n/$v built for no kernel"
	say "$n/$v installed (DKMS); loaded from the next boot"
}

dkms_drop() {
	local dir=$1 n v
	n=$(dkms_pkg "$dir"); v=$(dkms_ver "$dir")
	dkms remove "$n/$v" --all 2>/dev/null || true
	rm -rf "/usr/src/$n-$v"
	depmod -a
	say "$n/$v removed; the kernel's own driver from the next boot"
}

# run another script of this repo (printed only in a dry run)
run_script() { if (( DRY )); then dry "$@"; else "$@"; fi; }

# any command in $OWNER's running desktop session (no-op without one)
user_run() {
	local uid; uid=$(id -u "$OWNER")
	[[ -S /run/user/$uid/bus ]] || return 0
	sudo -u "$OWNER" XDG_RUNTIME_DIR="/run/user/$uid" \
		DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" "$@"
}
user_systemctl() { user_run systemctl --user "$@"; }

newest_pkg() { local f; f=$(ls -t "$@" 2>/dev/null | head -1) || true; echo "$f"; }

# ---- check --------------------------------------------------------------

do_check() {
	local k
	echo "model:    $(model) (tested: $TESTED_MODEL)"
	echo "T1:       $(t1_state)"
	echo "kernels:  $(kernels | tr '\n' ' ')"
	echo "running:  $(uname -r)"
	if command -v dkms >/dev/null; then
		command dkms status 2>/dev/null | grep -E 'snd_hda_macbookpro|appletb-t1|apple-ibridge|mbp-t1-display|uvcvideo-t1|apple-t1-ncm|tb-ar-shim' | sed 's/^/dkms:     /' || true
	fi
	for k in tiny-dfr t1bridge-touchid libfprint-t1bridge fprintd-t1bridge; do
		command pacman -Q "$k" 2>/dev/null | sed 's/^/package:  /' || true
	done
}

# ---- kernel modules -----------------------------------------------------

install_sound() {
	warn_other_model "the CS8409 driver"
	if ! (( DRY )) && ! { command -v wget && command -v patch; } >/dev/null; then
		die "the sound driver build needs wget and patch: sudo pacman -S --needed wget patch"
	fi
	# its build takes sound/hda of the kernel's own version from kernel.org
	# (once per version, cached in /var/cache/snd_hda_macbookpro)
	dkms_put drivers/snd_hda_macbookpro
	[[ $(model) == "$TESTED_MODEL" ]] ||
		warn "the speakers of this Mac get no protection: do NOT install 'audio' (its parameters are for a $TESTED_MODEL and can damage other speakers); keep the volume low"
}
remove_sound() { dkms_drop drivers/snd_hda_macbookpro; }

install_appletb() {
	require_t1 "the Touch Bar driver"
	warn_other_model "the Touch Bar driver"
	if dkms_has apple-ibridge; then fail "spi-legacy is installed: run './install.sh spi-legacy-remove' first (only one Touch Bar driver)"; fi
	dkms_put drivers/appletb-t1
}
remove_appletb() { dkms_drop drivers/appletb-t1; }

install_spi_legacy() {
	require_t1 "the Touch Bar driver"
	warn_other_model "macbook12-spi-driver"
	if dkms_has appletb-t1; then fail "appletb is installed: run './install.sh appletb-remove' first (only one Touch Bar driver)"; fi
	dkms_put drivers/macbook12-spi-driver
	write /etc/modprobe.d/mbp-no-appletb.conf $'blacklist hid_appletb_kbd\nblacklist hid_appletb_bl'
	write /etc/modules-load.d/apple-ibridge.conf $'apple-ibridge\napple-ib-tb\napple-ib-als'
}
remove_spi_legacy() {
	dkms_drop drivers/macbook12-spi-driver
	rm -f /etc/modprobe.d/mbp-no-appletb.conf /etc/modules-load.d/apple-ibridge.conf
}

install_t1display() {
	require_t1 "the Touch Bar display driver"
	warn_other_model "the Touch Bar display driver"
	dkms_put drivers/mbp-t1-display
	install -Dm644 drivers/mbp-t1-display/90-mbp-t1-display.rules /etc/udev/rules.d/90-mbp-t1-display.rules
	udevadm control --reload
}
remove_t1display() {
	dkms_drop drivers/mbp-t1-display
	rm -f /etc/udev/rules.d/90-mbp-t1-display.rules
	udevadm control --reload
}

install_uvcvideo() {
	require_t1 "the camera driver"
	warn_other_model "the camera driver"
	run_script drivers/uvcvideo-t1/install.sh
}
remove_uvcvideo() { run_script drivers/uvcvideo-t1/remove.sh; }

install_t1ncm() {
	require_t1 "the T1 network link"
	warn_other_model "the T1 network link"
	dkms_put drivers/apple-t1-ncm
	install -Dm644 drivers/apple-t1-ncm/50-apple-t1-ncm.link /etc/systemd/network/50-apple-t1-ncm.link
	say "t1ncm0 is unmanaged by NetworkManager; with a firewall allow TCP 61500 in on t1ncm0 only (Touch ID storage)"
}
remove_t1ncm() {
	dkms_drop drivers/apple-t1-ncm
	rm -f /etc/systemd/network/50-apple-t1-ncm.link
}

install_thunderbolt() {
	warn_other_model "tb_ar_shim"
	dkms_put drivers/tb-ar-shim
	# loads by PCI ID from the next boot; now it re-binds the two bridges
	modprobe tb_ar_shim || warn "tb_ar_shim not loaded now (from the next boot)"
}
remove_thunderbolt() {
	rmmod tb_ar_shim 2>/dev/null || true
	dkms_drop drivers/tb-ar-shim
}

# ---- system setup -------------------------------------------------------

BASE_FILES=(etc/modprobe.d/apple-ibridge-pm.conf etc/modprobe.d/brcmfmac.conf
	etc/tmpfiles.d/nvme-d3cold.conf etc/udev/rules.d/90-mbp-thunderbolt-d3cold.rules)
install_base() {
	require_tested_model "base (PCI paths and sleep quirks)"
	local f
	for f in "${BASE_FILES[@]}"; do install -Dm644 "$f" "/$f"; done
	udevadm control --reload
	systemd-tmpfiles --create /etc/tmpfiles.d/nvme-d3cold.conf
	say "base installed: ${BASE_FILES[*]}"
}
remove_base() {
	local f
	for f in "${BASE_FILES[@]}"; do rm -f "/$f"; done
	udevadm control --reload
	say "base removed (in effect from the next boot)"
}

AUDIO_LIB=/usr/local/lib/mbp-audio
install_audio() {
	require_tested_model "the speaker protection and EQ"
	dkms_has snd_hda_macbookpro || fail "the sound driver is not installed: ./install.sh sound first"
	command pacman -Q pipewire-audio wireplumber >/dev/null 2>&1 ||
		fail "PipeWire's filter-chain needs: sudo pacman -S --needed pipewire-audio pipewire-pulse wireplumber"
	sudo -u "$OWNER" make -C audio/speakerguard
	make -C audio/speakerguard install
	install -Dm644 audio/acp/mbp13-2.conf /usr/share/alsa-card-profile/mixer/profile-sets/mbp13-2.conf
	install -Dm644 audio/acp/mbp13-2-speakers.conf /usr/share/alsa-card-profile/mixer/paths/mbp13-2-speakers.conf
	install -Dm644 audio/wireplumber/90-mbp13-2-acp.conf audio/wireplumber/91-mbp13-2-speakers.conf \
		-t /etc/wireplumber/wireplumber.conf.d/
	install -Dm644 audio/pipewire/90-mbp13-2-rate.conf -t /etc/pipewire/pipewire.conf.d/
	install -Dm644 audio/mkgraph.py audio/extract_layout54.py audio/speaker-map -t "$AUDIO_LIB"
	install -Dm755 audio/get-layout.sh "$AUDIO_LIB/get-layout.sh"
	install -Dm755 audio/mbp-audio /usr/local/bin/mbp-audio
	install -d -m755 -o "$OWNER" /var/lib/mbp-audio
	if [[ -s $AUDIO_LIB/layout54.json ]]; then
		sudo -u "$OWNER" /usr/local/bin/mbp-audio gen || true
	else
		warn "no speaker data yet: the speakers stay silent (never unprotected) until you run"
		warn "  sudo $AUDIO_LIB/get-layout.sh <layout54.xml.zlib from this Mac's macOS>   (see audio/README.md)"
	fi
	say "audio installed; as $OWNER run: systemctl --user restart wireplumber pipewire pipewire-pulse"
	warn "the macOS leveler/BuzzKill are not included; a static +11 dB make-up gain stands in."
	warn "This graph was NOT tested on hardware. The protection (limiter -3 dBFS, thermal model)"
	warn "runs, but listen carefully: start at a low volume and raise it step by step."
}
remove_audio() {
	rm -f /usr/local/lib/ladspa/mbp_speakerguard.so \
		/usr/share/alsa-card-profile/mixer/profile-sets/mbp13-2.conf \
		/usr/share/alsa-card-profile/mixer/paths/mbp13-2-speakers.conf \
		/etc/wireplumber/wireplumber.conf.d/90-mbp13-2-acp.conf \
		/etc/wireplumber/wireplumber.conf.d/91-mbp13-2-speakers.conf \
		/etc/pipewire/pipewire.conf.d/90-mbp13-2-rate.conf \
		/usr/local/bin/mbp-audio /etc/modprobe.d/mbp13-2-audio.conf
	rm -rf "$AUDIO_LIB" /var/lib/mbp-audio
	say "audio removed; as $OWNER run: systemctl --user restart wireplumber pipewire pipewire-pulse"
	warn "without 'audio' the four speakers get the stereo signal unprotected: keep the volume low or remove 'sound' too"
}

WIFI_LIB=/usr/local/lib/mbp-wifi
install_wifi() {
	require_tested_model "the Wi-Fi NVRAM recipe"
	install -d "$WIFI_LIB"
	install -m644 wifi/srom2nvram.py wifi/srom_tables.py wifi/w1.recipe -t "$WIFI_LIB"
	install -m755 wifi/read_srom.py wifi/mknvram.py -t "$WIFI_LIB"
	install -Dm755 wifi/mbp-wifi /usr/local/bin/mbp-wifi
	install -Dm755 wifi/mbp-wifi.sleep /usr/lib/systemd/system-sleep/mbp-wifi
	say "wifi tools installed; build the NVRAM now: sudo mbp-wifi nvram (Wi-Fi drops for a few seconds)"
}
remove_wifi() {
	if [[ -x /usr/local/bin/mbp-wifi ]]; then run_script /usr/local/bin/mbp-wifi remove; fi
	rm -f /usr/lib/systemd/system-sleep/mbp-wifi /usr/local/bin/mbp-wifi
	rm -rf "$WIFI_LIB"
	say "wifi removed (the SROM copy stays in /var/lib/mbp-wifi, delete it if you like)"
}

install_lidwake() {
	warn_other_model "the lid wake hook"
	install -Dm755 lid/mbp-lid-wake.sleep /usr/lib/systemd/system-sleep/mbp-lid-wake
	say "lid wake hook installed"
}
remove_lidwake() {
	rm -f /usr/lib/systemd/system-sleep/mbp-lid-wake
	# a sleep cut short could leave the lid wake off
	if [[ $(awk '$1 == "LID0" {print $3}' /proc/acpi/wakeup 2>/dev/null) == '*disabled' ]]; then
		write /proc/acpi/wakeup LID0
	fi
	rm -f /run/mbp-lid-wake
	say "lid wake hook removed"
}

install_pacman() {
	install -Dm755 pacman/mbp-dkms-precheck /usr/local/bin/mbp-dkms-precheck
	install -Dm644 pacman/90-mbp-dkms-precheck.hook /etc/pacman.d/hooks/90-mbp-dkms-precheck.hook
	say "pacman hook installed"
}
remove_pacman() {
	rm -f /usr/local/bin/mbp-dkms-precheck /etc/pacman.d/hooks/90-mbp-dkms-precheck.hook
	say "pacman hook removed"
}

TB_MODE_GLOB='/sys/bus/hid/drivers/hid-appletb-kbd/*/mode'
install_touchbar() {
	require_t1 "the Touch Bar settings"
	install -Dm644 touchbar/hid-appletb-kbd.conf /etc/modprobe.d/hid-appletb-kbd.conf
	install -Dm644 touchbar/90-mbp-touchbar.rules /etc/udev/rules.d/90-mbp-touchbar.rules
	install -Dm755 touchbar/mbp-touchbar /usr/local/bin/mbp-touchbar
	install -Dm644 touchbar/mbp-touchbar-mode@.service /etc/systemd/user/mbp-touchbar-mode@.service
	install -Dm644 touchbar/mbp-touchbar-app@.service /etc/systemd/user/mbp-touchbar-app@.service
	rm -rf /usr/share/kwin/scripts/mbp-touchbar-fnapps
	cp -r touchbar/kwin/mbp-touchbar-fnapps /usr/share/kwin/scripts/
	chmod -R a+rX /usr/share/kwin/scripts/mbp-touchbar-fnapps
	# now, without a reboot: the timeouts and the access to the mode file
	if [[ -d /sys/module/hid_appletb_kbd ]]; then
		write /sys/module/hid_appletb_kbd/parameters/dim_timeout 30
		write /sys/module/hid_appletb_kbd/parameters/idle_timeout 30
	fi
	udevadm control --reload
	local m
	for m in $TB_MODE_GLOB; do
		if [[ -e $m ]]; then chgrp wheel "$m" && chmod 0664 "$m"; fi
	done
	say "touchbar installed; as $OWNER run: ./touchbar/enable-kwin-script.sh"
}
remove_touchbar() {
	rm -f /etc/modprobe.d/hid-appletb-kbd.conf /etc/udev/rules.d/90-mbp-touchbar.rules \
		/usr/local/bin/mbp-touchbar /etc/systemd/user/mbp-touchbar-mode@.service \
		/etc/systemd/user/mbp-touchbar-app@.service
	rm -rf /usr/share/kwin/scripts/mbp-touchbar-fnapps
	if [[ -d /sys/module/hid_appletb_kbd ]]; then
		write /sys/module/hid_appletb_kbd/parameters/dim_timeout 60
		write /sys/module/hid_appletb_kbd/parameters/idle_timeout 15
	fi
	udevadm control --reload
	local m
	for m in $TB_MODE_GLOB; do
		if [[ -e $m ]]; then chgrp root "$m" && chmod 0644 "$m" && write "$m" 2; fi
	done
	say "touchbar settings removed (driver defaults: 60 + 15 s, media keys)"
}

install_tinydfr() {
	require_t1 "tiny-dfr"
	local pkg cfg=${TINYDFR_CONFIG:-tiny-dfr/config.toml} was_active=0 keep
	pkg=${TINYDFR_PKG:-$(newest_pkg tiny-dfr/out/tiny-dfr-[0-9]*.pkg.tar.zst)}
	[[ -n $pkg && -e $pkg ]] || fail "no tiny-dfr package in tiny-dfr/out: run tiny-dfr/build.sh first"
	[[ -e $cfg ]] || fail "no $cfg"
	pacman -U --noconfirm "$pkg"
	# a running tiny-dfr reloads the config the moment it changes: stop it
	# before the icons and the config, start it again at the end
	if systemctl -q is-active tiny-dfr.service 2>/dev/null; then was_active=1; systemctl stop tiny-dfr.service; fi
	run_script tiny-dfr/icons/install-icons.sh /etc/tiny-dfr
	# changes made in the settings window would be lost: keep them beside it
	if [[ -e /etc/tiny-dfr/.gui-edited ]] && ! cmp -s "$cfg" /etc/tiny-dfr/config.toml; then
		keep=/etc/tiny-dfr/config.toml.gui-$(date +%Y%m%d-%H%M%S)
		cp -p /etc/tiny-dfr/config.toml "$keep"
		say "/etc/tiny-dfr/config.toml had changes from the settings window: kept as $keep"
	fi
	install -Dm644 "$cfg" /etc/tiny-dfr/config.toml
	rm -f /etc/tiny-dfr/.gui-edited
	install -Dm755 tiny-dfr/settings/mbp-touchbar-settings /usr/local/bin/mbp-touchbar-settings
	install -Dm755 tiny-dfr/settings/mbp-touchbar-apply /usr/local/libexec/mbp-touchbar-apply
	install -Dm644 tiny-dfr/settings/org.mbp.touchbar.apply.policy /usr/share/polkit-1/actions/org.mbp.touchbar.apply.policy
	install -Dm644 tiny-dfr/settings/mbp-touchbar-settings.desktop /usr/local/share/applications/mbp-touchbar-settings.desktop
	write /etc/tmpfiles.d/mbp-tiny-dfr.conf "$(sed "s/@OWNER@/$OWNER/" tiny-dfr/mbp-tiny-dfr.tmpfiles)"
	systemd-tmpfiles --create /etc/tmpfiles.d/mbp-tiny-dfr.conf
	install -Dm755 tiny-dfr/mbp-touchbar-bridge /usr/local/bin/mbp-touchbar-bridge
	install -Dm644 tiny-dfr/mbp-touchbar-bridge.service /etc/systemd/user/mbp-touchbar-bridge.service
	systemctl --global enable mbp-touchbar-bridge.service
	user_systemctl daemon-reload || true
	user_systemctl restart mbp-touchbar-bridge.service || true
	install -Dm755 tiny-dfr/mbp-tiny-dfr-check /usr/local/bin/mbp-tiny-dfr-check
	install -Dm644 tiny-dfr/91-mbp-tiny-dfr-check.hook /etc/pacman.d/hooks/91-mbp-tiny-dfr-check.hook
	if [[ -e /usr/local/bin/mbp-touchbar && ! -e /usr/local/bin/mbp-touchbar.pre-tiny-dfr ]]; then
		cp -p /usr/local/bin/mbp-touchbar /usr/local/bin/mbp-touchbar.pre-tiny-dfr
	fi
	install -Dm755 touchbar/mbp-touchbar /usr/local/bin/mbp-touchbar
	# Touch ID hint: root watches fprintd and writes /run/tiny-dfr/touchid
	install -Dm755 touchid/hint/mbp-touchid-hint /usr/local/bin/mbp-touchid-hint
	install -Dm644 touchid/hint/mbp-touchid-hint.service /etc/systemd/system/mbp-touchid-hint.service
	systemctl daemon-reload
	systemctl enable mbp-touchid-hint.service
	systemctl restart mbp-touchid-hint.service
	# key sets per program: the KWin script reports the window class
	install -Dm644 touchbar/mbp-touchbar-app@.service /etc/systemd/user/mbp-touchbar-app@.service
	rm -rf /usr/share/kwin/scripts/mbp-touchbar-fnapps
	cp -r touchbar/kwin/mbp-touchbar-fnapps /usr/share/kwin/scripts/
	chmod -R a+rX /usr/share/kwin/scripts/mbp-touchbar-fnapps
	user_systemctl daemon-reload || true
	user_run dbus-send --session --type=method_call --dest=org.kde.KWin /Scripting \
		org.kde.kwin.Scripting.unloadScript string:mbp-touchbar-fnapps || true
	user_run dbus-send --session --type=method_call --dest=org.kde.KWin /KWin org.kde.KWin.reconfigure || true
	run_script /usr/local/bin/mbp-tiny-dfr-check || true
	if [[ $was_active == 1 ]]; then systemctl start tiny-dfr.service; fi
	say "tiny-dfr installed (runs in T1 configuration 2 only: see t1stack); optional KDE keys: as $OWNER run ./tiny-dfr/kde-shortcuts.sh apply"
}
remove_tinydfr() {
	systemctl stop tiny-dfr.service tiny-dfr-test.service 2>/dev/null || true
	user_systemctl stop mbp-touchbar-bridge.service 2>/dev/null || true
	systemctl disable --now mbp-touchid-hint.service 2>/dev/null || true
	rm -f /usr/local/bin/mbp-touchid-hint /etc/systemd/system/mbp-touchid-hint.service /run/tiny-dfr/touchid
	systemctl --global disable mbp-touchbar-bridge.service 2>/dev/null || true
	rm -f /usr/local/bin/mbp-touchbar-bridge /etc/systemd/user/mbp-touchbar-bridge.service
	local p
	for p in tiny-dfr tiny-dfr-debug; do
		if command pacman -Q "$p" >/dev/null 2>&1; then pacman -R --noconfirm "$p"; fi
	done
	run_script tiny-dfr/icons/install-icons.sh --remove /etc/tiny-dfr
	rm -f /usr/local/bin/mbp-touchbar-settings /usr/local/libexec/mbp-touchbar-apply \
		/usr/share/polkit-1/actions/org.mbp.touchbar.apply.policy \
		/usr/local/share/applications/mbp-touchbar-settings.desktop /etc/tiny-dfr/.gui-edited \
		/etc/tiny-dfr/config.toml /etc/tmpfiles.d/mbp-tiny-dfr.conf \
		/usr/local/bin/mbp-tiny-dfr-check /etc/pacman.d/hooks/91-mbp-tiny-dfr-check.hook
	rmdir /etc/tiny-dfr 2>/dev/null || true
	rm -rf /run/tiny-dfr
	if [[ -e /usr/local/bin/mbp-touchbar.pre-tiny-dfr ]]; then
		mv -f /usr/local/bin/mbp-touchbar.pre-tiny-dfr /usr/local/bin/mbp-touchbar
	fi
	say "tiny-dfr removed; if you applied the KDE keys: as $OWNER run ./tiny-dfr/kde-shortcuts.sh restore"
}

install_camera() {
	require_t1 "the camera"
	command pacman -Q v4l2loopback-dkms >/dev/null 2>&1 || fail "first: sudo pacman -S v4l2loopback-dkms"
	dkms_has uvcvideo-t1 || warn "uvcvideo-t1 not installed: no H.264 camera in configuration 2 (./install.sh uvcvideo)"
	install -Dm755 camera/mbp-t1-camera /usr/local/bin/mbp-t1-camera
	install -Dm644 camera/mbp-t1-camera.service /etc/systemd/system/mbp-t1-camera.service
	install -Dm644 camera/72-mbp-t1-camera.rules /etc/udev/rules.d/72-mbp-t1-camera.rules
	install -Dm755 camera/mbp-t1-camera.sleep /usr/lib/systemd/system-sleep/mbp-t1-camera
	udevadm control --reload
	systemctl daemon-reload
	say "camera installed (not enabled; t1stack starts it in configuration 2)"
}
remove_camera() {
	systemctl disable --now mbp-t1-camera.service 2>/dev/null || true
	rm -f /usr/local/bin/mbp-t1-camera /etc/systemd/system/mbp-t1-camera.service \
		/etc/udev/rules.d/72-mbp-t1-camera.rules /usr/lib/systemd/system-sleep/mbp-t1-camera
	udevadm control --reload
	systemctl daemon-reload
	say "camera removed; v4l2loopback-dkms stays (sudo pacman -R v4l2loopback-dkms)"
}

T1S=touchbar/t1stack
install_t1stack() {
	require_t1 "the Touch Bar stack"
	warn_other_model "the Touch Bar stack"
	command pacman -Q tiny-dfr >/dev/null 2>&1 || fail "first: ./install.sh tinydfr"
	[[ -x /usr/local/bin/mbp-t1-camera ]] || fail "first: ./install.sh camera"
	dkms_has mbp-t1-display || fail "first: ./install.sh t1display"
	sudo -u "$OWNER" make -s -C touchbar/fallback
	install -Dm755 touchbar/fallback/mbp-t1-fallback /usr/local/bin/mbp-t1-fallback
	install -Dm755 $T1S/mbp-t1-restart /usr/local/bin/mbp-t1-restart
	install -d /usr/local/lib/mbp-t1 /var/lib/mbp-t1
	install -m755 $T1S/mbp-t1-switch $T1S/notify $T1S/notice $T1S/giveup $T1S/boot-entries -t /usr/local/lib/mbp-t1
	install -m644 $T1S/mbp-t1-switch.service $T1S/mbp-t1-cfg2.target $T1S/mbp-t1-fallback.service \
		$T1S/mbp-t1-giveup.service -t /etc/systemd/system
	install -Dm644 $T1S/tiny-dfr.service.d/mbp-t1.conf /etc/systemd/system/tiny-dfr.service.d/mbp-t1.conf
	install -Dm644 $T1S/mbp-t1-camera.service.d/mbp-t1.conf /etc/systemd/system/mbp-t1-camera.service.d/mbp-t1.conf
	install -Dm644 $T1S/mbp-t1-notice.service /etc/systemd/user/mbp-t1-notice.service
	install -m644 $T1S/91-mbp-t1-stack.rules $T1S/99-zz-mbp-t1-backlight.rules -t /etc/udev/rules.d
	install -Dm644 $T1S/50-mbp-t1-restart.rules /etc/polkit-1/rules.d/50-mbp-t1-restart.rules
	install -Dm644 $T1S/mbp-t1-restart.desktop /usr/share/applications/mbp-t1-restart.desktop
	ln -sfn /usr/share/applications/mbp-t1-restart.desktop /usr/share/kglobalaccel/mbp-t1-restart.desktop
	install -Dm644 $T1S/93-mbp-t1-boot-entries.hook /etc/pacman.d/hooks/93-mbp-t1-boot-entries.hook
	udevadm control --reload
	systemctl daemon-reload
	systemctl enable mbp-t1-switch.service
	systemctl --global enable mbp-t1-notice.service
	run_script /usr/local/lib/mbp-t1/boot-entries
	run_script /usr/local/lib/mbp-t1/mbp-t1-switch check || true
	say "t1stack installed: active from the next boot; hotkey Meta+Ctrl+T after the next login"
}
remove_t1stack() {
	systemctl disable mbp-t1-switch.service 2>/dev/null || true
	systemctl --global disable mbp-t1-notice.service 2>/dev/null || true
	systemctl stop mbp-t1-fallback.service 2>/dev/null || true
	if [[ -x /usr/local/lib/mbp-t1/boot-entries ]]; then run_script /usr/local/lib/mbp-t1/boot-entries remove || true; fi
	rm -f /usr/local/bin/mbp-t1-fallback /usr/local/bin/mbp-t1-restart \
		/etc/systemd/system/mbp-t1-switch.service /etc/systemd/system/mbp-t1-cfg2.target \
		/etc/systemd/system/mbp-t1-fallback.service /etc/systemd/system/mbp-t1-giveup.service \
		/etc/systemd/system/tiny-dfr.service.d/mbp-t1.conf /etc/systemd/system/mbp-t1-camera.service.d/mbp-t1.conf \
		/etc/systemd/user/mbp-t1-notice.service /etc/udev/rules.d/91-mbp-t1-stack.rules \
		/etc/udev/rules.d/99-zz-mbp-t1-backlight.rules /etc/polkit-1/rules.d/50-mbp-t1-restart.rules \
		/usr/share/kglobalaccel/mbp-t1-restart.desktop /usr/share/applications/mbp-t1-restart.desktop \
		/etc/pacman.d/hooks/93-mbp-t1-boot-entries.hook
	rmdir /etc/systemd/system/tiny-dfr.service.d /etc/systemd/system/mbp-t1-camera.service.d 2>/dev/null || true
	rm -rf /usr/local/lib/mbp-t1 /var/lib/mbp-t1 /run/mbp-t1
	udevadm control --reload
	systemctl daemon-reload
	say "t1stack removed: configuration 1 from the next boot (tiny-dfr and the camera stay installed, not started)"
}

TOUCHID_UNITS=(t1-touchid-auth.socket t1bridge-fingerprint.socket)
install_touchid() {
	require_t1 "Touch ID"
	warn_other_model "Touch ID"
	dkms_has apple-t1-ncm || fail "first: ./install.sh t1ncm"
	[[ -e /etc/systemd/system/mbp-t1-switch.service ]] || fail "first: ./install.sh t1stack (Touch ID needs T1 configuration 2)"
	local p f pkgs=()
	for p in libfprint-t1bridge fprintd-t1bridge t1bridge-touchid; do
		f=$(newest_pkg "touchid/out/$p"-[0-9]*.pkg.tar.zst)
		[[ -n $f ]] || fail "no $p package in touchid/out: run touchid/build.sh first"
		pkgs+=("$f")
	done
	pacman -U --noconfirm "${pkgs[@]}"
	systemctl daemon-reload
	systemctl enable --now "${TOUCHID_UNITS[@]}"
	install -Dm755 touchid/mbp-touchid.sleep /usr/lib/systemd/system-sleep/mbp-touchid
	say "Touch ID services installed. Next (touchid/README.md):"
	say "  1. sudo ./touchid/import-fdrdata.sh   sensor calibration from this Mac's Apple EFI partition (read only)"
	say "  2. fprintd-enroll                     in the desktop session"
	say "  3. PAM: add pam_fprintd to /etc/pam.d/sudo by hand (the password stays as fallback)"
}
remove_touchid() {
	systemctl disable --now "${TOUCHID_UNITS[@]}" 2>/dev/null || true
	rm -f /usr/lib/systemd/system-sleep/mbp-touchid
	local p
	for p in t1bridge-touchid fprintd-t1bridge libfprint-t1bridge; do
		if command pacman -Q "$p" >/dev/null 2>&1; then pacman -R --noconfirm "$p"; fi
	done
	say "Touch ID removed; take pam_fprintd out of /etc/pam.d/* if you added it."
	say "/var/lib/t1bridge (sensor data, keybag) stays: delete it to forget everything."
}

# ---- main ---------------------------------------------------------------

MODULES="sound appletb spi-legacy t1display uvcvideo t1ncm thunderbolt base audio wifi lidwake touchbar tinydfr camera t1stack touchid pacman"

args=()
for a in "$@"; do
	case $a in
	-h|--help) usage; exit 0 ;;
	-n|--dry-run) DRY=1 ;;
	*) args+=("$a") ;;
	esac
done
[[ ${#args[@]} -gt 0 ]] || { usage >&2; exit 1; }
for a in "${args[@]}"; do
	m=${a%-remove}
	[[ $a == check || " $MODULES " == *" $m "* ]] || { echo "unknown module: $a (see --help)" >&2; exit 1; }
done
if (( DRY )); then dry_shims; say "dry run: nothing is changed"; fi

for a in "${args[@]}"; do
	if [[ $a == check ]]; then do_check; continue; fi
	need_root
	m=${a%-remove}; fn=${m//-/_}
	if [[ $a == *-remove ]]; then
		say "remove $m"; "remove_$fn"
	else
		say "install $m"; "install_$fn"
	fi
done
