#!/bin/sh
zServAddr=$1
zServPort=$2
zShadowPath="${HOME}/zgit_shadow"

cd $zShadowPath
git stash
git pull
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./scripts/post-update
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./scripts/post-update

killall zauto_restart.sh
killall -9 git
killall -9 git_shadow

mkdir -p ${zShadowPath}/bin
mkdir -p ${zShadowPath}/log
mkdir -p ${zShadowPath}/conf
touch ${zShadowPath}/conf/master.conf
rm -rf ${zShadowPath}/bin/*

# 编译正则库
cd ${zShadowPath}/lib/
rm -rf pcre2*
wget https://ftp.pcre.org/pub/pcre/pcre2-10.23.tar.gz
mkdir pcre2
tar -xf pcre2-10.23.tar.gz
cd pcre2-10.23
./configure --prefix=$HOME/zgit_shadow/lib/pcre2
make -j 9 && make install

# 编译主程序，静态库文件路径一定要放在源文件之后
cc -Wall -Wextra -std=c99 -O2 -lpthread \
    -D_XOPEN_SOURCE=700 \
    -I${zShadowPath}/inc \
    -o ${zShadowPath}/bin/git_shadow \
    ${zShadowPath}/src/zmain.c \
    ${zShadowPath}/lib/pcre2/lib/libpcre2-8.a

strip ${zShadowPath}/bin/git_shadow

# 编译客户端
cc -O2 -Wall -Wextra -std=c99 \
    -I ${zShadowPath}/inc \
    -D_XOPEN_SOURCE=700 \
    -o ${zShadowPath}/bin/git_shadow_client \
    ${zShadowPath}/src/client/zmain_client.c

strip ${zShadowPath}/bin/git_shadow_client

${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort 2>${zShadowPath}/log/log 1>&2

# 后台进入退出重启机制
./zauto_restart.sh $zServAddr $zServPort &
