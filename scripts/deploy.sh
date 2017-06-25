#!/usr/bin/env bash

zget_modified_list() {
	git pull --force server:master
	return $(git diff --name-only HEAD CURRENT)
}

zprint_diff_content() {
	return $(git diff HEAD CURRENT -- $1)
}

# FUCNTION: deploy code
# RETURN:Prev commit id if success, or -1 if failed
# -t:major ECS hosts(IP addr: 10.10.10.10 etc.) to deploy to
# -p:code repo path
# -m:git commit contents
zdeploy() {
	local zEcsList=
	local zComment=
	local zCodePath=
    while getopts t:m:p: zOption
    do
        case $zOption in
        t)	zEcsList="$OPTARG";;
        m)	zComment="$OPTARG";;
        p)	zCodePath="$OPTARG";;
		?)	return -1;;
        esac
    done
    shift $[$OPTIND − 1]
	if [[ '' == zEcsList || '' == zComment || '' == zCommitId ]];then return -1; fi

	cat $zCodePath/.zLock >/dev/null
	cd $zCodePath

	if [[ 0 -eq $# ]];then  # If no file name given, meaning deploy all
		git add .
	else
		for zFile in $@
		do
			git add $zFile
		done
	fi
	git commit -m "$zComment"

	local i=0
	local j=0
	for zEcs in $zEcsList
	do
		let i++
		git push --force git@${zEcs}:${zCodePath}/.git server:master
		if [[ $? -ne 0 ]]; then let j++; fi
	done

	if [[ $i -eq $j ]]; then
		git reset --hard CURRENT
		git stash
		git stash clear
		echo 0 > $zCodePath/.zLock
		return -1
	fi

	zPrevCommitId=$(git log --format=%H -n 1)

	git tag -d CURRENT
	git tag CURRENT

	echo 0 > $zCodePath/.zLock
	return $zPrevCommitId
}

# FUNCTION: undo deploy
# RETURN: new created branch name if success, or -1 if failed
# -i: specify where to fallback (git commit SHA1 id)
zrevoke() {
	local zCommitId=
    while getopts i: zOption
    do
        case $zOption in
        i)	zCommitId="$OPTARG";;
		?)	return -1;;
        esac
    done
    shift $[$OPTIND − 1]
	if [[ '' == $zCommitId ]]; then return -1; fi

	cat $zCodePath/.zLock >/dev/null

	zBranchId=$(git log CURRENT -n 1 --format=%H)
	git branch "$zBranchId"

	if [[ 0 -eq $# ]];then  # If no file name given, meaning revoke all
		git reset --hard zCommitId
		if [[ 0 -ne $? ]]; then git checkout "$zCommitId"; fi
	else
		git reset zCommitId -- $@
	fi

	if [[ 0 -ne $? ]]; then
		echo 0 > $zCodePath/.zLock
		git branch -D $zBranchId
		return -1
	fi

	git commit -m "files below have been revoked to $zCommitId:\n$*"
	git stash
	git stash clear
	git tag -d CURRENT
	git tag CURRENT
	git branch -m -f master

	echo 0 > $zCodePath/.zLock
	return $zBranchId
}
