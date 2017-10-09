#!/usr/bin/env bash
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 入参是本机所有Ip地址，上层调用者必须已进入对应项目的 _SHADOW 目录

zProjId=`cat ./info/repo_id`
zSelfIpList=$1
zMasterIpAddr=__MASTER_ADDR
zMasterPort=__MASTER_PORT

zCurPath=`pwd`
zProjPath=`echo ${zCurPath} | sed -n 's/_SHADOW$//p'`

cd $zProjPath
zLocalSig=`git log -1 --format=%H`
cd $zCurPath

for zIpStrAddr in `echo ${zSelfIpList}`
do
    exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpStrAddr},\"ExtraData\":${zLocalSig}}]">&777
    exec 777>&-
done

for zIpStrAddr in `echo ${zSelfIpList}`
do
    exec 777>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
    printf "[{\"OpsId\":13,\"ProjId\":${zProjId},\"data\":${zIpStrAddr},\"ExtraData\":${zLocalSig}}]">&777
    exec 777>&-
done
