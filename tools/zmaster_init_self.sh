#!/usr/bin/env bash

# 预环境要求：
#   (1) /etc/ssh/sshd_config 中的 MaxStartups 参数指定为 1024 以上
#   (2) /etc/sysctl.conf 中的 net.core.somaxconn 参数指定为 1024 以上，之后执行 sysctl -p 使之立即生效
#   (3) yum install openssl-devel

# 布署系统全局共用变量
export zGitShadowPath=${HOME}/zgit_shadow2

zServAddr=$1
zServPort=$2
zShadowPath=$zGitShadowPath  # 系统全局变量 $zGitShadowPath

cd $zShadowPath
#git stash
#git pull  # 有时候不希望更新到最新代码

eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/post-update
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/post-update
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/zhost_self_deploy.sh
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/zhost_self_deploy.sh

kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -oP "\d+(?=\s+\w*\s*${zShadowPath}/tools/zauto_restart.sh)"`
kill -9 `ps ax -o pid,cmd | grep -v 'grep' | grep -oP "\d+(?=\s+${zShadowPath}/bin/git_shadow)"`

mkdir -p ${zShadowPath}/bin
mkdir -p ${zShadowPath}/log
mkdir -p ${zShadowPath}/conf
touch ${zShadowPath}/conf/master.conf
rm -rf ${zShadowPath}/bin/*

# build libpcre2
# wget https://ftp.pcre.org/pub/pcre/pcre2-10.23.tar.gz
# mkdir ${zShadowPath}/lib/libpcre2_source/____build
# if [[ 0 -eq $? ]]; then
#     cd ${zShadowPath}/lib/libpcre2_source/____build && rm -rf * .*
#     cmake .. \
#         -DCMAKE_INSTALL_PREFIX=${zShadowPath}/lib/libpcre2 \
#         -DBUILD_SHARED_LIBS=ON \
#         -DPCRE2_BUILD_PCRE2GREP=OFF \
#         -DPCRE2_BUILD_TESTS=OFF
#     cmake --build . --target install
# fi
# zLibPcrePath=${zShadowPath}/lib/libpcre2/lib64
# if [[ 0 -eq  `ls ${zLibPcrePath} | wc -l` ]]; then zLibPcrePath=${zShadowPath}/lib/libpcre2/lib; fi

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

# 主程序编译
cd ${zShadowPath}/src && make SSH_LIB_DIR=${zLibSshPath} GIT_LIB_DIR=${zLibGitPath} install
# strip ${zShadowPath}/bin/git_shadow  # RELEASE 版本

# 编译 notice 程序，用于通知主程序有新的提交记录诞生
clang -Wall -Wextra -std=c99 -O2 \
    -I${zShadowPath}/inc \
    -o ${zShadowPath}/tools/notice \
    ${zShadowPath}/src/zExtraUtils/znotice.c
strip ${zShadowPath}/tools/notice

export LD_LIBRARY_PATH=${zShadowPath}/lib/libssh2/lib64:${zShadowPath}/lib/libgit2/lib:$LD_LIBRARY_PATH
${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort >>${zShadowPath}/log/ops.log 2>>${zShadowPath}/log/err.log

# 后台进入退出重启机制
# ${zShadowPath}/tools/zauto_restart.sh $zServAddr $zServPort &
