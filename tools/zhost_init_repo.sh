#!/bin/sh
zMajorAddr=$1
zSlaveAddr=$2
zProjId=$3
zPathOnHost=$(echo $4 | sed -n 's%/\+%/%p')
zShadowPath=/home/git/zgit_shadow

# 将点分格式的 IPv4 地址转换为数字格式
zIPv4NumAddr=0
zCnter=0
for zField in `echo ${zSlaveAddr} | grep -oP '\d+'`
do
    let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
    let zCnter++
done

zPipePath=/home/git/.fifo.$$  # 以 fifo.<自身进程号> 命名保证管道名称唯一性
mkfifo -m 0700 $zPipePath

(
    ssh -t $zMajorAddr "ssh $zSlaveAddr \"
        exec 777>/dev/tcp/__MASTER_ADDR/__MASTER_PORT
        echo '[{\\\"OpsId\\\":8,\\\"ProjId\\\":${zProjId},\\\"HostId\\\":${zIPv4NumAddr},\\\"ExtraData\\\":\\\"A\\\"}]'>&777
        exec 777>&-
\
        rm ${zPathOnHost}_SHADOW
        rm ${zPathOnHost}
        mkdir -p ${zPathOnHost}_SHADOW
        mkdir -p ${zPathOnHost}
\
        cd ${zPathOnHost}_SHADOW
        git init .
        git config user.name "git_shadow"
        git config user.email "git_shadow@"
        git commit --allow-empty -m "__init__"
        git branch -f server
\
        cd $zPathOnHost
        git init .
        git config user.name "_"
        git config user.email "_@"
        git commit --allow-empty -m "__init__"
        git branch -f server
\
        cat > .git/hooks/post-update
        chmod 0755 .git/hooks/post-update
        \"" < /home/git/${zPathOnHost}_SHADOW/tools/post-update

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
    kill -9 `ps ax -o pid,cmd | grep -oP "\d+(?=\s+ssh -t $zMajorAddr \"ssh $zSlaveAddr)"`
    exit 255  # 若失败，则以退出码 255 结束进程
fi
