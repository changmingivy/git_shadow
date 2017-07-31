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

mkdir -p ${zDeployPath}/.git_shadow
    if [[ 0 -ne $? ]];then exit 255; fi

if [[ "svn" == $zRemoteVcsType ]]; then
    svn co $zPullAddr ${zDeployPath}/sync_svn_to_git  # 将 svn 代码库内嵌在 git 仓库下建一个子目录中，svn 会自动创建该目录
        if [[ 0 -ne $? ]];then exit 255; fi
    cd ${zDeployPath}/sync_svn_to_git
        if [[ 0 -ne $? ]];then exit 255; fi
    git init .
        if [[ 0 -ne $? ]];then exit 255; fi
    git config user.name "sync_svn_to_git"
        if [[ 0 -ne $? ]];then exit 255; fi
    git config user.email "sync_svn_to_git@${zProjNo}"
        if [[ 0 -ne $? ]];then exit 255; fi
    printf ".svn" > .gitignore
        if [[ 0 -ne $? ]];then exit 255; fi
    git add --all .
        if [[ 0 -ne $? ]];then exit 255; fi
    git commit --allow-empty -m "__init__"
        if [[ 0 -ne $? ]];then exit 255; fi
fi

cp -rf ${zShadowPath}/bin ${zDeployPath}/.git_shadow/
    if [[ 0 -ne $? ]];then exit 255; fi
cp -rf ${zShadowPath}/scripts ${zDeployPath}/.git_shadow/
    if [[ 0 -ne $? ]];then exit 255; fi

# 初始化 git_shadow 自身的库，不需要建 CURRENT 与 server 分支
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

#Init Deploy Git Env
cd $zDeployPath
    if [[ 0 -ne $? ]];then exit 255; fi
git init .
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.name "$zProjNo"
    if [[ 0 -ne $? ]];then exit 255; fi
git config user.email "${zProjNo}@${zProjPath}"
    if [[ 0 -ne $? ]];then exit 255; fi
printf ".git_shadow" > .gitignore  # 项目 git 库设置忽略 .git_shadow 目录
    if [[ 0 -ne $? ]];then exit 255; fi
git add --all .
    if [[ 0 -ne $? ]];then exit 255; fi
git commit --allow-empty -m "__init__"
    if [[ 0 -ne $? ]];then exit 255; fi
git branch CURRENT
    if [[ 0 -ne $? ]];then exit 255; fi
git branch server  #Act as Git server
    if [[ 0 -ne $? ]];then exit 255; fi

echo "${zProjNo} ${zProjPath} ${zPullAddr} ${zRemoteMasterBranchName} ${zRemoteVcsType}" >> ${zShadowPath}/conf/master.conf
exit 0
