#!/bin/sh
# TEST:PASS
# 拉取server分支分代码到master分支；
# 通知中控机已收到代码；
# 判断自身是否是ECS分发节点，如果是，则向同一项目下的所有其它ECS推送最新收到的代码
export zProjPath="_PROJ_PATH"
export zEcsAddrMajorListPath="${zProjPath}_SHADOW/info/host_ip_major.txt"
export zEcsAddrListPath="${zProjPath}_SHADOW/info/host_ip_all.txt"

export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"
#unset $(git rev-parse --local-env-vars)  # 将 git hook 特定的环境变量值全部置为空

# 
cd ${zProjPath}_SHADOW &&
export GIT_DIR="${zProjPath}_SHADOW/.git" &&
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
for zAddr in $(ip addr | grep -oP '(\d+\.){3}\d+' | grep -vE '^(169|127|0|255)\.$'); do
    if [[ 0 -lt $(cat $zEcsAddrMajorListPath | grep -c $zAddr) ]]; then
        zEcsAddrList=$(cat $zEcsAddrListPath | grep -oP '(\d{1,3}\.){3}\d{1,3}')
        for zEcsAddr in $zEcsAddrList; do
            if [[ $zAddr == $zEcsAddr ]];then continue; fi

            ( \
                export GIT_DIR="${zProjPath}_SHADOW/.git" \
                && cd ${zProjPath}_SHADOW \
                && git push --force git@${zEcsAddr}:${zProjPath}_SHADOW/.git master:server \
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
cd ${zProjPath}_SHADOW
./bin/git_shadow_client -h _MASTER_ADDR -p _MASTER_PORT
i=0
while [[ 10 -gt $i ]]; do
    sleep 1
    ./bin/git_shadow_client -h _MASTER_ADDR -p _MASTER_PORT
    let i++
done
