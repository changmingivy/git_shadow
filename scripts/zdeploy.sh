#!/usr/bin/env bash

# FUCNTION: deploy code
# RETURN:Prev commit id if success, or -1 if failed
# $1:major ECS hosts(IP addr: 10.10.10.10 etc.) to deploy to
# $2:code repo path
# $@(after shift):files to deploy
zdeploy() {
    if [[ '' == $zCodePath ]]; then zCodePath=`pwd`; fi

    local zEcsList=`cat $zCodePath/.git_shadow/info/client_ip_major.txt`
    cd $zCodePath

    zCommitContent=`git log -n 1 | tail -n 1`
    git reset CURRENT  # 将 master 分支提交状态回退到 CURRENT 分支状态，即上一次布署的状态

    if [[ 0 -eq $# ]];then  # If no file name given, meaning deploy all
        git add --all .
    else
        git add $@
    fi
    git commit -m "[DEPLOY]:=>${zCommitContent}"  # Maybe reveive contents from frontend?

    local i=0
    local j=0
    for zEcs in $zEcsList
    do
        let i++
        git push --force git@${zEcs}:${zCodePath}/.git master:server &

        if [[ $? -ne 0 ]]; then let j++; fi
    done

    if [[ $i -eq $j ]]; then
        git reset --hard CURRENT &&
        git stash &&
        git stash clear &
        git pull --force ${zCodePath}/.git server:master
        return -1
    fi

    git branch -D CURRENT
    git branch CURRENT
}

# FUNCTION: undo deploy
# RETURN: new created branch name if success, or -1 if failed
# $1: specify where to fallback (git commit SHA1 id)
# $@(after shift):the file to deploy
zrevoke() {
    if [[ '' == ${zCommitId} ]]; then return -1; fi
    if [[ '' == $zCodePath ]]; then zCodePath=`pwd`; fi

    local zEcsList=`cat $zCodePath/.git_shadow/info/client_ip_major.txt`
    cd $zCodePath

    zBranchId=$(git log CURRENT -n 1 --format=%H) # 取 CURRENT 分支的 SHA1 sig
    git branch --force "$zBranchId"

    if [[ 0 -eq $# ]]; then  # If no file name given, meaning revoke all
        git checkout ${zCommitId}
        if [[ 0 -ne $? ]]; then
            git reset ${zCommitId}
        fi
    else
        git checkout ${zCommitId}
        git reset ${zCommitId} -- $@
    fi

    if [[ 0 -ne $? ]]; then
        git branch -D $zBranchId
        return -1
    fi

    zCommitContent=`git log CURRENT -n 1 | tail -n 1`
    git commit -m "[REVOKE]=>${zCommitContent}"

    local i=0
    local j=0
    for zEcs in $zEcsList
    do
        let i++
        git push --force git@${zEcs}:${zCodePath}/.git master:server &

        if [[ $? -ne 0 ]]; then let j++; fi
    done

    if [[ $i -eq $j ]]; then
        git reset --hard CURRENT
        git stash
        git stash clear
        return -1
    fi

    git branch -D CURRENT
    git branch CURRENT
    git branch -M master # 将当前所在分支名称强制修改为 master
}

#######################################################
zDeployAll=2
zDeployOne=1
zRevokeOne=-1
zRevokeAll=-2

zComment=
zCodePath=
zCommitId=
zActionType=
while getopts dDrRP:i: zOption
do
    case $zOption in
        d) zActionType=zDeployOne;;
        D) zActionType=zDeployAll;;
        r) zActionType=zRevokeOne;;
        R) zActionType=zRevokeAll;;
        P) zCodePath="$OPTARG";;  # code path
        i) zCommitId="$OPTARG";;  #used by function 'zrevoke'
#        m) zComment="$OPTARG";;  # used by 'git commit -m '
        ?) return -1;;
    esac
done
shift $[$OPTIND - 1]

if [[ $zActionType -eq $zDeployOne ]]; then
    zdeploy $@
elif [[ $zActionType -eq $zDeployAll ]]; then
    zdeploy
elif [[ $zActionType -eq $zRevokeOne ]]; then
    zrevoke $@
elif [[ $zActionType -eq $zRevokeAll ]]; then
    zrevoke
else
    printf "\033[31;01Deploy: unknown request!\033[00m\n" 1>&2
fi
