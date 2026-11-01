#!/bin/bash
# Microphone, step A (measure, change nothing): for each slider position
# (KDE %, pactl) record the internal microphone 11 s - 3 s quiet (the click
# at the start of a recording, the noise floor), then a notification "speak"
# and 8 s of the phrase - and note what the codec really has (0x22 capture,
# 0x44 boost) while it records. The slider is put back at the end.
#   mic-a.sh <out dir> [levels...]     (default 50 75 100)
set -euo pipefail
out=$1; shift
levels=("${@:-50 75 100}"); [[ $# -eq 0 ]] && levels=(50 75 100)
uid=$(id -u)
export XDG_RUNTIME_DIR=/run/user/$uid DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus
src=alsa_input.pci-0000_00_1f.3.analog-stereo
mkdir -p "$out"
before=$(pactl get-source-volume "$src" | grep -o '[0-9]*%' | head -1)
trap 'pactl set-source-volume "$src" "$before"' EXIT
codec() { awk "/^Node $1 /,/^Node $2 /" /proc/asound/card0/codec#0 | grep 'Amp-In vals' | head -1 | tr -s ' '; }
for l in "${levels[@]}"; do
	pactl set-source-volume "$src" "$l%"
	notify-send -u low -t 2500 "Мікрофон $l %" "Тиша 3 секунди…"
	sleep 4   # the notification's own sound, if any, is over
	pw-record --target "$src" --rate 44100 --channels 2 --format s32 "$out/mic$l.wav" &
	rec=$!
	sleep 3
	notify-send -u critical -t 7000 "Говори зараз ($l %)" "Скажи фразу двічі, з паузою"
	sleep 2
	{ echo "level $l%"; echo "0x22 $(codec 0x22 0x23)"; echo "0x44 $(codec 0x44 0x45)"; } >> "$out/codec.txt"
	pw-dump 2>/dev/null | python3 -c '
import json,sys
for o in json.load(sys.stdin):
    p=o.get("info",{}).get("params",{})
    for r in p.get("Route",[]):
        if r.get("name")=="analog-input-internal-mic":
            print("route softVolumes", r["props"].get("softVolumes"), "channelVolumes", r["props"].get("channelVolumes"))
' >> "$out/codec.txt" || true
	sleep 6
	kill -INT $rec; wait $rec 2>/dev/null || true
	notify-send -u low -t 2000 "Мікрофон $l %" "Готово"
	sleep 3
done
echo "slider back to $before"
