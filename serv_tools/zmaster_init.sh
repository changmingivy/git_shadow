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
zServBranchName="server${zProjId}"

if [[ "" == $zProjId
    || "" == $zDeployPath
    || "" == $zPullAddr
    || "" == $zRemoteMasterBranchName
    || "" == $zRemoteVcsType ]]
then
    exit 1
fi

# 为服务分支 server$ProjId 建立统一的别名 server，方便 gitlab hooks 推送代码
ln -sT ${zDeployPath}/.git/refs/heads/server${zProjId} ${zDeployPath}/.git/refs/heads/server

# 已存在相同路径的情况：若项目路径相同，但ID不同，返回失败
if [[ 0 -lt `ls -d ${zDeployPath} 2>/dev/null | wc -l` ]]; then
    cd ${zDeployPath}
    if [[ 0 -ne $? ]]; then exit 255; fi
    if [[ ${zProjId} -eq `git branch | grep 'server[0-9]\+$' | grep -o '[0-9]\+$'` ]]; then
        git branch ${zServBranchName}  # 兼容已有的代码库，否则没有 server${zProjId} 分支
        cd ${zDeployPath}_SHADOW
        if [[ 0 -ne $? ]]; then exit 255; fi
        rm -rf ./tools
        cp -r ${zShadowPath}/tools ./
        eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./tools/post-update
        exit 0
    else
        exit 255
    fi
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
git init .
git config user.name "$zProjId"
git config user.email "${zProjId}@${zPathOnHost}"
git add --all .
git commit -m "____Dp_System_Init____"
git branch -f CURRENT
git branch -f ${zServBranchName}  # 远程代码接收到 server${zProjId} 分支

# 元数据：创建以 <项目名称>_SHADOW 命名的目录，初始化为git库
mkdir -p ${zDeployPath}_SHADOW
cd ${zDeployPath}_SHADOW
if [[ 0 -ne $? ]]; then exit 255; fi
rm -rf ./tools
cp -r ${zShadowPath}/tools ./

git init .
git config user.name "git_shadow"
git config user.email "git_shadow@${zProjId}"
git add --all .
git commit --allow-empty -m "____Dp_System_Init____"

# 创建必要的目录与文件
cd ${zDeployPath}_SHADOW && mkdir -p info log && chmod -R 0755 .

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
