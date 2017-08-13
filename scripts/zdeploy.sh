#!/bin/sh
zCommitSig=$1
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zMajorAddr=$3  # 中转机IPv4地址

shift 3
zHostList=$@
zShadowPath=/home/git/zgit_shadow

if [[ "" == $zCommitSig
    || "" == ${zPathOnHost}
    || "" == $zMajorAddr
    || "" == $zHostList ]]; then
    exit 1
fi

cd /home/git/${zPathOnHost}
rm -rf *

git pull --force ./.git server:master
git reset ${zCommitSig}

# 更新中转机(MajorHost)
cd /home/git/${zPathOnHost}_SHADOW

cp -rf ${zShadowPath}/bin/* ./bin/
cp -rf ${zShadowPath}/scripts/* ./scripts/
eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./scripts/post-update
printf "$RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM" >> ./bin/git_shadow_client
git add --all .
git commit --allow-empty -m "__DP__"

git push --force git@${zMajorAddr}:${zPathOnHost}_SHADOW/.git master:server

cd /home/git/${zPathOnHost}
git push --force git@${zMajorAddr}:${zPathOnHost}/.git master:server

# 通过中转机布署到终端集群
ssh $zMajorAddr "
    cd ${zPathOnHost}_SHADOW &&
    for zHostAddr in $zHostList; do
        ( git push --force git@\${zHostAddr}:${zPathOnHost}_SHADOW/.git server:server ) &
    done
    rm -rf * &&
\
    cd ${zPathOnHost} &&
    for zHostAddr in $zHostList; do
        ( git push --force git@\${zHostAddr}:${zPathOnHost}/.git server:server ) &
    done
    rm -rf *
"

# 中控机：布署后环境设置
cd /home/git/${zPathOnHost}
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
