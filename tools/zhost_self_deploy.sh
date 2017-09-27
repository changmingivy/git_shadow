#!/usr/bin/env bash
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 入参是本机所有Ipv4地址，上层调用者必须已进入对应项目的 _SHADOW 目录

zProjId=`cat ./info/repo_id`
zProxyIpv4Addr=$1
zSelfIpv4List=$2
zMasterIpv4Addr=__MASTER_ADDR
zMasterPort=__MASTER_PORT

exec 777>/dev/tcp/${zMasterIpv4Addr}/${zMasterPort}
printf "[{\"OpsId\":4,\"ProjId\":${zProjId},\"data\":${zProxyIpv4Addr},\"ExtraData\":1}]">&777
exec 777>&-

exec 777>/dev/tcp/${zMasterIpv4Addr}/${zMasterPort}
printf "[{\"OpsId\":4,\"ProjId\":${zProjId},\"data\":${zProxyIpv4Addr},\"ExtraData\":1}]">&777
exec 777>&-

for zIpv4StrAddr in `echo ${zSelfIpv4List}`
do
    exec 777>/dev/tcp/${zMasterIpv4Addr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpv4StrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done

for zIpv4StrAddr in `echo ${zSelfIpv4List}`
do
    exec 777>/dev/tcp/${zMasterIpv4Addr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpv4StrAddr},\"ExtraData\":1}]">&777
    exec 777>&-
done
