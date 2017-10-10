#!/usr/bin/env bash
zProjId=$1
zCommitSig=$2
zPathOnHost=$(printf $3 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zHostList=$4

zServBranchName="server${zProjId}"
zShadowPath=/home/git/zgit_shadow

# 打印日志时间戳
printf "\n\n\033[31;01m[ `date` ] \n\033[00m" 1>&2

if [[ "" == ${zProjId}
    || "" == ${zCommitSig}
    || "" == ${zPathOnHost}
    || "" == ${zHostList} ]]; then
    exit 1
fi

cd /home/git/${zPathOnHost}
if [[ 0 -ne $? ]]; then exit 1; fi  # 当指定的路径不存在，此句可防止 /home/git 下的项目文件被误删除

\ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf
git stash
git stash clear
git pull --force ./.git ${zServBranchName}:master
git reset --hard ${zCommitSig}

# 用户指定的在部置之前执行的操作
bash ____pre-deploy.sh 2>/dev/null
git add --all .
git commit --allow-empty -m "____pre-deploy.sh"

# 暂停全量sha1sum校验
#find . -path './.git' -prune -o -type f -print | fgrep -v ' ' | sort | xargs cat | sha1sum | grep -oP '^\S+' > /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.res

cd /home/git/${zPathOnHost}_SHADOW
rm -rf ./tools
cp -R ${zShadowPath}/tools ./
chmod 0755 ./tools/post-update
eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./tools/post-update
git add --all .
git commit --allow-empty -m "_"  # 提交一次，允许空记录，用于保证每次推送 post-upate 都能执行

# 布署到终端集群，先推项目代码，后推 <_SHADOW>
for zHostAddr in `echo $zHostList`; do
    (\
        cd /home/git/${zPathOnHost} &&\
        git push --force git@${zHostAddr}:${zPathOnHost}/.git master:${zServBranchName};\
        cd /home/git/${zPathOnHost}_SHADOW &&\
        git push --force git@${zHostAddr}:${zPathOnHost}_SHADOW/.git master:${zServBranchName}\
    )&
done

# 中控机：布署后环境设置
cd /home/git/${zPathOnHost}
zOldSig=`git log CURRENT -1 --format=%H`
git branch -f $zOldSig  # 创建一个以原有的 CURRENT 分支的 SHA1 sig 命名的分支
git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
