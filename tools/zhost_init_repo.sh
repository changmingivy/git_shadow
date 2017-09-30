#!/usr/bin/env bash
zProxyHostAddr=$1
zSlaveAddrList=$2
zProjId=$3
zPathOnHost=$(printf $4 | sed -n 's%/\+%/%p')
zShadowPath=/home/git/zgit_shadow

zServBranchName="server${zProjId}"

for zSlaveAddr in $zSlaveAddrList
do
    # 将点分格式的 IPv4 地址转换为数字格式
    zIPv4NumAddr=0
    zCnter=0
    for zField in `printf ${zSlaveAddr} | grep -oP '\d+'`
    do
        let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]
        let zCnter++
    done

    # 多进程模型后执行，不用线程模型
    (
        ssh -t $zProxyHostAddr "ssh $zSlaveAddr \"
            kill -9 \\\`ps ax -o pid,cmd | grep -v 'grep' | grep -oP \\\"^\s*\d+(?=\s.*${zServBranchName})\\\" | grep -v \\\"\\\$$\\\"\\\`
            rm -rf ${zPathOnHost}/.git
            rm -rf ${zPathOnHost}_SHADOW/.git
\
            rm ${zPathOnHost}
            rm ${zPathOnHost}_SHADOW
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
            git config user.email "git_shadow@"
            git commit --allow-empty -m "__init__"
            git branch -f ${zServBranchName}
\
            cd ${zPathOnHost}_SHADOW
            git init .
            git config user.name "_"
            git config user.email "_@"
            git commit --allow-empty -m "__init__"
            git branch -f ${zServBranchName}
\
            cat > .git/hooks/post-update
            chmod 0755 .git/hooks/post-update
\
            exec 777>/dev/tcp/__MASTER_ADDR/__MASTER_PORT
            printf '[{\\\"OpsId\\\":8,\\\"ProjId\\\":${zProjId},\\\"HostId\\\":${zIPv4NumAddr},\\\"ExtraData\\\":\\\"A\\\"}]'>&777
            exec 777>&-
            \"" < /home/git/${zPathOnHost}_SHADOW/tools/post-update
    ) &
done


###########################################################################################################################################
#            "'rm -f %s %s_SHADOW;"
#            "mkdir -p %s %s_SHADOW;"
#            "rm -f %s/.git/index.lock %s_SHADOW/.git/index.lock;"
#            "cd %s && git init . && git config user.name _ && git config user.email _ && git commit --allow-empty -m _ && git branch server%d;"
#            "cd %s_SHADOW && git init . && git config user.name _ && git config user.email _ && git commit --allow-empty -m _ && git branch server%d;",
#            ""
#            "exec 777<>/dev/tcp/%s/%s;"
#            "printf '[{\"OpsId\":13,\"ProjId\":%d,\"data\":%s_SHADOW/tools/post-update}]'>&777;"
#            "cat <&777 >.git/hooks/post-update;"
#            "chmod 0755 .git/hooks/post-update;"
#            "exec 777>&-;"
#            "exec 777<&-;"
#            ""
#            "zIPv4NumAddr=0; zCnter=0; for zField in `printf ${zSlaveAddr} | grep -oP '\d+'`; do let zIPv4NumAddr+=$[${zField} << (8 * ${zCnter})]; let zCnter++; done;"
#            "exec 777>/dev/tcp/%s/%s;"
#            "printf '[{\"OpsId\":8,\"ProjId\":%d,\"HostId\":${zIPv4NumAddr},\"ExtraData\":\"A\"}]'>&777;"
#            "exec 777>&-'"
