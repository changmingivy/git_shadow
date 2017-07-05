#!/usr/bin/env sh

zSvnServPath=~svn/svn_repo  #Subversion repo to receive code from remote developers
zSyncPath=~git/sync_repo  #Sync snv repo to git repo
zDeployPath=~git/miaopai #Used to deploy code! --CORE--
zSshKeyPath=$zSyncPath/.git_shadow/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

yes|yum install subversion git
#Init Subversion Server
userdel -r svn
useradd -m svn -s $(which sh)
mkdir -p $zSvnServPath
svnadmin create $zSvnServPath
perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
svnserve -d -r $zSvnServPath

#Init svn repo
svn co svn://127.0.0.1/ $zSyncPath
svn propset svn:ignore '.git
.gitignore' $zSyncPath

#Init Sync Git Env
userdel -r git
useradd -m git -s $(which sh)
su git -c "yes|ssh-keygen -t rsa -P '' -f ~git/.ssh/id_rsa"

git init $zSyncPath

mkdir -p $zSyncPath/.git_shadow
touch $zSshKeyPath
chmod 0600 $zSshKeyPath
cd $zSyncPath
git config --global user.email "git_shadow@yixia.com"
git config --global user.name "git_shadow"
git add --all .
git commit -m "init"
git branch -m master client  #Act as Git client
git branch server  #Act as Git server

#Init Deploy Git Env
mkdir -p $zDeployPath
git init $zDeployPath
cd $zDeployPath
git pull --force $zSyncPath/.git server:master  # 将sync库的server分支代码拉到deploy库的master分支

#Config svn hooks，锁机制需要替换
printf "#!/bin/sh\n>/dev/null">$zSvnServPath/hooks/pre-commit
chmod u+x $zSvnServPath/hooks/pre-commit

printf "#!/bin/sh \n\
	cd $zSyncClientPath \n\
	svn update \n\
	git add --all . \n\
	git commit -m \"\$1:\$2\" \n\
	git push --force $zSyncPath/.git client:server">$zSvnServPath/hooks/post-commit  # 将sync库的client分支代码推送到自身的server分支
chmod u+x $zSvnServPath/hooks/post-commit

#Config Sync git hooks，锁机制需要替换
printf "#!/bin/sh\n">$zSyncPath/.git/hooks/post-receive
chmod u+x $zSyncPath/.git/hooks/post-receive

#Config Deploy git hooks，锁机制需要替换
printf "#!/bin/sh\n">$zSyncPath/.git/hooks/pre-commit
chmod u+x $zSyncPath/.git/hooks/pre-commit
