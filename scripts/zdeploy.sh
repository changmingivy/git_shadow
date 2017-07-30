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
        ?) exit 255;;
    esac
done
shift $[$OPTIND - 1]

cd $zRepoPath

if [[ '' == $zHostIp ]]; then
    zHostList=`cat ${zRepoPath}/${zHostListPath}`
else
    zHostList=$zHostIp
fi

git reset ${zCommitSig} -- $zFilePath
if [[ 0 -ne $? ]]; then exit 255; fi

# git_shadow 作为独立的 git 库内嵌于项目代码库当中，因此此处必须进入 .git_shadow 目录执行
cd $zRepoPath/.git_shadow
git add --all .
if [[ 0 -ne $? ]]; then exit 255; fi
git commit --allow-empty -m "_"
if [[ 0 -ne $? ]]; then exit 255; fi

i=0
j=0
for zHostAddr in $zHostList; do
    let i++
    # 必须首先切换目录
    ( \
        cd $zRepoPath/.git_shadow \
        && git push --force git@${zHostAddr}:${zRepoPath}/.git_shadow/.git master:server \
        \
        && cd .. \
        && git push --force git@${zHostAddr}:${zRepoPath}/.git master:server \
    ) &

    if [[ $? -ne 0 ]]; then let j++; fi
done

if [[ $i -eq $j ]]; then
    git stash
    git stash clear
    git pull --force ${zRepoPath}/.git server:master
    exit 255
fi

cd $zRepoPath
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支

exit 0
