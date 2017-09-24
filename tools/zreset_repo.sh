#!/bin/sh
zProjId=$1
zPathOnHost=$(printf $2 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zProxyHostAddr=$3  # 中转机IPv4地址
zHostList=$4

zServBranchName="server${zProjId}"
zShadowPath=/home/git/zgit_shadow

# kill 掉可能存在的本地僵死进程
kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -E "[^0-9]${zProjId}[^0-9]" | grep -oP "\d+(?=\s.*${zProxyHostAddr}.*${zHostList})" | grep -v "$$" | tr '\n' ' '`

# 清理中转机的项目文件、元文件及相关进程
ssh $zProxyHostAddr "\
    kill -9 \`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \"\d+(?=\s.*${zServBranchName})\" | grep -v \"\$$\" | tr '\n' ' '\`;\
    rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git\
    "
if [[ 0 -ne $? ]]; then exit 255; fi

# 清理目标集群各主机的项目文件、元文件及相关进程
ssh $zProxyHostAddr "
    for zSlaveAddr in $zHostList
    do
        (\
            ssh \$zSlaveAddr \"\
            kill -9 \\\`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \\\"\d+(?=\s.*${zServBranchName})\\\" | grep -v \\\"\\\$$\\\" | tr '\n' ' '\\\`;\
            rm -rf ${zPathOnHost}/.git ${zPathOnHost}_SHADOW/.git\
            \"\
        ) &
    done
"
