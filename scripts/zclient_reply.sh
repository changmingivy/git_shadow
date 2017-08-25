#!/bin/sh
zMasterAddr=$1
zMasterPort=$2

zRelativeRepoIdPath="./info/repo_id"
zIPv4StrAddrPath="/home/git/zself_ipv4_addr.txt"

# 将点分格式的 IPv4 地址转换为数字格式
zIPv4NumAddr=0
zCnter=4
for zField in `cat ${zIPv4StrAddrPath} | grep -oP '\d+'`
do
    let zCnter--
    let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
done

# 建立 TCP 连接
zSd=65535
exec ${zSd}<>/dev/tcp/${zMasterAddr}/${zMasterPort}
while [[ 0 -ne $? ]]; do
    let zSd--
    if [[ 2 -ge $zSd ]]; then exit 1; fi
    exec ${zSd}<>/dev/tcp/${zMasterAddr}/${zMasterPort}
done

# 发送正文
echo "{\"OpsId\":8,\"ProjId\":`cat ${zRelativeRepoIdPath}`,\"HostId\":${zIPv4NumAddr}}" >&"$zSd"

# exec ${zSd}<&- # 关闭读连接
# exec ${zSd}>&- # 关闭写连接
