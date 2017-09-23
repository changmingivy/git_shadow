#!/bin/sh
zPathOnHost=$(printf $1 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zMajorAddr=$2  # 中转机IPv4地址

shift 2
zHostList=$@
zShadowPath=/home/git/zgit_shadow

# 清理中转机项目文件及元文件
ssh $zMajorAddr "rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git"
if [[ 0 -ne $? ]]; then exit 255; fi

# 清理目标集群各主机的项目文件及元文件
for zSlaveAddr in $zHostList
do
    ssh -t $zMajorAddr "ssh $zSlaveAddr \"rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git\"" &
done
