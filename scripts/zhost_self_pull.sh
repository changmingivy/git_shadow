#!/bin/sh

for zProjPath in `find /home/git -maxdepth 1 -type d | grep -oP '.+(?=_SHADOW$)'`
do
    (
        cd $zProjPath
        git add --all .
        git commit -m "_"
        git pull git@192.168.11.59:/home/git/${zProjPath}/.git CURRENT:server
        git checkout server
        git checkout -b TMP
        git reset -q --hard
        git branch -M master
    ) &
done
