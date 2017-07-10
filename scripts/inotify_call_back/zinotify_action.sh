#!/bin/sh
zDeployPath=$2
zSyncPath=`dirname zDeployPath`/sync_`basename zDeployPath`

cd $zSyncPath
svn update
git add --all .
git commit -m "[REPO $1]:$2"
git push --force $zDeployPath/.git sync_git:server
