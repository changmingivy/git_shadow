#!/bin/sh
zSelfIpv4List=`ip addr | grep -oP '(\d{1,3}\.){3}\d{1,3}(?=/\d+)' | grep -vE '^(127|169|0)\.'`

for zProjMetaPath in `find /home/git -maxdepth 1 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zProjMetaPath
    sh ./tools/zhost_self_deploy.sh $zSelfIpv4List
done
