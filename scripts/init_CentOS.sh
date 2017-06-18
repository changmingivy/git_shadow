#!/bin/sh

if [[ 0 -eq `which gcc | wc -l` ]]; then
	yes|yum install gcc
elif [[ 0 -eq `which git | wc -l` ]]; then
	yes|yum install git
fi

mkdir -p ../bin

if [[ 0 -eq `ls ../bin | grep -c git_shadow` ]]; then
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

killall git_shadow

for zDir in `grep -oP '(?<=\s)/[/|\w]+' ../conf/sample.conf`; do
	rm -rf $zDir/.git/index.lock
done

../bin/git_shadow -f `dirname $PWD`/conf/sample.conf >> ../log/log 2>&1 
