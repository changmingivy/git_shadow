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

zSelfPid=$$  # 获取自身PID
zTmpFile=`mktemp /tmp/${zSelfPid}.XXXXXXXX`
echo $zSelfPid > $zTmpFile

(sleep 9; if [[ "" != `cat $zTmpFile` ]]; then kill -9 $zSelfPid; fi; rm $zTmpFile) &  # 防止遇到无效IP时长时间卡住

ssh -t $zMajorAddr "ssh $zSlaveAddr \"
    mkdir -p ${zPathOnHost}
    mkdir -p ${zPathOnHost}_SHADOW
\
    cd ${zPathOnHost}_SHADOW
    git init .
    git config user.name "git_shadow"
    git config user.email "git_shadow@"
    git commit --allow-empty -m "__init__"
    git branch -f server
    echo ${zSlaveAddr} > /home/git/zself_ipv4_addr.txt
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
    exec 777>&-
    exec 777<&-
    exec 777>/dev/tcp/__MASTER_ADDR/__MASTER_PORT
    echo '[{\\\"OpsId\\\":8,\\\"ProjId\\\":${zProjId},\\\"HostId\\\":${zIPv4NumAddr},\\\"ExtraData\\\":\\\"A\\\"}]'>&777
    exec 777>&-
    exec 777<&-
    \"" < /home/git/${zPathOnHost}_SHADOW/scripts/post-update

echo "" > $zTmpFile  # 提示后台监视线程已成功执行，不要再kill，防止误杀其它进程
