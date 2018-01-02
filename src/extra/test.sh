#/bin/sh

# 创建新项目
# zContent="{\"repoId\":11,\"opsId\":1,\"pathOnHost\":\"/home/git/11_Y\",\"sourceURL\":\"https://git.coding.net/kt10/zgit_shadow.git\",\"sourceBranch\":\"master\",\"sourceVcsType\":\"git\",\"needPull\":\"Y\",\"sshUserName\":\"git\",\"sshPort\":\"22\"}"

# 查询版本号列表
zContent="{\"repoId\":11,\"opsId\":9,\"dataType\":0}"

# 打印差异文件列表
# zContent="{\"repoId\":11,\"opsId\":10,\"revId\":1,\"cacheId\":1000000000,\"dataType\":0}"

# 打印差异文件内容
#zContent="{\"repoId\":11,\"opsId\":11,\"revId\":0,\"fileId\":0,\"cacheId\":1000000000,\"dataType\":0}"

# 布署/撤销
# zContent="{\"repoId\":11,\"opsId\":12,\"revId\":${1},\"cacheId\":0,\"dataType\":0,\"ipList\":\"::1\",\"ipCnt\":1,\"sshUserName\":\"git\",\"sshPort\":\"22\"}"

# 实时进度查询
# zContent="{\"repoId\":11,\"opsId\":15}"

# 布署系统升级后，目标机组件更新
# zContent="{\"opsId\":2}"

# 更换源库 URL 或分支
# zContent="{\"repoId\":11,\"opsId\":3,\"sourceURL\":\"https://githuhub.com/kt10/zgit_shadow.git\",\"sourceBranch\":\"master2\"}"

zTcpSend() {
    exec 44<>/dev/tcp/${1}/${2}
    printf "${3}">&44
    cat<&44
    exec 44<&-
    exec 44>&-
}

zTcpSend '::1' '20000' "${zContent}"
