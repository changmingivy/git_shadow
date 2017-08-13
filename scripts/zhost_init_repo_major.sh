#!/bin/sh
zMajorAddr=$1
zPathOnHost=$2

eval sed -i 's%__PROJ_PATHR%${zPathOnHost}%g' /home/git/${zPathOnHost}_SHADOW/post-update

ssh $zMajorAddr "
    mkdir -p ${zPathOnHost}_SHADOW &&
    mkdir -p ${zPathOnHost} &&
\
    cd ${zPathOnHost}_SHADOW &&
    git init . &&
    git config user.name "git_shadow" &&
    git config user.email "git_shadow@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server &&
\
    cd $zPathOnHost &&
    git init . &&
    git config user.name "MajorHost" &&
    git config user.email "MajorHost@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server
    "
