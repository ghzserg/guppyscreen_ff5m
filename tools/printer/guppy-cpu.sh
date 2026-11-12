#!/bin/sh
# usage: guppy-cpu.sh LABEL -- restart guppyscreen, wait 90 s, then CPU ticks (utime+stime, 100/s) over 3 x 60 s
chroot /data/.mod/.zmod /etc/init.d/S80guppyscreen restart >/dev/null 2>&1
sleep 90
P=$(pidof guppyscreen)
for i in 1 2 3; do a=$(awk '{print $14+$15}' /proc/$P/stat); sleep 60; b=$(awk '{print $14+$15}' /proc/$P/stat); echo "$1 ticks/60s $((b-a))"; done
