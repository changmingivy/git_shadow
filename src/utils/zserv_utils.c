#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
/*
 * 专用于缓存的内存调度分配函数，适用多线程环境，不需要free
 * 调用此函数的父函数一定是已经取得全局写锁的，故此处不能再试图加写锁
 */
void *
zalloc_cache(_i zRepoId, size_t zSiz) {
// TEST:PASS
    pthread_mutex_lock(&(zppGlobRepoIf[zRepoId]->MemLock));

    if ((zSiz + zppGlobRepoIf[zRepoId]->MemPoolOffSet) > zMemPoolSiz) {
        void **zppPrev, *zpCur;
        /* 新增一块内存区域加入内存池，以上一块内存的头部预留指针位存储新内存的地址 */
        if (MAP_FAILED == (zpCur = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
            zPrint_Err(0, NULL, "mmap failed!");
            exit(1);
        }
        zppPrev = zpCur;
        zppPrev[0] = zppGlobRepoIf[zRepoId]->p_MemPool;  // 首部指针位指向上一块内存池map区
        zppGlobRepoIf[zRepoId]->p_MemPool = zpCur;  // 更新当前内存池指针
        zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);  // 初始化新内存池区域的 offset
    }

    void *zpX = zppGlobRepoIf[zRepoId]->p_MemPool + zppGlobRepoIf[zRepoId]->MemPoolOffSet;
    zppGlobRepoIf[zRepoId]->MemPoolOffSet += zSiz;

    pthread_mutex_unlock(&(zppGlobRepoIf[zRepoId]->MemLock));
    return zpX;
}

/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zppGlobRepoIf[zRepoId]->MemLock));\
    \
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        munmap(zppGlobRepoIf[zRepoId]->p_MemPool, zMemPoolSiz);\
        zppGlobRepoIf[zRepoId]->p_MemPool = zppPrev;\
    }\
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);\
    \
    pthread_mutex_unlock(&(zppGlobRepoIf[zRepoId]->MemLock));\
} while(0)

/**************
 * NATIVE OPS *
 **************/
/* 在任务分发之前执行：定义必要的计数器、锁、条件变量等 */
#define zCcur_Init(zRepoId, zSuffix) \
    _i *zpFinMark##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpSelfCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpThreadCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    *zpFinMark##zSuffix = 0;\
    *zpSelfCnter##zSuffix = 0;\
    *zpThreadCnter##zSuffix = 0;\
\
    pthread_cond_t *zpCondVar##zSuffix = zalloc_cache(zRepoId, sizeof(pthread_cond_t));\
    pthread_mutex_t *zpMutexLock##zSuffix = zalloc_cache(zRepoId, 2 * sizeof(pthread_mutex_t));\
    pthread_cond_init(zpCondVar##zSuffix, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix + 1, NULL);\
\
    pthread_mutex_lock(zpMutexLock##zSuffix);

/* 配置将要传递给工作线程的参数(结构体) */
#define zCcur_Sub_Config(zpSubIf, zSuffix) \
    zpSubIf->p_FinMark = zpFinMark##zSuffix;\
    zpSubIf->p_SelfCnter = zpSelfCnter##zSuffix;\
    zpSubIf->p_ThreadCnter = zpThreadCnter##zSuffix;\
    zpSubIf->p_CondVar = zpCondVar##zSuffix;\
    zpSubIf->p_MutexLock[0] = zpMutexLock##zSuffix;\
    zpSubIf->p_MutexLock[1] = zpMutexLock##zSuffix + 1;\

/* 放置于调用者每次分发任务之前(即调用工作线程之前)，其中zStopExpression指最后一次循环的判断条件，如：A > B && C < D */
#define zCcur_Fin_Mark(zStopExpression, zSuffix) do {\
        (*zpSelfCnter##zSuffix)++;\
        if (zStopExpression) {\
            *zpFinMark##zSuffix = 1;\
        }\
    } while(0)

/*
 * 当调用者任务分发完成之后执行，之后释放资源占用
 * 不能使用while，而要使用 do...while，至少让调用者有一次收信号的机会
 * 否则可能导致在下层通知未执行之前条件变量被销毁，从而带来不确定的后果
 */
#define zCcur_Wait(zSuffix) do {\
        do {\
            pthread_cond_wait(zpCondVar##zSuffix, zpMutexLock##zSuffix);\
        } while (*zpSelfCnter##zSuffix != *zpThreadCnter##zSuffix);\
        pthread_mutex_unlock(zpMutexLock##zSuffix);\
        pthread_cond_destroy(zpCondVar##zSuffix);\
        pthread_mutex_destroy(zpMutexLock##zSuffix + 1);\
        pthread_mutex_destroy(zpMutexLock##zSuffix);\
    } while(0)

/* 放置于工作线程的回调函数末尾 */
#define zCcur_Fin_Signal(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[1]);\
        (*zpIf->p_ThreadCnter)++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[1]);\
        if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_SelfCnter) == *(zpIf->p_ThreadCnter))) {\
            pthread_mutex_lock(zpIf->p_MutexLock[0]);\
            pthread_mutex_unlock(zpIf->p_MutexLock[0]);\
            pthread_cond_signal(zpIf->p_CondVar);\
        }\
    } while(0)

/* 用于提取深层对象 */
#define zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf)
#define zGet_OneFileVecWrapIf(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_SubVecWrapIf)

#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_data)

/*
 *  定时(10s)同步远程代码
 */
void
zauto_pull(void *_) {
    _i zCnter;
    while (1) {
        for(zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
            if (NULL == zppGlobRepoIf[zCnter] || NULL == zppGlobRepoIf[zCnter]->p_PullCmd) {
                continue;
            }

            zAdd_To_Thread_Pool(zthread_system, zppGlobRepoIf[zCnter]->p_PullCmd);
        }
        sleep(8);
    }
}

/*
 * 功能：生成单个文件的差异内容缓存
 */
void
zget_diff_content(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf = (struct zMetaInfo *)zpIf;
    struct zVecWrapInfo *zpTopVecWrapIf;
    struct zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zShellBuf[128], zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s CURRENT -- %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zpMetaIf->p_data);

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    for (zCnter = 0; 0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zBaseDataInfo) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0]; }
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[0] = zpTmpBaseDataIf[0]->p_next;
    }
    pclose(zpShellRetHandler);

    if (0 == zCnter) {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = NULL;
    } else {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = zCnter;
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_RefDataIf = NULL;
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_base = zpTmpBaseDataIf[2]->p_data;
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_len = zpTmpBaseDataIf[2]->DataLen;
        }
    }

    /* >>>>任务完成，尝试通知上层调用者 */
    zCcur_Fin_Signal(zpMetaIf);
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表与每个文件的差异内容
 */
void
zget_file_list_and_diff_content(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf, *zpSubMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf;
    struct zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zShellBuf[128], zJsonBuf[zBytes(256)], zRes[zBytes(1024)];

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        exit(1);
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff --name-only %s CURRENT",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zCnter = 0; NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler); zCnter++) {
        zBaseDataLen = strlen(zRes);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zBaseDataInfo) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0]; }
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[0] = zpTmpBaseDataIf[0]->p_next;
    }
    pclose(zpShellRetHandler);

    if (0 == zCnter) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 1;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = NULL;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec));

        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
        zpSubMetaIf->OpsId = 0;
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->CommitId = zpMetaIf->CommitId;
        zpSubMetaIf->FileId = -1;  // 置为 -1，不允许再查询下一级内容
        zpSubMetaIf->HostId = 0;
        zpSubMetaIf->CacheId = zpMetaIf->CacheId;
        zpSubMetaIf->DataType = zpMetaIf->DataType;
        zpSubMetaIf->p_TimeStamp = NULL;
        zpSubMetaIf->p_data = "\033[31;01m==> 此为最新的已布署版本 <==\033[00m";
    
        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);
    
        zVecDataLen = strlen(zJsonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base, zJsonBuf, zVecDataLen);
    } else {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zCnter;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct zRefDataInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));

        /* >>>>初始化线程同步环境 */
        zCcur_Init(zpMetaIf->RepoId, A);

        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;

            zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
            /* >>>>填充必要的线程间同步数据 */
            zCcur_Sub_Config(zpSubMetaIf, A);
            /* 用于转换成JsonStr以及传向下一级函数 */
            zpSubMetaIf->OpsId = 0;
            zpSubMetaIf->RepoId = zpMetaIf->RepoId;
            zpSubMetaIf->CommitId = zpMetaIf->CommitId;
            zpSubMetaIf->FileId = i;
            zpSubMetaIf->HostId = 0;
            zpSubMetaIf->CacheId = zpMetaIf->CacheId;
            zpSubMetaIf->DataType = zpMetaIf->DataType;
            zpSubMetaIf->p_TimeStamp = NULL;
            zpSubMetaIf->p_data = zpTmpBaseDataIf[2]->p_data;
    
            /* 将zMetaInfo转换为JSON文本 */
            zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);
    
            zVecDataLen = strlen(zJsonBuf);
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);
    
            /* >>>>检测是否是最后一次循环 */
            zCcur_Fin_Mark(i == zCnter - 1, A);

            /* 进入下一层获取对应的差异内容 */
            zAdd_To_Thread_Pool(zget_diff_content, zpSubMetaIf);
        }

        /* >>>>等待分发出去的所有任务全部完成 */
        zCcur_Wait(A);

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';
    }

    /* >>>>任务完成，尝试通知上层调用者 */
    zCcur_Fin_Signal(zpMetaIf);
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zgenerate_cache(void *zpIf) {
// TEST:PASS
    struct zMetaInfo *zpMetaIf, *zpSubMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    struct zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[128], zJsonBuf[zBytes(256)];

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        sprintf(zShellBuf, "cd %s && git log server --format=\"%%H_%%ct\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath); // 取 server 分支的提交记录
        zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDeployVecWrapIf);
        sprintf(zShellBuf, "cat %s%s", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zLogPath);
        zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }
    
    for (zCnter = 0; (zCnter < zCacheSiz) && (NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler)); zCnter++) {
        zBaseDataLen = strlen(zRes);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zBaseDataInfo) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0]; }
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[0] = zpTmpBaseDataIf[0]->p_next;
    }
    pclose(zpShellRetHandler);

    /* 存储的是实际的对象数量 */
    zpSortedTopVecWrapIf->VecSiz = zpTopVecWrapIf->VecSiz = zCnter;

    if (0 != zCnter) {
        /* >>>>初始化线程同步环境 */
        zCcur_Init(zpMetaIf->RepoId, A);

        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zpTmpBaseDataIf[2]->p_data[40] = '\0';
            zpTopVecWrapIf->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;

            zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
            /* >>>>填充必要的线程间同步数据 */
            zCcur_Sub_Config(zpSubMetaIf, A);
            /* 用于转换成JsonStr以及传向下一级函数 */
            zpSubMetaIf->OpsId = 0;
            zpSubMetaIf->RepoId = zpMetaIf->RepoId;
            zpSubMetaIf->CommitId = i;
            zpSubMetaIf->FileId = -1;
            zpSubMetaIf->HostId = 0;
            zpSubMetaIf->CacheId = zpMetaIf->CacheId;
            zpSubMetaIf->DataType = zpMetaIf->DataType;
            zpSubMetaIf->p_TimeStamp = &(zpTmpBaseDataIf[2]->p_data[41]);
            zpSubMetaIf->p_data = zpTmpBaseDataIf[2]->p_data;
    
            /* 将zMetaInfo转换为JSON文本 */
            zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);
    
            zVecDataLen = strlen(zJsonBuf);
            zpTopVecWrapIf->p_VecIf[i].iov_len = zVecDataLen;
            zpTopVecWrapIf->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zpTopVecWrapIf->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);
    
            /* >>>>检测是否是最后一次循环 */
            zCcur_Fin_Mark(i == zCnter - 1, A);

            /* 进入下一层获取对应的差异文件列表 */
            zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubMetaIf);
        }

        /* >>>>等待分发出去的所有任务全部完成 */
        zCcur_Wait(A);

        if (zIsDeployDataType == zpMetaIf->DataType) {
            /* 将布署记录按逆向时间排序（新记录显示在前面） */
            for (_i i = 0; i < zpTopVecWrapIf->VecSiz; i++) {
                zCnter--;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_base = zpTopVecWrapIf->p_VecIf[i].iov_base;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_len = zpTopVecWrapIf->p_VecIf[i].iov_len;
            }
        } else {
            /* 新生成的提交记录缓存本来就是有序的，不需要额外排序 */
            for (_i i = 0; i < zpTopVecWrapIf->VecSiz; i++) {
                zpSortedTopVecWrapIf->p_VecIf[i].iov_base = zpTopVecWrapIf->p_VecIf[i].iov_base;
                zpSortedTopVecWrapIf->p_VecIf[i].iov_len = zpTopVecWrapIf->p_VecIf[i].iov_len;
            }
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
    }

    /* 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置 */
    zppGlobRepoIf[zpMetaIf->RepoId]->CommitCacheQueueHeadId = zCacheSiz - 1;

    /* 防止意外访问导致的程序崩溃 */
    memset(zpTopVecWrapIf->p_RefDataIf + zpTopVecWrapIf->VecSiz, 0, sizeof(struct zRefDataInfo) * (zCacheSiz - zpTopVecWrapIf->VecSiz));

    /* >>>>任务完成，尝试通知上层调用者 */
    zCcur_Fin_Signal(zpMetaIf);
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
    zpTopVecWrapIf = &(zppGlobRepoIf[zpObjIf->RepoId]->CommitVecWrapIf);
    zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpObjIf->RepoId]->SortedCommitVecWrapIf);

    pthread_rwlock_wrlock( &(zppGlobRepoIf[zpObjIf->RepoId]->RwLock) );

    zpHeadId = &(zppGlobRepoIf[zpObjIf->RepoId]->CommitCacheQueueHeadId);

    // 必须在shell命令中切换到正确的工作路径，取 server 分支的提交记录
    sprintf(zShellBuf, "cd %s && git log server -1 --format=\"%%H_%%ct\"", zppGlobRepoIf[zpObjIf->RepoId]->p_RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler);
    pclose(zpShellRetHandler);

    zRes[strlen(zRes) - 1] = '\0';  // 去掉换行符
    zRes[40] = '\0';

    /* 防止冗余事件导致的重复更新 */
    if (0 == strcmp(zRes,
                zppGlobRepoIf[zpObjIf->RepoId]->CommitVecWrapIf.p_RefDataIf[(*zpHeadId == (zCacheSiz - 1)) ? 0 : (1 + *zpHeadId)].p_data)) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpObjIf->RepoId]->RwLock) );
        return;
    }

    zCheck_Null_Exit( zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data = zalloc_cache(zpObjIf->RepoId, zBytes(41)) );
    strcpy(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, zRes);

    /* >>>>初始化线程同步环境 */
    zCcur_Init(zpObjIf->RepoId, A);
    /* >>>>填充必要的线程间同步数据 */
    zpSubMetaIf = zalloc_cache(zpObjIf->RepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf, A);
    /* 转换成JsonStr以及传向下一级函数 */
    zpSubMetaIf->OpsId = 0;
    zpSubMetaIf->RepoId = zpObjIf->RepoId;
    zpSubMetaIf->CommitId = *zpHeadId;  // 逆向循环索引号更新
    zpSubMetaIf->FileId = -1;
    zpSubMetaIf->HostId = -1;
    zpSubMetaIf->CacheId = zppGlobRepoIf[zpObjIf->RepoId]->CacheId;
    zpSubMetaIf->DataType = zIsCommitDataType;
    zpSubMetaIf->p_TimeStamp = &(zRes[41]);
    zpSubMetaIf->p_data = zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data;
    /* >>>>通知工作线程是最后一次循环 */
    zCcur_Fin_Mark(1 == 1, A);
    /* 生成下一级缓存 */
    zAdd_To_Thread_Pool( zget_file_list_and_diff_content, zpSubMetaIf );
    /* >>>>等待分发出去的所有任务全部完成 */
    zCcur_Wait(A);

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

    /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
    ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpObjIf->RepoId]->RwLock) );
}

// /*
//  * 通用函数，调用外部程序或脚本文件执行相应的动作
//  * 传入参数：
//  * $1：代码库ID
//  * $2：代码库绝对路径
//  * $3：受监控路径名称
//  */
// void
// zinotify_common_callback(void *zpIf) {
//     struct zObjInfo *zpObjIf = (struct zObjInfo *) zpIf;
//     char zShellBuf[zCommonBufSiz];
// 
//     sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify.sh %d %s %s",
//         zppGlobRepoIf[zpObjIf->RepoId]->p_RepoPath,
//         zpObjIf->RepoId,
//         zppGlobRepoIf[zpObjIf->RepoId]->p_RepoPath,
//         zpObjHash[zpObjIf->UpperWid]->path);
// 
//     if (0 != system(zShellBuf)) {
//         zPrint_Err(0, NULL, "[system]: shell command failed!");
//     }
// }

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
// TEST:PASS
    char zShellBuf[128], zRes[zCommonBufSiz];
    FILE *zpFile;
    _i zLen;

    // 将 CURRENT 标签的40位sig字符串及10位时间戳追加写入.git_shadow/log/meta
    sprintf(zShellBuf, "cd %s && git log CURRENT -1 --format=\"%%H_%%ct\"", zppGlobRepoIf[zRepoId]->p_RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zget_one_line(zRes, zCommonBufSiz, zpFile);
    zLen = strlen(zRes);  // 写入文件时，不能写入最后的 '\0'

    if (zLen != write(zppGlobRepoIf[zRepoId]->LogFd, zRes, zLen)) {
        zPrint_Err(0, NULL, "日志写入失败： <.git_shadow/log/deploy/meta> !");
        exit(1);
    }
}

/*
 * 内部函数，无需直接调用
 * 更新ipv4 地址缓存
 */
_i
zupdate_ipv4_db_hash(_i zRepoId) {
// TEST:PASS
//    struct stat zStatIf;
//    struct zDeployResInfo *zpTmpIf;
//    char zPathBuf[zCommonBufSiz];
//    _i zFd;
//
//    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zAllIpPath);
//    zCheck_Negative_Exit( zFd = open(zPathBuf, O_RDONLY) );  // 打开客户端ip地址数据库文件
//    zCheck_Negative_Exit( fstat(zFd, &zStatIf) );
//
//    zppGlobRepoIf[zRepoId]->TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
//    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_DpResList, struct zDeployResInfo, zppGlobRepoIf[zRepoId]->TotalHost);  // 分配数组空间，用于顺序读取
//
//    for (_i zCnter = 0; zCnter < zppGlobRepoIf[zRepoId]->TotalHost; zCnter++) {
//        zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].RepoId = zRepoId;  // 写入代码库索引值
//        zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
//        zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].p_next = NULL;
//
//        errno = 0;
//        if (zSizeOf(_ui) != read(zFd, &(zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
//            zPrint_Err(errno, NULL, "read client info failed!");
//            return -1;
//        }
//
//        zpTmpIf = zppGlobRepoIf[zRepoId]->p_DpResHash[(zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].ClientAddr) % zDeployHashSiz];  // HASH 定位
//        if (NULL == zpTmpIf) {
//            zppGlobRepoIf[zRepoId]->p_DpResHash[(zppGlobRepoIf[zRepoId]->p_DpResList[zCnter].ClientAddr) % zDeployHashSiz] = &(zppGlobRepoIf[zRepoId]->p_DpResList[zCnter]);  // 若顶层为空，直接指向数组中对应的位置
//        } else {
//            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
//                zpTmpIf = zpTmpIf->p_next;
//            }
//
//            zpTmpIf->p_next = &(zppGlobRepoIf[zRepoId]->p_DpResList[zCnter]);
//        }
//    }
//
//    close(zFd);
    return 0;
}

/*
 * 除初始化时独立执行一次外，此函数仅会被 zupdate_ipv4_db_glob 函数调用
 */
_i
zupdate_ipv4_db(_i zRepoId) {
// TEST:PASS
//    char zPathBuf[zCommonBufSiz];
//    char zResBuf[zBytes(8192)] = {'\0'};
//    FILE *zpFileHandler;
//    _ui zIpv4Addr;
//    _i zFd;
//
//    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zAllIpTxtPath);
//    zCheck_Null_Exit(zpFileHandler = fopen(zPathBuf, "r"));
//    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zAllIpPath);
//    zCheck_Negative_Exit(zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600));
//
//    zget_one_line(zResBuf, zBytes(8192), zpFileHandler);
//    fclose(zpFileHandler);
//
//    zPCREInitInfo *zpPCREInitIf = zpcre_init("(\\d{1,3}\\.){3}\\d{1,3}");
//    zPCRERetInfo *zpPCREResIf = zpcre_match(zpPCREInitIf, zResBuf, 1);
//    for (_i i = 0; i < zpPCREResIf->cnt; i++) {
//        zIpv4Addr = zconvert_ipv4_str_to_bin(zpPCREResIf->p_rets[i]);
//
//        if (sizeof(_ui) != write(zFd, &zIpv4Addr, sizeof(_ui))) {
//            zPrint_Err(0, NULL, "Write to $zAllIpPath failed!");
//            return -1;
//        }
//    }
//
//    close(zFd);
//    zpcre_free_tmpsource(zpPCREResIf);
//    zpcre_free_metasource(zpPCREInitIf);
//
//    // ipv4 数据文件更新后，立即更新对应的缓存中的列表与HASH
//    if (-1 == zupdate_ipv4_db_hash(zRepoId)) { return -1; }
//
    return 0;
}

/************
 * INIT OPS *
 ************/
/*
 * 参数：
 *   新建项目基本信息五个字段
 *   初次启动标记(zInitMark: 1 表示为初始化时调用，0 表示动态更新时调用)
 * 返回值:
        -33;  // 请求创建的新项目路径无权访问
        -34;  // 请求创建的新项目信息格式错误（合法字段数量不是5个）
        -35;  // 请求创建的项目ID已存在或不合法（创建项目代码库时出错）
        -36;  // 请求创建的项目路径已存在
        -37;  // 请求创建项目时指定的源版本控制系统错误（非git或svn）
        -38;  // 拉取(git clone)远程代码失败
        -39;  // 项目ID写入失败
 */
#define zFree_Source() do {\
    close(zFd);\
    close(zppGlobRepoIf[zRepoId]->LogFd);\
    free(zppGlobRepoIf[zRepoId]->p_RepoPath);\
    free(zppGlobRepoIf[zRepoId]);\
    zMem_Re_Alloc(zppGlobRepoIf, struct zRepoInfo *, zGlobMaxRepoId + 1, zppGlobRepoIf);\
    zpcre_free_tmpsource(zpRetIf);\
    zpcre_free_metasource(zpInitIf);\
} while(0)

_i
zadd_one_repo_env(char *zpRepoStrIf) {
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;

    struct zMetaInfo *zpMetaIf[2];
    struct zObjInfo *zpObjIf;
    struct stat zStatIf;

    _i zRepoId,zFd;

    /* 正则匹配项目基本信息（5个字段） */
    zpInitIf = zpcre_init("(\\w|[[:punct:]])+");
    zpRetIf = zpcre_match(zpInitIf, zpRepoStrIf, 1);
    if (5 != zpRetIf->cnt) {
        zPrint_Time();
        fprintf(stderr, "\033[31m\"%s\": 新项目信息错误!\033[00m\n", zpRepoStrIf);
        return -34;
    }

    /* 提取项目ID */
    zRepoId = strtol(zpRetIf->p_rets[0], NULL, 10);
    if (zRepoId > zGlobMaxRepoId) {
        zMem_Re_Alloc(zppGlobRepoIf, struct zRepoInfo *, zRepoId + 1, zppGlobRepoIf);
        for (_i i = zGlobMaxRepoId + 1; i < zRepoId; i++) {
            zppGlobRepoIf[i] = NULL;
        }
    } else {
        if (NULL != zppGlobRepoIf[zRepoId]) {
            zpcre_free_tmpsource(zpRetIf);
            zpcre_free_metasource(zpInitIf);
            return -35;
        }
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zppGlobRepoIf[zRepoId], struct zRepoInfo, 1);
    zppGlobRepoIf[zRepoId]->RepoId = zRepoId;

    /* 提取项目绝对路径 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, 1 + strlen("/home/git/") + strlen(zpRetIf->p_rets[1]));
    sprintf(zppGlobRepoIf[zRepoId]->p_RepoPath, "%s%s", "/home/git/", zpRetIf->p_rets[1]);

    /* 调用SHELL执行检查和创建 */
    char *zpCmd = "/home/git/zgit_shadow/scripts/zmaster_init_repo.sh";
    char *zppArgv[] = {"", zpRetIf->p_rets[0], zpRetIf->p_rets[1], zpRetIf->p_rets[2], zpRetIf->p_rets[3], zpRetIf->p_rets[4], NULL};
    zfork_do_exec(zpCmd, zppArgv);

    /* 打开日志文件 */
    char zPathBuf[zCommonBufSiz];
    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zLogPath);
    zppGlobRepoIf[zRepoId]->LogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zRepoIdPath);
    zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0644);

    if (-1 == zFd || -1 == zppGlobRepoIf[zRepoId]->LogFd) {
        zFree_Source();
        zPrint_Err(0, NULL, "项目日志文件或repo_id路径不存在!");
        return -38;
    }

    /* 在每个代码库的<_SHADOW/info/repo_id>文件中写入所属代码库的ID */
    if (sizeof(zRepoId) != write(zFd, &zRepoId, sizeof(zRepoId))) {
        zFree_Source();
        zPrint_Err(0, NULL, "项目ID写入repo_id文件失败!");
        return -39;
    }
    close(zFd);

    /* 检测并生成项目代码定期更新命令 */
    char zPullCmdBuf[zCommonBufSiz];
    if (0 == strcmp("git", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/%s && git pull --force %s %s:server",
                zpRetIf->p_rets[1],
                zpRetIf->p_rets[2],
                zpRetIf->p_rets[3]);
    } else if (0 == strcmp("svn", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/%s/.sync_svn_to_git && svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server",
                zpRetIf->p_rets[1]);
    } else {
        zFree_Source();
        zPrint_Err(0, NULL, "无法识别的远程版本管理系统：不是git也不是svn!");
        return -37;
    }

    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zppGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 清理资源占用 */
    zpcre_free_tmpsource(zpRetIf);
    zpcre_free_metasource(zpInitIf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zppGlobRepoIf[zRepoId]->p_MemPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Err(0, NULL, "mmap failed!");
        exit(1);
    }
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zppGlobRepoIf[zRepoId]->MemLock), NULL) );

    /* 初始化日志下一次写入偏移量 */
    zppGlobRepoIf[zRepoId]->zDeployLogOffSet = zStatIf.st_size;

    /* inotify */
    zpObjIf = zalloc_cache(zRepoId, sizeof(struct zObjInfo) + 1 + strlen(zInotifyObjRelativePath) + strlen(zppGlobRepoIf[zRepoId]->p_RepoPath));
    zpObjIf->RepoId = zRepoId;
    zpObjIf->RecursiveMark = 0;  // 不必递归监控
    zpObjIf->CallBack = zupdate_one_commit_cache;
    zpObjIf->UpperWid = -1; /* 填充 -1，提示 zinotify_add_sub_watch 函数这是顶层监控对象 */
    sprintf(zpObjIf->p_path, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zInotifyObjRelativePath);
    zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);

    /* 为每个代码库生成一把读写锁，锁属性设置写者优先 */
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_init(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_setkind_np(&(zppGlobRepoIf[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) );
    zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zppGlobRepoIf[zRepoId]->RwLock), &(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_destroy(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&zppGlobRepoIf[zRepoId]->MutexLock, NULL) );

    /* 用于收集布署尚未成功的主机列表，第一个元素用于存放实时时间戳，因此要多分配一个元素的空间 */
    zppGlobRepoIf[zRepoId]->p_FailingList = zalloc_cache(zRepoId, 1 + zppGlobRepoIf[zRepoId]->TotalHost);

    /* 缓存版本初始化 */
    zppGlobRepoIf[zRepoId]->CacheId = 1000000000;

    /* 指针指向自身的静态数据项 */
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->CommitRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedCommitVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->DeployVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->DeployRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedDeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedDeployVecIf;

    /* 初始化任务分发环境 */
    zCcur_Init(zRepoId, A);  //___
    zCcur_Init(zRepoId, B);  //___

    /* 生成提交记录缓存 */
    zpMetaIf[0] = zalloc_cache(zRepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[0], A);  //___
    zpMetaIf[0]->RepoId = zRepoId;
    zpMetaIf[0]->CacheId = zppGlobRepoIf[zRepoId]->CacheId;
    zpMetaIf[0]->DataType = zIsCommitDataType;
    zCcur_Fin_Mark(1 == 1, A);  //___
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[0]);

    /* 生成布署记录缓存 */
    zpMetaIf[1] = zalloc_cache(zRepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[1], B);  //___
    zpMetaIf[1]->RepoId = zRepoId;
    zpMetaIf[1]->CacheId = zppGlobRepoIf[zRepoId]->CacheId;
    zpMetaIf[1]->DataType = zIsDeployDataType;
    zCcur_Fin_Mark(1 == 1, B);  //___
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[1]);

    /* 等待两批任务完成，之后释放相关资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    zGlobMaxRepoId = zRepoId;
    return 0;
}
#undef zFree_Source

/* 读取项目信息 */
void
zinit_env(const char *zpConfPath) {
    FILE *zpFile;
    char zRes[zCommonBufSiz];

    /* json 解析时的回调函数索引 */
    zJsonParseOps['O']  // OpsId
        = zJsonParseOps['P']  // ProjId
        = zJsonParseOps['R']  // RevId
        = zJsonParseOps['F']  // FileId
        = zJsonParseOps['H']  // HostId
        = zJsonParseOps['C']  // CacheId
        = zJsonParseOps['D']  // DataType
        = zParseDigit;
    zJsonParseOps['d']  // data
        = zParseStr;

    zCheck_Null_Exit( zpFile = fopen(zpConfPath, "r") );
    while (NULL != zget_one_line(zRes, zCommonBufSiz, zpFile)) {
        zadd_one_repo_env(zRes);
    }

    if (0 > zGlobMaxRepoId) {
        zPrint_Err(0, NULL, "未读取到有效代码库信息!");
    }
    fclose(zpFile);
}
