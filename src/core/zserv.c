#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
#define zGet_JsonStr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_VecIf[zSelfId].iov_base)
#define zGet_SubVecWrapIf(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_SubVecWrapIf)
#define zGet_NativeData(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_data)

#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) zGet_NativeData(zpTopVecWrapIf, zCommitId)

/*
 * 专用于缓存的内存调度分配函数，适用多线程环境，不需要free
 * 调用此函数的父函数一定是已经取得全局写锁的，故此处不能再试图加写锁
 */
void zgenerate_cache(void *);

void *
zalloc_cache(_i zRepoId, size_t zSiz) {
    pthread_mutex_lock(&(zpGlobRepoIf[zRepoId].MemLock));

    // 有死锁问题
//    if ((zSiz + zpGlobRepoIf[zRepoId].MemPoolHeadId) > zpGlobRepoIf[zRepoId].MemPoolSiz) {
//        /* 内存池容量不足时，需要全量刷新缓存，必须首先拿到写锁权限方可执行 */
//        pthread_rwlock_wrlock( &(zpGlobRepoIf[zRepoId].RwLock) );
//
//        struct zMetaInfo zMetaIf;
//
//        munmap(zpGlobRepoIf[zRepoId].p_MemPool, zpGlobRepoIf[zRepoId].MemPoolSiz);
//        zpGlobRepoIf[zRepoId].MemPoolSiz *= 2;
//        if (MAP_FAILED == (zpGlobRepoIf[zRepoId].p_MemPool = mmap(NULL, zpGlobRepoIf[zRepoId].MemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
//            zPrint_Err(0, NULL, "mmap failed!");
//            exit(1);
//        }
//
//        zMetaIf.DataType = zIsCommitDataType;
//        zMetaIf.RepoId = zRepoId;
//        zMetaIf.CcurSwitch = zCcurOff;  // 串行执行，保证数据一致性
//        zgenerate_cache(&(zMetaIf));
//
//        zMetaIf.DataType = zIsDeployDataType;
//        zMetaIf.RepoId = zRepoId;
//        zMetaIf.CcurSwitch = zCcurOff;
//        zgenerate_cache(&(zMetaIf));
//
//        pthread_rwlock_wrlock( &(zpGlobRepoIf[zRepoId].RwLock) );
//
//        return NULL;
//    }

    void *zpX = zpGlobRepoIf[zRepoId].p_MemPool + zpGlobRepoIf[zRepoId].MemPoolHeadId;
    zpGlobRepoIf[zRepoId].MemPoolHeadId += zSiz;

    pthread_mutex_unlock(&(zpGlobRepoIf[zRepoId].MemLock));
    return zpX;
}

/**************
 * NATIVE OPS *
 **************/
/*
 *  传入的是一个包含单次 commit 信息的额外malloc出来的 zVerWrapInfo 结构体指针，需要释放其下的文件列表结构及其内部的文件内容结构
 */
void
zfree_one_commit_cache(void *zpIf) {  // zpIf本体在代码库内存池中，不需要释放
    struct zVecWrapInfo *zpVecWrapIf = (struct zVecWrapInfo *) zpIf;
    for (_i zFileId = 0; zFileId < zpVecWrapIf->VecSiz; zFileId++) {
        free(zGet_SubVecWrapIf(zpVecWrapIf, zFileId)->p_VecIf);
    }
    free(zpVecWrapIf->p_VecIf);
}

/*
 * 功能：生成单个文件的差异内容缓存
 */
void
zget_diff_content(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpUpperVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], zRes[zBytes(4096)];

    char *zpData;  // 此项是 iovec 的 io_base 字段
    _i zVecCnter;
    _i zVecDataLen;
    _i zAllocSiz = 1;

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
    }

    zpUpperVecWrapIf = zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId);

    zpCurVecWrapIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
    /* 关联到上一级数据结构 */
    zpUpperVecWrapIf->p_RefDataIf[zpMetaIf->FileId].p_SubVecWrapIf = zpCurVecWrapIf;

    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s CURRENT -- %s",
            zpGlobRepoIf[zpMetaIf->RepoId].RepoPath,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zGet_NativeData(zpUpperVecWrapIf, zpMetaIf->FileId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    for (zVecCnter = 0; 0 < zget_str_content(zRes, zBytes(4096), zpShellRetHandler); zVecCnter++) {
        if (zVecCnter >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf);
        }

        zVecDataLen = 1 + strlen(zRes);
        /* 因为差异内容级别不使用json，故不必再开辟RefData */
        zCheck_Null_Exit( zpData = zalloc_cache(zpMetaIf->RepoId, zVecDataLen) );
        strcpy(zpData, zRes);

        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base = zpData;
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_len = zVecDataLen;

    }
    pclose(zpShellRetHandler);

    zpCurVecWrapIf->VecSiz = zVecCnter;
    if (0 == zpCurVecWrapIf->VecSiz) {
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = NULL;
        return;
    } else {
        /* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf);
        /* 因为没有下一级数据，所以置为NULL */
        zpCurVecWrapIf->p_RefDataIf = NULL;
    }
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表与每个文件的差异内容
 */
void
zget_file_list_and_diff_content(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf, *zpSubMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpCurVecWrapIf, *zpOldVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], zRes[zBytes(1024)];

    char zJsonBuf[zBytes(256)];
    _i zVecCnter;
    _i zVecDataLen, zDataLen;
    _i zAllocSiz = 64;

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
    }

    // 检查是否有旧数据，有则释放空间
    if (NULL != zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        zpOldVecWrapIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));

        zpOldVecWrapIf->p_VecIf = zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf;
        zpOldVecWrapIf->VecSiz = zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz;

        zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
        zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = NULL;
    }

    zpCurVecWrapIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
    /* 关联到上一级数据结构 */
    zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zpCurVecWrapIf;

    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);
    zMem_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff --name-only %s CURRENT",
            zpGlobRepoIf[zpMetaIf->RepoId].RepoPath,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecCnter = 0;  NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler); zVecCnter++) {
        if (zVecCnter > (zAllocSiz - 2)) {  // For json ']'
            zAllocSiz *= 2;
            zMem_Re_Alloc( zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf );
            zMem_Re_Alloc( zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zAllocSiz, zpCurVecWrapIf->p_RefDataIf );
        }

        zDataLen = strlen(zRes);
        zRes[zDataLen - 1] = '\0';
        zCheck_Null_Exit( zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data = zalloc_cache(zpMetaIf->RepoId, zDataLen) );
        strcpy(zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data, zRes);  // 信息正文实际存放的位置

        /* 用于转换成JsonStr以及传向下一级函数 */
        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
        zpSubMetaIf->OpsId = 0;
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->CommitId = zpMetaIf->CommitId;
        zpSubMetaIf->FileId = zVecCnter;
        zpSubMetaIf->HostId = -1;
        zpSubMetaIf->CacheId = zpMetaIf->CacheId;
        zpSubMetaIf->DataType = zpMetaIf->DataType;
        zpSubMetaIf->CcurSwitch = zpMetaIf->CcurSwitch;
        zpSubMetaIf->p_TimeStamp = "";
        zpSubMetaIf->p_data = zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);

        zVecDataLen = strlen(zJsonBuf);
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base, zJsonBuf, zVecDataLen);
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_len = zVecDataLen;

        /* 进入下一层获取对应的差异内容 */
        if (zCcurOn == zpMetaIf->CcurSwitch) {
            zAdd_To_Thread_Pool(zget_diff_content, zpSubMetaIf);
        } else {
            zget_diff_content(zpSubMetaIf);
        }
    }
    pclose(zpShellRetHandler);

    if (0 == zVecCnter) {
        /* 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空 */
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = NULL;
        zpCurVecWrapIf->VecSiz = 0;
        return;
    } else {
        zpCurVecWrapIf->VecSiz = zVecCnter + 1;  // 最后有一个额外的成员存放 json ']'

        /* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf);  // 多留一项用于存放二维json最后的']'
        zMem_Re_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_RefDataIf);

        /* 修饰第一项，添加最后一项，形成二维json格式 */
        ((char *)(zpCurVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base = "]";
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_len= zBytes(1);  // 不发送最后的 '\0'

        // 防止意外访问出错
        zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data = NULL;
        zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_SubVecWrapIf = NULL;
    }
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zgenerate_cache(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf, *zpSubMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf;

    char zJsonBuf[zBytes(256)];  // iov_base
    _i zVecDataLen, zVecCnter;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[128], zLogPathBuf[128];

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
        // 必须在shell命令中切换到正确的工作路径
        sprintf(zShellBuf, "cd %s && git log --format=%%H_%%ct",
                zpGlobRepoIf[zpMetaIf->RepoId].RepoPath);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);

        strcpy(zLogPathBuf, zpGlobRepoIf[zpMetaIf->RepoId].RepoPath);
        strcat(zLogPathBuf, "/");
        strcat(zLogPathBuf, zLogPath);
        sprintf(zShellBuf, "cat %s", zLogPathBuf);
    }
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    // zCacheSiz - 1 :留一个空间给json需要 ']'
    for (zVecCnter = 0; (NULL != zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler)) && (zVecCnter < (zCacheSiz - 1)); zVecCnter++) {
        zRes[strlen(zRes) - 1] = '\0';
        zRes[40] = '\0';
        zCheck_Null_Exit( zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data = zalloc_cache(zpMetaIf->RepoId, zBytes(41)) );
        strcpy(zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data, zRes);

        /* 用于转换成JsonStr以及传向下一级函数 */
        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
        zpSubMetaIf->OpsId = 0;
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->CommitId = zVecCnter;
        zpSubMetaIf->FileId = -1;
        zpSubMetaIf->HostId = -1;
        zpSubMetaIf->CacheId = zpMetaIf->CacheId;
        zpSubMetaIf->DataType = zpMetaIf->DataType;
        zpSubMetaIf->CcurSwitch = zpMetaIf->CcurSwitch;
        zpSubMetaIf->p_TimeStamp = &(zRes[41]);
        zpSubMetaIf->p_data = zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);

        /* 将JsonStr内容存放到iov_base中 */
        zVecDataLen = strlen(zJsonBuf);
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base, zJsonBuf, zVecDataLen);
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len = zVecDataLen;

        /* 生成下一级缓存 */
        if (zCcurOn == zpMetaIf->CcurSwitch) {
            zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubMetaIf);
        } else {
            zget_file_list_and_diff_content(zpSubMetaIf);
        }

        /* 新生成的缓存本来就是有序的，不需要额外排序 */
        if (zIsCommitDataType ==zpMetaIf->DataType) {
            zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_base = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base;
            zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_len = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len;
        }
    }
    pclose(zpShellRetHandler);

    /* 存储的是实际的对象数量 */
    zpTopVecWrapIf->VecSiz = zVecCnter + 1;

    /* 修饰第一项，添加最后一项，形成二维json */
    if (0 != zVecCnter) {
        ((char *)(zpTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base = "]";
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len= zBytes(1);  // 不发送最后的 '\0'
    }

    // 防止意外访问
    for (_i i = zVecCnter; i < zCacheSiz; i++) {
        zpTopVecWrapIf->p_RefDataIf[i].p_data = NULL;
        zpTopVecWrapIf->p_RefDataIf[i].p_SubVecWrapIf = NULL;
    }

    if (zIsCommitDataType ==zpMetaIf->DataType) {
        zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf.VecSiz = zpTopVecWrapIf->VecSiz;
        zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_base = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base;
        zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_len = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len;
    }

    // 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置
    zpGlobRepoIf[zpMetaIf->RepoId].CommitCacheQueueHeadId = zCacheSiz - 1;
}

/*
 * 当监测到有新的代码提交时，为新版本代码生成缓存
 * 此函数在 inotify 中使用，传入的参数是 struct zObjInfo 数型
 */
void
zupdate_one_commit_cache(void *zpIf) {
// TEST:PASS
    struct zObjInfo *zpObjIf;
    struct zMetaInfo *zpSubMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;

    char zJsonBuf[zBytes(256)];  // iov_base
    _i zVecDataLen, *zpHeadId;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[128];

    zpObjIf = (struct zObjInfo*)zpIf;
    zpTopVecWrapIf = &(zpGlobRepoIf[zpObjIf->RepoId].CommitVecWrapIf);
    zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpObjIf->RepoId].SortedCommitVecWrapIf);

    pthread_rwlock_wrlock( &(zpGlobRepoIf[zpObjIf->RepoId].RwLock) );

    zpHeadId = &(zpGlobRepoIf[zpObjIf->RepoId].CommitCacheQueueHeadId);

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log -1 --format=%%H_%%ct",
            zpGlobRepoIf[zpObjIf->RepoId].RepoPath);

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler);
    pclose(zpShellRetHandler);

    zRes[strlen(zRes) - 1] = '\0';  // 去掉换行符
    zRes[40] = '\0';

    /* 防止冗余事件导致的重复更新 */
    if (0 == strcmp(zRes,
                zpGlobRepoIf[zpObjIf->RepoId].CommitVecWrapIf.p_RefDataIf[(*zpHeadId == (zCacheSiz - 1)) ? 0 : (1 + *zpHeadId)].p_data)) {
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpObjIf->RepoId].RwLock) );
        return;
    }

    zCheck_Null_Exit( zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data = zalloc_cache(zpObjIf->RepoId, zBytes(41)) );
    strcpy(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, zRes);

    /* 用于转换成JsonStr以及传向下一级函数 */
    zpSubMetaIf = zalloc_cache(zpObjIf->RepoId, sizeof(struct zMetaInfo));
    zpSubMetaIf->OpsId = 0;
    zpSubMetaIf->RepoId = zpObjIf->RepoId;
    zpSubMetaIf->CommitId = *zpHeadId;  // 逆向循环索引号更新
    zpSubMetaIf->FileId = -1;
    zpSubMetaIf->HostId = -1;
    zpSubMetaIf->CacheId = zpGlobRepoIf[zpObjIf->RepoId].CacheId;
    zpSubMetaIf->DataType = zIsCommitDataType;
    zpSubMetaIf->CcurSwitch = zCcurOn;  // 并发执行
    zpSubMetaIf->p_TimeStamp = &(zRes[41]);
    zpSubMetaIf->p_data = zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data;

    /* 生成下一级缓存 */
    zAdd_To_Thread_Pool( zget_file_list_and_diff_content, zpSubMetaIf );

    /* 将zMetaInfo转换为JSON文本 */
    zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);

    /* 将JsonStr内容存放到iov_base中 */
    zVecDataLen = strlen(zJsonBuf);
    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_base = zalloc_cache(zpObjIf->RepoId, zVecDataLen);
    memcpy(zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_base, zJsonBuf, zVecDataLen);
    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_len = zVecDataLen;

    /* 若未达到容量上限，VecSiz 加 1*/
    if (zCacheSiz > zpTopVecWrapIf->VecSiz) {
        zpSortedTopVecWrapIf->VecSiz = ++(zpTopVecWrapIf->VecSiz);
    }

    /* 改变 Sorted 序列之前，将原先的json开头 '[' 还原为 ','，形成二维json */
    ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = ',';

    // 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
    for (_i i = 0, j = *zpHeadId; i < zpTopVecWrapIf->VecSiz; i++) {
        zpSortedTopVecWrapIf->p_VecIf[i].iov_base = zpTopVecWrapIf->p_VecIf[j].iov_base;
        zpSortedTopVecWrapIf->p_VecIf[i].iov_len = zpTopVecWrapIf->p_VecIf[j].iov_len;

        if ((zCacheSiz - 1) == j) {
            j = 0;
        } else {
            j++;
        }
    }

    /* 更新队列下一次将写入的位置的索引 */
    if (0 == *zpHeadId) {
        *zpHeadId = zCacheSiz - 1;
    } else {
        (*zpHeadId)--;
    }

    /* 修饰第一项和最后一项，形成二维json */
    ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
    zpSortedTopVecWrapIf->p_VecIf[zpTopVecWrapIf->VecSiz].iov_base = "]";
    zpSortedTopVecWrapIf->p_VecIf[zpTopVecWrapIf->VecSiz].iov_len= zBytes(1);  // 不发送最后的 '\0'

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpObjIf->RepoId].RwLock) );
}

// /*
//  * 通用函数，调用外部程序或脚本文件执行相应的动作
//  * 传入参数：
//  * $1：代码库ID
//  * $2：代码库绝对路径
//  * $3：受监控路径名称
//  */
// void
// zinotify_common_callback(struct zMetaInfo *zpMetaIf) {
//     struct zObjInfo *zpObjIf = (struct zObjInfo *) zpIf;
//     char zShellBuf[zCommonBufSiz];
//
//     sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify %d %s %s",
//         zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
//         zpObjIf->RepoId,
//         zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
//         zpObjHash[zpObjIf->UpperWid]->path);
//
//     if (0 != system(zShellBuf)) {
//         zPrint_Err(0, NULL, "[system]: shell command failed!");
//     }
// }

/***********
 * NET OPS *
 ***********/
/* 检查 CommitId 是否合法 */
#define zCheck_CommitId() do {\
    if (0 > zpMetaIf->CommitId || (zCacheSiz - 1) < zpMetaIf->CommitId || NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf) {\
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );\
        zPrint_Err(0, NULL, "Commit ID 不存在!");\
        return -3;\
    }\
} while(0)

/* 检查 FileId 是否合法 */
#define zCheck_FileId() do {\
    if (0 > zpMetaIf->FileId || (zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) < zpMetaIf->FileId) {\
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );\
        zPrint_Err(0, NULL, "差异文件ID不存在!");\
        return -4;\
    }\
} while(0)

/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作 */
#define zCheck_CacheId() do {\
    if (zpGlobRepoIf[zpMetaIf->RepoId].CacheId != zpMetaIf->CacheId) {\
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );\
        zPrint_Err(0, NULL, "前端发送的缓存ID已失效!");\
        return -8;\
    }\
} while(0)

/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDeployLocked == zpGlobRepoIf[zpMetaIf->RepoId].DpLock) {\
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );\
        return -6;\
    }\
} while(0)

/*
 * 0：空函数，占位
 */
_i
zzero(struct zMetaInfo *_, _i __) {
    return 0;
}

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(struct zMetaInfo *zpMetaIf, _i zSd) {
    return 0;
}

/*
 * 6：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(struct zMetaInfo *zpMetaIf, _i zSd) {
    struct zVecWrapInfo *zpSortedTopVecWrapIf;
    char zJsonBuf[256];

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) )) {
        return -11;
    };

    /* 若上一次布署是失败的，则提醒前端此时对比的数据可能是不准确的 */
    if (zRepoDamaged == zpGlobRepoIf[zpMetaIf->RepoId].RepoState) {
        zpMetaIf->OpsId = -13;  // 此时代表错误码
        zconvert_struct_to_json_str(zJsonBuf, zpMetaIf);
        zsendto(zSd, zJsonBuf, strlen(zJsonBuf), 0, NULL);
    }

    zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );
    return 0;
}

/*
 * 5：开发人员已提交的版本号列表
 * 6：历史布署版本号列表
 * 10：显示差异文件路径列表
 */
_i
zprint_diff_files(struct zMetaInfo *zpMetaIf, _i zSd) {
    struct zVecWrapInfo *zpTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁

    zsendmsg(zSd, zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );
    return 0;
}

/*
 * 6：版本号列表
 * 10：显示差异文件路径列表
 * 11：显示差异文件内容
 */
_i
zprint_diff_content(struct zMetaInfo *zpMetaIf, _i zSd) {
    struct zVecWrapInfo *zpTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁
    zCheck_FileId();  // 宏内部会解锁

    zsendmsg(zSd, zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->p_RefDataIf[zpMetaIf->FileId].p_SubVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );
    return 0;
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
    struct stat zStatIf;
    char zShellBuf[128], zRes[zCommonBufSiz];
    FILE *zpFile;
    _i zFd[2], zLen;

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zLogPath, O_RDONLY));
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));  // 获取当前sig日志文件属性
    close(zFd[0]);
    close(zFd[1]);

    // 将 CURRENT 标签的40位sig字符串及10位时间戳追加写入.git_shadow/log/sig，连同行末的 '\0' 合计 51位
    sprintf(zShellBuf, "cd %s && git log -1 CURRENT --format=%%H%%ct", zpGlobRepoIf[zRepoId].RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zget_one_line(zRes, zCommonBufSiz, zpFile);

    if (zBytes(51) != (zLen = 1 + strlen(zRes))) {
        zPrint_Err(0, NULL, "外部命令返回的信息错误：CURRENTsig + TimeStamp 的长度不等于 51 !");
        exit(1);
    }

    if (zLen != write(zpGlobRepoIf[zRepoId].LogFd, zRes, zLen)) {
        zCheck_Negative_Exit(ftruncate(zpGlobRepoIf[zRepoId].LogFd, zStatIf.st_size));
        zPrint_Err(0, NULL, "日志写入失败： <.git_shadow/log/deploy/sig> !");
        exit(1);
    }
}

/*
 * 5：布署
 * 6：撤销
 */
_i
zdeploy(struct zMetaInfo *zpMetaIf, _i zSd) {
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    struct stat zStatIf;
    _i zFd;

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char zIpv4AddrStr[INET_ADDRSTRLEN] = "\0";
    char *zpFilePath;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId].CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].SortedCommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf = zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId].DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_trywrlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) )) {  // 加写锁
        return -11;
    };

    zCheck_Lock_State();  // 这个宏内部会释放写锁
    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁

    // 减 2 是为适应 json 二维结构，最后有一个 ']' 也会计入 VecSiz
    if (0 > zpMetaIf->FileId) {
        zpFilePath = "";
    } else if ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 2) < zpMetaIf->FileId) {
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "差异文件ID不存在!");\
        return -4;\
    } else {
        zpFilePath = zGet_NativeData(zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId), zpMetaIf->FileId);
    }

    zCheck_Negative_Exit( zFd = open(zpGlobRepoIf[zpMetaIf->RepoId].RepoPath, O_RDONLY) );
    zCheck_Negative_Exit( fstatat(zFd, zAllIpPath, &zStatIf, 0) );

    if (0 == zStatIf.st_size
            || (0 != (zStatIf.st_size % sizeof(_ui)))
            || (zStatIf.st_size / zSizeOf(_ui)) != zpGlobRepoIf[zpMetaIf->RepoId].TotalHost) {
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "集群 IP 地址数据库异常!");
        return -9;
    }

    /* 重置布署状态 */
    zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt = -1;
    for (_i i = 0; i < zpGlobRepoIf[zpMetaIf->RepoId].TotalHost; i++) {
        zpGlobRepoIf[zpMetaIf->RepoId].p_DpResList[i].DeployState = 0;
    }

    /* 若前端指定了HostId，则本次操作为单主机布署 */
    if (0 != zpMetaIf->HostId) {
        zconvert_ipv4_bin_to_str(zpMetaIf->HostId, zIpv4AddrStr);
    }

    /* 执行外部脚本使用 git 进行布署 */
    sprintf(zShellBuf, "./.git_shadow/scripts/zdeploy.sh -p %s -i %s -f %s -H %s -P %s",
            zpGlobRepoIf[zpMetaIf->RepoId].RepoPath,  // 指定代码库的绝对路径
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),  // 指定40位SHA1  commit sig
            zpFilePath,  // 指定目标文件相对于代码库的路径
            zIpv4AddrStr,  // 点分格式的ipv4地址
            zMajorIpTxtPath);  // Host 主节点 IP 列表相对于代码库的路径

    /* 调用 git 命令执行布署 */
    if (0 != system(zShellBuf)) {
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "Shell 布署命令执行错误!");
        return -12;
    }

    //等待所有主机的状态都得到确认，每隔 1 秒向前端发送已成功部署的数量统计，120秒超时
    _i zTimeCnter = 0;
    while (zpGlobRepoIf[zpMetaIf->RepoId].TotalHost != zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt) {
        sleep(1);
        //zsleep(0.2);
        zsendto(zSd, &(zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt), sizeof(zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt), 0, NULL);

        if (120 < zTimeCnter) {
            // 如果布署失败，代码库状态置为 "损坏" 状态
            zpGlobRepoIf[zpMetaIf->RepoId].RepoState = zRepoDamaged;
            pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );  // 释放写锁
            zPrint_Err(0, NULL, "布署超时(>120s)!");
            return -12;
        }

        zTimeCnter++;
    }
    // 布署成功，代码库状态复位
    zpGlobRepoIf[zpMetaIf->RepoId].RepoState = zRepoGood;

    /* 将本次布署信息写入日志 */
    zwrite_log(zpMetaIf->RepoId);

    /* 重置内存池状态 */
    pthread_mutex_lock(&(zpGlobRepoIf[zpMetaIf->RepoId].MemLock));
    zpGlobRepoIf[zpMetaIf->RepoId].MemPoolHeadId = 0;
    pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId].MemLock));

    /* 更新全局缓存 */
    zpMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
    zpMetaIf->RepoId = zpMetaIf->RepoId;
    zpMetaIf->CacheId = zpGlobRepoIf[zpMetaIf->RepoId].CacheId;
    zpMetaIf->DataType = zIsCommitDataType;
    zpMetaIf->CcurSwitch = zCcurOn;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);

    zpMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
    zpMetaIf->RepoId = zpMetaIf->RepoId;
    zpMetaIf->CacheId = zpGlobRepoIf[zpMetaIf->RepoId].CacheId;
    zpMetaIf->DataType = zIsDeployDataType;
    zpMetaIf->CcurSwitch = zCcurOn;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);  // 数据一致性问题？？？

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );

    return 0;
}

/*
 * 7：回复尚未确认成功的主机列表
 */
_i
zprint_failing_list(struct zMetaInfo *zpMetaIf, _i zSd) {
    _ui *zpFailingList = zpGlobRepoIf[zpMetaIf->RepoId].p_FailingList;
    /* 第一个元素写入实时时间戳 */
    zpFailingList[0] = time(NULL);

    if (zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt == zpGlobRepoIf[zpMetaIf->RepoId].TotalHost) {
        zsendto(zSd, zpFailingList, sizeof(zpFailingList[0]), 0, NULL);
    } else {
        _i zUnReplyCnt = 1;

        char *zpJsonStrBuf, *zpBasePtr;
        _i zDataLen = 16 * zpGlobRepoIf[zpMetaIf->RepoId].TotalHost;

        zMem_Alloc(zpMetaIf->p_data, char, zDataLen);
        zMem_Alloc(zpJsonStrBuf, char, 256 + zDataLen);
        zpBasePtr = zpMetaIf->p_data;

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        for (_i i = 0; i < zpGlobRepoIf[zpMetaIf->RepoId].TotalHost; i++) {
            if (0 == zpGlobRepoIf[zpMetaIf->RepoId].p_DpResList[i].DeployState) {
                zpFailingList[zUnReplyCnt] = zpGlobRepoIf[zpMetaIf->RepoId].p_DpResList[i].ClientAddr;

                sprintf(zpBasePtr, "%u\n", zpFailingList[zUnReplyCnt]);
                zpBasePtr += 1 + strlen(zpBasePtr);

                zUnReplyCnt++;
            }
        }

        zconvert_struct_to_json_str(zpJsonStrBuf, zpMetaIf);
        zsendto(zSd, zpJsonStrBuf, strlen(zpJsonStrBuf), 0, NULL);

        free(zpMetaIf->p_data);
        free(zpJsonStrBuf);
    }

    return 0;
}

/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
_i
zstate_confirm(struct zMetaInfo *zpMetaIf, _i _) {
    struct zDeployResInfo *zpTmp = zpGlobRepoIf[zpMetaIf->RepoId].p_DpResHash[zpMetaIf->HostId % zDeployHashSiz];

    for (; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (zpTmp->ClientAddr == zpMetaIf->HostId) {
            zpTmp->DeployState = 1;

            // 需要原子性递增
            pthread_mutex_lock( &(zpGlobRepoIf[zpMetaIf->RepoId].MutexLock) );
            zpGlobRepoIf[zpMetaIf->RepoId].ReplyCnt++;
            pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].MutexLock) );

            return 0;
        }
    }

    return 0;
}

/*
 * 内部函数，无需直接调用
 * 更新ipv4 地址缓存
 */
void
zupdate_ipv4_db_hash(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    struct zDeployResInfo *zpTmpIf;

    _i zFd[2] = {-100};
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
 * 除初始化时独立执行一次外，此函数仅会被 zupdate_ipv4_db_glob 函数调用
 */
void
zupdate_ipv4_db(void *zpIf) {
// TEST:PASS
    _i zRepoId = *((_i *)zpIf);
    FILE *zpFileHandler = NULL;
    char zBuf[zCommonBufSiz];
    _ui zIpv4Addr = 0;
    _i zFd[3] = {0};

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));

    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpTxtPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zAllIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = fdopen(zFd[1], "r"));
    zPCREInitInfo *zpPCREInitIf = zpcre_init("^(\\d{1,3}\\.){3}\\d{1,3}$");
    zPCRERetInfo *zpPCREResIf;
    for (_i i = 1; NULL != zget_one_line(zBuf, zCommonBufSiz, zpFileHandler); i++) {
        zpPCREResIf = zpcre_match(zpPCREInitIf, zBuf, 0);
        if (0 == zpPCREResIf->cnt) {
            zpcre_free_tmpsource(zpPCREResIf);
            zPrint_Time();
            fprintf(stderr, "\033[31;01m[%s]-[Line %d]: Invalid entry!\033[00m\n", zAllIpPath, i);
            exit(1);
        }
        zpcre_free_tmpsource(zpPCREResIf);

        zIpv4Addr = zconvert_ipv4_str_to_bin(zBuf);
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
}

/*
 * 4：仅更新集群中负责与中控机直接通信的主机的 ip 列表
 * 5：更新集群中所有主机的 ip 列表
 */
_i
zupdate_ipv4_db_glob(struct zMetaInfo *zpMetaIf, _i zSd) {
    char zRecvBuf[zCommonBufSiz], zPathBuf[128], *zpWritePath;
    struct zObjInfo *zpObjIf;
    _i zFd, zRecvSiz;

    zpWritePath = (4 == zpMetaIf->OpsId) ? zMajorIpTxtPath : zAllIpTxtPath;

    strcpy(zPathBuf, zpGlobRepoIf[zpMetaIf->RepoId].RepoPath);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWritePath);

    if (EBUSY == pthread_rwlock_trywrlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) )) {  // 加写锁
        return -11;
    };

    zCheck_Lock_State();

    zCheck_Negative_Exit( zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600) );

    zMem_Alloc(zpObjIf, char, 1 + strlen(zPathBuf) + sizeof(struct zObjInfo));
    zupdate_ipv4_db(&(zpMetaIf->RepoId));

    /* 接收网络数据并同步写入文件 */
    while (0 < (zRecvSiz = recv(zSd, zRecvBuf, zCommonBufSiz, 0))) {
        if (zRecvSiz != write(zFd, zRecvBuf, zRecvSiz)) {
            zPrint_Err(errno, NULL, "Write ipv4 list to txt file failed!");
            exit(1);
        }
    }
    zCheck_Negative_Exit(zRecvSiz);

    close(zFd);

    /* 将新接收的文件的 MD5 checksum 发送给前端校验 */
    zsendto(zSd, zgenerate_file_sig_md5(zPathBuf), zBytes(36), 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId].RwLock) );
    return 0;
}

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
_i
zlock_repo(struct zMetaInfo *zpMetaIf, _i _) {
    pthread_rwlock_wrlock(&(zpGlobRepoIf[zpMetaIf->RepoId].RwLock));

    if (2 == zpMetaIf->OpsId) {
        zpGlobRepoIf[zpMetaIf->RepoId].DpLock = zDeployLocked;
    } else {
        zpGlobRepoIf[zpMetaIf->RepoId].DpLock = zDeployUnLock;
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId].RwLock));
    return 0;
}

/*
 * 网络服务路由函数
 */
void
zops_route(void *zpSd) {
#define zSizMark 256
    _i zSd = *((_i *)zpSd);
    _i zBufSiz = zSizMark;
    _i zRecvdLen;
    _i zErrNo;
    char zJsonBuf[zBufSiz];
    char *zpJsonBuf = zJsonBuf;

    struct zMetaInfo zMetaIf;
    cJSON *zpJsonRootObj;

    /* 用于接收IP地址列表的场景 */
    if (zBufSiz == (zRecvdLen = zrecv_nohang(zSd, zpJsonBuf, zBufSiz, 0, NULL))) {
        _i zRecvSiz, zOffSet;
        zRecvSiz = zOffSet = zBufSiz;
        zBufSiz = 8192;
        zMem_Alloc(zpJsonBuf, char, zBufSiz);
        strcpy(zpJsonBuf, zJsonBuf);

        while(0 < (zRecvdLen = recv(zSd, zpJsonBuf + zOffSet, zBufSiz - zRecvSiz, 0))) {
            zOffSet += zRecvdLen;
            zRecvSiz -= zRecvdLen;
            if (zOffSet == zBufSiz) {
                zRecvSiz += zBufSiz;
                zBufSiz *= 2;
                zMem_Re_Alloc(zpJsonBuf, char ,zBufSiz, zpJsonBuf);
            }
        }

        zRecvdLen = zOffSet;
        zMem_Re_Alloc(zpJsonBuf, char, zRecvdLen, zpJsonBuf);
    }

    if (zBytes(6) > zRecvdLen) { return; }

fprintf(stderr, "DEBUG[zops_route]: %s\n", zpJsonBuf);
    if (NULL == (zpJsonRootObj = zconvert_json_str_to_struct(zpJsonBuf, &zMetaIf))) {
        // 此时因为解析失败，zMetaIf处于未初始化状态，需要手动赋值
        memset(&zMetaIf, 0, sizeof(zMetaIf));
        zMetaIf.OpsId = -7;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, zpJsonBuf, strlen(zpJsonBuf), 0, NULL);
        shutdown(zSd, SHUT_RDWR);
        zPrint_Err(0, NULL, "接收到的数据无法解析!");
        goto zMark;
    }

    if (0 > zMetaIf.OpsId || zServHashSiz <= zMetaIf.OpsId) {
        zMetaIf.OpsId = -1;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, zpJsonBuf, zRecvdLen, 0, NULL);
        zPrint_Err(0, NULL, "接收到的指令ID不存在!");
        goto zMark;
    }

    if (0 > (zErrNo = zNetServ[zMetaIf.OpsId](&zMetaIf, zSd))) {
        zMetaIf.OpsId = zErrNo;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, zpJsonBuf, zRecvdLen, 0, NULL);
    }

zMark:
    if (3 == zMetaIf.OpsId || 4 == zMetaIf.OpsId) {
        zjson_obj_free(zpJsonRootObj);
    }

    if (zSizMark < zBufSiz) {
        free(zpJsonBuf);
    }

    shutdown(zSd, SHUT_RDWR);
#undef zSizMark
}

/************
 * 网络服务 *
 ************/
/* 执行结果状态码对应表
 * -1：操作指令不存在（未知／未定义）
 * -2：项目ID不存在
 * -3：代码版本ID不存在
 * -4：差异文件ID不存在
 * -5：指定的主机 IP 不存在
 * -6：项目布署／撤销／更新ip数据库的权限被锁定
 * -7：后端接收到的数据无法解析，要求前端重发
 * -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 * -9：集群 ip 地址数据库不存在或数据异常，需要更新
 * -10：前端请求的数据类型错误
 * -11：正在布署／撤销过程中（请稍后重试？）
 * -12：布署失败（超时？未全部返回成功状态）
 * -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确（此时前端需要再收取一次数据）
 */
void
zstart_server(void *zpIf) {
// TEST:PASS
#define zMaxEvents 64
    // 顺序不可变
    zNetServ[0] = zzero;  // 直接返回0，空函数，用于避免0下标冲突
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = zupdate_ipv4_db_glob;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[5] = zupdate_ipv4_db_glob;  // 更新集群中所有主机的 ip 列表
    zNetServ[6] = zprint_record;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zNetServ[7] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[8] = zstate_confirm;  // 布署成功状态人工确认
    zNetServ[9] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[10] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[11] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[12] = zdeploy;  // 布署(如果 zMetaInfo 中 IP 地址数据段不为0，则表示仅布署到指定的单台主机，更多的适用于测试场景，仅需一台机器的情形)
    zNetServ[13] = zdeploy;  // 撤销(如果 zMetaInfo 中 IP 地址数据段不为0，则表示仅布署到指定的单台主机)

    /* 如下部分配置 epoll 环境 */
    struct zNetServInfo *zpNetServIf = (struct zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Exit( epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv) );

    for (;;) {
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return(zEvNum,);

        for (_i i = 0; i < zEvNum; i++) {
           if (zEvents[i].data.fd == zMajorSd) {
                zCheck_Negative_Exit( zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0) );
                zEv.events = EPOLLIN | EPOLLET;  /* 边缘触发 */
                zEv.data.fd = zConnSd;
                zCheck_Negative_Exit( epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv) );
            } else {
                zAdd_To_Thread_Pool(zops_route, &zEvents[i].data.fd);
            }
        }
    }
#undef zMaxEvents
}
