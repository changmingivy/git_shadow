#!/usr/bin/env sh
zBridge=br0

ip link set $1 up
sleep 0.1
ip link set $1 master $zBridge
