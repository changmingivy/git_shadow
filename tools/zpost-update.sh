#!/bin/sh
export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"

git clone $1 $2

#    zPathOnHost=$1
#    zProjOnLinePath=$2
#    # zServBranchName=$3
#
#    ######################################################################
#    # 采取换软链接的方式，避免推送大量代码过程中线上代码出现不一致的情况 #
#    ######################################################################
#    rm -rf ${zProjOnLinePath}
#    rm -rf ${zProjOnLinePath}_SHADOW  # 一次性使用，清理旧项目遗留的文件
#    # 临时切换至布署仓库工作区
#    ln -s ${zPathOnHost} ${zProjOnLinePath}
#    rm -rf ${zPathOnHost}_OnLine
#    mkdir ${zPathOnHost}_OnLine
#    git clone $zPathOnHost/.git ${zPathOnHost}_OnLine
#    # 切换回线上仓库工作区
#    rm -rf ${zProjOnLinePath}
#    ln -s ${zPathOnHost}_OnLine ${zProjOnLinePath}
#
#    # 布署完成之后需要执行的动作：<项目名称.sh>
#    (cd ${zPathOnHost}_OnLine && sh ${zPathOnHost}_OnLine/____post-deploy.sh) &
