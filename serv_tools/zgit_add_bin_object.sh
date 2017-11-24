#!/bin/sh

git checkout master
if [[ 0 -ne $? ]]; then
    git init .
    git config user.name "_"
    git config user.email "_@_"

    git checkout master
    if [[ 0 -ne $? ]]; then
        printf "\033[31;01mFATAL!!!\033[00m  Git branch 'master' is invalid.\n"
        exit 255
    fi
fi

git add --all .
git commit -m "`\ls -lh`"

# 若 .git 占用空间超过 200M，则执行清理，仅保留最近的 10 次提交
if [[ 200 -lt `du -sm .git | grep -o '[0-9]\+'` ]]; then
    zBaseRef=`git log --format=%H | head -10 | tail -1`

    git checkout --orphan temp ${zBaseRef}
    git commit -m "clean old refs"
    git rebase --onto temp ${zBaseRef} master
    git branch -D temp

    git reflog expire --expire=now --all
    git gc --aggressive --prune=all
fi
