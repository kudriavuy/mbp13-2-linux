#!/bin/bash
# CPU load over N seconds (default 30): whole system and per browser
# (chrome, firefox), in % of one core; 400 % = all 4 threads.
#   cpuload.sh [seconds]
T=${1:-30}
HZ=$(getconf CLK_TCK)
snap() {
	local n p
	for p in /proc/[0-9]*; do
		n=$(tr -d '\0' < "$p/comm" 2>/dev/null) || continue
		case $n in chrome|firefox|Isolated*|Web\ Content|RDD\ Process|GPU\ Process|Socket\ Process|Utility\ Process|WebExtensions|Privileged\ Cont|kwin_wayland|pipewire*) ;;
		*) continue ;;
		esac
		# comm may hold spaces: the fields after ")" start at state
		local rest
		rest=$(sed 's/.*) //' "$p/stat" 2>/dev/null) || continue
		set -- $rest
		echo "$(basename "$p") $n $((${12} + ${13}))"
	done
}
grp() { case $1 in chrome) echo chrome ;; kwin_wayland) echo kwin ;; pipewire*) echo pipewire ;; *) echo firefox ;; esac; }
a=$(snap); c1=$(awk '/^cpu /{print $2+$3+$4+$6+$7+$8, $5}' /proc/stat)
sleep "$T"
b=$(snap); c2=$(awk '/^cpu /{print $2+$3+$4+$6+$7+$8, $5}' /proc/stat)
read -r u1 i1 <<<"$c1"; read -r u2 i2 <<<"$c2"
echo "system: $(( 400 * (u2 - u1) / (u2 - u1 + i2 - i1) )) % of 400 over ${T}s"
join -j1 <(echo "$a" | awk '{print $1, $NF}' | sort) <(echo "$b" | awk '{print $1, $0}' | sort) |
while read -r _ t1 _ rest; do
	t2=${rest##* }; n=${rest% *}
	echo "$(grp "$n") $(( (t2 - t1) ))"
done | awk -v hz="$HZ" -v t="$T" '{s[$1]+=$2} END {for (k in s) printf "%-9s %5.1f %%\n", k, 100*s[k]/hz/t}' | sort
for z in /sys/class/thermal/thermal_zone*; do
	[[ $(cat "$z/type") == x86_pkg_temp ]] && echo "package $(( $(cat "$z/temp") / 1000 )) °C"
done
true
