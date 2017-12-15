#!/usr/bin/env bash

for zProjMetaPath in `find ${HOME}/.____DpSystem -maxdepth 2 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zProjMetaPath && bash ./tools/zhost_self_deploy.sh &
done
