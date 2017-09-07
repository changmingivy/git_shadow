#!/bin/sh
zMajorAddr=$1
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')

zPipePath=/tmp/____fifo.$$  # 以 fifo.<自身进程号> 命名保证管道名称唯一性
mkfifo -m 0700 $zPipePath

(
    ssh $zMajorAddr "
        mkdir -p ${zPathOnHost}_SHADOW &&
        mkdir -p ${zPathOnHost} &&
\
        cd ${zPathOnHost}_SHADOW &&
        git init . &&
        git config user.name "git_shadow" &&
        git config user.email "git_shadow@$x" &&
        git commit --allow-empty -m "__init__" &&
        git branch -f server &&
\
        cd $zPathOnHost &&
        git init . &&
        git config user.name "MajorHost" &&
        git config user.email "MajorHost@$x" &&
        git commit --allow-empty -m "__init__" &&
        git branch -f server
        "

        if [[ (0 -eq $?) && (1 -eq `ls ${zPipePath} | wc -l`) ]]; then
            echo "Success" > $zPipePath
        fi
) &

# 防止遇到无效IP时，长时间阻塞
(
    sleep 6
    if [[ 1 -eq `ls ${zPipePath} | wc -l` ]]; then
        echo "Fail" > $zPipePath
    fi
) &

if [[ "Success" == `cat ${zPipePath}` ]]; then
    rm $zPipePath
    exit 0
else
    rm $zPipePath
    exit 255  # 若失败，则以退出码 255 结束进程
fi
