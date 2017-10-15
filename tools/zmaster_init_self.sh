#!/usr/bin/env bash

# 预环境要求：
#   (1) /etc/ssh/sshd_config 中的 MaxStartups 参数指定为 1024 以上
#   (2) /etc/sysctl.conf 中的 net.core.somaxconn 参数指定为 1024 以上，之后执行 sysctl -p 使之立即生效
#   (3) yum install openssl-devel

# 布署系统全局共用变量
export zGitShadowPath=/home/git/zgit_shadow2

zServAddr=$1
zServPort=$2
zShadowPath=$zGitShadowPath  # 系统全局变量 $zGitShadowPath

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

# build libssh2
mkdir ${zShadowPath}/lib/libssh2_source/____build
if [[ 0 -eq $? ]]; then
    cd ${zShadowPath}/lib/libssh2_source/____build && rm -rf * .*
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=${zShadowPath}/lib/libssh2 \
        -DBUILD_SHARED_LIBS=ON
    cmake --build . --target install
fi
zLibSshPath=${zShadowPath}/lib/libssh2/lib64
if [[ 0 -eq  `ls ${zLibSshPath} | wc -l` ]]; then zLibSshPath=${zShadowPath}/lib/libssh2/lib; fi

# build libgit2
mkdir ${zShadowPath}/lib/libgit2_source/____build
if [[ 0 -eq $? ]]; then
    cd ${zShadowPath}/lib/libgit2_source/____build && rm -rf * .*
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=${zShadowPath}/lib/libgit2 \
        -DLIBSSH2_INCLUDEDIR=${zShadowPath}/lib/libssh2/include \
        -DLIBSSH2_LIBDIR=`dirname ${zLibSshPath}` \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_CLAR=OFF
    cmake --build . --target install
fi
zLibGitPath=${zShadowPath}/lib/libgit2/lib64
if [[ 0 -eq  `ls ${zLibGitPath} | wc -l` ]]; then zLibGitPath=${zShadowPath}/lib/libgit2/lib; fi

# 编译主程序，静态库文件路径一定要放在源文件之后，如查使用静态库，则必须在此之前链接 zlib curl openssl crypto (-lz -lcurl -lssl -lcrypto)
clang -Wall -Wextra -std=c99 -O2 -lpthread \
    -I${zShadowPath}/inc \
    -I${zShadowPath}/lib/libssh2/include \
    -I${zShadowPath}/lib/libgit2/include \
    -L${zLibSshPath} \
    -L${zLibGitPath} \
    -lssh2 \
    -lgit2 \
    -o ${zShadowPath}/bin/git_shadow \
    ${zShadowPath}/src/zmain.c
strip ${zShadowPath}/bin/git_shadow

# 编译 notice 程序，用于通知主程序有新的提交记录诞生
clang -Wall -Wextra -std=c99 -O2 \
    -I${zShadowPath}/inc \
    -o ${zShadowPath}/tools/notice \
    ${zShadowPath}/src/extra/znotice.c
strip ${zShadowPath}/tools/notice

export LD_LIBRARY_PATH=${zShadowPath}/lib/libssh2/lib64:${zShadowPath}/lib/libgit2/lib:$LD_LIBRARY_PATH
${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort >>${zShadowPath}/log/ops.log 2>>${zShadowPath}/log/err.log

# 后台进入退出重启机制
# ${zShadowPath}/tools/zauto_restart.sh $zServAddr $zServPort &
