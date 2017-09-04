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

zTmpFile=`mktemp /tmp/${zSelfPid}.XXXXXXXX`
echo "Orig" > $zTmpFile

(
    ssh -t $zMajorAddr "ssh $zSlaveAddr \"
        exec 777>/dev/tcp/__MASTER_ADDR/__MASTER_PORT
        echo '[{\\\"OpsId\\\":8,\\\"ProjId\\\":${zProjId},\\\"HostId\\\":${zIPv4NumAddr},\\\"ExtraData\\\":\\\"A\\\"}]'>&777
        exec 777>&-
\
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
        \"" < /home/git/${zPathOnHost}_SHADOW/scripts/post-update

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
