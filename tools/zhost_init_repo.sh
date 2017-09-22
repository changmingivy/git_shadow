#!/bin/sh
zMajorAddr=$1
zSlaveAddrList=$2
zProjId=$3
zPathOnHost=$(echo $4 | sed -n 's%/\+%/%p')
zShadowPath=/home/git/zgit_shadow

for zSlaveAddr in $zSlaveAddrList
do
    # 将点分格式的 IPv4 地址转换为数字格式
    zIPv4NumAddr=0
    zCnter=0
    for zField in `echo ${zSlaveAddr} | grep -oP '\d+'`
    do
        let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
        let zCnter++
    done

    # 多进程模型后执行，不用线程模型
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
    ) &
done
