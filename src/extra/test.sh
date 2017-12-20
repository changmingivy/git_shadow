#/bin/sh

# 列出单个项目元信息
#zContent="{\"OpsId\":6,\"ProjId\":11}"

# 创建新项目
#zContent="{\"OpsId\":1,\"ProjId\":\"111\",\"PathOnHost\":\"/home/git/111_Y\",\"SourceUrl\":\"https://git.coding.net/kt10/FreeBSD.git\",\"SourceBranch\":\"master\",\"SourceVcsType\":\"git\",\"NeedPull\":\"Y\",\"SSHUserName\":\"git\",\"SSHPort\":\"22\"}"

# 查询版本号列表
zContent="{\"OpsId\":9,\"ProjId\":11,\"DataType\":0}"

# 打印差异文件列表
#zContent="{\"OpsId\":10,\"ProjId\":11,\"RevId\":1,\"CacheId\":1000000000,\"DataType\":0}"

# 打印差异文件内容
#zContent="{\"OpsId\":11,\"ProjId\":11,\"RevId\":0,\"FileId\":0,\"CacheId\":1000000000,\"DataType\":0}"

# 布署/撤销
# zContent="{\"OpsId\":12,\"ProjId\":11,\"RevId\":${1},\"CacheId\":0,\"DataType\":0,\"IpList\":\"::1\",\"IpCnt\":1,\"SSHUserName\":\"git\",\"SSHPort\":\"22\"}"

# 实时进度查询
# zContent="{\"OpsId\":15,\"ProjId\":11}"

# 系统升级更新
# zContent="{\"OpsId\":2}"

# 更换源库分支
# zContent="{\"OpsId\":3,\"ProjId\":11,\"CodeSyncBranch\":\"master2\"}"

zTcpSend() {
    exec 44<>/dev/tcp/${1}/${2}
    printf "${3}">&44
    cat<&44
    exec 44<&-
    exec 44>&-
}

zTcpSend '::1' '20000' "${zContent}"
