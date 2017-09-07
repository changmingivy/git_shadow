#!/bin/sh
zMasterAddr=$1
zMasterPort=$2
zReplyType=$3  # 'B' 用于标识这是布署状态回复，'A' 用于标识远程主机初始化状态回复

zRelativeRepoIdPath="./info/repo_id"

for zIpv4StrAddr in `ip addr | grep -oP '(\d{1,3}\.){3}\d{1,3}(?=/\d+)' | grep -vE '^(127|169|0)\.'`
do
    # 将点分格式的 IPv4 地址转换为数字格式
    zIPv4NumAddr=0
    zCnter=0
    for zField in `echo ${zIpv4StrAddr} | grep -oP '\d+'`
    do
        let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
        let zCnter++
    done
    
    # 首先使用 notice 工具回复，之后使用 BASH 回复
    ./tools/notice "$zMasterAddr" "$zMasterPort" "8" "`cat ${zRelativeRepoIdPath}`" "$zIPv4NumAddr" "B"
    
    # 关闭套接字读写端，防止先前已打开相同描述符
    #exec 777>&-
    #exec 777<&-
    # 建立 TCP 连接
    exec 777>/dev/tcp/${zMasterAddr}/${zMasterPort}
    
    # 发送正文
    echo "[{\"OpsId\":8,\"ProjId\":`cat ${zRelativeRepoIdPath}`,\"HostId\":${zIPv4NumAddr},\"ExtraData\":\"${zReplyType}\"}]">&777
    
    # 关闭读写端
    #exec 777<&-
    exec 777>&-
done
