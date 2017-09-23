#!/bin/sh
# 每隔 0.5 秒扫描一次，若服务程序已退出，重启之
# 多线程程序永远不能说已通过充分的测试，此脚本有一直存在的必要

zServAddr=$1
zServPort=$2

while :
do
    if [[ 1 -gt `pgrep -x -u git -U git "^git_shadow$" | wc -l` ]]; then
        /home/git/zgit_shadow/bin/git_shadow -f /home/git/zgit_shadow/conf/master.conf -h $zServAddr -p $zServPort
        printf "`date`\n" >> /tmp/zgit_shadow_restart_cnt
    fi

    sleep 0.5
done
