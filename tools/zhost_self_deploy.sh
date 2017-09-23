#!/bin/sh
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 入参是本机所有Ipv4地址，上层调用者必须已进入对应项目的 _SHADOW 目录

zProjId=`cat ./info/repo_id`
for zIpv4StrAddr in $@
do
    exec 777>/dev/tcp/__MASTER_ADDR/__MASTER_PORT
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpv4StrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done
