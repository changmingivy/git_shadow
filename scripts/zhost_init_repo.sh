#!/bin/sh
zMajorAddr=$1
zSlaveAddr=$2
zPathOnHost=$(echo $3 | sed -n 's%/\+%/%p')

zSelfPid=$$  # 获取自身PID
zTmpFile=`mktemp /tmp/${zSelfPid}.XXXXXXXX`
echo $zSelfPid > $zTmpFile

(sleep 5; if [[ "" != `cat $zTmpFile` ]]; then kill -9 $zSelfPid; fi; rm $zTmpFile) &  # 防止遇到无效IP时长时间卡住

ssh -t $zMajorAddr "ssh $zSlaveAddr \"
    mkdir -p ${zPathOnHost}
    mkdir -p ${zPathOnHost}_SHADOW
\
    cd ${zPathOnHost}_SHADOW
    git init .
    git config user.name "git_shadow"
    git config user.email "git_shadow@$x"
    git commit --allow-empty -m "__init__"
    git branch -f server
    echo ${zSlaveAddr} > /home/git/zself_ipv4_addr.txt
\
    cd $zPathOnHost
    git init .
    git config user.name "_"
    git config user.email "_@$x"
    git commit --allow-empty -m "__init__"
    git branch -f server
\
    cat > .git/hooks/post-update
    chmod 0755 .git/hooks/post-update
    \"" < /home/git/${zPathOnHost}_SHADOW/scripts/post-update

echo "" > $zTmpFile  # 提示后台监视线程已成功执行，不要再kill，防止误杀其它进程
