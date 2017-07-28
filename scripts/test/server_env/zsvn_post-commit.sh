#!/bin/sh
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    zSvnServPath="/home/git/svn_miaopai"  #Subversion repo to receive code from remote developers

    cd $zSyncPath &&
    svn cleanup &&
    svn up &&

    export HOME="/home/git"  # git commit 需要据此搜索git config参数
    git add --all . &&
    git commit --allow-empty -m "{REPO => $1} {REV => $2}" &&
    git push --force ${zDeployPath}/.git sync_git:server
