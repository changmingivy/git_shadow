#ifndef _Z
    #include "../zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
// 此部分的多个函数用于生成缓存：差异文件列表、每个文件的差异内容、最近的布署日志信息

void
zupdate_sig_cache(void *zpRepoId) {
// TEST:PASS
    _i zRepoId = *((_i *)zpRepoId);
    char zShellBuf[zCommonBufSiz], *zpRes;
    FILE *zpShellRetHandler;

    /* 以下部分更新所属代码库的CURRENT SHA1 sig值 */
    sprintf(zShellBuf, "cd %s && git log --format=%%H -n 1 CURRENT", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));
    zCheck_Null_Exit(zpRes = zget_one_line_from_FILE(zpShellRetHandler));  // 读取CURRENT分支的SHA1 sig值

    if (zBytes(40) > strlen(zpRes)) {
        zPrint_Err(0, NULL, "Invalid CURRENT sig!!!");
        exit(1);
    }

    if (NULL == zppCURRENTsig[zRepoId]) {
        zMem_Alloc(zppCURRENTsig[zRepoId], char, zBytes(41));  // 含 '\0'
    }

    strncpy(zppCURRENTsig[zRepoId], zpRes, zBytes(41));  // 更新对应代码库的最新CURRENT 分支SHA1 sig
    pclose(zpShellRetHandler);
}

void
zupdate_log_cache(void *zpDeployLogIf) {
// TEST:PASS
    if (NULL == zpDeployLogIf) { return; }  // robustness

    zDeployLogInfo *zpLogIf = (zDeployLogInfo *)zpDeployLogIf;
    size_t zRealLen = sizeof(zDeployLogInfo) + zBytes(zpLogIf->PathLen);
    zDeployLogInfo *zpLogCacheIf = malloc(zRealLen);

    char zCommitSig[zBytes(41)], zShellBuf[zCommonBufSiz], *zpLineContent;
    FILE *zpFile;
    _i zLen;

    _i zFd[2];
    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zpLogIf->RepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(pread(zFd[1], zCommitSig, zBytes(41), zBytes(41) * zpLogIf->index));
    close(zFd[0]);
    close(zFd[1]);

    sprintf(zShellBuf, "cd %s && git log %s --name-only --format=", zpRepoGlobIf[zpLogIf->RepoId].RepoPath, zCommitSig);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    for (size_t zWrOffSet = 0; NULL != (zpLineContent = zget_one_line_from_FILE(zpFile));) {
        zLen = strlen(zpLineContent);
        zpLineContent[zLen] = '\0';
        zpLineContent[zLen - 1] = '\n';
        strcpy(zpLogCacheIf->path + zWrOffSet, zpLineContent);
        zWrOffSet += 1 + zLen;
    }

    if (zpLogCacheQueueHeadIndex[zpLogIf->RepoId] == zpLogCacheVecSiz[zpLogIf->RepoId] - 1) {
        zpLogCacheQueueHeadIndex[zpLogIf->RepoId] = 0;
    } else {
        zpLogCacheQueueHeadIndex[zpLogIf->RepoId]++;
    }

    struct iovec *zpVecQueueHead = &zppLogCacheVecIf[zpLogIf->RepoId][ zpLogCacheQueueHeadIndex[zpLogIf->RepoId] ];
    if (NULL != zpVecQueueHead->iov_base) {
        free(zpVecQueueHead->iov_base);
    };
    zpVecQueueHead->iov_base = zpLogCacheIf;
    zpVecQueueHead->iov_len = zRealLen;

    // 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
    for (_i i = 0, j = zpLogCacheQueueHeadIndex[zpLogIf->RepoId]; i < zpLogCacheVecSiz[zpLogIf->RepoId]; i++) {
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_base = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_base;
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_len = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_len;

        if (0 == j) {
            j = zpLogCacheVecSiz[zpLogIf->RepoId] - 1;
        } else {
            j--;
        }
    }
}

/*
 * 功能：生成单次提交的文件差异列表及差异内容缓存
 * 返回：若 zFileId 参数为 -1，返回文件差异内容；否则返回差异文件列表，以 iovec 数组形式存储
 */
#define zget_sub_info(zpObj, zSubObjId) ((zCodeInfo *)((((zpObj)->p_SubObjVecIf)->p_vec)[zSubObjId].iov_base))
void
zget_file_diff_info(_i zRepoId, _i zCommitId, _i zFileId) {
    zVecInfo *zpRetIf;
    struct iovec *zpVecIf;
    zCodeInfo *zpCodeIf;  // 此项是 iovec 的 io_base 字段
    zCodeInfo *zpCommitSigIf, *zpFileDiffIf;
    _i zVecId, zDataLen;
    _i zAllocSiz = 128;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[zCommonBufSiz];

    zMem_Alloc(zpVecIf, struct iovec, zAllocSiz);

    zpCommitSigIf = zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec[zCommitId].iov_base;  // 按 zCommitId 索引出 "CommitSig" 所在的 zVecInfo 结构体
    zpFileDiffIf = (-1 == zFileId) ? NULL : zget_sub_info(zpCommitSigIf, zFileId);  // 按 zFileId 索引出 "文件路径名称" 所在的 zVecInfo 结构体，只有取文件内容差异的时候会用到

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git diff %s %s CURRENT -- %s", zpRepoGlobIf[zRepoId].RepoPath,
        (-1 == zFileId) ? "--name-only" : "",
        zpCommitSigIf->data,
        (-1 == zFileId) ? "" : zpFileDiffIf->data);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); zVecId++) {
        if (zVecId >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpVecIf, struct iovec, zAllocSiz, zpVecIf);
        }

        zDataLen = 1 + strlen(zpRes) + sizeof(zCodeInfo);
        zCheck_Null_Exit( zpCodeIf = malloc(zDataLen) );

        zpCodeIf->SelfId = zVecId;
        zpCodeIf->len = zDataLen;
        strcpy(zpCodeIf->data, zpRes);

        zpVecIf[zVecId].iov_base = zpCodeIf;
    }

    pclose(zpShellRetHandler);
    if (0 == zVecId) {
        free(zpVecIf);  // 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空
        zpVecIf = NULL;
    } else {
        zMem_Re_Alloc(zpVecIf, struct iovec, zVecId, zpVecIf);  // for 循环结束后，zVecId 的值即为最终的成员数量
    }

    zMem_Alloc(zpRetIf, zVecInfo, 1);
    zpRetIf->p_vec = zpVecIf;
    zpRetIf->VecSiz = zVecId;

    if (-1 == zFileId) {
        ((zCodeInfo *)(zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec[zCommitId].iov_base))->p_SubObjVecIf = zpRetIf;
        for (_i i = 0; i < zpRetIf->VecSiz; i++) {
               zget_file_diff_info(zRepoId, zCommitId, i);
        }
    } else {
        ((zCodeInfo *)(((zCodeInfo *)(zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec[zCommitId].iov_base))->p_SubObjVecIf->p_vec[zFileId].iov_base))->p_SubObjVecIf = zpRetIf;
    }
}

/*
 *  传入的是一个单次commit信息，内含 “差异文件列表的 zCodeInfo 结构体指针“，需要释放文件列表结构及其内部的文件内容结构
 */
void
zfree_version_source(zCodeInfo *zpIf) {
    zCodeInfo *zpToFree;
    _i i, j;

    if (NULL == zpIf) { return; }

    for (i = 0; i < zpIf->p_SubObjVecIf->VecSiz; i++) {  // 按文件个数循环
        zpToFree = (zCodeInfo *) (zpIf->p_SubObjVecIf->p_vec[i].iov_base);
        for (j = 0; j < zpToFree->p_SubObjVecIf->VecSiz; j++) {  // 按差异内容行数循环
            free(zpToFree->p_SubObjVecIf->p_vec[j].iov_base);  // 行内容即 zCodeInfo 的最后的 data 字段，释放其所在结构体即可
        }
        free(zpToFree->p_SubObjVecIf);
        free(zpToFree);  // 释放文件列表级别的zCodeInfo
    }
    free(zpIf->p_SubObjVecIf);
    free(zpIf);  // 释放传入的单个CommitSig级别的zCodeInfo
}

/*
 * 功能：逐层生成单个代码库的commit列表、文件列表及差异内容缓存
 * 返回：以 iovec 数组形式存储
 */
void
zinit_commit_version_info(_i zRepoId) {
    static struct iovec zSortedVersionVecIf[zVersionHashSiz];
    zCodeInfo *zpVersionIf;  // 此项是 iovec 的 io_base 字段
    _i zDataLen, zCnter;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[zCommonBufSiz];

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log --format=%%H\\0%%ct", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zCnter = 0; (zCnter < zVersionHashSiz) && (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))); zCnter++) {
        zpRes[40] = '\0';
        zDataLen = 1 + strlen(zpRes) + sizeof(zCodeInfo);
        zCheck_Null_Exit( zpVersionIf= malloc(zDataLen) );

        zpVersionIf->SelfId = zCnter;
        zpVersionIf->len = zDataLen;
        strcpy(zpVersionIf->data, zpRes);

        zSortedVersionVecIf[zCnter].iov_base = zpVersionIf;
    }
    pclose(zpShellRetHandler);

    static zVecInfo zRetIf;
    zRetIf.p_vec = zSortedVersionVecIf;
    zRetIf.VecSiz = zCnter;  // 存储的是实际的对象数量
    zpRepoGlobIf[zRepoId].p_VecIf[0] = &zRetIf;

    for (zCnter = 0; zCnter < (zCommitPreCacheSiz < zRetIf.VecSiz ? zCommitPreCacheSiz : zRetIf.VecSiz); zCnter++) {  // 预生成最近 zCommitPreCacheSiz 次提交的缓存
        zget_file_diff_info(zRepoId, zCnter, -1);
    }
}

void
zupdate_commit_version_info(_i zRepoId) {
    struct iovec zVersionVecIf[zVersionHashSiz];
    static struct iovec zSortedVersionVecIf[zVersionHashSiz];
    zCodeInfo *zpVersionIf;  // 此项是 iovec 的 io_base 字段
    _i zDataLen, zVecId, zCnter, zObjCnt;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[zCommonBufSiz];

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log -1 --format=%%H\\0%%ct", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = zpRepoGlobIf[zRepoId].p_VecIf[0]->VecSiz, zCnter = 0;
        (zCnter < zVersionHashSiz) && (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))); zVecId++, zCnter++) {
        zpRes[40] = '\0';
        if (zVersionHashSiz == zVecId) { zVecId = 0; }

        if (NULL != zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec[zVecId].iov_base) {
            zfree_version_source((zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec)[zVecId].iov_base);
        }

        zDataLen = 1 + strlen(zpRes) + sizeof(zCodeInfo);
        zCheck_Null_Exit( zpVersionIf= malloc(zDataLen) );

        zpVersionIf->SelfId = zVecId;
        zpVersionIf->len = zDataLen;
        strcpy(zpVersionIf->data, zpRes);

        zSortedVersionVecIf[zCnter].iov_base = zVersionVecIf[zVecId].iov_base = zpVersionIf;
    }
    pclose(zpShellRetHandler);

    zObjCnt = zVersionHashSiz <= (zpRepoGlobIf[zRepoId].p_VecIf[0]->VecSiz + zCnter) ? zVersionHashSiz : (zpRepoGlobIf[zRepoId].p_VecIf[0]->VecSiz + zCnter);
    while (zCnter < zObjCnt) {
        if (zVersionHashSiz == zVecId) { zVecId = 0; }
        zSortedVersionVecIf[zCnter++] = zVersionVecIf[zVecId++];
    }

    static zVecInfo zRetIf;
    zRetIf.p_vec = zSortedVersionVecIf;
    zRetIf.VecSiz = zObjCnt;  // 存储的是实际的对象数量
    zpRepoGlobIf[zRepoId].p_VecIf[0] = &zRetIf;

    _i i;
    for (i = 0; i < (zCommitPreCacheSiz < zRetIf.VecSiz ? zCommitPreCacheSiz : zRetIf.VecSiz); i++) {  // 预生成最近 zCommitPreCacheSiz 次提交的缓存
        zget_file_diff_info(zRepoId, i, -1);
    }
    for(; (i < zRetIf.VecSiz) && (NULL != (zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec)[i].iov_base); i++) {  // 释放旧缓存（已失效）
        zfree_version_source((zpRepoGlobIf[zRepoId].p_VecIf[0]->p_vec)[i].iov_base);
    }
}

void
zthread_update_commit_cache(void *zpIf) {
// TEST:PASS
    _i zRepoId = *((_i *)(zpIf));

    pthread_rwlock_wrlock( &(zpRepoGlobIf[zRepoId].RwLock) );
    zupdate_commit_version_info(zRepoId);
    pthread_rwlock_unlock( &(zpRepoGlobIf[zRepoId].RwLock) );
}

/*
 * 将文本格式的ipv4地址转换成二进制无符号整型(按网络字节序，即大端字节序)
 */
_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
// TEST:PASS
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit(inet_pton(AF_INET, zpStrAddr, &zIpv4Addr));
    return zIpv4Addr.s_addr;
}

/*
 * 客户端更新自身ipv4数据库文件
 */
void
zupdate_ipv4_db_self(_i zBaseFd) {
// TEST:PASS
    _ui zIpv4Addr;
    char *zpBuf;
    FILE *zpFileHandler;

    _i zFd;
    zCheck_Negative_Exit(zFd = openat(zBaseFd, zSelfIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = popen("ip addr | grep -oP '(\\d{1,3}\\.){3}\\d{1,3}' | grep -v 127", "r"));
    while (NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler))) {
        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (zSizeOf(_ui) != write(zFd, &zIpv4Addr, zSizeOf(_ui))) {
            zPrint_Err(0, NULL, "Write to $zSelfIpPath failed!");
            exit(1);
        }
    }

    fclose(zpFileHandler);
    close(zFd);
}

/*
 * 更新ipv4 地址缓存
 */
void
zupdate_ipv4_db_hash(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    zDeployResInfo *zpTmpIf;

    _i zFd[2] = {-9};
    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zpRepoGlobIf[zRepoId].TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zpRepoGlobIf[zRepoId].p_DpResList, zDeployResInfo, zpRepoGlobIf[zRepoId].TotalHost);  // 分配数组空间，用于顺序读取

    for (_i j = 0; j < zpRepoGlobIf[zRepoId].TotalHost; j++) {
        (zpRepoGlobIf[zRepoId].p_DpResList->hints)[3] = sizeof(zDeployResInfo) - sizeof(zpRepoGlobIf[zRepoId].p_DpResList->DeployState) - sizeof(zpRepoGlobIf[zRepoId].p_DpResList->p_next);  // hints[3] 用于提示前端回发多少字节的数据
        zpRepoGlobIf[zRepoId].p_DpResList[j].RepoId = zRepoId;  // 写入代码库索引值
        zpRepoGlobIf[zRepoId].p_DpResList[j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zpRepoGlobIf[zRepoId].p_DpResList[j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zpRepoGlobIf[zRepoId].p_DpResList->ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpRepoGlobIf[zRepoId].p_DpResHash[(zpRepoGlobIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpRepoGlobIf[zRepoId].p_DpResHash[(zpRepoGlobIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz] = &(zpRepoGlobIf[zRepoId].p_DpResList[j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zpRepoGlobIf[zRepoId].p_DpResList[j]);
        }
    }

    close(zFd[1]);
}

/*
 * 监控到ip数据文本文件变动，触发此函数执行二进制ip数据库更新，更新全员ip数据库
 */
void
zupdate_ipv4_db_all(_i zRepoId) {
// TEST:PASS
    FILE *zpFileHandler = NULL;
    char *zpBuf = NULL;
    _ui zIpv4Addr = 0;
    _i zFd[3] = {0};

    pthread_rwlock_wrlock(&(zpRepoGlobIf[zRepoId].RwLock));

    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zRepoId].RepoPath, O_RDONLY));

    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPathTxt, O_RDONLY));
    zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zAllIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = fdopen(zFd[1], "r"));
    zPCREInitInfo *zpPCREInitIf = zpcre_init("^(\\d{1,3}\\.){3}\\d{1,3}$");
    zPCRERetInfo *zpPCREResIf;
    for (_i i = 1; NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler)); i++) {
        zpPCREResIf = zpcre_match(zpPCREInitIf, zpBuf, 0);
        if (0 == zpPCREResIf->cnt) {
            zpcre_free_tmpsource(zpPCREResIf);
            zPrint_Time();
            fprintf(stderr, "\033[31;01m[%s]-[Line %d]: Invalid entry!\033[00m\n", zAllIpPath, i);
            exit(1);
        }
        zpcre_free_tmpsource(zpPCREResIf);

        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (sizeof(_ui) != write(zFd[2], &zIpv4Addr, sizeof(_ui))) {
            zPrint_Err(0, NULL, "Write to $zAllIpPath failed!");
            exit(1);
        }
    }
    zpcre_free_metasource(zpPCREInitIf);
    fclose(zpFileHandler);
    close(zFd[2]);
    close(zFd[1]);
    close(zFd[0]);

    // ipv4 数据文件更新后，立即更新对应的缓存中的列表与HASH
    zupdate_ipv4_db_hash(zRepoId);

    pthread_rwlock_unlock(&(zpRepoGlobIf[zRepoId].RwLock));
}

void
zthread_update_ipv4_db_all(void *zpIf) {
// TEST:PASS
    _i zRepoId = *((_i *)zpIf);
    zupdate_ipv4_db_all(zRepoId);
}
/*
 * 通用函数，调用外部程序或脚本文件执行相应的动作
 * 传入参数：
 * $1：代码库ID
 * $2：代码库绝对路径
 */
void
zthread_common_func(void *zpIf) {
    zObjInfo *zpObjIf = (zObjInfo *) zpIf;
    char zShellBuf[zCommonBufSiz];

    sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify %d %s %s",
        zpRepoGlobIf[zpObjIf->RepoId].RepoPath,
        zpObjIf->RepoId,
        zpRepoGlobIf[zpObjIf->RepoId].RepoPath,
        zpObjHash[zpObjIf->UpperWid]->path);

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}
