#!/bin/sh
zCommitSig=$1
zPathOnHost=$(echo $2 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zMajorAddr=$3  # 中转机IPv4地址

shift 3
zHostList=$@
zShadowPath=/home/git/zgit_shadow

zOps() {
    if [[ "" == ${zCommitSig}
        || "" == ${zPathOnHost}
        || "" == ${zMajorAddr}
        || "" == ${zHostList} ]]; then
        exit 1
    fi

    cd /home/git/${zPathOnHost}
    if [[ 0 -ne $? ]]; then exit 1; fi  # 当指定的路径不存在，此句可防止 /home/git 下的项目文件被误删除

    \ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf
    git stash
    git checkout server 
    git branch -D master
    git checkout -b master
    git reset --hard ${zCommitSig}
    find . | grep -vE '(^|/)\.git($|/)' | sort | cpio -o > /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.cpio && sha1sum /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.cpio | grep -oP '^\S+' > /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.txt
    rm /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.cpio

    # 更新中转机(MajorHost)
    cd /home/git/${zPathOnHost}_SHADOW
    cp -uR ${zShadowPath}/tools/* ./tools/
    chmod 0755 ./tools/post-update
    eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./tools/post-update

    git add --all .
    git commit -m "__DP__"
    git push --force git@${zMajorAddr}:${zPathOnHost}_SHADOW/.git master:server

    cd /home/git/${zPathOnHost}
    git push --force git@${zMajorAddr}:${zPathOnHost}/.git master:server

    # 通过中转机布署到终端集群
    ssh $zMajorAddr "
        for zHostAddr in $zHostList; do
            (\
                cd ${zPathOnHost}_SHADOW &&\
                git push --force git@\${zHostAddr}:${zPathOnHost}_SHADOW/.git server:server;\
                cd ${zPathOnHost} &&\
                git push --force git@\${zHostAddr}:${zPathOnHost}/.git server:server\
            )&
        done
    "

    # 中控机：布署后环境设置
    cd /home/git/${zPathOnHost}
    zOldSig=`git log CURRENT -1 --format=%H`
    git branch -f $zOldSig  # 创建一个以原有的 CURRENT 分支的 SHA1 sig 命名的分支
    git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
}

echo -e "====[`date`]====\n" >> /home/git/${zPathOnHost}_SHADOW/log/${zCommitSig}.log 2>&1
zOps >> /home/git/${zPathOnHost}_SHADOW/log/${zCommitSig}.log 2>&1
echo -e "\n\n\n\n" >> /home/git/${zPathOnHost}_SHADOW/log/${zCommitSig}.log 2>&1
