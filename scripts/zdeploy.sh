#!/bin/sh

# $@(after shift):files to deploy
zdeploy() {
    zCommitContent=`git log -n 1 | tail -n 1 | grep -o '[^ ].*'`

    git reset CURRENT  # 将 master 分支提交状态回退到 CURRENT 分支状态，即上一次布署的状态

    if [[ 0 -eq $# ]]; then  # If no file name given, meaning deploy all
        git add --all .
    else
        git add $@
    fi

    if [[ 0 -ne $? ]]; then exit 1; fi

    git commit --allow-empty -m "[DEPLOY FROM]:${zCommitContent}"  # Maybe reveive contents from frontend?
}

# $@(after shift):the file to deploy
zrevoke() {
    if [[ '' == ${zCommitId} ]]; then exit 1; fi
    zCommitContent=`git log CURRENT -n 1 | tail -n 1 | grep -o '[^ ].*'`

    if [[ 0 -eq $# ]]; then # If no file name given, meaning revoke all
        git reset ${zCommitId}
    else
        git reset ${zCommitId} -- $@
    fi

    if [[ 0 -ne $? ]]; then exit 1; fi

    git commit --allow-empty -m "[REVOKE FROM]:${zCommitContent}" # commit 不能以 $? 变量不为 0 终止进程，因为当可提交内容为空时，$? 为 1
}

#######################################################
zDeployAll=2; zDeployOne=1; zRevokeOne=-1; zRevokeAll=-2

-D -f %s -H %s

zActionType=
zCodePath=
zCommitId=

while getopts dDrRP:i: zOption
do
    case $zOption in
        d) zActionType=zDeployOne;;
        D) zActionType=zDeployAll;;
        r) zActionType=zRevokeOne;;
        R) zActionType=zRevokeAll;;
        P) zCodePath="$OPTARG";;  # code path
        i) zCommitId="$OPTARG";;  #used by function 'zrevoke'
        ?) exit 1;;
    esac
done
shift $[$OPTIND - 1]

if [[ '' == $zCodePath ]]; then zCodePath=`pwd`; fi

cd $zCodePath
zEcsList=`cat ${zCodePath}/.git_shadow/info/client_ip_major.txt`

if [[ $zActionType -eq $zDeployOne ]]; then zdeploy $@
elif [[ $zActionType -eq $zDeployAll ]]; then zdeploy
elif [[ $zActionType -eq $zRevokeOne ]]; then zrevoke $@
elif [[ $zActionType -eq $zRevokeAll ]]; then zrevoke
else printf "\033[31;01Deploy: unknown request!\033[00m\n" 1>&2
fi

i=0
j=0
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
    git pull --force ${zCodePath}/.git server:master
    exit 1
fi

zCurSig=$(git log CURRENT -n 1 --format=%H) # 取 CURRENT 分支的 SHA1 sig
git branch -f $zCurSig # 创建一个以 SHA1 sig 命名的分支
git branch -f CURRENT
