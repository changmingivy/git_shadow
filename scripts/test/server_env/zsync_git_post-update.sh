#!/bin/sh
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    zDeployPath=/home/git/miaopai
    export GIT_DIR="${zDeployPath}/.git" # 设定git hook 工作路径，默认为'.'，即hook文件所在路径，会带来异常

    cd $zDeployPath &&
    git pull --force ./.git server:master &&

    cp -rf ${zDeployPath}_shadow ./.git_shadow
