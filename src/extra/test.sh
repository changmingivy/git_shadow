#/bin/sh

# 创建新项目
# zContent="{\"repoID\":11,\"opsID\":1,\"pathOnHost\":\"/home/git/11_Y\",\"sourceURL\":\"https://git.coding.net/kt10/zgit_shadow.git\",\"sourceBranch\":\"master\",\"sourceVcsType\":\"git\",\"needPull\":\"Y\",\"sshUserName\":\"git\",\"sshPort\":\"22\"}"

# 查询版本号列表
zContent="{\"repoID\":11,\"opsID\":9,\"dataType\":0}"

# 打印差异文件列表
# zContent="{\"repoID\":11,\"opsID\":10,\"revID\":1,\"cacheID\":1000000000,\"dataType\":0}"

# 打印差异文件内容
#zContent="{\"repoID\":11,\"opsID\":11,\"revID\":0,\"fileID\":0,\"cacheID\":1000000000,\"dataType\":0}"

# 布署/撤销
# zContent="{\"repoID\":11,\"opsID\":12,\"revID\":${1},\"cacheID\":0,\"dataType\":0,\"ipList\":\"::1\",\"ipCnt\":1,\"sshUserName\":\"git\",\"sshPort\":\"22\"}"

# 实时进度查询
# zContent="{\"repoID\":11,\"opsID\":15}"

# 更换源库 URL 或分支
# zContent="{\"repoID\":11,\"opsID\":3,\"sourceURL\":\"https://github.com/kt10/zgit_shadow.git\",\"sourceBranch\":\"master2\"}"

zTcpSend() {
    exec 44<>/dev/tcp/${1}/${2}
    printf "${3}">&44
    cat<&44
    exec 44<&-
    exec 44>&-
}

zTcpSend '::1' '20000' "${zContent}"
