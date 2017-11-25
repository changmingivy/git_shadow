#/bin/bash
zRepoId=$1

zPathOnServ=$2
zPathOnHost=$3

zServIp=$4
zServPort=$5
zSelfIp=$6

zMd5Sum=$7

# 首先清除自身，只留内存中的副本，确保本次执行即可
rm -rf `pwd`

rm -f $zPathOnHost ${zPathOnHost}_SHADOW  # 尝试清除软链
rm -f $zPathOnHost/.git/index.lock ${zPathOnHost}_SHADOW/.git/index.lock
rm -f $zPathOnHost/.git/post-update ${zPathOnHost}_SHADOW/.git/post-update

mkdir -p $zPathOnHost ${zPathOnHost}_SHADOW/tools  # 创建必要的路径

cd ${zPathOnHost}_SHADOW
if [[ 0 -ne $? ]]; then exit 255; fi

rm -rf .git
git init .
git config user.name _
git config user.email _

cd $zPathOnHost
if [[ 0 -ne $? ]]; then exit 255; fi

rm -rf .git
git init .
git config user.name _
git config user.email _

echo "$zSelfIp" > ${zPathOnHost}_SHADOW/.____zself_ip_addr_${zRepoId}.txt

zBaseTimeStamp=`date +%s`
while [[ "${zMd5Sum}" != `md5sum ${zPathOnHost}_SHADOW/tools/notice | grep -oP '\w{32}'` ]]
do
    if [[ 300 < $((`date +%s` - ${zBaseTimeStamp})) ]]; then
        exit 255
    fi

    exec 777<>/dev/tcp/${zServIp}/${zServPort}
    printf "{\"OpsId\":14,\"ProjId\":%d,\"Path\":\"${zPathOnServ}_SHADOW/tools/notice\"}" >&777
    cat <&777 >${zPathOnHost}_SHADOW/tools/notice
    chmod 0755 ${zPathOnHost}_SHADOW/tools/notice
    exec 777>&-
    exec 777<&-
done

${zPathOnHost}_SHADOW/tools/notice\
    "${zServIp}"\
    "${zServPort}"\
    "{\"OpsId\":14,\"ProjId\":${zRepoId},\"Path\":\"${zPathOnServ}_SHADOW/tools/post-update\"}"\
    >${zPathOnHost}/.git/hooks/post-update

if [[ 0 -eq $? ]]; then
    chmod 0755 ${zPathOnHost}/.git/hooks/post-update
else
    exit 255
fi
