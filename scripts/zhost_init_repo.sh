#!/bin/sh
# TEST:PASS
zPathOnMaster=$1
zPathOnHost=`echo $1 | grep -oP '(?<=/home/git).*'`
zMajorHostAddrListPath="${zPathOnMaster}/.git_shadow/info/host_ip_major.txt"
zAllHostAddrListPath="${zPathOnMaster}/.git_shadow/info/host_ip_all.txt"

zMajorIpList=`cat $zMajorHostAddrListPath`
zAllIpList=`cat $zAllHostAddrListPath`

for x in $zMajorIpList; do
    (\
        ssh $x "
            if [[ 0 -ne \`ls -d $zPathOnHost | wc -l\` ]];then exit; fi &&
            mkdir -p $zPathOnHost/.git_shadow &&
\
            cd $zPathOnHost/.git_shadow &&
            git init . &&
            git config user.name "git_shadow" &&
            git config user.email "git_shadow@$x" &&
            git commit --allow-empty -m "__init__" &&
            git branch -f server &&
\
            cd $zPathOnHost &&
            git init . &&
            git config user.name "`basename $zPathOnHost`" &&
            git config user.email "`basename ${zPathOnHost}`@$x" &&
            git commit --allow-empty -m "__init__" &&
            git branch -f server
            "
    
        scp zhost_post-update.sh git@${x}:${zPathOnHost}/.git/hooks/post-update &&
        scp -r $zPathOnMaster/.git_shadow/info git@${x}:${zPathOnHost}/.git_shadow/ &&
        scp zhost_init_repo_slave.sh git@${x}:/tmp/zhost_init_repo_slave.sh &&
        scp zhost_post-update.sh git@${x}:/tmp/zhost_post-update.sh &&
    
        ssh $x "
            eval sed -i 's%\<PROJ_PATH\>%${zPathOnHost}%g' ${zPathOnHost}/.git/hooks/post-update &&
            chmod 0755 ${zPathOnHost}/.git/hooks/post-update
            "
    
        ssh $x "
            PATH="/sbin:\$PATH" &&
            for zAddr in \`ifconfig | grep -oP '(\d+\.){3}\d+' | grep -vE '^(169|127|0|255)\.|\.255$'\`;do
                if [[ 0 -ne \`echo \"${zMajorIpList}\" | grep -c \$zAddr\` ]];then
                    zEcsAddrList=\`echo \"${zAllIpList}\" | tr '\n' ' '\`
                    for zEcsAddr in \$zEcsAddrList;do
                        if [[ \$zAddr == \$zEcsAddr ]];then continue; fi &&
                        sh /tmp/zhost_init_repo_slave.sh \$zEcsAddr $zPathOnHost
                    done
                    break
                fi
            done
            "
    ) &
done
