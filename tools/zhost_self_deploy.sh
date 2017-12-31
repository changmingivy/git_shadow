#!/usr/bin/env bash
# 主机开机启动时，自动向中控机请求同步最新的已布署版本号
# 上层调用者必须已进入对应项目的 _SHADOW 目录

##-- [分支格式]：meta@${zMasterAddr}@${zMasterPort}@${zRepoId}@${zSelfIpStrAddr} --##

zTcpSend() {  # bash tcp fd: 3
    exec 3<>/dev/tcp/${1}/${2}
    printf "${3}">&3
    exec 3<&-
    exec 3>&-
}

zRepoPath=`\`pwd\` | sed -n 's/_SHADOW$//p'`

(
while :
do
    cd ${zRepoPath}
    zServBranch=`git branch | grep -m 1 'meta@'`

    zRepoId=`echo ${zServBranch} | awk -F@ '{print $4}'`
    zMasterPort=`echo ${zServBranch} | awk -F@ '{print $3}'`

    # 传输过程中IPv6 地址中的冒号以 '_' 进行了替换，此处需要还原
    zMasterAddr=`echo ${zServBranch} | awk -F@ '{print $2}' | sed 's/_/:/g'`
    zSelfIp=`echo ${zServBranch} | awk -F@ '{print $5}' | sed 's/_/:/g'`

    zLocalSig=`git log -1 --format=%H`

    zTcpSend "${zMasterIpAddr}" "${zMasterPort}" \
        "{\"OpsId\":13,\"RepoId\":${zRepoId},\"HostAddr\":\"${zIpStrAddr}\",\"RevSig\":\"${zLocalSig}\"}"

    sleep 60
done
) &
