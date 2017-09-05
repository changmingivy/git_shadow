#!/bin/sh
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号

zRelativeRepoIdPath="./info/repo_id"
zHostIPv4StrAddr="cat /home/git/zself_ipv4_addr.txt"

for zProjMetaPath in `find /home/git -maxdepth 1 -type d | grep -E '^.+_SHADOW$'`
do
    cd $zProjMetaPath
    exec 777>/dev/tcp/192.168.210.59/20000
    echo "[{\"OpsId\":13,\"ProjId\":`cat ${zRelativeRepoIdPath}`,\"data\":${zHostIPv4StrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done
