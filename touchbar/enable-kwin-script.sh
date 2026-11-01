#!/bin/bash
# As the desktop user: turn the KWin script on (default) or off.
set -euo pipefail
on=true; [[ ${1:-on} == off ]] && on=false
kwriteconfig6 --file kwinrc --group Plugins --key mbp-touchbar-fnappsEnabled "$on"
dbus-send --session --type=method_call --dest=org.kde.KWin /KWin org.kde.KWin.reconfigure
if [[ $on == false ]]; then
	dbus-send --session --type=method_call --dest=org.kde.KWin /Scripting \
		org.kde.kwin.Scripting.unloadScript string:mbp-touchbar-fnapps >/dev/null || true
	/usr/local/bin/mbp-touchbar mode media 2>/dev/null || true
fi
echo "KWin script mbp-touchbar-fnapps: $on"
