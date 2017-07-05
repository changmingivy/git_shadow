#!/usr/bin/env sh
zBridgeIf=bridge0

ifconfig $1 up
sleep 0.1
ifconfig $zBridgeIf addm  $1
