#!/bin/sh

zProjPath=
zCommitSig=
zFilePath=
zHostIp=
zHostListPath=

while getopts p:i:h:f:P: zOption
do
    case $zOption in
        p) zProjPath=$OPTARG;;  # repo path
        i) zCommitSig=$OPTARG;;  # commit id(SHA1 sig)
        f) zFilePath=$OPTARG;;  # file path
        h) zHostIp=$OPTARG;;  # host ip
        P) zHostListPath=$OPTARG;;  # major host list path
        ?) exit 255;;
    esac
done
shift $[$OPTIND - 1]

cd $zProjPath
git stash
git stash clear
git pull --force ./.git server:master

# 非单台布署情况下，host ip会被指定为0
if [[ '0' == $zHostIp ]]; then
    zHostList=`cat ${zProjPath}/${zHostListPath}`
else
    zHostList=$zHostIp
fi

git reset ${zCommitSig} -- $zFilePath
echo "$zFilePath $zCommitSig" >> DP_LOG
git add DP_LOG
if [[ "" == $zFilePath ]]; then
    git commit -m "单文件布署：$zFilePath $zCommitSig"
else
    git commit -m "版本布署：$zCommitSig"
fi

# git_shadow 作为独立的 git 库内嵌于项目代码库当中，因此此处必须进入 .git_shadow 目录执行
cd $zProjPath/.git_shadow
git add --all .
git commit -m "__DP__"

zProjPathOnHost=`echo $zProjPath | sed -n 's%/home/git/\+%/%p'`
for zHostAddr in $zHostList; do
    let i++
    # 必须首先切换目录
    ( \
        cd $zProjPath/.git_shadow \
        && git push --force git@${zHostAddr}:${zProjPathOnHost}/.git_shadow/.git master:server \
        \
        && cd $zProjPath \
        && git push --force git@${zHostAddr}:${zProjPathOnHost}/.git master:server \
    ) &

done

cd $zProjPath
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
