#!/usr/bin/env bash
zSelfIpv4List=`ip addr | grep -oP '(\d{1,3}\.){3}\d{1,3}(?=/\d+)' | grep -vE '^(127|169|0)\.'`

for zProjMetaPath in `find /home/git/.____DpSystem -maxdepth 1 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zProjMetaPath
    bash ./tools/zhost_self_deploy.sh $zSelfIpv4List
done
