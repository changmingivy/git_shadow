#!/bin/sh
export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"

zPathOnHost=$1
# zServBranchName=$2
zProjName=`basename ${zPathOnHost}`
zProjOnLinePath=`dirname \`dirname ${zPathOnHost}\``

######################################################################
# 采取换软链接的方式，避免推送大量代码过程中线上代码出现不一致的情况 #
######################################################################
rm -rf ${zProjOnLinePath}/${zProjName}  # 一次性使用，清理旧项目遗留的文件
rm -rf ${zProjOnLinePath}/${zProjName}_SHADOW  # 一次性使用，清理旧项目遗留的文件
# 临时切换至布署仓库工作区
ln -s ${zPathOnHost} ${zProjOnLinePath}/${zProjName}
rm -rf ${zPathOnHost}_OnLine
mkdir ${zPathOnHost}_OnLine
git clone $zPathOnHost/.git ${zPathOnHost}_OnLine
# 切换回线上仓库工作区
rm -rf ${zProjOnLinePath}/${zProjName}
ln -s ${zPathOnHost}_OnLine ${zProjOnLinePath}/${zProjName}

# 布署完成之后需要执行的动作：<项目名称.sh>
(cd ${zPathOnHost}_OnLine && sh ${zPathOnHost}_OnLine/____post-deploy.sh) &
