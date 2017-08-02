#!/bin/sh
# TEST:PASS
zPathOnMaster=$1
zPathOnHost=`echo $1 | grep -oP '(?<=/home/git).*'`
zHostAddrListPath="${zPathOnMaster}/.git_shadow/info/host_ip_all.txt"

for x in `cat $zHostAddrListPath`; do
    ssh $x "
        rm -rf $zPathOnHost &&\
        mkdir -p $zPathOnHost/.git_shadow &&\
\
        cd $zPathOnHost/.git_shadow &&\
        git init . &&\
        git config user.name "git_shadow" &&\
        git config user.email "git_shadow@$x" &&\
        git commit --allow-empty -m "__init__" &&\
        git branch -f server &&\
\
        cd $zPathOnHost &&\
        git init . &&\
        git config user.name "`basename $zPathOnHost`" &&\
        git config user.email "`basename ${zPathOnHost}`@$x" &&\
        git commit --allow-empty -m "__init__" &&\
        git branch -f server
        "

    scp zhost_post-update.sh git@${x}:${zPathOnHost}/.git/hooks/post-update

    ssh $x "
        eval sed -i 's%\<PROJ_PATH\>%${zPathOnHost}%g' ${zPathOnHost}/.git/hooks/post-update &&\
        chmod 0755 ${zPathOnHost}/.git/hooks/post-update
        "
done
