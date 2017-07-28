#!/bin/sh

zRepoPath=
zCommitSig=
zFilePath=
zHostIp=
zHostListPath=

while getopts p:i:h:f:P: zOption
do
    case $zOption in
        p) zRepoPath=$OPTARG;;  # repo path
        i) zCommitSig=$OPTARG;;  # commit id(SHA1 sig)
        f) zFilePath=$OPTARG;;  # file path
        h) zHostIp=$OPTARG;;  # host ip
        P) zHostListPath=$OPTARG;;  # major host list path
        ?) exit 1;;
    esac
done
shift $[$OPTIND - 1]

cd $zCodePath

if [[ '' == $zHostIp ]]; then
    zHostList=`cat ${zCodePath}/${zHostListPath}`
else
    zHostList=$zHostIp
fi

git reset $zCommitSig -- $zFilePath
if [[ 0 -ne $? ]]; then exit 1; fi

git commit --allow-empty -m "==> <${zCommitSig}>"
if [[ 0 -ne $? ]]; then exit 1; fi

i=0
j=0
for zHostAddr in $zHostList
do
    let i++
    git push --force git@${zHostAddr}:${zCodePath}/.git master:server &

    if [[ $? -ne 0 ]]; then let j++; fi
done

if [[ $i -eq $j ]]; then
    git stash
    git stash clear
    git pull --force ${zCodePath}/.git server:master
    exit 1
fi

git branch -f `git log CURRENT -1 --format=%H` # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支

exit 0
