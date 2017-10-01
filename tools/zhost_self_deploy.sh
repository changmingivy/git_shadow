#!/usr/bin/env bash
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 入参是本机所有Ip地址，上层调用者必须已进入对应项目的 _SHADOW 目录

zProjId=`cat ./info/repo_id`
zProxyIpAddr=$1
zSelfIpList=$2
zMasterIpAddr=__MASTER_ADDR
zMasterPort=__MASTER_PORT

exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
printf "[{\"OpsId\":4,\"ProjId\":${zProjId},\"data\":${zProxyIpAddr},\"ExtraData\":1}]">&777
exec 777>&-

exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
printf "[{\"OpsId\":4,\"ProjId\":${zProjId},\"data\":${zProxyIpAddr},\"ExtraData\":1}]">&777
exec 777>&-

for zIpStrAddr in `echo ${zSelfIpList}`
do
    exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpStrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done

for zIpStrAddr in `echo ${zSelfIpList}`
do
    exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpStrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done
