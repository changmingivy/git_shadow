#!/usr/bin/env bash
zProjId=$1
zProxyHostAddr=$2
zPathOnHost=$(printf $3 | sed -n 's%/\+%/%p')

zServBranchName="server${zProjId}"

zFinMarkFilePath=/home/git/.____FinMark.$$  # 以 <自身进程号> 命名保证名称唯一
rm -f $zFinMarkFilePath
touch $zFinMarkFilePath

# 清理中控机本地进程
kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -E "[^0-9]${zProjId}[^0-9]" | grep -oP "^\s*\d+(?=\s.*${zProxyHostAddr}.*${zHostList})" | grep -v "$$" | tr '\n' ' '`

(
    ssh $zProxyHostAddr "
        kill -9 \`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \"^\s*\d+(?=\s.*${zServBranchName})\" | grep -v \"\$$\" | tr '\n' ' '\`
        rm -rf ${zPathOnHost}/.git
        rm -rf ${zPathOnHost}_SHADOW/.git
\
        rm -f ${zPathOnHost}_SHADOW
        rm -f ${zPathOnHost}
\
        mkdir -p ${zPathOnHost}
        mkdir -p ${zPathOnHost}_SHADOW
\
        rm -f ${zPathOnHost}/.git/index.lock
        rm -f ${zPathOnHost}_SHADOW/.git/index.lock
\
        cd $zPathOnHost
        git init .
        git config user.name "git_shadow"
        git config user.email "git_shadow@$x"
        git commit --allow-empty -m "__init__"
        git branch -f ${zServBranchName}
\
        cd ${zPathOnHost}_SHADOW
        git init .
        git config user.name "MajorHost"
        git config user.email "MajorHost@$x"
        git commit --allow-empty -m "__init__"
        git branch -f ${zServBranchName}
        "

        if [[ (0 -eq $?) && (1 -eq `ls ${zFinMarkFilePath} | wc -l`) ]]; then
            printf "Success" > $zFinMarkFilePath
        fi
) &

# 防止遇到无效IP时，长时间阻塞
(
    sleep 6
    if [[ 1 -eq `ls ${zFinMarkFilePath} | wc -l` ]]; then
        printf "Fail" > $zFinMarkFilePath
    fi
) &

while :
do
    sleep 0.2

    if [[ "Success" == `cat ${zFinMarkFilePath}` ]]; then
        rm -f $zFinMarkFilePath
        exit 0
    elif [[ "Fail" == `cat ${zFinMarkFilePath}` ]]; then
        rm -f $zFinMarkFilePath
        exit 255  # 若失败，则以退出码 255 结束进程
    fi
done
