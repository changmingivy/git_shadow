#!/bin/sh
zShadowPath=$HOME/zgit_shadow

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
printf ".svn/\n.git_shadow/\n.sync_svn_to_git/" >> .gitignore

# 非单台布署情况下，host ip会被指定为0
if [[ "0" == $zHostIp ]]; then
    zHostList=`cat ${zProjPath}/${zHostListPath}`
else
    zHostList=$zHostIp
fi

git reset ${zCommitSig} -- $zFilePath
if [[ "_" == $zFilePath ]]; then
    git commit --allow-empty -m "版本布署：$zCommitSig"
else
    git commit --allow-empty -m "单文件布署：$zFilePath $zCommitSig"
fi

# git_shadow 作为独立的 git 库内嵌于项目代码库当中，因此此处必须进入 .git_shadow 目录执行
cd $zProjPath/.git_shadow
rm -rf ./bin
rm -rf ./scripts
git add --all .
git commit --allow-empty -m "_"
cp -rf ${zShadowPath}/bin ${zProjPath}/.git_shadow/
cp -rf ${zShadowPath}/scripts ${zProjPath}/.git_shadow/
printf "%RANDOM %RANDOM %RANDOM %RANDOM %RANDOM %RANDOM %RANDOM %RANDOM" >> ${zShadowPath}/bin/git_shadow_client
git add --all .
git commit --allow-empty -m "__DP__"

zProjPathOnHost=`echo $zProjPath | sed -n 's%/home/git/\+%/%p'`
for zHostAddr in $zHostList; do
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
