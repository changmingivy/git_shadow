#!/usr/bin/env bash

# FUCNTION: deploy code
# RETURN:Prev commit id if success, or -1 if failed
# $1:major ECS hosts(IP addr: 10.10.10.10 etc.) to deploy to
# $2:code repo path
# $@(after shift):files to deploy
zdeploy() {
	local zCodePath=$1
	if [[ 0 < $# ]]; then
		shift 1
	fi

	local zEcsList=`cat $zCodePath/.git_shadow/major_list`

	if [[ '' == $zCodePath ]]; then 
		return -1
	fi

	cd $zCodePath

	if [[ 0 -eq $# ]];then  # If no file name given, meaning deploy all
		git add -A .
	else
		for zFile in $@
		do
			git add $zFile
		done
	fi
	git commit -m "${zCodePath}: `date -Is`"  # Maybe reveive contents from frontend?

	local i=0
	local j=0
	for zEcs in $zEcsList
	do
		let i++
		git push --force git@${zEcs}:${zCodePath}/.git master:server
		if [[ $? -ne 0 ]]; then let j++; fi
	done

	if [[ $i -eq $j ]]; then
		git reset --hard CURRENT
		git stash
		git stash clear
		return -1
	fi

	zPrevCommitId=$(git log --format=%H -n 1)

	git tag -d CURRENT
	git tag CURRENT
}

# FUNCTION: undo deploy
# RETURN: new created branch name if success, or -1 if failed
# $1: specify where to fallback (git commit SHA1 id)
# $@(after shift):the file to deploy
zrevoke() {
	local zCommitId=$1
	if [[ 0 < $# ]]; then
		shift 1
	fi

	if [[ '' == ${zCommitId} ]]; then
		return -1
	fi

	cd $zCodePath
	
	zBranchId=$(git log CURRENT -n 1 --format=%H)
	git branch --force "$zBranchId"

	if [[ 0 -eq $# ]]; then  # If no file name given, meaning revoke all
		git reset --hard ${zCommitId}
		if [[ 0 -ne $? ]]; then
			git checkout ${zCommitId}
		fi
	else
		git reset ${zCommitId} -- $@
	fi

	if [[ 0 -ne $? ]]; then
		git branch -D $zBranchId
		return -1
	fi

	git commit -m "files below have been revoked to ${zCommitId}:\n$*"
	git stash
	git stash clear
	git tag -d CURRENT
	git tag CURRENT
	git branch -M master
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
#		m) zComment="$OPTARG";;  # used by 'git commit -m '
		?) return -1;;
    esac
done
shift $[$OPTIND - 1]

if [[ $zActionType -eq $zDeployOne ]]; then
	zdeploy $zCodePath $@
elif [[ $zActionType -eq $zDeployAll ]]; then
	zdeploy $zCodePath
elif [[ $zActionType -eq $zRevokeOne ]]; then
	zrevoke ${zCommitId} $@
elif [[ $zActionType -eq $zRevokeAll ]]; then
	zrevoke ${zCommitId}
else
	printf "\033[31;01mUnknown request!\033[00m\n" 1>&2
fi
