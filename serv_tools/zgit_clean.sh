#!/bin/sh

git checkout --orphan temp $1
git commit -m "截取的历史记录起点"
git rebase --onto temp $1 master
git branch -D temp

git reflog expire --expire=now --all
git gc --aggressive --prune=all
