#!/bin/sh
# TEST:PASS
zCodePath=`echo $1 | grep -oP '(?<=/home/git).*'`
zHostAddrListPath="${zCodePath}/.git_shadow/info/host_ip_all.txt"

for x in `cat $zHostAddrListPath`; do
    ssh $x "
        rm -rf $zCodePath &&\
        mkdir -p $zCodePath/.git_shadow &&\
\
        cd $zCodePath/.git_shadow &&\
        git init . &&\
        git config user.name "git_shadow" &&\
        git config user.email "git_shadow@$x" &&\
        git commit --allow-empty -m "__init__" &&\
        git branch -f server &&\
\
        cd $zCodePath &&\
        git init . &&\
        git config user.name "`basename $zCodePath`" &&\
        git config user.email "`basename ${zCodePath}`@$x" &&\
        git commit --allow-empty -m "__init__" &&\
        git branch -f server
        "

    scp zhost_post-update.sh git@${x}:${zCodePath}/.git/hooks/post-update

    ssh $x "
        eval sed -i 's%\<PROJ_PATH\>%${zCodePath}%g' ${zCodePath}/.git/hooks/post-update &&\
        chmod 0755 ${zCodePath}/.git/hooks/post-update
        "
done
