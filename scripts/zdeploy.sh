#!/bin/sh
zShadowPath=$HOME/zgit_shadow

zCommitSig=$1
zProjPath=$2  # 布署目标上的绝对路径
zFilePath=$3  # 相对于代码库的路径
zMajorAddr=$4  # 中转机IPv4地址

shift 4
zHostList=$@

cd $zProjPath
git stash
git stash clear
git pull --force ./.git server:master

if [[ "_" == $zFilePath ]]; then
    git reset ${zCommitSig}
else
    git reset ${zCommitSig} -- $zFilePath
fi

# 更新远端自身工具集
cd ${zProjPath}_SHADOW
cp -rf ${zShadowPath}/bin/* ./bin/
cp -rf ${zShadowPath}/scripts/* ./scripts/
printf "$RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM" >> ./bin/git_shadow_client
git add --all .
git commit --allow-empty -m "__DP__"

# 更新中转机(MajorHost)
cd ${zProjPath}_SHADOW
git push --force git@${zMajorAddr}:${zProjPath}_SHADOW/.git master:server
cd ${zProjPath}
git push --force git@${zMajorAddr}:${zProjPath}/.git master:server

# 通过中转机布署
ssh $zMajorAddr "
	cd ${zProjPath}_SHADOW &&
    for zHostAddr in \"$zHostList\"; do
        ( git push --force git@\${zHostAddr}:${zProjPath}_SHADOW/.git master:server ) &
    done
\
	cd $zProjPath &&
    for zHostAddr in \"$zHostList\"; do
        ( git push --force git@\${zHostAddr}:${zProjPath}/.git master:server ) &
    done
"

cd $zProjPath
git stash
git stash clear
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
