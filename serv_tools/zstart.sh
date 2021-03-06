#!/usr/bin/env bash

# 预环境要求：
#   (1) /etc/ssh/sshd_config 中的 MaxStartups 参数指定为 1024 以上
#   (2) /etc/sysctl.conf 中的 net.core.somaxconn 参数指定为 1024 以上，之后执行 sysctl -p 使之立即生效
#   (3) 安装 openssl 开发库：CentOS/openssl-devel、Debian/openssl-dev

# 布署系统全局共用变量
export zGitShadowPath=${HOME}/zgit_shadow
export zPgPath=${HOME}/.____PostgreSQL

zServAddr=$1
zServPort=$2
zSystemd=$3
zShadowPath=$zGitShadowPath  # 系统全局变量 $zGitShadowPath

cd $zShadowPath
#git stash
#git pull  # 有时候不希望更新到最新代码

mkdir -p ${zShadowPath}/log
mkdir -p ${zShadowPath}/bin
rm -rf ${zShadowPath}/bin/*
if [[ 0 == `\ls ${HOME}/.pgpass | wc -l` ]]; then
    echo "# hostname:port:database:username:password" > ${HOME}/.pgpass
fi

# build postgreSQL
if [[ 0 -eq `\ls -d ${zPgPath} | wc -l` ]]; then
    cd ${zShadowPath}/lib
    if [[ 0 -eq `\ls postgresql-10.1.tar.bz2 | wc -l` ]]; then
        wget https://ftp.postgresql.org/pub/source/v10.1/postgresql-10.1.tar.bz2
    fi
    tar -xf postgresql-10.1.tar.bz2
    cd postgresql-10.1
    if [[ "" == ${zSystemd} ]]; then
        ./configure --prefix=${zPgPath}
    else
        ./configure --prefix=${zPgPath} --with-systemd
    fi
    make -j5 && make install
fi

# start postgresql
zPgLibPath=${zPgPath}/lib
zPgBinPath=${zPgPath}/bin
zPgDataPath=${zPgPath}/data

# PG: 留空表示仅接受本地 UNIX 域套接字连接
sed -i '/#*listen_addresses =/d' ${zPgDataPath}/postgresql.conf
echo "listen_addresses = ''" >> ${zPgDataPath}/postgresql.conf

# PG: 以源码根路径作为 UNIX 域套接字存放路径
sed -i '/#*unix_socket_directories =/d' ${zPgDataPath}/postgresql.conf
echo "unix_socket_directories = '${zShadowPath}'" >> ${zPgDataPath}/postgresql.conf

# PG: UNIX 域套接字权限
sed -i '/#*unix_socket_permissions =/d' ${zPgDataPath}/postgresql.conf
echo "unix_socket_permissions = 0700" >> ${zPgDataPath}/postgresql.conf

# PG: 最大连接数
sed -i '/#*max_connections =/d' ${zPgDataPath}/postgresql.conf
echo "max_connections = 1024" >> ${zPgDataPath}/postgresql.conf

# PG: 事务锁上限
sed -i '/#*max_locks_per_transaction =/d' ${zPgDataPath}/postgresql.conf
echo "max_locks_per_transaction = 512" >> ${zPgDataPath}/postgresql.conf

# PG: shared buffers siz，设置为总内存的 1/3
sed -i '/#*shared_buffers =/d' ${zPgDataPath}/postgresql.conf
echo "shared_buffers = $((`free -m | fgrep -i 'mem' | awk -F' ' '{print $2}'` / 3))MB" >> ${zPgDataPath}/postgresql.conf

# PG: max_wal_size，设置为总内存的 1/2
sed -i '/#*max_wal_size =/d' ${zPgDataPath}/postgresql.conf
echo "max_wal_size = $((`free -m | fgrep -i 'mem' | awk -F' ' '{print $2}'` / 2))MB" >> ${zPgDataPath}/postgresql.conf

# PG: work_mem，设置为 64MB
sed -i '/#*work_mem =/d' ${zPgDataPath}/postgresql.conf
echo "work_mem = 64MB" >> ${zPgDataPath}/postgresql.conf

# PG: max_stack_depth，设置为系统线程栈的大小 - 1M
sed -i '/#*max_stack_depth =/d' ${zPgDataPath}/postgresql.conf
echo "max_stack_depth = $((`ulimit -s` / 1024 - 1))MB" >> ${zPgDataPath}/postgresql.conf

${zPgBinPath}/pg_ctl -D ${zPgDataPath} initdb
${zPgBinPath}/pg_ctl stop -D ${zPgDataPath} -l ${zPgDataPath}/log
${zPgBinPath}/pg_ctl start -D ${zPgDataPath} -l ${zPgDataPath}/log
${zPgBinPath}/createdb -O `whoami` svdp

# 需要 root 权限，防止 postgresql 主进程被 linux OOM_killer 杀掉
# zPgPid=`head -1 ${zPgDataPath}/postmaster.pid`
# (echo -1000 > /proc/$pid/oom_score_adj; echo -17 > /proc/$pid/oom_adj) 2>${zPgDataPath}/log

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
if [[ 0 -eq  `\ls ${zLibSshPath} | wc -l` ]]; then zLibSshPath=${zShadowPath}/lib/libssh2/lib; fi

# build libgit2
# 线程安全由调度者保证，不需要此库自身保证
mkdir ${zShadowPath}/lib/libgit2_source/____build
if [[ 0 -eq $? ]]; then
    cd ${zShadowPath}/lib/libgit2_source/____build && rm -rf * .*
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=${zShadowPath}/lib/libgit2 \
        -DLIBSSH2_INCLUDEDIR=${zShadowPath}/lib/libssh2/include \
        -DLIBSSH2_LIBDIR=`dirname ${zLibSshPath}` \
        -DBUILD_SHARED_LIBS=ON \
        -DTHREADSAFE=ON\
        -DBUILD_CLAR=OFF
    cmake --build . --target install
fi
zLibGitPath=${zShadowPath}/lib/libgit2/lib64
if [[ 0 -eq  `\ls ${zLibGitPath} | wc -l` ]]; then zLibGitPath=${zShadowPath}/lib/libgit2/lib; fi

# 主程序编译
cd ${zShadowPath}/src &&
    make SSH_LIB_DIR=${zLibSshPath} GIT_LIB_DIR=${zLibGitPath} PG_LIB_DIR=${zPgLibPath} install &&
    make clean
strip ${zShadowPath}/bin/git_shadow  # RELEASE 版本

killall -SIGUSR1 git_shadow

export LD_LIBRARY_PATH=${zLibSshPath}:${zLibGitPath}:${zPgLibPath}:${LD_LIBRARY_PATH}
${zShadowPath}/bin/git_shadow\
    -x ${zShadowPath}\
    -u `whoami`\
    -h $zServAddr\
    -p $zServPort\
    -U `whoami` >> ${zShadowPath}/log/ops.log 2>>${zShadowPath}/log/err.log


##################################################################################################

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
# if [[ 0 -eq  `\ls ${zLibPcrePath} | wc -l` ]]; then zLibPcrePath=${zShadowPath}/lib/libpcre2/lib; fi

##################################################################################################
