#!/bin/sh
zServAddr=$1
zServPort=$2
zShadowPath="/home/git/zgit_shadow"

git stash
git pull
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./zhost_init_repo.sh
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./zhost_init_repo.sh
eval sed -i 's%__MASTER_ADDR%${zServAddr}%g' ./zhost_init_repo_slave.sh
eval sed -i 's%__MASTER_PORT%${zServPort}%g' ./zhost_init_repo_slave.sh

killall -9 git 2>/dev/null
killall -9 git_shadow 2>/dev/null

mkdir -p ${zShadowPath}/bin
mkdir -p ${zShadowPath}/log
rm -rf ${zShadowPath}/bin/*

cc -O2 -Wall -Wextra -std=c99 \
    -I ${zShadowPath}/inc \
    -L ${zShadowPath}/lib/pcre2 \
    -lm \
    -lpthread \
    -lpcre2-8 \
    -D_XOPEN_SOURCE=700 \
    -o ${zShadowPath}/bin/git_shadow \
    ${zShadowPath}/src/zmain.c

strip ${zShadowPath}/bin/git_shadow

cc -O2 -Wall -Wextra -std=c99 \
    -I ${zShadowPath}/inc \
    -D_XOPEN_SOURCE=700 \
    -o ${zShadowPath}/bin/git_shadow_client \
    ${zShadowPath}/src/client/zmain_client.c

strip ${zShadowPath}/bin/git_shadow_client
printf "    `date +%s`" >> ${zShadowPath}/bin/git_shadow_client  # 末尾追加随机字符，防止git不识别二进制文件变动

${zShadowPath}/bin/git_shadow -f ${zShadowPath}/conf/master.conf -h $zServAddr -p $zServPort 2>${zShadowPath}/log/log 1>&2
