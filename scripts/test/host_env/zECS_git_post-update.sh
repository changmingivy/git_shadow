#!/bin/sh
# 拉取server分支分代码到client分支；通知中控机已收到代码；判断自身是否是ECS分发节点，如果是，则向同一项目下的所有其它ECS推送最新收到的代码

zCodePath="/home/git/miaopai"
zEcsAddrMajorListPath="${zCodePath}/.git_shadow/info/host_ip_major.txt"
zEcsAddrListPath="${zCodePath}/.git_shadow/info/host_ip_all.txt"

export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"
export GIT_DIR="${zCodePath}/.git"  # 设定 git hook 工作路径，默认为 '.'，即 hook 文件所在路径，会带来异常
#unset $(git rev-parse --local-env-vars)  # 将 git hook 特定的环境变量值全部置为空

cd $zCodePath &&  # 必须首先切换路径，否则 reset 不会执行

git checkout server &&
git checkout -b TMP &&
git reset -q --hard &&  # 注：代码初始状态只是接收到git库中，需要将其reset至工作区路径
git branch -M client &&

(
    killall -9 git_shadow
    ${zCodePath}/.git_shadow/bin/git_shadow_client -h 10.30.2.126 -p 20000
    i=0
    while [[ 0 -ne $? && 3 -gt $i ]]; do
        sleep 1
        ${zCodePath}/.git_shadow/bin/git_shadow_client -t 10.30.2.126 -p 20000
        let i++
    done
) &

# 检测自身是否是负责对接中控机的主HOST，若是，则向集群主机推送代码
for zAddr in $(ifconfig | grep -oP '(d+.){3}d+' | grep -vE '^(127|0|255).|.255$'); do
    if [[ 0 -lt $(cat $zEcsAddrMajorListPath | grep -c $zAddr) ]]; then
        zEcsAddrList=$(cat $zEcsAddrListPath | tr '\n' ' ')
        for zEcsAddr in $zEcsAddrList; do
            git push --force git@${zEcsAddr}:${zCodePath}/.git client:server &
        done
        break
    fi
done
