#!/bin/sh
# usage: guppy-latency.sh LABEL -- restart guppyscreen, wait 90 s, then 5 x (tap Tune tab, tap Home tab):
# ms from the start of the tap command until the tab-bar pixel shows the tab selected.
# Only the Home (30,48) and Tune (30,140) tabs are tapped (see ui_scenario.py SAFE_TAPS); needs /tmp/guppy-tap.
# Times come from /proc/uptime (10 ms resolution). Includes guppy-tap's own touch duration.
chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1
sleep 90
px() { dd if=/dev/fb0 bs=4 skip=$(($2 * 800 + $1)) count=1 2>/dev/null | od -An -tx1 | tr -d ' \n'; }
ON=46423fff
ms() { awk '{print int($1 * 1000)}' /proc/uptime; }
wait_on() { t0=$1; while [ "$(px 5 $2)" != "$ON" ]; do :; done; echo $(( $(ms) - t0 )); }
[ "$(px 5 48)" = "$ON" ] || { echo "$1: main screen not showing"; exit 1; }
for i in 1 2 3 4 5; do
  t=$(ms); /tmp/guppy-tap 30 140; a=$(wait_on $t 140)
  sleep 1
  t=$(ms); /tmp/guppy-tap 30 48; b=$(wait_on $t 48)
  echo "$1 tune ${a} ms, home ${b} ms"
  sleep 1
done
