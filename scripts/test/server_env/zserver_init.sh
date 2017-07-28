#!/bin/sh

zInitEnv() {
    zProjName=$1
    zSvnServPath=/home/git/svn_$zProjName  #Subversion repo to receive code from remote developers
    zSyncPath=/home/git/sync_$zProjName  #Sync snv repo to git repo
    zDeployPath=/home/git/$zProjName #Used to deploy code! --CORE--

    rm -rf /home/git/*
    mkdir -p $zDeployPath
    cp -rp ../../../demo/${zProjName}_shadow /home/git

    #Init Subversion Server
    mkdir $zSvnServPath
    svnadmin create $zSvnServPath
    perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
    svnserve --listen-port=$2 -d -r $zSvnServPath

    #Init svn repo，svn 会自动创建目录
    svn co svn://10.30.2.126:$2/ $zSyncPath
    svn propset svn:ignore '.git' $zSyncPath

    #Init Sync Git Env
    cd $zSyncPath
    git init .
    git config --global user.email git_shadow@yixia.com
    git config --global user.name git_shadow

    printf ".svn" > .gitignore
    git add --all .
    git commit --allow-empty -m "__sync_init__"
    git branch -M master sync_git  # 此git的作用是将svn库代码转换为git库代码

    #Init Deploy Git Env
    cd $zDeployPath
    git init .

    printf ".svn" > .gitignore
    git add --all .
    git commit --allow-empty -m "__deploy_init__"
    git branch CURRENT
    git branch server  #Act as Git server

    cp ./zsvn_post-commit.sh ${zSvnServPath}/hooks/post-commit
    chmod 0700 ${zSvnServPath}/hooks/post-commit

    cp ./zsync_git_post-update.sh ${zDeployPath}/.git/hooks/post-update
    chmod 0700 ${zDeployPath}/.git/hooks/post-update
}

# 运行环境
killall svnserve
zInitEnv miaopai 50000

# 启动服务器
zCurDir=$PWD

mkdir -p ../bin
mkdir -p ../log
rm -rf ../bin/*

cc -O2 -Wall -Wextra -std=c99 \
    -I../inc \
    -lpthread \
    -lpcre2-8 \
    -D_XOPEN_SOURCE=700 \
    -o ../../../bin/git_shadow \
    ../../../src/zmain.c
strip ../../../bin/git_shadow

cc -O2 -Wall -Wextra -std=c99 \
    -I../inc \
    -D_XOPEN_SOURCE=700 \
    -o ../../../bin/git_shadow_client \
    ../../../src/zmain.c
strip ../../../bin/git_shadow_client

cd $zCurDir
killall -9 ssh 2>/dev/null
killall -9 git 2>/dev/null
killall -9 git_shadow 2>/dev/null
../bin/git_shadow -f `dirname $zCurDir`/conf/sample.conf -h 10.30.2.126 -p 20000 2>>../log/log 1>&2

# 摸拟一个 svn 客户端
rm -rf /tmp/miaopai
mkdir /tmp/miaopai
cd /tmp/miaopai
svn co svn://10.30.2.126:50000
cp -r /etc/* ./ 2>/dev/null
svn add *
svn commit -m "etc files"
svn up
