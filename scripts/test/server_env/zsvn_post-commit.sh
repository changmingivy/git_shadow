#!/bin/sh
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    export HOME="/home/git"  # git commit 需要据此搜索git config参数

    cd /home/git/sync_miaopai &&
    svn cleanup &&
    svn up &&

    git config user.name "git_shadow"
    git config user.email "_@_"

    git add --all . &&
    git commit --allow-empty -m "{REPO => $1} {REV => $2}" &&
    git push --force git@10.30.2.126:/home/git/miaopai/.git sync_git:server
