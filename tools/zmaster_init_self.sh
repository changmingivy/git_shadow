#!/usr/bin/env bash

# 预环境要求：
#   (1) /etc/ssh/sshd_config 中的 MaxStartups 参数指定为 1024 以上
#   (2) /etc/sysctl.conf 中的 net.core.somaxconn 参数指定为 1024 以上，之后执行 sysctl -p 使之立即生效
#   (3) yum install openssl-devel

zServAddr=$1
zServPort=$2
zShadowPath="${HOME}/zgit_shadow"

cd $zShadowPath
#git stash
#git pull  # 有时候不希望更新到最新代码

eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/post-update
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/post-update

kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -oP '\d+(?=.*zauto_restart.sh)'`
kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -oP '\d+(?=.*git_shadow)'`

mkdir -p ${zShadowPath}/bin
mkdir -p ${zShadowPath}/log
mkdir -p ${zShadowPath}/conf
touch ${zShadowPath}/conf/master.conf
rm -rf ${zShadowPath}/bin/*

# 编译 libssh2 静态库
mkdir -p ${zShadowPath}/lib/libssh2_source/____build
cd ${zShadowPath}/lib/libssh2_source/____build && rm -rf * .*
cmake -DCMAKE_INSTALL_PREFIX=${zShadowPath}/lib/libssh2 -DBUILD_SHARED_LIBS=OFF ..
cmake --build . --target install

# 编译主程序，静态库文件路径一定要放在源文件之后
cc -Wall -Wextra -std=c99 -O2 -lpthread -lssl -lcrypto \
    -I${zShadowPath}/inc \
    -I${zShadowPath}/lib/libssh2/include \
    -o ${zShadowPath}/bin/git_shadow \
    ${zShadowPath}/src/zmain.c\
    ${zShadowPath}/lib/libssh2/lib64/libssh2.a  # 必须在此之前链接 -lssl -lcrypto
strip ${zShadowPath}/bin/git_shadow

# 编译 zssh_XXXX 程序，用于中转机代替 OpenSSH 更高效并发执行 SSH 连接
cc -Wall -Wextra -std=c99 -O2 -lpthread -lssl -lcrypto \
    -I${zShadowPath}/inc \
    -I${zShadowPath}/lib/libssh2/include \
    -o ${zShadowPath}/tools/zssh \
    ${zShadowPath}/src/extra/zssh_cli.c\
    ${zShadowPath}/lib/libssh2/lib64/libssh2.a  # 必须在此之前链接 -lssl -lcrypto
strip ${zShadowPath}/tools/zssh

# 编译 notice 程序，用于通知主程序有新的提交记录诞生
cc -Wall -Wextra -std=c99 -O2 \
    -I${zShadowPath}/inc \
    -o ${zShadowPath}/tools/notice \
    ${zShadowPath}/src/extra/znotice.c
strip ${zShadowPath}/tools/notice

${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort >>${zShadowPath}/log/ops.log 2>>${zShadowPath}/log/err.log

# 后台进入退出重启机制
#${zShadowPath}/tools/zauto_restart.sh $zServAddr $zServPort &
