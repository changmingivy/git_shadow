#!/bin/sh
zMajorAddr=$1
zSlaveAddr=$2
zPathOnHost=$3

ssh -t $zMajorAddr "ssh $zSlaveAddr \"
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
    git config user.name "_" &&
    git config user.email "_@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server &&
\
	cat > .git/hooks/post-update &&
	chmod 0755 .git/hooks/post-update
    \"" < /home/git/${zPathOnHost}_SHADOW/scripts/post-update_slave
