#!/usr/bin/env bash
zProjName=$1
zSvnServPath=~svn/svn_$zProjName  #Subversion repo to receive code from remote developers
zSyncPath=~git/sync_$zProjName  #Sync snv repo to git repo
zDeployPath=~git/$zProjName #Used to deploy code! --CORE--
zSshKeyPath=$zSyncPath/.git_shadow/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

yes|yum install subversion git
cp -r ../demo/$ProjName ~git/

#Init Subversion Server
useradd -m svn -s $(which sh)
mkdir -p $zSvnServPath
svnadmin create $zSvnServPath
perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
svnserve --listen-port=$2 -d -r $zSvnServPath

#Init svn repo
svn co svn://127.0.0.1/ $zSyncPath
svn propset svn:ignore '.git
.gitignore' $zSyncPath

#Init Sync Git Env
useradd -m git -s $(which sh)
su git -c "yes|ssh-keygen -t rsa -P '' -f ~git/.ssh/id_rsa"

git init $zSyncPath

mkdir -p $zSyncPath/.git_shadow
touch $zSshKeyPath
chmod 0600 $zSshKeyPath
cd $zSyncPath
git init .
git config --global user.email "git_shadow@yixia.com"
git config --global user.name "git_shadow"
git add --all .
git commit -m "init"
git branch -M master sync_git  # 此git的作用是将svn库代码转换为git库代码

#Init Deploy Git Env
mkdir -p $zDeployPath
cd $zDeployPath
git init .
git branch server  #Act as Git server

# Config svn hooks，锁机制需要替换
#printf "#!/bin/sh\n>/dev/null">$zSvnServPath/hooks/pre-commit
#chmod u+x $zSvnServPath/hooks/pre-commit

printf "#!/bin/sh \n\
	cd $zSyncPath \n\
	svn update \n\
	git add --all . \n\
	git commit -m \"\$1:\$2\" \n\
	git push --force $zDeployPath/.git sync_git:server \n\
	">$zSvnServPath/hooks/post-commit
chmod u+x $zSvnServPath/hooks/post-commit

# Config Sync git hooks，锁机制需要替换
printf "#!/bin/sh \n\
	cd $zDeployPath \n\
	git pull --force ./.git server:master \n\
	">$zSyncPath/.git/hooks/post-receive
chmod u+x $zSyncPath/.git/hooks/post-receive

# Config Deploy git hooks，锁机制需要替换
#printf "#!/bin/sh\n">$zSyncPath/.git/hooks/pre-commit
#chmod u+x $zSyncPath/.git/hooks/pre-commit
