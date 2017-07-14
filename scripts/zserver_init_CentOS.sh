#!/bin/sh

zCurDir=$PWD

if [[ 0 -eq `which gcc | wc -l` ]]; then
    yes|yum install gcc
elif [[ 0 -eq `which git | wc -l` ]]; then
    yes|yum install git
elif [[ 0 -eq `which pcre2-config | wc -l` ]]; then
    yes|yum install pcre2-devel
fi

mkdir -p ../bin
rm -rf ../bin/git_shadow

if [[ 0 -eq `ls ../bin | grep -c 'git_shadow'` ]]; then
    clang -O2 \
        -Wall \
        -Wextra \
        -std=c99 \
        -I../inc \
        -lpthread \
        -lpcre2-8 \
        -D_XOPEN_SOURCE=700 \
        -o ../bin/git_shadow \
        ../src/zmain.c
fi

cd $zCurDir
killall -9 git_shadow 2>/dev/null
../bin/git_shadow -f `dirname $zCurDir`/conf/sample.conf -h 127.0.0.1 -p 20000
#2>>../log/log 1>&2
