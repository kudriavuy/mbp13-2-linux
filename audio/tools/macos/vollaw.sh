#!/bin/bash
# vollaw.sh: run in macOS (bash, no compiler). For slider 0..100 % sets the
# output volume with osascript and reads the raw value (0..127 = -63.5..0
# dB, IOAudioLevelControlMin/MaxDB) of the AppleHDA output level controls
# (usage 'outp') from ioreg. Restores the volume at the end.
# Output: "percent left right".
old=$(osascript -e 'output volume of (get volume settings)')
for v in $(seq 0 1 100); do
	osascript -e "set volume output volume $v"
	sleep 0.25
	echo "$v $(ioreg -r -c IOAudioLevelControl -l -w0 | awk '
		/IOAudioControlValue/ { val = $NF }
		/IOAudioControlChannelID/ { ch = $NF }
		/IOAudioControlUsage/ { if ($NF == 1869968496) r[ch] = val }
		END { print r[1], r[2] }')"
done
osascript -e "set volume output volume $old"
