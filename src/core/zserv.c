#ifndef _Z
    #include "../zmain.c"
#endif

#define zTypeConvert(zSrcObj, zTypeTo) ((zTypeTo)(zSrcObj))

/************
 * META OPS *
 ************/
#define zGet_SendIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_VecIf[zSelfId].iov_base)
#define zGet_SubVecWrapIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_SubVecWrapIf)
#define zGet_NativeDataPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_data)

#define zGet_OneCommitSendIfPtr(zpTopVecWrapIf, zCommitId) ((char *)zGet_SendIfPtr(zpTopVecWrapIf, zCommitId))
#define zGet_OneFileSendIfPtr(zpTopVecWrapIf, zCommitId, zFileId) zGet_SendIfPtr(zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCommitId), zFileId)
#define zGet_OneCommitSigPtr(zpTopVecWrapIf, zCommitId) zGet_NativeDataPtr(zpTopVecWrapIf, zCommitId)

#define zIsCommitCacheType 0
#define zServHashSiz 16

/* 执行结果状态码对应表
 * -100：动作执行成功 (仅表示动作本身已执行，不代表最终结果，如：布署请求返回 -100 表示中控机已开始布署，但是否布署成功尚不能确认)
 * -1：操作指令不存在（未知／未定义）
 * -2：项目ID不存在
 * -3：代码版本ID不存在
 * -4：差异文件ID不存在
 * -5：指定的主机 IP 不存在
 * -6：项目布署／撤销权限被锁定
 * -7：后端接收到的数据不完整，要求前端重发
 * -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 */

/**************
 * NATIVE OPS *
 **************/
/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表及差异内容缓存
 */
void
zget_file_list_and_diff_content(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpUpperVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], *zpRes;

    struct zSendInfo *zpSendIf;  // 此项是 iovec 的 io_base 字段
    _i zVecId;
    _i zSendDataLen, zVecDataLen;
    _i zAllocSiz = 128;

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;
    if (0 ==zpCacheMetaIf->TopObjTypeMark) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].DeployVecWrapIf);
    }

    if (-1 == zpCacheMetaIf->FileId) {
         zpUpperVecWrapIf = zpTopVecWrapIf;
    } else {
         zpUpperVecWrapIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zpCacheMetaIf->CommitId);
    }

	zMem_Alloc(zpCurVecWrapIf, struct zVecWrapInfo, 1);
    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s %s CURRENT -- %s", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath,
            (-1 == zpCacheMetaIf->FileId) ? "--name-only" : "",
            zGet_OneCommitSigPtr(zpTopVecWrapIf, zpCacheMetaIf->CommitId),
            (-1 == zpCacheMetaIf->FileId) ? "" : (char *)(((struct zSendInfo *)zGet_SendIfPtr(zpUpperVecWrapIf, zpCacheMetaIf->FileId) )->data));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); zVecId++) {
        if (zVecId >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf);
        }

        zSendDataLen = 1 + strlen(zpRes);
        zVecDataLen = zSendDataLen + sizeof(struct zSendInfo);
        zCheck_Null_Exit( zpSendIf = malloc(zVecDataLen) );

        zpSendIf->SelfId = zVecId;
        zpSendIf->DataLen = zSendDataLen;
        strcpy((char *)zpSendIf->data, zpRes);

        zpCurVecWrapIf->p_VecIf[zVecId].iov_base = zpSendIf;
        zpCurVecWrapIf->p_VecIf[zVecId].iov_len = zVecDataLen;
    }
    pclose(zpShellRetHandler);

    zpCurVecWrapIf->VecSiz = zVecId;
    if (0 == zpCurVecWrapIf->VecSiz) {
        /* 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空 */
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = NULL;
        return;
    } else {
        /* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf);
    }

    /* 将自身关联到上一级数据结构 */
    zpUpperVecWrapIf->p_RefDataIf->p_SubVecWrapIf = zpCurVecWrapIf;

    if (-1 == zpCacheMetaIf->FileId) {
        /* 如果是文件路径级别，且差异文件数量不为0，则需要分配RefData空间，指向文件对比差异内容VecInfo */
        zMem_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zpCurVecWrapIf->VecSiz);

        /* 如果是文件级别，则进入下一层获取对应的差异内容 */
        for (_i zId = 0; zId < zpCurVecWrapIf->VecSiz; zId++) {
            zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);
            zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
            zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
            zpSubCacheMetaIf->CommitId = zpCacheMetaIf->CommitId;
            zpSubCacheMetaIf->FileId = zId;

            zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubCacheMetaIf);
        }
    } else {
        /* 如果是文件差异内容级别，因为没有下一级数据，所以置为NULL */
        zpCurVecWrapIf->p_RefDataIf = NULL;
    }

    free(zpIf);  // 上一级传入的结构体空间是在堆上分配的
}

/*
 *  传入的是一个包含单次 commit 信息的额外malloc出来的 zVerWrapInfo 结构体指针，需要释放其下的文件列表结构及其内部的文件内容结构
 */
void
zfree_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zVecWrapInfo *zpVecWrapIf = (struct zVecWrapInfo *) zpIf;
    for (_i zFileId = 0; zFileId < zpVecWrapIf->VecSiz; zFileId++) {
        for (_i zLineId = 0; zLineId < zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->VecSiz; zLineId++) {
            free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf[zLineId].iov_base);
        }
        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf);

        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_RefDataIf->p_SubVecWrapIf);
        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_RefDataIf);

        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId));

        free(zpVecWrapIf->p_VecIf[zFileId].iov_base);
    }
    free(zpVecWrapIf->p_VecIf);

    free(zpVecWrapIf->p_RefDataIf->p_data);
    free(zpVecWrapIf->p_RefDataIf->p_SubVecWrapIf);
    free(zpVecWrapIf->p_RefDataIf);

    free(zpVecWrapIf);  // 传入的对象是为线程任务新开辟的内存空间，需要释放掉
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zgenerate_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpOldVecWrapIf;

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

    struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, zCnter, *zpIntPtr;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128], zLogPathBuf[128];

    if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);

        sprintf(zShellBuf, "cd %s && git log --format=%%H%%ct", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath); // 必须在shell命令中切换到正确的工作路径
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].DeployVecWrapIf);

        strcpy(zLogPathBuf, zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath);
        strcat(zLogPathBuf, "/");
        strcat(zLogPathBuf, zSigLogPath);
        sprintf(zShellBuf, "cat %s", zLogPathBuf);
    }
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    
    for (zCnter = 0; (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))) && (zCnter < zCacheSiz); zCnter++) {
        zSendDataLen = 2 * sizeof(zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
        zVecDatalen = zSendDataLen + sizeof(struct zSendInfo);

        zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

        zpCommitSendIf->SelfId = zCnter;
        zpCommitSendIf->DataLen = zSendDataLen;
		zpIntPtr = zpCommitSendIf->data;
		zpIntPtr[0] = zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
        zpIntPtr[1] = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

        if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf) {
            zMem_Alloc(zpOldVecWrapIf, struct zVecWrapInfo, 1);

            zpOldVecWrapIf->p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_RefDataIf;
            zpOldVecWrapIf->p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf;
            zpOldVecWrapIf->VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->VecSiz;

            zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
            zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter) = NULL;
        }

        zpTopVecWrapIf->p_VecIf[zCnter].iov_base = zpCommitSendIf;
        zpTopVecWrapIf->p_VecIf[zCnter].iov_len = zVecDatalen;

        zpRes[zBytes(40)] = '\0';
        zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
        strcpy(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, zpRes);

        zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zCnter].iov_base
            = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.p_VecIf[zCnter].iov_base;
        zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zCnter].iov_len
            = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.p_VecIf[zCnter].iov_len;
    }
    pclose(zpShellRetHandler);

    zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.VecSiz 
        = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.VecSiz 
        = zCnter;  // 存储的是实际的对象数量

    zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitCacheQueueHeadId = zCacheSiz;  // 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置

    // 生成下一级缓存
    _i zCacheUpdateLimit = (zPreLoadCacheSiz < zCnter) ? zPreLoadCacheSiz : zCnter;
    for (zCnter = 0; zCnter < zCacheUpdateLimit; zCnter++) {
        zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);

        zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
        zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
        zpSubCacheMetaIf->CommitId = zCnter;
        zpSubCacheMetaIf->FileId = -1;

        zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubCacheMetaIf); // +
    }
}

/*
 * 当监测到有新的代码提交时，为新版本代码生成缓存
 */
void
zupdate_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf, *zpOldVecWrapIf;

    struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, *zpHeadId, *zpIntPtr;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128];

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;
    zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);
    zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf);

    zpHeadId = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitCacheQueueHeadId);

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log -1 --format=%%H%%ct", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    zpRes = zget_one_line_from_FILE(zpShellRetHandler);
    pclose(zpShellRetHandler);

    zSendDataLen = 2 * sizeof(zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
    zVecDatalen = zSendDataLen + sizeof(struct zSendInfo);

    zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

    zpCommitSendIf->SelfId = *zpHeadId;
    zpCommitSendIf->DataLen = zSendDataLen;
	zpIntPtr = zpCommitSendIf->data;
	zpIntPtr[0] = zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
    zpIntPtr[1] = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

    if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)) {
        zMem_Alloc(zpOldVecWrapIf, struct zVecWrapInfo, 1);

        zpOldVecWrapIf->p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_RefDataIf;
        zpOldVecWrapIf->p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_VecIf;
        zpOldVecWrapIf->VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->VecSiz;

        zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
        zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId) = NULL;
    }

    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_base = zpCommitSendIf;
    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_len = zVecDatalen;

    zpRes[zBytes(40)] = '\0';
    zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
    strcpy(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, zpRes);

    if (zCacheSiz > zpTopVecWrapIf->VecSiz) {
        zpTopVecWrapIf->VecSiz++;
        zpSortedTopVecWrapIf->VecSiz++;
    }

    // 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
    for (_i i = 0, j = *zpHeadId; i < zpTopVecWrapIf->VecSiz; i++) {
        zpSortedTopVecWrapIf->p_VecIf[i].iov_base = zpTopVecWrapIf->p_VecIf[j].iov_base;
        zpSortedTopVecWrapIf->p_VecIf[i].iov_len = zpTopVecWrapIf->p_VecIf[j].iov_len;

        if ((zpTopVecWrapIf->VecSiz - 1) == j) {
            j = 0;
        } else {
            j++;
        }
    }

    /* 生成下一级缓存 */
    zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);

    zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
    zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
    zpSubCacheMetaIf->CommitId = *zpHeadId;
    zpSubCacheMetaIf->FileId = -1;

    zget_file_list_and_diff_content(zpSubCacheMetaIf);

    /* 更新队列下一次将写入的位置的索引 */
    if (0 == *zpHeadId) {
        *zpHeadId = zCacheSiz -1;
    } else {
        (*zpHeadId)--;
    }
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
    struct zDeployResInfo *zpTmpIf;

    _i zFd[2] = {-9};
    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zpGlobRepoIf[zRepoId].TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zpGlobRepoIf[zRepoId].p_DpResList, struct zDeployResInfo, zpGlobRepoIf[zRepoId].TotalHost);  // 分配数组空间，用于顺序读取

    for (_i j = 0; j < zpGlobRepoIf[zRepoId].TotalHost; j++) {
        zpGlobRepoIf[zRepoId].p_DpResList[j].RepoId = zRepoId;  // 写入代码库索引值
        zpGlobRepoIf[zRepoId].p_DpResList[j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zpGlobRepoIf[zRepoId].p_DpResList[j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zpGlobRepoIf[zRepoId].p_DpResList->ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpGlobRepoIf[zRepoId].p_DpResHash[(zpGlobRepoIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpGlobRepoIf[zRepoId].p_DpResHash[(zpGlobRepoIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz] = &(zpGlobRepoIf[zRepoId].p_DpResList[j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zpGlobRepoIf[zRepoId].p_DpResList[j]);
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

    pthread_rwlock_wrlock(&(zpGlobRepoIf[zRepoId].RwLock));

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));

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

    pthread_rwlock_unlock(&(zpGlobRepoIf[zRepoId].RwLock));
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
    struct zObjInfo *zpObjIf = (struct zObjInfo *) zpIf;
    char zShellBuf[zCommonBufSiz];

    sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify %d %s %s",
        zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
        zpObjIf->RepoId,
        zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
        zpObjHash[zpObjIf->UpperWid]->path);

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}

/***********
 * NET OPS *
 ***********/

#define zRecv_Cmd_And_Check(zSd) do {\
    if (zSizeOf(struct zRecvInfo) > zrecv_nohang(zSd, &zRecvIf, sizeof(struct zRecvInfo), 0, NULL)) {\
        zPrint_Err(0, NULL, "接收到的数据不完整!");\
		zErrNo = -7;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
        shutdown(zSd, SHUT_RDWR);\
        return;\
    }\
\
    if (0 > (zRecvIf).OpsId || zServHashSiz <= zRecvIf.RepoId) {\
        zPrint_Err(0, NULL, "操作指令ID不存在!");\
		zErrNo = -1;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
        shutdown(zSd, SHUT_RDWR);\
        return;\
    }\
\
    if (0 > (zRecvIf).RepoId || zGlobRepoNum <= zRecvIf.RepoId) {\
        zPrint_Err(0, NULL, "项目ID不存在!");\
		zErrNo = -2;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
        shutdown(zSd, SHUT_RDWR);\
        return;\
    }\
} while(0)\

/*
 * 0：添加新项目（代码库）
 */
void
zadd_repo(void *zpIf) {
	struct zRecvInfo zRecvIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);

	zRecv_Cmd_And_Check(zSd);
	// TO DO
}

/*
 * 1：回复开发人员已提交的版本号列表
 * 2：回复历史布署版本号列表
 */
void
zprint_record(void *zpIf) {
	struct zRecvInfo zRecvIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);

	zRecv_Cmd_And_Check(zSd);

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
	if (1 == zRecvIf.OpsId) {
    	zsendmsg(zSd, zpGlobRepoIf[zRecvIf.RepoId].SortedCommitVecWrapIf, 0, NULL);
	} else {
    	zsendmsg(zSd, zpGlobRepoIf[zRecvIf.RepoId].DeployVecWrapIf, 0, NULL);
	}
    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );

    shutdown(zSd, SHUT_RDWR);
}

    zNetServ[3] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[4] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[5] = zdeploy;  // 布署(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[6] = zdeploy;　// 强制布署（当前询所依据的缓存版本与中控机不同时）
    zNetServ[7] = zdeploy;  // 撤销(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[8] = zdeploy;　// 强制撤销（当前询所依据的缓存版本与中控机不同时）
    zNetServ[9] = zstate_confirm;  // 布署成功状态人工确认
    zNetServ[10] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[11] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[12] = zupdate_ipv4_db_major;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[13] = zupdate_ipv4_db_all;  // 更新集群中所有主机的 ip 列表
    zNetServ[14] = zallow_deploy;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[15] = zdeny_deploy;  // 恢复布署／撤销功能

/*
 * 3：显示差异文件路径列表
 */
void
zprint_diff_files(void *zpIf) {
	struct zRecvInfo zRecvIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);

	zRecv_Cmd_And_Check(zSd);

    if (0 > (zRecvIf).CommitId || zCacheSiz <= zRecvIf.CommitId) {
        zPrint_Err(0, NULL, "版本号ID不存在!");
		zErrNo = -3;
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (zCommitPreCacheSiz <= zIf.CommitId) {
        pthread_rwlock_wrlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );
        zfree_commit_source((zpGlobRepoIf[zIf.RepoId].p_VecIf[0]->p_vec)[zIf.CommitId].iov_base);  // 内部已检查 NULL
        zget_file_diff_info(zIf.RepoId, zIf.CommitId, -1);
        pthread_rwlock_unlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );
    }

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );

	if (1 == zRecvIf.OpsId) {
    	zsendmsg(zSd, zpGlobRepoIf[zRecvIf.RepoId].SortedCommitVecWrapIf.p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf, 0, NULL);
	} else {
    	zsendmsg(zSd, zpGlobRepoIf[zRecvIf.RepoId].DeployVecWrapIf.p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf, 0, NULL);
	}

    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

// 打印当前版本缓存与CURRENT标签之间的指定文件的内容差异
void
zprint_diff_contents(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zCodeInfo *zpOneCommitIf, *zpOneFileIf;
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    pthread_rwlock_rdlock(&(zpGlobRepoIf[zIf.RepoId].RwLock));
    zpOneCommitIf = (zCodeInfo *) (zpGlobRepoIf[zIf.RepoId].p_VecIf[0]->p_vec[zIf.CommitId].iov_base);
    zpOneFileIf= (zCodeInfo *)(zpOneCommitIf->p_SubObjVecIf->p_vec[zIf.FileId].iov_base);
    zsendmsg(zSd, zpOneFileIf->p_SubObjVecIf, 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );

    shutdown(zSd, SHUT_RDWR);
}

// 列出最近zLogCacheSiz次或全部历史布署日志
void
zlist_log(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    _i zVecSiz;
    zVecInfo zVecIf;

    zDeployLogInfo zIf = { .RepoId = -1, };
    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (NULL == zppLogCacheVecIf[zIf.RepoId]) {
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );

    if ( 'l' == zIf.hints[0]){    // 默认直接直接回复预存的最近zLogCacheSiz次记录
        zVecIf.p_vec = zppSortedLogCacheVecIf[zIf.RepoId];
        zVecIf.VecSiz = zpLogCacheVecSiz[zIf.RepoId];
        zsendmsg(zSd, &zVecIf, 0, NULL);  // 按时间戳降序排列的缓存结果
    } else {  // 若前端请求列出所有历史记录，从日志文件中读取
        zDeployLogInfo *zpMetaLogIf;
        char *zpDpSig, *zpPathBuf, zShellBuf[zCommonBufSiz], *zpLineContent;
        struct stat zStatIf[2];
        FILE *zpFile;

        _i zFd[3];
        zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zIf.RepoId].RepoPath, O_RDONLY));
        zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zMetaLogPath, O_RDONLY));
        zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zSigLogPath, O_RDONLY));
        zCheck_Negative_Exit(fstat(zFd[1], &(zStatIf[0])));  // 获取日志属性
        zCheck_Negative_Exit(fstat(zFd[2], &(zStatIf[1])));  // 获取日志属性
        close(zFd[0]);

        zVecSiz = 2 * zStatIf[0].st_size / sizeof(zDeployLogInfo);  // 确定存储缓存区的大小
        struct iovec zVec[zVecSiz];

        zCheck_Null_Exit(zpMetaLogIf = (zDeployLogInfo *) mmap(NULL, zStatIf[0].st_size, PROT_READ, MAP_PRIVATE, zFd[1], 0));  // 将meta日志mmap至内存
        madvise(zpMetaLogIf, zStatIf[0].st_size, MADV_WILLNEED);  // 提示内核大量预读

        zCheck_Null_Exit(zpDpSig = (char *) mmap(NULL, zStatIf[1].st_size, PROT_READ, MAP_PRIVATE, zFd[2], 0));  // 将sig日志mmap至内存
        madvise(zpMetaLogIf, zStatIf[1].st_size, MADV_WILLNEED);  // 提示内核大量预读

        for (_i i = 0; i < zVecSiz; i++) {  // 拼装日志信息
            if (0 == i % 2) {
                zVec[i].iov_base = zpMetaLogIf + i / 2;
                zVec[i].iov_len = sizeof(zDeployLogInfo);
            } else {
                zMem_Alloc(zpPathBuf, char, (zpMetaLogIf + i / 2)->PathLen);
                sprintf(zShellBuf, "cd %s && git log %s -1 --name-only --format=", zpGlobRepoIf[zIf.RepoId].RepoPath, zpDpSig + (i / 2) * zBytes(41));
                zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));

                for (size_t zWrOffSet = 0; NULL != (zpLineContent = zget_one_line_from_FILE(zpFile));) {
                    zLen = strlen(zpLineContent);
                    zpLineContent[zLen] = '\0';
                    zpLineContent[zLen - 1] = '\n';
                    strcpy(zpPathBuf + zWrOffSet, zpLineContent);
                    zWrOffSet += 1 + zLen;
                }

                zVec[i].iov_base = zpPathBuf;
                zVec[i].iov_len = (zpMetaLogIf + i / 2)->PathLen;
            }
        }

        zVecIf.p_vec = zVec;
        zVecIf.VecSiz = zVecSiz;
        zsendmsg(zSd, &zVecIf, 0, NULL);    // 发送结果

        munmap(zpMetaLogIf, zStatIf[0].st_size);
        munmap(zpMetaLogIf, zStatIf[1].st_size);
        close(zFd[1]);
        close(zFd[2]);
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zIf.RepoId].RwLock));
    shutdown(zSd, SHUT_RDWR);
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    char zShellBuf[128], *zpRes;
    FILE *zpFile;
    _i zFd[2];

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));  // 获取当前sig日志文件属性
    close(zFd[0]);
    close(zFd[1]);

    // 将CURRENT标签的40位sig字符串追加写入.git_shadow/log/sig
    sprintf(zShellBuf, "cd %s && git log -1 CURRENT --format=%%H", zpGlobRepoIf[zRepoId].RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zpRes = zget_one_line_from_FILE(zpFile);

    if ( zBytes(41) != write(zpGlobRepoIf[zRepoId].LogFd, zppCURRENTsig[zRepoId], zBytes(41))) {
        zCheck_Negative_Exit(ftruncate(zpGlobRepoIf[zRepoId].LogFd, zStatIf.st_size));
        zPrint_Err(0, NULL, "Can't write to <.git_shadow/log/deploy/sig> !");
        exit(1);
    }
}

// 执行布署，目前仅支持单文件布署与全部布署两种模式（文件多选布署待实现）
void
zdeploy(void *zpIf) {
// TEST:PASS
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    _i zSd = *((_i *)zpIf);
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp);
    zCodeInfo *zpOneCommitIf, *zpOneFileIf;

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if ('D' == zIf.hints[0]) {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -D", zpGlobRepoIf[zIf.RepoId].RepoPath);
    } else {
        zpOneCommitIf = (zCodeInfo *) (zpGlobRepoIf[zIf.RepoId].p_VecIf[0]->p_vec[zIf.CommitId].iov_base);
        zpOneFileIf= (zCodeInfo *)(zpOneCommitIf->p_SubObjVecIf->p_vec[zIf.FileId].iov_base);
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -d %s", zpGlobRepoIf[zIf.RepoId].RepoPath, zpOneFileIf->data);
    }

    pthread_rwlock_wrlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );  // 加锁，布署没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "shell 布署命令出错!");
    }

    _ui zSendBuf[zpGlobRepoIf[zIf.RepoId].TotalHost];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    do {
        zsleep(0.2);  // 每隔0.2秒向前端返回一次结果

        _i zUnReplyCnt = 0;  // 必须放在 do...while 循环内部
        for (_i i = 0; i < zpGlobRepoIf[zIf.RepoId].TotalHost; i++) {  // 登记尚未确认状态的客户端ip列表
            if (0 == zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->DeployState) {
                zSendBuf[zUnReplyCnt] = zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->ClientAddr;
                zUnReplyCnt++;
            }
        }

        zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);
    } while (zpGlobRepoIf[zIf.RepoId].ReplyCnt < zpGlobRepoIf[zIf.RepoId].TotalHost);  // 等待所有client端确认状态：前端人工标记＋后端自动返回
    zpGlobRepoIf[zIf.RepoId].ReplyCnt = 0;

    zwrite_log_and_update_cache(zIf.RepoId);  // 将本次布署信息写入日志

    for (_i i = 0; i < zpGlobRepoIf[zIf.RepoId].TotalHost; i++) {
        zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->DeployState = 0;  // 重置client状态，以便下次布署使用
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zIf.RepoId].RwLock));  // 释放锁
    shutdown(zSd, SHUT_RDWR);
}

// 依据布署日志，撤销指定文件或整次提交，目前仅支持单文件撤销与全部整批次撤销两种模式（文件多选撤销待实现）
void
zrevoke(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zDeployLogInfo zIf = { .RepoId = -1, };
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char zCommitSigBuf[41];  // 存放40位的git commit sig 及 一个 '\0'

    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);
    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (zIf.index >= zLogCacheSiz) {
        zPrint_Err(0, NULL, "请求撤销的文件索引超出缓存范围!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    _i zFd[2];
    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zIf.RepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Return(pread(zFd[1], &zCommitSigBuf, zBytes(40), zBytes(40) * zIf.index),);
    zCommitSigBuf[40] = '\0';
    close(zFd[0]);
    close(zFd[1]);

    if ('R' == zIf.hints[0]) {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -R -i %s", zpGlobRepoIf[zIf.RepoId].RepoPath, zCommitSigBuf);
    } else {
        _i zPathIdInCache;  // 用于接收某个文件路径名称在路径列表中的行号（从1开始）

        if (zSizeOf(_i) > zrecv_nohang(zSd, &zPathIdInCache, sizeof(_i), 0, NULL)) {
            zPrint_Err(0, NULL, "接收到的数据不完整!");
            shutdown(zSd, SHUT_RDWR);
            return;
        }

        _i zOffSet = 0;
        for (_i i = 0; i < zPathIdInCache; i++) {
            zOffSet += 1 + strlen(zTypeConvert(zppLogCacheVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo*)->path + zOffSet);
        }

        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -r -i %s %s", zpGlobRepoIf[zIf.RepoId].RepoPath, zCommitSigBuf, zTypeConvert(zppLogCacheVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo*)->path + zOffSet);
    }

    pthread_rwlock_wrlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等

    if (0 != system(zShellBuf)) {  // 执行外部 shell 命令
    }

    _ui zSendBuf[zpGlobRepoIf[zIf.RepoId].TotalHost];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    _i zUnReplyCnt = 0;  // 尚未回复的目标主机计数
    do {
        zsleep(0.2);  // 每0.2秒统计一次结果，并发往前端

        for (_i i = 0; i < zpGlobRepoIf[zIf.RepoId].TotalHost; i++) {
            if (0 == zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->DeployState) {
            zSendBuf[zUnReplyCnt] = zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->ClientAddr;
            zUnReplyCnt++;
            }
        }

        zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);  // 向前端发送当前未成功的列表
    } while (zpGlobRepoIf[zIf.RepoId].ReplyCnt < zpGlobRepoIf[zIf.RepoId].TotalHost);  // 一直等待到所有client状态确认为止：前端人工确认＋后端自动确认
    zpGlobRepoIf[zIf.RepoId].ReplyCnt = 0;

    zwrite_log_and_update_cache(zIf.RepoId);  // 将本次布署信息写入日志

    for (_i i = 0; i < zpGlobRepoIf[zIf.RepoId].TotalHost; i++) {
        zpGlobRepoIf[zIf.RepoId].p_DpResHash[i]->DeployState = 0;  // 将本项目各主机状态重置为0
    }

    pthread_rwlock_unlock( &(zpGlobRepoIf[zIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

// 接收并更新对应的布署状态确认信息
void
zconfirm_deploy_state(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zDeployResInfo zIf = { .RepoId = -1 };

    if ((zSizeOf(zIf) - zSizeOf(zIf.p_next)) > zrecv_nohang(zSd, &zIf, (zSizeOf(zIf) - zSizeOf(zIf.p_next)), 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    // HASH 索引
    for (zDeployResInfo *zpTmp = zpGlobRepoIf[zIf.RepoId].p_DpResHash[zIf.ClientAddr % zDeployHashSiz]; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (zpTmp->ClientAddr == zIf.ClientAddr) {
            zpTmp->DeployState = 1;
            zpGlobRepoIf[zIf.RepoId].ReplyCnt++;
            shutdown(zSd, SHUT_RDWR);
            return;
        }
    }

    zPrint_Err(0, NULL, "不明来源的确认信息!");
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 从前端接收新的ipv4列表及其 md5 sig
 * 若接收的md5 sig 与本地文件不相等，则写入txt文件
 * 若相等，则不需要写入操作
 */
void
zupdate_ipv4_db_txt(void *zpIf) {
    char zRecvBuf[zCommonBufSiz], zPathBuf[zCommonBufSiz], *zpSigMD5, *zpWritePath;
    _i zRepoId, zFd, zRecvSiz, zSd = * ((_i *) zpIf);

    if ((zBytes(36) + zSizeOf(_i)) > recv(zSd, zRecvBuf, (zBytes(36) + zSizeOf(_i)), 0)) { // 接收4字节提示信息＋4字节代码库ID＋32节字MD5 sig
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    zpWritePath = ('u' == zRecvBuf[0]) ? zMajorIpPathTxt : zAllIpPathTxt;
    zRepoId = (_i) zRecvBuf[zBytes(4)];

    if (0 > zRepoId || zRepoNum <= zRepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    strcpy(zPathBuf, zpGlobRepoIf[zRepoId].RepoPath);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWritePath);
    if (NULL == (zpSigMD5 = zgenerate_file_sig_md5(zPathBuf))) {
        goto zMarkWrite;  // 文件不存在，跳转到写入环节
    }

    for (_i i = zBytes(4) + zSizeOf(_i); i < (zBytes(36) + zSizeOf(_i)); i++) {
        if (zRecvBuf[i] != zpSigMD5[i]) {
            goto zMarkWrite;  // MD5 sig 不同，跳转到写入环节
        }
    }
    shutdown(zSd, SHUT_RDWR);  // 若 MD5 sig 相同，无需写入
    return;

zMarkWrite:
    zCheck_Negative_Exit(zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600));
    zCheck_Negative_Return(zFd,);

    // 接收网络数据并同步写入文件
    while (0 < (zRecvSiz = recv(zSd, zRecvBuf, zCommonBufSiz, 0))) {
        if (zRecvSiz != write(zFd, zRecvBuf, zRecvSiz)) {
            zPrint_Err(errno, NULL, "Write ipv4 list to txt file failed!");
            exit(1);
        }
    }
    close(zFd);
    zCheck_Negative_Return(zRecvSiz,);

    // 检验文件新的 MD5 checksum
    if (0 != strcmp(zpSigMD5, zgenerate_file_sig_md5(zPathBuf))) {
        zsendto(zSd, "!", zBytes(1), 0, NULL);  //  若MD5 sig 不符，要求前端重发报文
        zPrint_Err(0, NULL, "ECS主机ip列表文件接收异常：MD5 检验失败!");
    }

    shutdown(zSd, SHUT_RDWR);
}

/*
 * 核心路由函数
 */
void
zstart_server(void *zpIf) {
#define zMaxEvents 64
    /* 如下部分定义服务接口 */
    zThreadPoolOps zNetServ[zServHashSiz];
    zNetServ[0] = zadd_repo;  // 添加新代码库
    zNetServ[1] = zprint_record;  // 显示提交记录
    zNetServ[2] = zprint_record;  // 显示布署记录
    zNetServ[3] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[4] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[5] = zdeploy;  // 布署(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[6] = zdeploy;　// 强制布署（当前询所依据的缓存版本与中控机不同时）
    zNetServ[7] = zdeploy;  // 撤销(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[8] = zdeploy;　// 强制撤销（当前询所依据的缓存版本与中控机不同时）
    zNetServ[9] = zstate_confirm;  // 布署成功状态人工确认
    zNetServ[10] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[11] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[12] = zupdate_ipv4_db_major;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[13] = zupdate_ipv4_db_all;  // 更新集群中所有主机的 ip 列表
    zNetServ[14] = zallow_deploy;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[15] = zdeny_deploy;  // 恢复布署／撤销功能

    /* 如下部分配置 epoll 环境 */
    struct zNetServInfo *zpNetServIf = (struct zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv),);

    // 如下部分启动 epoll 监听服务
    for (_i zCmd = 0;;) {  // zCmd: 用于存放前端发送的操作指令
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return(zEvNum,);

        for (_i i = 0; i < zEvNum; i++, zCmd = 0) {
           if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
               zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
               zCheck_Negative_Return(zConnSd,);

               zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
               zEv.data.fd = zConnSd;
               zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv),);
            } else {
               if ((sizeof(_i) == zrecv_nohang(zEvents[i].data.fd, &zCmd, sizeof(_i), MSG_PEEK, NULL))
					   && (zServHashSiz > zCmd) && (0 <= zCmd)) {
                   zAdd_To_Thread_Pool(zNetServ[zCmd], &(zEvents[i].data.fd));
               }
            }
        }
    }
#undef zMaxEvents
}

/*
 * 用于集群中的主机向中控机发送状态确认信息
 */
void
zstate_reply(char *zpHost, char *zpPort) {
	struct zRecvInfo zDpResIf;
    _i zFd, zSd, zResLen;
    _ui zIpv4Bin;

    zCheck_Negative_Exit( zFd = open(zRepoIdPath, O_RDONLY) );
    zCheck_Negative_Exit( read(zFd, &(zDpResIf.RepoId), sizeof(_i)) ); // 读取版本库ID
	zDpResIf.OpsId = 10;  // 填充动作代号

	/* 以点分格式的ipv4地址连接服务端 */
    if (-1== (zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        zPrint_Err(0, NULL, "无法与中控机建立连接！");
        exit(1);
    }

	/* 读取本机的所有非回环ip地址，依次发送状态确认信息至服务端 */
    zCheck_Negative_Exit( zFd = open(zSelfIpPath, O_RDONLY) );
    while (0 < (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
        zDpResIf.ClientAddr = zIpv4Bin;  // 标识本机身份：ipv4地址
        if (sizeof(zDpResIf) != zsendto(zSd, &zDpResIf, sizeof(zDpResIf), 0, NULL)) {
            zPrint_Err(0, NULL, "布署状态信息回复失败！");
        }
    }

    close(zFd);
    shutdown(zSd, SHUT_RDWR);
}
