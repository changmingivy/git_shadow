#!/usr/bin/env bash

###################
zProjId=$1  # 项目ID
zPathOnHost=$(printf $2 | sed -n 's%/\+%/%p')  # 生产机上的绝对路径
zPullAddr=$3  # 拉取代码所用的完整地址
zRemoteMasterBranchName=$4  # 源代码服务器上用于对接生产环境的分支名称
zRemoteVcsType=$5  # 仅 git
###################

zShadowPath=${zGitShadowPath}
zDeployPath=${HOME}${zPathOnHost}  # ${zPathOnHost} 是以 '/' 开头的

if [[ "" == $zProjId
    || "" == $zDeployPath
    || "" == $zPullAddr
    || "" == $zRemoteMasterBranchName
    || "" == $zRemoteVcsType ]]
then
    exit 1
fi

# 已存在相同路径的情况：若项目路径相同，但ID不同，返回失败
if [[ 0 -lt `ls -d ${zDeployPath} 2>/dev/null | wc -l` ]]; then
    cd ${zDeployPath}
    if [[ 0 -ne $? ]]; then exit 255; fi
        git branch "____serv"
        cd ${zDeployPath}_SHADOW
        if [[ 0 -ne $? ]]; then exit 255; fi
        rm -rf ./tools
        cp -r ${zShadowPath}/tools ./
        exit 0
fi

# 创建项目路径
mkdir -p $zDeployPath
if [[ $? -ne 0 ]]; then exit 254; fi

# 拉取远程代码
git clone $zPullAddr $zDeployPath

if [[ $? -ne 0 ]]; then
    rm -rf $zDeployPath
    exit 253
fi

# 代码库：环境初始化
cd $zDeployPath
git config user.name "$zProjId"
git config user.email "${zProjId}@${zPathOnHost}"
git add --all .
git commit -m "____Dp_System_Init____"
git branch "master"
git checkout master
git branch -f "____serv"  # 远程代码接收到 ____serv 分支

# use to get diff when no deploy log has been written
cd ${zDeployPath}
git branch ____base.XXXXXXXX &&\
    (\
        git checkout ____base.XXXXXXXX;\
        \ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf;\
        git add --all .;\
        git commit --allow-empty -m "_";\
        git branch `git log -1 --format="%H"`;\
        git checkout master\
    )
