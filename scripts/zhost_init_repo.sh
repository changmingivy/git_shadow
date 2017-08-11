#!/bin/sh
# TEST:PASS
zPathOnMaster=$1
zPathOnHost=`echo $zPathOnMaster | sed -n 's%/home/git/\+%/%p'`
zMajorHostAddrListPath="${zPathOnMaster}_SHADOW/info/host_ip_major.txt"
zAllHostAddrListPath="${zPathOnMaster}_SHADOW/info/host_ip_all.txt"
zOpsRootPath="/home/git/zgit_shadow/scripts"

zMajorIpList=`cat $zMajorHostAddrListPath`
zAllIpList=`cat $zAllHostAddrListPath`

for x in $zMajorIpList; do
    (\
        ssh $x "
            if [[ 0 -ne \`ls -d $zPathOnHost 2>/dev/null | wc -l\` ]];then exit; fi &&
            mkdir -p ${zPathOnHost} &&
            mkdir -p ${zPathOnHost}_SHADOW &&
\
            cd ${zPathOnHost}_SHADOW &&
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
    
        scp ${zOpsRootPath}/zhost_post-update.sh git@${x}:${zPathOnHost}/.git/hooks/post-update &&
        scp -r ${zPathOnMaster}_SHADOW/info git@${x}:${zPathOnHost}_SHADOW/ &&
        scp ${zOpsRootPath}/zhost_init_repo_slave.sh git@${x}:/home/git/zhost_init_repo_slave.sh &&
        scp ${zOpsRootPath}/zhost_post-update.sh git@${x}:/home/git/zhost_post-update.sh &&
    
        ssh $x "
            eval sed -i 's%_PROJ_PATH%${zPathOnHost}%g' ${zPathOnHost}/.git/hooks/post-update &&
            eval sed -i 's%_MASTER_ADDR%__MASTER_ADDR%g' ${zPathOnHost}/.git/hooks/post-update &&
            eval sed -i 's%_MASTER_PORT%__MASTER_PORT%g' ${zPathOnHost}/.git/hooks/post-update &&
            chmod 0755 ${zPathOnHost}/.git/hooks/post-update
            "
    
        ssh $x "
            for zAddr in \`ip addr | grep -oP '(\d+\.){3}\d+' | grep -vE '^(169|127|0|255)\.$'\`;do
                if [[ 0 -ne \`echo \"${zMajorIpList}\" | grep -c \$zAddr\` ]];then
                    zEcsAddrList=\`echo \"${zAllIpList}\" | tr '\n' ' '\`
                    for zEcsAddr in \$zEcsAddrList;do
                        if [[ \$zAddr == \$zEcsAddr ]];then continue; fi &&
                        sh /home/git/zhost_init_repo_slave.sh \$zEcsAddr $zPathOnHost
                    done
                    break
                fi
            done
            "
    ) &
done
