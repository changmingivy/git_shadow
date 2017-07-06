#ifndef _Z
    #include "../zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
/*
 * 生成缓存：差异文件列表、每个文件的差异内容、最近的布署日志信息
 */
struct iovec *
zgenerate_cache(_i zRepoId) {
    _i zNewVersion = (_i)time(NULL);  // 以时间戳充当缓存版本号

    struct iovec *zpNewCacheVec[2] = {NULL};  // 维持2个iovec数据，一个用于缓存文件列表，另一个按行缓存每一个文件差异信息

    FILE *zpShellRetHandler[2] = {NULL};  // 执行SHELL命令，读取此句柄获取返回信息
    _i zDiffFileNum = 0, zDiffLineNum = 0, zLen = 0;  // 差异文件总数

    char *zpRes[2] = {NULL};  // 存储从命令行返回的原始文本信息
    char zShellBuf[2][zCommonBufSiz] = {{'\0'}, {'\0'}};  // 存储命令行字符串

    zFileDiffInfo *zpIf;

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf[0], "cd %s "
            "&& git diff --name-only HEAD CURRENT | wc -l "
            "&& git diff --name-only HEAD CURRENT "
            "&& git log --format=%%H -n 1 CURRENT", zppRepoPathList[zRepoId]);
    zpShellRetHandler[0] = popen(zShellBuf[0], "r");
    zCheck_Null_Return(zpShellRetHandler, NULL);

    pthread_rwlock_wrlock(&(zpRWLock[zRepoId]));  // 更新缓存前阻塞相同代码库的其它相关的写操作：布署、撤销等
    if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) {  // 第一行返回的是文件总数
        return NULL;  // 命令没有返回结果，代表没有差异文件，理论上不存在此种情况
    }
    else {
        if (0 == (zDiffFileNum = strtol(zpRes[0], NULL, 10))) {
            return NULL;  // 同上，用于防止意外原因扰乱缓存数据
        }
        zMem_Alloc(zpNewCacheVec[0], struct iovec, zDiffFileNum);   // 为存储文件路径列表的iovec[0]分配空间
        zpCacheVecSiz[zRepoId] = zDiffFileNum;  // 更新对应代码库的差异文件数量（更新到全局变量）

        for (_i i = 0; i < zDiffFileNum; i++) {
            zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0]);
            zLen = 1 + strlen(zpRes[0]) + sizeof(zFileDiffInfo);

            zCheck_Null_Exit(zpIf = malloc(zLen));
            zpIf->CacheVersion = zNewVersion;
            zpIf->RepoId= zRepoId;
            zpIf->FileIndex = i;
            strcpy(zpIf->path, zpRes[0]);

            zpNewCacheVec[0][i].iov_base = zpIf;
            zpNewCacheVec[0][i].iov_len = zLen;

            // 必须在shell命令中切换到正确的工作路径
            sprintf(zShellBuf[1], "cd %s && git diff HEAD CURRENT -- %s | wc -l && git diff HEAD CURRENT -- %s", zppRepoPathList[zRepoId], zpRes[0], zpRes[0]);
            zpShellRetHandler[1] = popen(zShellBuf[1], "r");
            zCheck_Null_Return(zpShellRetHandler, NULL);

            zCheck_Null_Exit(
                    zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])  // 读出差异行总数
                    );
            zDiffLineNum = strtol(zpRes[1], NULL, 10);

            zpIf->VecSiz = zDiffLineNum;

            zMem_Alloc(zpNewCacheVec[1], struct iovec, zDiffLineNum);  // 为每个文件的详细差异内容分配iovec[1]分配空间

            for (_i j = 0; NULL != (zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])); j++) {
                zLen = 1 + strlen(zpRes[1]);
                zMem_Alloc(zpNewCacheVec[1][j].iov_base, char, zLen);
                strcpy(zpNewCacheVec[1][j].iov_base, zpRes[1]);

                zpNewCacheVec[1][j].iov_len = zLen;
            }
            pclose(zpShellRetHandler[1]);
        }

        /* 以下四行更新所属代码库的CURRENT tag SHA1 sig值 */
        char *zpBuf = zget_one_line_from_FILE(zpShellRetHandler[0]);  // 读取最后一行：CURRENT标签的SHA1 sig值
        zMem_Alloc(zppCurTagSig[zRepoId], char, 40);  // 存入前40位，丢弃最后的'\0'
        strncpy(zppCurTagSig[zRepoId], zpBuf, 40);  // 更新对应代码库的最新CURRENT tag SHA1 sig
        pclose(zpShellRetHandler[0]);
    }

    /* 以下部分更新日志缓存 */
    zDeployLogInfo *zpMetaLogIf, *zpTmpIf;
    struct stat zStatBufIf;
    size_t zRealLogNum, zDataLogCacheSiz;
    char *zpDataLogCache;

    zCheck_Negative_Return(
            fstat(zpLogFd[0][zRepoId], &zStatBufIf),  // 获取当前日志文件属性
            NULL);
    if (0 == (zRealLogNum = zStatBufIf.st_size / sizeof(zDeployLogInfo))) {
        goto zMark;
    }

    zpPreLoadLogVecSiz[zRepoId] = zRealLogNum > zPreLoadLogSiz ? zPreLoadLogSiz : zRealLogNum;  // 计算需要缓存的实际日志数量
    zMem_Alloc(zppPreLoadLogVecIf[zRepoId], struct iovec, zpPreLoadLogVecSiz[zRepoId]);  // 根据计算出的数量分配相应的内存

    zpMetaLogIf = (zDeployLogInfo *) mmap(NULL, zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo), PROT_READ, MAP_PRIVATE, zpLogFd[0][zRepoId], zStatBufIf.st_size - zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo));  // 将meta日志mmap至内存
    zCheck_Null_Return(zpMetaLogIf, NULL);

    zCheck_Negative_Return(
            fstat(zpLogFd[1][zRepoId], &zStatBufIf),  // 获取当前日志文件属性
            NULL);
    zpTmpIf = zpMetaLogIf + zpPreLoadLogVecSiz[zRepoId] - 1;
    if (zStatBufIf.st_size != zpTmpIf->offset + zpTmpIf->PathLen) {
        zPrint_Err(0, NULL, "布署日志异常：data实际长度与meta标注的不一致！");
        exit(1);
    }

    zDataLogCacheSiz = zpTmpIf->offset + zpTmpIf->PathLen- zpMetaLogIf->offset;  // 根据meta日志属性确认data日志偏移量
    zpDataLogCache = mmap(NULL, zDataLogCacheSiz, PROT_READ, MAP_PRIVATE, zpLogFd[1][zRepoId], zpMetaLogIf->offset);  // 将data日志mmap至内存
    zCheck_Null_Return(zpDataLogCache, NULL);

    for (_i i = 0; i < 2 * zpPreLoadLogVecSiz[zRepoId]; i++) {  // 拼装日志信息
        if (0 == i % 2) {
            zppPreLoadLogVecIf[zRepoId][i].iov_base =  zpMetaLogIf + i / 2;
            zppPreLoadLogVecIf[zRepoId][i].iov_len = sizeof(zDeployLogInfo);
        }
        else {
            zppPreLoadLogVecIf[zRepoId][i].iov_base = zpDataLogCache + (zpMetaLogIf + i / 2)->offset - zpMetaLogIf->offset;
            zppPreLoadLogVecIf[zRepoId][i].iov_len = (zpMetaLogIf + i / 2)->PathLen;
        }
    }

zMark:
    pthread_rwlock_unlock(&(zpRWLock[zRepoId]));
    return zpNewCacheVec[0];
}

void
zupdate_cache(void *zpIf) {
    _i zRepoId = *((_i *)zpIf);
    struct iovec *zpOldCacheIf = zppCacheVecIf[zRepoId];
    if (NULL == (zppCacheVecIf[zRepoId] = zgenerate_cache(zRepoId))) {
        zppCacheVecIf[zRepoId] = zpOldCacheIf;  // 若新缓存返回状态不正常，回退到上一版缓存状态
    }
    else {
          // 若新缓存正常返回，释放掉老缓存的内存空间
        for (_i i = 0; i < zpCacheVecSiz[zRepoId]; i++) {
            for (_i j = 0; j < ((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->VecSiz; j++) {
                free((((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent[j]).iov_base);
            }
            free(((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent);
            free(zpOldCacheIf[i].iov_base);
        }
        free(zpOldCacheIf);
    }

    if (NULL != zppPreLoadLogVecIf[zRepoId]) { // 如下部分用于销毁旧的布署日志缓存
        zDeployLogInfo *zpTmpIf = (zDeployLogInfo *)(zppPreLoadLogVecIf[zRepoId]->iov_base);
        munmap(zppPreLoadLogVecIf[zRepoId]->iov_base, zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo));
        munmap(zppPreLoadLogVecIf[zRepoId + 1], (zpTmpIf + zpPreLoadLogVecSiz[zRepoId])->offset + (zpTmpIf + zpPreLoadLogVecSiz[zRepoId])->PathLen - zpTmpIf->offset);
    }
}

/*
 * 将文本格式的ipv4地址转换成二进制无符号整型(按网络字节序，即大端字节序)
 */
_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit(
            inet_pton(AF_INET, zpStrAddr, &zIpv4Addr)
            );
    return zIpv4Addr.s_addr;
}

/*
 * 客户端更新自身ipv4数据库文件
 */
void
zupdate_ipv4_db_self(_i zBaseFd) {
    char *zpBuf = NULL;
    _ui zIpv4Addr = 0;
    _i zFd = openat(zBaseFd, zSelfIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    zCheck_Negative_Return(zFd,);

    FILE *zpFileHandler = popen("ip addr | grep -oP '(\\d{1,3}\\.){3}\\d{1,3}' | grep -v 127", "r");
    zCheck_Null_Return(zpFileHandler,);
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
    _i zFd[2] = {0};
    struct stat zStatIf;
    zDeployResInfo *zpTmpIf;

    zFd[0] = open(zppRepoPathList[zRepoId], O_RDONLY);
    zCheck_Negative_Return(zFd[0],);
    // 打开客户端ip地址数据库文件
    zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY);
    zCheck_Negative_Return(fstat(zFd[1], &zStatIf),);
    close(zFd[0]);

    zpTotalHost[zRepoId] = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zppDpResList[zRepoId], zDeployResInfo, zpTotalHost[zRepoId]);  // 分配数组空间，用于顺序读取
    zMem_C_Alloc(zpppDpResHash[zRepoId], zDeployResInfo *, zDeployHashSiz);  // 对应的 HASH 索引,用于快速定位写入
    for (_i j = 0; j < zpTotalHost[zRepoId]; j++) {
        zppDpResList[zRepoId][j].RepoId = zRepoId;  // 写入代码库索引值
        zppDpResList[zRepoId][j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zppDpResList[zRepoId][j].ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpppDpResHash[zRepoId][j % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpppDpResHash[zRepoId][j % zDeployHashSiz] = &(zppDpResList[zRepoId][j]);  // 若顶层为空，直接指向数组中对应的位置
            zpppDpResHash[zRepoId][j % zDeployHashSiz]->p_next = NULL;
        }
        else {
            while (NULL != zpTmpIf->p_next) {  // 若顶层不为空，分配一个新的链表节点指向数据中对应的位置
                zpTmpIf = zpTmpIf->p_next;
            }
            zMem_Alloc(zpTmpIf->p_next, zDeployResInfo, 1);
            zpTmpIf->p_next->p_next = NULL;
            zpTmpIf->p_next = &(zppDpResList[zRepoId][j]);
        }
    }
    close(zFd[1]);
}

/*
 * 监控到ip数据文本文件变动，触发此函数执行二进制ip数据库更新，更新全员ip数据库
 */
void
zupdate_ipv4_db_all(void *zpIf) {
    FILE *zpFileHandler = NULL;
    char *zpBuf = NULL;
    _ui zIpv4Addr = 0;
    _i zFd[3] = {0};
    _i zRepoId = *((_i *)zpIf);

    pthread_rwlock_wrlock(&(zpRWLock[zRepoId]));

    zFd[0] = open(zppRepoPathList[zRepoId], O_RDONLY);
    zCheck_Negative_Return(zFd[0],);

    zFd[1] = openat(zFd[0], zAllIpPathTxt, O_RDONLY);
    zCheck_Negative_Return(zFd[1],);
    zFd[2] = openat(zFd[0], zAllIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    zCheck_Negative_Return(zFd[2],);

    zpFileHandler = fdopen(zFd[1], "r");
    zCheck_Null_Return(zpFileHandler,);
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

    pthread_rwlock_unlock(&(zpRWLock[zRepoId]));
}
