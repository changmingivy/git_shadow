#!/bin/sh
zCommitSig=$1
zProjPath=$2  # 布署目标上的绝对路径
zMajorAddr=$3  # 中转机IPv4地址

shift 3
zHostList=$@
zShadowPath=/home/git/zgit_shadow

cd /home/git/${zProjPath}
git stash
git stash clear
git pull --force ./.git server:master

git reset ${zCommitSig}

# 更新中转机(MajorHost)
cd /home/git/${zProjPath}_SHADOW

cp -rf ${zShadowPath}/bin/* ./bin/
cp -rf ${zShadowPath}/scripts/* ./scripts/
printf "$RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM" >> ./bin/git_shadow_client
git add --all .
git commit --allow-empty -m "__DP__"

git push --force git@${zMajorAddr}:${zProjPath}_SHADOW/.git master:server

cd /home/git/${zProjPath}
git push --force git@${zMajorAddr}:${zProjPath}/.git master:server

# 通过中转机布署到终端集群
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

# 中控机：布署后环境设置
cd /home/git/$zProjPath
git stash
git stash clear
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
