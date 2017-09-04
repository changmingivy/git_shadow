#!/bin/sh
# 新机器开机时，自动从中控机上拉取最新版的已布署版本代码

for zProjPath in `find /home/git -maxdepth 1 -type d | grep -oP '.+(?=_SHADOW$)'`
do
    (
        cd $zProjPath
        git add --all .
        git commit -m "_"
        git pull git@192.168.210.59:/home/git/${zProjPath}/.git CURRENT:server
        git checkout server
        git checkout -b TMP
        git reset -q --hard
        git branch -M master
    ) &
done
