#!/bin/sh
zMajorAddr=$1
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')

zFinMarkFilePath=/home/git/.____fifo.$$  # 以 <自身进程号> 命名保证名称唯一
rm $zFinMarkFilePath
touch $zFinMarkFilePath

(
    ssh $zMajorAddr "
        rm ${zPathOnHost}_SHADOW
        rm ${zPathOnHost}
\
        mkdir -p ${zPathOnHost} &&
        mkdir -p ${zPathOnHost}_SHADOW &&
\
        rm -f ${zPathOnHost}/.git/index.lock
        rm -f ${zPathOnHost}_SHADOW/.git/index.lock
\
        cd $zPathOnHost &&
        git init . &&
        git config user.name "git_shadow" &&
        git config user.email "git_shadow@$x" &&
        git commit --allow-empty -m "__init__" &&
        git branch -f server &&
\
        cd ${zPathOnHost}_SHADOW &&
        git init . &&
        git config user.name "MajorHost" &&
        git config user.email "MajorHost@$x" &&
        git commit --allow-empty -m "__init__" &&
        git branch -f server
        "

        if [[ (0 -eq $?) && (1 -eq `ls ${zFinMarkFilePath} | wc -l`) ]]; then
            echo "Success" > $zFinMarkFilePath
        fi
) &

# 防止遇到无效IP时，长时间阻塞
(
    sleep 6
    if [[ 1 -eq `ls ${zFinMarkFilePath} | wc -l` ]]; then
        echo "Fail" > $zFinMarkFilePath
    fi
) &

while :
do
    sleep 0.2

    if [[ "Success" == `cat ${zFinMarkFilePath}` ]]; then
        rm $zFinMarkFilePath
        exit 0
    elif [[ "Fail" == `cat ${zFinMarkFilePath}` ]]; then
        rm $zFinMarkFilePath
        exit 255  # 若失败，则以退出码 255 结束进程
    fi
done
