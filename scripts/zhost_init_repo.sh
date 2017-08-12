#!/bin/sh
zMajorAddr=$1
zPathOnHost=$2

eval sed -i 's%__PROJ_PATHR%${zPathOnHost}%g' ./post-update_slave

ssh $zMajorAddr "
    mkdir -p ${zPathOnHost}_SHADOW &&
\
    cd ${zPathOnHost}_SHADOW &&
    git init . &&
    git config user.name "git_shadow" &&
    git config user.email "git_shadow@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server &&
    "
