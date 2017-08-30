#!/bin/sh
zMasterAddr=$1
zMasterPort=$2

zRelativeRepoIdPath="./info/repo_id"
zIPv4StrAddrPath="/home/git/zself_ipv4_addr.txt"

# 将点分格式的 IPv4 地址转换为数字格式
zIPv4NumAddr=0
zCnter=0
for zField in `cat ${zIPv4StrAddrPath} | grep -oP '\d+'`
do
    let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
    let zCnter++
done

# 关闭套接字读写端，防止先前已打开相同描述符
exec 777>&-
exec 777<&-
# 建立 TCP 连接
exec 777>/dev/tcp/${zMasterAddr}/${zMasterPort}

# 发送正文
echo "[{\"OpsId\":8,\"ProjId\":`cat ${zRelativeRepoIdPath}`,\"HostId\":${zIPv4NumAddr}}]">&777

# 关闭套接字读写端
exec 777<&-
exec 777>&-
