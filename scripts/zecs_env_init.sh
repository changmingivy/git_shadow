#!/usr/bin/env sh

zCodePath=~git/MiaoPaiCode
zEcsAddrListPath=$zCodePath/.git_shadow/ecs_host_list  #store all ECSs' private IPs
zEcsAddrMajorListPath=$zCodePath/.git_shadow/ecs_host_major_list  #store all major ECSs' public IPs
zSshKeyPath=$zCodePath/.git_shadow/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

yes|yum install git
#Init git env
useradd -m git -s `which sh`
su git -c "yes|ssh-keygen -t rsa -P '' -f ~git/.ssh/id_rsa"

mkdir -p $zCodePath
git init $zCodePath

mkdir -p $zCodePath/.git_shadow
touch $zSshKeyPath
chmod 0600 $zSshKeyPath
cd $zCodePath
git add .
git commit -m "init"
git branch -m master client
git branch server

#config git hook
printf "#!/bin/sh \n\
	git pull --force $zCodePath/.git server:client \n\
	$zCodePath/.git_shadow/git_shadow --notice \n\

	cp -up $zCodePath/.git_shadow/authorized_keys ~git/.ssh/ \n\

	zMyAddr=`ip addr | grep -oP '(\d+.){3}\d+(?=/\d+)' | grep -v '^127.0.0'` \n\
	zMajorAddrList=`cat $zEcsAddrMajorListPath` \n\

	for zAddr in $zMyAddr \n\
	do \n\
		if [[ 0 -lt `echo $zMajorAddrList | grep -c $zAddr` ]]; then \n\
			zEcsAddrList=`cat $zEcsAddrListPath` \n\
			for zEcsAddr in $zEcsAddrList \n\
			do \n\
				git push --force git@${zEcsAddr}:${zCodePath}/.git server:client \n\
			done \n\
			break \n\
		fi \n\
	done" > $zCodePath/hooks/post-receive

chmod u+x $zGitServPath/hooks/post-receive
