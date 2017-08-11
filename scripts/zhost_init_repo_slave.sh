#!/bin/sh
zSlaveAddr=$1
zPathOnHost=$2

ssh $zSlaveAddr "
    if [[ 0 -ne \`ls -d $zPathOnHost 2>/dev/null | wc -l\` ]];then exit; fi &&
    mkdir -p ${zPathOnHost} &&
    mkdir -p ${zPathOnHost}_SHADOW &&
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
    git config user.name "`basename $zPathOnHost`" &&
    git config user.email "`basename ${zPathOnHost}`@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server
    " &&

scp ${zPathOnHost}/.git/hooks/post-update git@${zSlaveAddr}:${zPathOnHost}/.git/hooks/post-update &&
ssh $zSlaveAddr " chmod 0755 ${zPathOnHost}/.git/hooks/post-update "
