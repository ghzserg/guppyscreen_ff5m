#!/bin/sh
# usage: guppy-smaps.sh <label> -- restart guppyscreen, wait 90 s, print Anon total and each anonymous mapping with Rss > 0
L=$1
chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1
sleep 90
P=$(pidof guppyscreen)
[ -z "$P" ] && { echo "$L: NOT RUNNING"; exit 1; }
A=$(awk '/^Anonymous:/{print $2}' /proc/$P/smaps_rollup)
echo "$L pid=$P Anon=$A"
awk -v L="$L" '
/^[0-9a-f]+-[0-9a-f]+ / { range=$1; name=$6; next }
/^Size:/ { size=$2 }
/^Rss:/ { rss=$2 }
/^Anonymous:/ { if ($2 > 0) printf "%s  %-20s size=%6d rss=%6d anon=%6d %s\n", L, range, size, rss, $2, name }
' /proc/$P/smaps
