#!/bin/sh

###################
zProjNo=$1  # 项目ID
zProjPath=$2  # 生产机上的绝对路径
zPullAddr=$3  # 拉取代码所用的完整地址
zRemoteMasterBranchName=$4  # 源代码服务器上用于对接生产环境的分支名称
zRemoteVcsType=$5  # svn 或 git
###################

zShadowPath=/home/git/zgit_shadow
zDeployPath=/home/git/$zProjPath

if [[ "" == $zProjNo
    || "" == $zProjPath
    || "" == $zPullAddr
    || "" == $zRemoteMasterBranchName
    || "" == $zRemoteVcsType ]]
then
    exit 255
fi

# 环境初始化
rm -rf $zDeployPath 2>/dev/null
git clone $zPullAddr $zDeployPath
    if [[ 0 -ne $? ]];then exit 255; fi
cd $zDeployPath
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.name "$zProjNo"
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.email "${zProjNo}@${zProjPath}"
    if [[ 0 -ne $? ]];then exit 255; fi
printf ".svn/\n.git_shadow/\n.sync_svn_to_git/" > .gitignore  # 忽略<.git_shadow>目录
    if [[ 0 -ne $? ]];then exit 255; fi
git add --all .
    if [[ 0 -ne $? ]];then exit 255; fi
git commit -m "__init__"
    if [[ 0 -ne $? ]];then exit 255; fi
git branch -f CURRENT
    if [[ 0 -ne $? ]];then exit 255; fi
git branch -f server  # 远程代码接收到server分支
    if [[ 0 -ne $? ]];then exit 255; fi

# 专用于远程库VCS是svn的场景
rm -rf ${zDeployPath}/.sync_svn_to_git 2>/dev/null
mkdir ${zDeployPath}/.sync_svn_to_git
    if [[ 0 -ne $? ]];then exit 255; fi

if [[ "svn" == $zRemoteVcsType ]]; then
    svn co $zPullAddr ${zDeployPath}/.sync_svn_to_git  # 将 svn 代码库内嵌在 git 仓库下建一个子目录中
        if [[ 0 -ne $? ]];then exit 255; fi
    cd ${zDeployPath}/.sync_svn_to_git
        if [[ 0 -ne $? ]];then exit 255; fi
    git init .
        if [[ 0 -ne $? ]];then exit 255; fi
    git config user.name "sync_svn_to_git"
        if [[ 0 -ne $? ]];then exit 255; fi
    git config user.email "sync_svn_to_git@${zProjNo}"
        if [[ 0 -ne $? ]];then exit 255; fi
    printf ".svn/" > .gitignore
        if [[ 0 -ne $? ]];then exit 255; fi
    git add --all .
        if [[ 0 -ne $? ]];then exit 255; fi
    git commit -m "__init__"
        if [[ 0 -ne $? ]];then exit 255; fi
fi

# <.git_shadow>以子库的形式内嵌于主库之中
rm -rf ${zDeployPath}/.git_shadow 2>/dev/null
mkdir ${zDeployPath}/.git_shadow
    if [[ 0 -ne $? ]];then exit 255; fi

cp -rf ${zShadowPath}/bin ${zDeployPath}/.git_shadow/
    if [[ 0 -ne $? ]];then exit 255; fi
cp -rf ${zShadowPath}/scripts ${zDeployPath}/.git_shadow/
    if [[ 0 -ne $? ]];then exit 255; fi

cd $zDeployPath/.git_shadow
    if [[ 0 -ne $? ]];then exit 255; fi
git init .
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.name "git_shadow"
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.email "git_shadow@${zProjNo}"
    if [[ 0 -ne $? ]];then exit 255; fi
git add --all .
    if [[ 0 -ne $? ]];then exit 255; fi
git commit --allow-empty -m "__init__"
    if [[ 0 -ne $? ]];then exit 255; fi

# 防止添加重复条目
zExistMark=`cat /home/git/zgit_shadow/conf/master.conf | grep -Pc "^\s*${zProjNo}\s*"`
if [[ 0 -eq $zExistMark ]];then
    echo "${zProjNo} ${zProjPath} ${zPullAddr} ${zRemoteMasterBranchName} ${zRemoteVcsType}" >> ${zShadowPath}/conf/master.conf
    if [[ 0 -ne $? ]];then exit 255; fi
fi

exit 0
