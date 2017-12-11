#!/bin/bash

# which branch to clean...
zBranchName=$1

git stash
git stash clear
echo $0 > .gitignore

git checkout master
if [[ 0 -ne $? ]]; then
    git init .
    git config user.name "_"
    git config user.email "_@_"

    git branch master
    git checkout master
    if [[ 0 -ne $? ]]; then
        printf "\033[31;01m[`date '+%F %H:%M:%S'`] FATAL!!!\033[00m  Git branch 'master' is invalid.\n"
        exit 255
    fi
fi

# 记录每个版本编译后生成的最终文件的详细信息
find . -type d | xargs ls -gGA --time-style=long-iso > ____version____

git add --all .
git commit -m "_"

# 若 .git 占用空间超过 50M，则执行清理，仅保留最近的 10 次提交
if [[ 50 -lt `du -sm .git | grep -o '[0-9]\+'` ]]; then
    zBaseRef=`git log --format=%H | head -10 | tail -1`

    git checkout --orphan temp ${zBaseRef}
    git commit -m "clean old refs"
    git rebase --onto temp ${zBaseRef} ${zBranchName}
    git branch -D temp

    git reflog expire --expire=now --all
    git gc --aggressive --prune=all
fi
