#!/usr/bin/env bash
zProjId=$1
zCommitSig=$2
zPathOnHost=$(printf $3 | sed -n 's%/\+%/%p')  # 布署目标上的绝对路径，处理掉可能存在的多个连续的 '/'
zProxyHostAddr=$4  # 中转机IPv4地址
zHostList=$5

zServBranchName="server${zProjId}"
zShadowPath=/home/git/zgit_shadow

zOps() {
    if [[ "" == ${zProjId}
        || "" == ${zCommitSig}
        || "" == ${zPathOnHost}
        || "" == ${zProxyHostAddr}
        || "" == ${zHostList} ]]; then
        exit 1
    fi

    cd /home/git/${zPathOnHost}
    if [[ 0 -ne $? ]]; then exit 1; fi  # 当指定的路径不存在，此句可防止 /home/git 下的项目文件被误删除

    \ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf
    git stash
    git stash clear
    git pull --force ./.git ${zServBranchName}:master
    git reset --hard ${zCommitSig}

    # 用户指定的在部置之前执行的操作
    bash ____pre-deploy.sh
    git add --all .
    git commit -m "____pre-deploy.sh"

    find . -path './.git' -prune -o -type f -print | fgrep -v ' ' | sort | xargs cat | sha1sum | grep -oP '^\S+' > /home/git/${zPathOnHost}_SHADOW/.____dp-SHA1.res

    # 更新中转机(MajorHost)
    cd /home/git/${zPathOnHost}
    git push --force git@${zProxyHostAddr}:${zPathOnHost}/.git master:${zServBranchName}

    cd /home/git/${zPathOnHost}_SHADOW
    rm -rf ./tools
    cp -R ${zShadowPath}/tools ./
    chmod 0755 ./tools/post-update
    eval sed -i 's%__PROJ_PATH%${zPathOnHost}%g' ./tools/post-update
    git add --all .
    git commit --allow-empty -m "_"  # 提交一次空记录，用于保证每次推送 post-upate 都能执行
    git push --force git@${zProxyHostAddr}:${zPathOnHost}_SHADOW/.git master:${zServBranchName}

    # 通过中转机布署到终端集群，先推项目代码，后推 <_SHADOW>
    ssh $zProxyHostAddr "
        for zHostAddr in $zHostList; do
            (\
                cd ${zPathOnHost} &&\
                git push --force git@\${zHostAddr}:${zPathOnHost}/.git ${zServBranchName}:${zServBranchName};\
                cd ${zPathOnHost}_SHADOW &&\
                git push --force git@\${zHostAddr}:${zPathOnHost}_SHADOW/.git ${zServBranchName}:${zServBranchName}\
            )&
        done
    "

    # 中控机：布署后环境设置
    cd /home/git/${zPathOnHost}
    zOldSig=`git log CURRENT -1 --format=%H`
    git branch -f $zOldSig  # 创建一个以原有的 CURRENT 分支的 SHA1 sig 命名的分支
    git branch -f CURRENT  # 下一次布署的时候会冲掉既有的 CURRENT 分支
}

printf "\n\n\n\n====[`date`]====\n" >> /home/git/${zPathOnHost}_SHADOW/log/${zCommitSig}.log 2>&1
zOps >> /home/git/${zPathOnHost}_SHADOW/log/${zCommitSig}.log 2>&1
