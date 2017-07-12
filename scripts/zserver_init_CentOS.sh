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

# if [[ 0 -eq `ls $HOME/.gitconfig 2>/dev/null | wc -l` ]]; then
#     git config --global user.name "_"
#     git config --global user.email "_"
# fi

# for zDir in `grep -oP '(?<=\s)/[/|\w]+' ../conf/sample.conf`; do
#     if [[ 1 -eq `ls -d $zDir 2>/dev/null | wc -l` ]]; then
#         cd $zDir
#         git init . #>/dev/null 2>&1
#         git config user.name "git_shadow"
#         git config user.email $PWD
#         rm -rf $zDir/.git/index.lock
#     fi
# done

cd $zCurDir
killall -9 git_shadow 2>/dev/null
../bin/git_shadow -f `dirname $zCurDir`/conf/sample.conf -h 10.30.2.126 -p 20000
#2>>../log/log 1>&2
