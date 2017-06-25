#!/usr/bin/env bash
zSvnServPath=~svn/svn_repo  #Subversion repo to receive code from remote developers
zSyncPath=~git/sync_repo  #Sync snv repo to git repo
zDeployPath=~git/deploy_repo  #Used to deploy code !CORE!
zSshKeyPath=$zSyncPath/GIT_SHADOW/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

yes|yum install subversion git
#Init Subversion Server
useradd -m svn -s $(which sh)
mkdir -p $zSvnServPath
svnadmin create $zSvnServPath
perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
svnserve -d -r $zSvnServPath

#Init svn repo
svn co svn://127.0.0.1/ $zSyncPath
svn propset svn:ignore '.git
.gitignore
.zLock' $zSyncPath

#Init Sync Git Env
useradd -m git -s $(which sh)
su git -c "yes|ssh-keygen -t rsa -P '' -f ~git/.ssh/id_rsa"

git init $zSyncPath
printf ".svn/\n.zLock" >> $zSyncPath/.gitignore

mkdir -p $zSyncPath/GIT_SHADOW
touch $zSshKeyPath
chmod 0600 $zSshKeyPath
cd $zSyncPath
git add .
git commit -m "init"
git branch -m master client  #Act as Git client
git branch server  #Act as Git server

#Init Deploy Git Env
mkdir -p $zDeployPath
git init $zDeployPath
cd $zDeployPath
git pull --force $zSyncPath/.git server:master

#Generate fifo files(used as sync lock)
mkfifo $zSyncPath/.zLock
printf 0 >$zSyncPath/.zLock &
mkfifo $zDeployPath/.zLock
printf 0 >$zDeployPath/.zLock &

#Config svn hooks
printf "#!/bin/sh\ncat $zSyncPath/.zLock>/dev/null">$zSvnServPath/hooks/pre-commit
chmod u+x $zSvnServPath/hooks/pre-commit

printf "#!/bin/sh \n\
	cd $zSyncClientPath \n\
	svn update \n\
	git add --all . \n\
	git commit -m \"\$1:\$2\" \n\
	git push --force $zSyncPath/.git client:server">$zSvnServPath/hooks/post-commit
chmod u+x $zSvnServPath/hooks/post-commit

#Config Sync git hooks
printf "#!/bin/sh\nprintf 0>$zSyncPath/.zLock">$zSyncPath/.git/hooks/post-receive
chmod u+x $zSyncPath/.git/hooks/post-receive

#Config Deploy git hooks
printf "#!/bin/sh\ncat $zDeployPath/.zLock">$zSyncPath/.git/hooks/pre-commit
chmod u+x $zSyncPath/.git/hooks/pre-commit
