#!/bin/sh

# 预环境要求：
#   (1) /etc/ssh/sshd_config 中的 MaxStartups 参数指定为 1024 以上
#   (2) /etc/sysctl.conf 中的 net.core.somaxconn 参数指定为 1024 以上，之后执行 sysctl -p 使之立即生效

zServAddr=$1
zServPort=$2
zShadowPath="${HOME}/zgit_shadow"

cd $zShadowPath
git stash
#git pull  # 有时候不希望更新到最新代码

eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/post-update
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/post-update
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/zhost_init_repo.sh
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/zhost_init_repo.sh
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./tools/zhost_self_deploy.sh
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./tools/zhost_self_deploy.sh

killall zauto_restart.sh
killall -9 git
killall -9 git_shadow

mkdir -p ${zShadowPath}/bin
mkdir -p ${zShadowPath}/log
mkdir -p ${zShadowPath}/conf
touch ${zShadowPath}/conf/master.conf
rm -rf ${zShadowPath}/bin/*

# 编译正则库
# cd ${zShadowPath}/lib/pcre2
# if [[ 0 -ne $? ]]; then
#     cd ${zShadowPath}/lib
#     rm -rf pcre2*
#     wget https://ftp.pcre.org/pub/pcre/pcre2-10.23.tar.gz
#     mkdir pcre2
#     tar -xf pcre2-10.23.tar.gz
#     cd pcre2-10.23
#     ./configure --prefix=$HOME/zgit_shadow/lib/pcre2
#     make -j 9 && make install
# fi

# 编译主程序，静态库文件路径一定要放在源文件之后
cc -Wall -Wextra -std=c99 -O2 -lpthread \
    -D_XOPEN_SOURCE=700 \
    -I${zShadowPath}/inc \
    -I${zShadowPath}/lib/pcre2/include \
    -o ${zShadowPath}/bin/git_shadow \
    ${zShadowPath}/src/zmain.c \
    ${zShadowPath}/lib/pcre2/lib/libpcre2-8.a
strip ${zShadowPath}/bin/git_shadow

# 编译 notice 程序，用于通知主程序有新的提交记录诞生
cc -Wall -Wextra -std=c99 -O2 \
    -D_XOPEN_SOURCE=700 \
    -I${zShadowPath}/inc \
    -o ${zShadowPath}/tools/notice \
    ${zShadowPath}/src/extra/znotice.c
strip ${zShadowPath}/tools/notice

${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort 2>${zShadowPath}/log/log 1>&2

# 后台进入退出重启机制
#${zShadowPath}/tools/zauto_restart.sh $zServAddr $zServPort &
