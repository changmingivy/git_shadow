#!/bin/sh
zProjId=$1
zPathOnHost=$(printf $2 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zProxyHostAddr=$3  # 中转机IPv4地址
zHostList=$4

zServBranchName="server${zProjId}"
zShadowPath=/home/git/zgit_shadow

# 清理中转机的项目文件、元文件及相关进程
ssh $zProxyHostAddr "kill -9 \`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \"\d+(?=\s.*${zServBranchName})\" | tr '\n' ' '\`; rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git"
if [[ 0 -ne $? ]]; then exit 255; fi

# 清理目标集群各主机的项目文件、元文件及相关进程
for zSlaveAddr in $zHostList
do
    ssh -t $zProxyHostAddr "ssh $zSlaveAddr \"kill -9 \\\`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \\\"\d+(?=\s.*${zServBranchName})\\\" | tr '\n' ' '\\\`; rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git\"" &
done
