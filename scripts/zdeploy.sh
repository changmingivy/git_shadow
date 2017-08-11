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
printf ".svn/" >> .gitignore

# 非单台布署情况下，host ip会被指定为0
if [[ "0" == $zHostIp ]]; then
    zHostList=`cat ${zProjPath}${zHostListPath} | grep -oP '(\d{1,3}\.){3}\d{1,3}'`
else
    zHostList=$zHostIp
fi

if [[ "_" == $zFilePath ]]; then
    git reset ${zCommitSig}
else
    git reset ${zCommitSig} -- $zFilePath
fi

#
cd ${zProjPath}_SHADOW
rm -rf ./bin ./scripts
cp -rf ${zShadowPath}/bin ${zShadowPath}/scripts ./
printf "$RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM" >> ./bin/git_shadow_client
git add --all .
git commit --allow-empty -m "__DP__"

zProjPathOnHost=`echo $zProjPath | sed -n 's%/home/git/\+%/%p'`
for zHostAddr in $zHostList; do
    # 必须首先切换目录
    ( \
        cd ${zProjPath}_SHADOW \
        && git push --force git@${zHostAddr}:${zProjPathOnHost}_SHADOW/.git master:server \
        \
        && cd $zProjPath \
        && git push --force git@${zHostAddr}:${zProjPathOnHost}/.git master:server \
    ) &

done

cd $zProjPath
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
git stash
git stash clear
