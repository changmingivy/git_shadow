#!/bin/sh

###################
zProjNo=$1  # 项目ID
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')  # 生产机上的绝对路径
zPullAddr=$3  # 拉取代码所用的完整地址
zRemoteMasterBranchName=$4  # 源代码服务器上用于对接生产环境的分支名称
zRemoteVcsType=$5  # svn 或 git
###################

zShadowPath=/home/git/zgit_shadow
zDeployPath=/home/git/$zPathOnHost

if [[ "" == $zProjNo
    || "" == $zPathOnHost
    || "" == $zPullAddr
    || "" == $zRemoteMasterBranchName
    || "" == $zRemoteVcsType ]]
then
    exit 1
fi

# 已存在相同路径的情况：若项目路径相同，但ID不同，返回失败
if [[ 0 -lt `ls -d ${zDeployPath} | wc -l` ]]; then
    if [[ $zProjNo -eq `cat ${zDeployPath}_SHADOW/info/repo_id` ]]; then
        cd ${zDeployPath}_SHADOW
        cp -rf ${zShadowPath}/scripts ./
        eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./scripts/post-update
        eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./scripts/post-merge
        chmod 0755 ./scripts/post-merge
        mv ./scripts/post-merge ${zDeployPath}/.git/hooks/
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

# 环境初始化
cd $zDeployPath
git config user.name "$zProjNo"
git config user.email "${zProjNo}@${zPathOnHost}"
printf ".svn/" > .gitignore  # 忽略<.svn>目录
git add --all .
git commit -m "__init__"
git branch -f CURRENT
git branch -f server  # 远程代码接收到server分支

# 专用于远程库VCS是svn的场景
if [[ "svn" == $zRemoteVcsType ]]; then
    rm -rf ${zDeployPath}_SYNC_SVN_TO_GIT
    mkdir ${zDeployPath}_SYNC_SVN_TO_GIT

    svn co $zPullAddr ${zDeployPath}_SYNC_SVN_TO_GIT  # 将 svn 代码库内嵌在 git 仓库下建一个子目录中
    cd ${zDeployPath}_SYNC_SVN_TO_GIT
    git init .
    git config user.name "sync_svn_to_git"
    git config user.email "sync_svn_to_git@${zProjNo}"
    printf ".svn/" > .gitignore
    git add --all .
    git commit -m "__init__"
fi

# 创建以 <项目名称>_SHADOW 命名的目录，初始化为git库
mkdir -p ${zDeployPath}_SHADOW/scripts
rm -rf ${zDeployPath}_SHADOW/scripts/*
cp -r ${zShadowPath}/scripts/* ${zDeployPath}_SHADOW/scripts/
cd ${zDeployPath}_SHADOW
eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./scripts/post-update
eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./scripts/post-merge
chmod 0755 ./scripts/post-merge
mv ./scripts/post-merge ${zDeployPath}/.git/hooks/

git init .
git config user.name "git_shadow"
git config user.email "git_shadow@${zProjNo}"
git add --all .
git commit --allow-empty -m "__init__"

# 防止添加重复条目
zExistMark=`cat /home/git/zgit_shadow/conf/master.conf | grep -Pc "^\s*${zProjNo}\s*"`
if [[ 0 -eq $zExistMark ]];then
    echo "${zProjNo} ${zPathOnHost} ${zPullAddr} ${zRemoteMasterBranchName} ${zRemoteVcsType}" >> ${zShadowPath}/conf/master.conf
fi

# 创建必要的目录与文件
cd ${zDeployPath}_SHADOW
mkdir -p ${zDeployPath}_SHADOW/{info,log/deploy}
touch ${zDeployPath}_SHADOW/info/repo_id
touch ${zDeployPath}_SHADOW/log/deploy/meta
chmod -R 0755 ${zDeployPath}_SHADOW

# use to get diff when no deploy log has been written
cd ${zDeployPath}
git branch ____base.XXXXXXXX &&\
    (\
        git checkout ____base.XXXXXXXX;\
        \ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf;\
        git add --all .;\
        git commit --allow-empty -m "_";\
        zOrigLog=`git log -1 --format="%H_%ct"`;\
        git branch ${zOrigLog};\
        mv ${zDeployPath}_SHADOW/log/deploy/meta ${zDeployPath}_SHADOW/log/deploy/meta.tmp;\
        echo ${zOrigLog} > ${zDeployPath}_SHADOW/log/deploy/meta;\
        cat ${zDeployPath}_SHADOW/log/deploy/meta.tmp >> ${zDeployPath}_SHADOW/log/deploy/meta;\
        rm ${zDeployPath}_SHADOW/log/deploy/meta.tmp;\
        git checkout master\
    )
