#!/bin/sh
zMajorAddr=$1
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')

zTmpFile=`mktemp /tmp/${zSelfPid}.XXXXXXXX`
echo "Orig" > $zTmpFile

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

        if [[ (0 -eq $?) && ("Orig" == `cat ${zTmpFile}`) ]]; then
            echo "Success" > $zTmpFile
        fi
) &

# 防止遇到无效IP时，长时间阻塞；若失败，则以退出码 255 结束进程
sleep 6
if [[ "Success" == `cat ${zTmpFile}` ]]; then
    rm $zTmpFile
    exit 0
else
    rm $zTmpFile
    exit 255
fi
