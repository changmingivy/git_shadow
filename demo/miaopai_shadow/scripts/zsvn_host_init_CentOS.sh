#!/bin/sh

# 说明：
#     此脚本接受两个参数，$1 用于指定本机启动 svn 服务所用的ip地址；$2 用于指定中控机的 ip 地址，使用 SSH 协议
#

zInitEnv() {
    zProjName="miaopai"
    zSvnServPath=/home/git/svn_$zProjName  #Subversion repo to receive code from remote developers
    zSyncPath=/home/git/sync_$zProjName  #Sync snv repo to git repo

    rm -rf /home/git/*

    #Init Subversion Server
    mkdir $zSvnServPath
    svnadmin create $zSvnServPath
    perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
    svnserve --listen-port=$2 -d -r $zSvnServPath

    #Init Sync Repo
    cd $zSyncPath
    svn co svn://$1:$2/ .
    svn propset svn:ignore '.git' $zSyncPath

    git init .  # 此处 git 的作用是将 svn 库转同步到 git 库
    git config --global user.email "_@yixia.com"
    git config --global user.name "_"

    printf ".svn\ngit_shadow" > .gitignore
    git add --all .
    git commit --allow-empty -m "__sync_init__"
    git branch -M master sync_git

    printf "#!/bin/sh
         export PATH=\"/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin\"
         export HOME=\"/home/git\"  # git commit 需要据此搜索git config参数

         cd $zSyncPath &&
         svn cleanup &&
         svn update &&

         git add --all . &&
         git commit --allow-empty -m \"{REPO => \$1} {REV => \$2}\" &&
         git push --force git@"${3}":"${zDeployPath}"/.git sync_git:server
		 " > $zSvnServPath/hooks/post-commit

    chmod 0555 $zSvnServPath/hooks/post-commit
}


killall svnserve 2>/dev/null
useradd -m -G wheel -s /bin/sh git
zInitEnv $1 50000 $2
chown -R git:git /home/git
#yes|ssh-keygen -t rsa -P '' -f /home/git/.ssh/id_rsa
