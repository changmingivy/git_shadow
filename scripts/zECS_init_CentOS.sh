#!/usr/bin/env sh

zCodePath=~git/miaopai
zEcsAddrListPath=$zCodePath/.git_shadow/info/client_ip_all.txt #store all ECSs' private IPs
zEcsAddrMajorListPath=$zCodePath/.git_shadow/info/client_ip_major.txt #store all major ECSs' public IPs
zSshKeyPath=$zCodePath/.git_shadow/info/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

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
git config --global user.email "ECS@aliyun.com"
git config --global user.name "ECS"
git add --all .
git commit -m "init"
git branch -m master client # 将master分支名称更改为client
git branch server # 创建server分支

# config git hook
# 拉取server分支分代码到client分支；通知中控机已收到代码；判断自身是否是ECS分发节点，如果是，则向同一项目下的所有其它ECS推送最新收到的代码
printf "#!/bin/sh \n\
	git pull --force $zCodePath/.git server:client \n\
	$zCodePath/.git_shadow/bin/git_shadow -h 10.30.2.126 -p 20000 -C \n\

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
