#!/bin/sh
# TEST:PASS
# 拉取server分支分代码到master分支；
# 通知中控机已收到代码；
# 判断自身是否是ECS分发节点，如果是，则向同一项目下的所有其它ECS推送最新收到的代码
export zProjPath="<PROJ_PATH>"
export zEcsAddrMajorListPath="${zProjPath}/.git_shadow/info/host_ip_major.txt"
export zEcsAddrListPath="${zProjPath}/.git_shadow/info/host_ip_all.txt"

export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"
#unset $(git rev-parse --local-env-vars)  # 将 git hook 特定的环境变量值全部置为空

# git_shadow 代码作为内联库存在
cd $zProjPath/.git_shadow &&
export GIT_DIR="${zProjPath}/.git_shadow/.git" &&
git add --all . &&
git commit --allow-empty -m "_" &&
git checkout server &&
git checkout -b TMP &&
git reset -q --hard &&  # 注：代码初始状态只是接收到git库中，需要将其reset至工作区路径
git branch -M master &&

cd $zProjPath &&  # 必须首先切换路径，否则 reset 不会执行
export GIT_DIR="${zProjPath}/.git" &&
git add --all . &&
git commit --allow-empty -m "_" &&
git checkout server &&
git checkout -b TMP &&
git reset -q --hard &&  # 注：代码初始状态只是接收到git库中，需要将其reset至工作区路径
git branch -M master &&

# 检测自身是否是负责对接中控机的主HOST，若是，则向集群主机推送代码
for zAddr in $(ifconfig | grep -oP '(\d+\.){3}\d+' | grep -vE '^(169|127|0|255)\.|\.255$'); do
    if [[ 0 -lt $(cat $zEcsAddrMajorListPath | grep -c $zAddr) ]]; then
        zEcsAddrList=$(cat $zEcsAddrListPath | tr '\n' ' ')
        for zEcsAddr in $zEcsAddrList; do
            if [[ $zAddr == $zEcsAddr ]];then continue; fi

            ( \
                export GIT_DIR="${zProjPath}/.git_shadow/.git" \
                && cd ${zProjPath}/.git_shadow \
                && git push --force git@${zEcsAddr}:${zProjPath}/.git_shadow/.git master:server \
                \
                && export GIT_DIR="${zProjPath}/.git" \
                && cd ${zProjPath} \
                && git push --force git@${zEcsAddr}:${zProjPath}/.git master:server \
            ) &
        done
        break
    fi
done

killall -9 git_shadow_client 2>/dev/null
${zProjPath}/.git_shadow/bin/git_shadow_client -h <MASTER_ADDR> -p <MASTER_PORT>
i=0
while [[ 10 -gt $i ]]; do
    sleep 1
    ${zProjPath}/.git_shadow/bin/git_shadow_client -h <MASTER_ADDR> -p <MASTER_PORT>
    let i++
done
