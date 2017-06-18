#!/bin/sh

zCurDir=$PWD

if [[ 0 -eq `which gcc | wc -l` ]]; then
	yes|yum install gcc
elif [[ 0 -eq `which git | wc -l` ]]; then
	yes|yum install git
fi

mkdir -p ../bin

if [[ 0 -eq `ls ../bin | grep -c 'git_shadow'` ]]; then
	yes|yum install pcre2-devel
	gcc -O2 \
		-std=c11 \
		-I../inc \
		-lpthread \
		-lpcre2-8 \
		-D_XOPEN_SOURCE=700 \
		-o ../bin/git_shadow \
		../src/zmain.c \
		../src/common/*
fi

for zDir in `grep -oP '(?<=\s)/[/|\w]+' ../conf/sample.conf`; do
	if [[ 1 -eq `ls -d $zDir 2>/dev/null | wc -l` ]]; then
		cd $zDir
		git init . >/dev/null 2>&1
		git config user.name "git_shadow"
		git config user.email $PWD
		rm -rf $zDir/.git/index.lock
	fi
done

cd $zCurDir
killall git_shadow 2>/dev/null
../bin/git_shadow -f `dirname $zCurDir`/conf/sample.conf >> ../log/log 2>&1 
