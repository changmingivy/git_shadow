#!/usr/bin/env bash

for zRepoMetaPath in `find ${HOME}/.____DpSystem -maxdepth 2 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zRepoMetaPath && bash ./tools/zhost_self_deploy.sh &
done
