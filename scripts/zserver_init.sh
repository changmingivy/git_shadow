#!/bin/sh
zProjNo=$1
zProjPath=$2

zShadowPath=/home/git/zgit_shadow
zDeployPath=/home/git/$zProjPath

mkdir -p $zDeployPath/.git_shadow
if [[ 0 -ne $? ]];then exit 255; fi

cp -rf zShadowPath/bin ${zDeployPath}/.git_shadow/
if [[ 0 -ne $? ]];then exit 255; fi
cp -rf zShadowPath/scripts ${zDeployPath}/.git_shadow/
if [[ 0 -ne $? ]];then exit 255; fi

# 初始化 git_shadow 自身的库，不需要建 CURRENT 与 server 分支
cd $zDeployPath/.git_shadow
if [[ 0 -ne $? ]];then exit 255; fi
git init .
if [[ 0 -ne $? ]];then exit 255; fi
git config user.name "git_shadow"
if [[ 0 -ne $? ]];then exit 255; fi
git config user.email "git_shadow@_"
if [[ 0 -ne $? ]];then exit 255; fi
git add --all .
if [[ 0 -ne $? ]];then exit 255; fi
git commit --allow-empty --allow-empty-message --allow-empty -m "_"
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
printf ".svn\n.git_shadow" > .gitignore  # 项目 git 库设置忽略 .git_shadow 目录
if [[ 0 -ne $? ]];then exit 255; fi
git add --all .
if [[ 0 -ne $? ]];then exit 255; fi
git commit --allow-empty --allow-empty-message --allow-empty -m "__deploy_init__"
if [[ 0 -ne $? ]];then exit 255; fi
git branch CURRENT
if [[ 0 -ne $? ]];then exit 255; fi
git branch server  #Act as Git server
if [[ 0 -ne $? ]];then exit 255; fi

cp ${zShadowPath}/scripts/git_hook/zgit_post-update.sh ${zDeployPath}/.git/hooks/post-update
if [[ 0 -ne $? ]];then exit 255; fi
chmod 0755 ${zDeployPath}/.git/hooks/post-update
if [[ 0 -ne $? ]];then exit 255; fi

echo $zProjNo $zProjPath >> $zShadowPath/conf/config
exit 0
