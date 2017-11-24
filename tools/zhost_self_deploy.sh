#!/usr/bin/env bash
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 入参是本机所有Ip地址，上层调用者必须已进入对应项目的 _SHADOW 目录

zSelfIpList=$1
zMasterIpAddr=__MASTER_ADDR
zMasterPort=__MASTER_PORT

zCurPath=`pwd`
zProjPath=`echo ${zCurPath} | sed -n 's/_SHADOW$//p'`

cd $zProjPath
zProjId=`git branch | grep 'server[0-9]\+$' | grep -o '[0-9]\+$' | head -1`

(
while :
do
    zLocalSig=`git log -1 --format=%H`  # 必须放在循环中，取实时的最新版本号发送给布署系统

    for zIpStrAddr in `echo ${zSelfIpList}`
    do
        # exec 775>/dev/tcp/${zMasterIpAddr}/${zMasterPort}
        # printf "{\"OpsId\":13,\"ProjId\":${zProjId},\"HostAddr\":\"${zIpStrAddr}\",\"RevSig\":\"${zLocalSig}\"}">&775
        # exec 775>&-
    done
    sleep 60
done
) &
