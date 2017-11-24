#!/usr/bin/env bash

zIpV4List=`ip addr | grep -oP '(?<=inet\s)(\w|\.|:)+(?=(/|\s))' | grep -vE '^(127|169|0)\.'`
zIpV6List=`ip addr | grep -oP '(?<=inet6\s)(\w|\.|:)+(?=(/|\s))' | grep -viE '(^::1$)|(^fe80::)'`


for zProjMetaPath in `find /home/git/.____DpSystem -maxdepth 2 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zProjMetaPath
    bash ./tools/zhost_self_deploy.sh "${zIpV4List} ${zIpV6List}" &
done
