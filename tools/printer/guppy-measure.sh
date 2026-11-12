#!/bin/sh
# usage: measure.sh <label>  -- restart guppyscreen via zMod's init script, then sample memory
L=$1
chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1
sleep 90
for i in 1 2 3; do
  P=$(pidof guppyscreen)
  [ -z "$P" ] && { echo "$L: guppyscreen NOT RUNNING"; exit 1; }
  R=$(awk '/^Rss:/{r=$2} /^Pss:/{p=$2} /^Anonymous:/{a=$2} /^Private_Dirty:/{d=$2} /^Private_Clean:/{c=$2} /^Shared_Clean:/{s=$2} END{printf "Rss=%s Pss=%s Anon=%s PrivDirty=%s PrivClean=%s SharedClean=%s", r,p,a,d,c,s}' /proc/$P/smaps_rollup)
  H=$(awk '/VmHWM|VmSize|VmData|Threads/{printf "%s%s ", $1, $2}' /proc/$P/status)
  echo "$L sample$i pid=$P $R $H"
  [ $i -lt 3 ] && sleep 20
done
grep -m1 "Version" /opt/config/mod_data/log/guppyscreen.log
grep -ciE "error|exception" /opt/config/mod_data/log/guppyscreen.log | sed "s/^/$L error-lines-in-log: /"
