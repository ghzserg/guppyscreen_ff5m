#!/bin/sh
# usage: guppy-reconnect.sh <label> [n]  -- restart guppyscreen, sample at 90 s, then n Moonraker restarts, sample 60 s after each
L=$1; N=${2:-3}
q() { wget -qO- "http://127.0.0.1:7125/printer/objects/query?print_stats=state" | grep -q '"standby"\|"complete"\|"cancelled"'; }
q || { echo "$L: printer busy"; exit 1; }
chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1
sleep 90
P=$(pidof guppyscreen)
echo "$L start Anon=$(awk '/^Anonymous:/{print $2}' /proc/$P/smaps_rollup)"
i=1
while [ $i -le $N ]; do
  q || { echo "$L: printer busy"; exit 1; }
  wget -qO- --post-data='' http://127.0.0.1:7125/server/restart >/dev/null
  sleep 60
  P2=$(pidof guppyscreen)
  [ "$P2" = "$P" ] || echo "$L: guppyscreen pid changed $P -> $P2"
  echo "$L restart$i Anon=$(awk '/^Anonymous:/{print $2}' /proc/$P2/smaps_rollup) klippy=$(wget -qO- http://127.0.0.1:7125/server/info | grep -o '"klippy_state":"[a-z]*"')"
  i=$((i+1))
done
