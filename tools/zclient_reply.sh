#!/usr/bin/env bash
zMasterAddr=$1
zMasterPort=$2
zMasterSig=$3
zReplyType=$4  # 'B' 用于标识这是布署状态回复，'A' 用于标识远程主机初始化状态回复
zProjId=$5
zHostId=$6

# 首先使用 notice 工具回复，之后使用 BASH 回复
./tools/notice "$zMasterAddr" "$zMasterPort" "8" "${zProjId}" "${zHostId}" "${zMasterSig}" "${zReplyType}"

# 关闭套接字读写端，防止先前已打开相同描述符
exec 777>&-
exec 777<&-
# 建立 TCP 连接
exec 777>/dev/tcp/${zMasterAddr}/${zMasterPort}

# 发送正文
printf "[{\"OpsId\":8,\"ProjId\":${zProjId},\"HostId\":${zHostId},\"data\":${zMasterSig},\"ExtraData\":${zReplyType}}]">&777

# 关闭读写端
#exec 777<&-
exec 777>&-
