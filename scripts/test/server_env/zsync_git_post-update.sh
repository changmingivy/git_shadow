#!/bin/sh
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    export GIT_DIR="/home/git/miaopai/.git" # 设定git hook 工作路径，默认为'.'，即hook文件所在路径，会带来异常

    cd /home/git/miaopai &&
    git pull --force ./.git server:master &&

    rm -rf ./.git_shadow &&
    cp -rf /home/git/miaopai_shadow ./.git_shadow
