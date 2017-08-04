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

/* 重置内存池状态 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zppGlobRepoIf[zRepoId]->MemLock));\
    \
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;\
    do {\
        zppPrev = zppPrev[0];\
        munmap(zppGlobRepoIf[zRepoId]->p_MemPool, zMemPoolSiz);\
        zppGlobRepoIf[zRepoId]->p_MemPool = zppPrev;\
    } while(NULL != zppPrev);\
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);\
    \
    pthread_mutex_unlock(&(zppGlobRepoIf[zRepoId]->MemLock));\
} while(0)

/**************
 * NATIVE OPS *
 **************/
/* 在任务分发之前执行：定义必要的计数器、锁、条件变量等 */
#define zCcur_Init(zRepoId) \
    _i *zpFinMark = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpSelfCnter = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpThreadCnter = zalloc_cache(zRepoId, sizeof(_i));\
    *zpFinMark = 0;\
    *zpSelfCnter = 0;\
    *zpThreadCnter = 0;\
\
    pthread_cond_t *zpCondVar = zalloc_cache(zRepoId, sizeof(pthread_cond_t));\
    pthread_cond_init(zpCondVar, NULL);\
\
    pthread_mutex_t *zpMutexLock = zalloc_cache(zRepoId, 3 * sizeof(pthread_mutex_t));\
    pthread_mutex_init(zpMutexLock, NULL);\
    pthread_mutex_init(zpMutexLock + 1, NULL);\
    pthread_mutex_init(zpMutexLock + 2, NULL);\
\
    pthread_mutex_lock(zpMutexLock + 1);\
    pthread_mutex_lock(zpMutexLock + 2);

/* 配置将要传递给工作线程的参数(结构体) */
#define zCcur_Sub_Config(zpSubIf) \
    zpSubIf->p_FinMark = zpFinMark;\
    zpSubIf->p_SelfCnter = zpSelfCnter;\
    zpSubIf->p_ThreadCnter = zpThreadCnter;\
    zpSubIf->p_CondVar = zpCondVar;\
    zpSubIf->pp_MutexLock[0] = zpMutexLock;\
    zpSubIf->pp_MutexLock[1] = zpMutexLock + 1;\
    zpSubIf->pp_MutexLock[2] = zpMutexLock + 2;

/* 放置于调用者每次分发任务之前(即调用工作线程之前) */
#define zCcur_Fin_Mark(zLoopObj, zFinalObj, zLoopObj_1, zFinalObj_1) do {\
        (*zpSelfCnter)++;\
        if (zLoopObj == zFinalObj && zLoopObj_1 == zFinalObj_1 ) {\
            *zpFinMark = 1;\
        }\
    } while(0)

/* 当调用者任务分发完成之后执行 */
#define zCcur_Wait() do {\
        pthread_mutex_lock(zpMutexLock);\
        pthread_mutex_unlock(zpMutexLock + 1);\
        while (*zpSelfCnter != *zpThreadCnter) {\
            pthread_cond_wait(zpCondVar, zpMutexLock);\
        }\
        pthread_mutex_unlock(zpMutexLock + 2);\
        pthread_mutex_unlock(zpMutexLock);\
    } while(0)

/* 放置于工作线程的回调函数末尾 */
#define zCcur_Fin_Signal(zpIf) do {\
        pthread_mutex_lock(zpIf->pp_MutexLock[0]);\
        (*zpIf->p_ThreadCnter)++;\
        pthread_mutex_unlock(zpIf->pp_MutexLock[0]);\
        if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_SelfCnter) == *(zpIf->p_ThreadCnter))) {\
            pthread_mutex_lock(zpIf->pp_MutexLock[1]);\
            do {\
                pthread_cond_signal(zpIf->p_CondVar);\
            } while (EAGAIN == pthread_mutex_trylock(zpIf->pp_MutexLock[2]));\
            pthread_mutex_unlock(zpIf->pp_MutexLock[1]);\
            pthread_mutex_unlock(zpIf->pp_MutexLock[2]);\
        }\
    } while(0)

/* 用于提取深层对象 */
#define zGet_JsonStr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_VecIf[zSelfId].iov_base)
#define zGet_SubVecWrapIf(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_SubVecWrapIf)
#define zGet_NativeData(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_data)
#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) zGet_NativeData(zpTopVecWrapIf, zCommitId)

/*
 *  定时(10s)同步远程代码
 */
void
zauto_pull(void *_) {
    _i zCnter;
zMark:
    for(zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
        if (NULL == zppGlobRepoIf[zCnter]) { continue; }

        if (255 == system(zppGlobRepoIf[zCnter]->p_PullCmd)) {
            zPrint_Err(0, NULL, zppGlobRepoIf[zCnter]->p_PullCmd);
        }
    }
    sleep(30);
    goto zMark;
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
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    }

    zpUpperVecWrapIf = zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId);

    zpCurVecWrapIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
    /* 关联到上一级数据结构 */
    zpUpperVecWrapIf->p_RefDataIf[zpMetaIf->FileId].p_SubVecWrapIf = zpCurVecWrapIf;

    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s CURRENT -- %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
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
        struct iovec *zpIoVecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec) * zpCurVecWrapIf->VecSiz);
        memcpy(zpIoVecIf, zpCurVecWrapIf->p_VecIf, sizeof(struct iovec) * zpCurVecWrapIf->VecSiz);
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = zpIoVecIf;
        /* 因为没有下一级数据，所以置为NULL */
        zpCurVecWrapIf->p_RefDataIf = NULL;
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
    struct zVecWrapInfo *zpTopVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], *zpRes, zRes[zBytes(1024)];

    char zJsonBuf[zBytes(256)];
    _i zVecCnter;
    _i zVecDataLen, zDataLen;
    _i zAllocSiz = 64;

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    }

    zpCurVecWrapIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zVecWrapInfo));
    /* 关联到上一级数据结构 */
    zGet_SubVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zpCurVecWrapIf;

    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);
    zMem_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff --name-only %s CURRENT",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* >>>>初始化线程同步环境 */
    zCcur_Init(zpMetaIf->RepoId);
    zpRes = zget_one_line(zRes, zBytes(1024), zpShellRetHandler);
    for (zVecCnter = 0;  NULL != zpRes; zVecCnter++) {
        if (zVecCnter > (zAllocSiz - 2)) {  // For json ']'
            zAllocSiz *= 2;
            zMem_Re_Alloc( zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf );
            zMem_Re_Alloc( zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zAllocSiz, zpCurVecWrapIf->p_RefDataIf );
        }

        zDataLen = strlen(zRes);
        zRes[zDataLen - 1] = '\0';
        zCheck_Null_Exit( zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data = zalloc_cache(zpMetaIf->RepoId, zDataLen) );
        strcpy(zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data, zRes);  // 信息正文实际存放的位置

        /* >>>>填充必要的线程间同步数据 */
        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
        zCcur_Sub_Config(zpSubMetaIf);
        /* 用于转换成JsonStr以及传向下一级函数 */
        zpSubMetaIf->OpsId = 0;
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->CommitId = zpMetaIf->CommitId;
        zpSubMetaIf->FileId = zVecCnter;
        zpSubMetaIf->HostId = -1;
        zpSubMetaIf->CacheId = zpMetaIf->CacheId;
        zpSubMetaIf->DataType = zpMetaIf->DataType;
        zpSubMetaIf->p_TimeStamp = "";
        zpSubMetaIf->p_data = zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);

        zVecDataLen = strlen(zJsonBuf);
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base, zJsonBuf, zVecDataLen);
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_len = zVecDataLen;

        /* 必须在上一个 zRes 使用完之后才能执行 */
        zpRes = zget_one_line(zRes, zBytes(1024), zpShellRetHandler);
        /* >>>>检测是否是最后一次循环 */
        zCcur_Fin_Mark(NULL, zpRes, zCacheSiz - 1, zVecCnter);
        /* 进入下一层获取对应的差异内容 */
        zAdd_To_Thread_Pool(zget_diff_content, zpSubMetaIf);
    }
    /* >>>>等待分发出去的所有任务全部完成 */
    zCcur_Wait();

    pclose(zpShellRetHandler);

    if (0 == zVecCnter) {
        /* 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空 */
        free(zpCurVecWrapIf->p_VecIf);
        free(zpCurVecWrapIf->p_RefDataIf);
        return;
    } else {
        zpCurVecWrapIf->VecSiz = zVecCnter + 1;  // 最后有一个额外的成员存放 json ']'

        /* 将已确定数量的对象指针复制到本项目内存池中，之后释放临时资源 */
        struct iovec *zpIoVecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec) * zpCurVecWrapIf->VecSiz);  // 多留一项用于存放二维json最后的']'
        struct zRefDataInfo *zpRefDataIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zRefDataInfo) * zpCurVecWrapIf->VecSiz);
        memcpy(zpIoVecIf, zpCurVecWrapIf->p_VecIf, sizeof(struct iovec) * zpCurVecWrapIf->VecSiz);
        memcpy(zpRefDataIf, zpCurVecWrapIf->p_RefDataIf, sizeof(struct zRefDataInfo) * zpCurVecWrapIf->VecSiz);
        free(zpCurVecWrapIf->p_VecIf);
        free(zpCurVecWrapIf->p_RefDataIf);
        zpCurVecWrapIf->p_VecIf = zpIoVecIf;
        zpCurVecWrapIf->p_RefDataIf = zpRefDataIf;

        /* 修饰第一项，添加最后一项，形成二维json格式 */
        ((char *)(zpCurVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_base = "]";
        zpCurVecWrapIf->p_VecIf[zVecCnter].iov_len= zBytes(1);  // 不发送最后的 '\0'

        // 防止意外访问出错
        zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_data = NULL;
        zpCurVecWrapIf->p_RefDataIf[zVecCnter].p_SubVecWrapIf = NULL;
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
    struct zVecWrapInfo *zpTopVecWrapIf;

    char zJsonBuf[zBytes(256)];  // iov_base
    _i zVecDataLen, zVecCnter;

    FILE *zpShellRetHandler;
    char *zpRes, zRes[zCommonBufSiz], zShellBuf[128], zLogPathBuf[128];

    zpMetaIf = (struct zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        // 必须在shell命令中切换到正确的工作路径，取 server 分支的提交记录
        sprintf(zShellBuf, "cd %s && git log server --format=\"%%H_%%ct\"",
                zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
    } else {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);

        strcpy(zLogPathBuf, zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
        strcat(zLogPathBuf, "/");
        strcat(zLogPathBuf, zLogPath);
        sprintf(zShellBuf, "cat %s", zLogPathBuf);
    }
    
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* >>>>初始化线程同步环境 */
    zCcur_Init(zpMetaIf->RepoId);
    /* zCacheSiz - 1 :留一个空间给json需要 ']' */
    zpRes = zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler);
    for (zVecCnter = 0; (NULL != zpRes) && (zVecCnter < (zCacheSiz - 1)); zVecCnter++) {
        zRes[strlen(zRes) - 1] = '\0';
        zRes[40] = '\0';
        zCheck_Null_Exit( zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data = zalloc_cache(zpMetaIf->RepoId, zBytes(41)) );
        strcpy(zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data, zRes);

        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
        /* >>>>填充必要的线程间同步数据 */
        zCcur_Sub_Config(zpSubMetaIf);
        /* 转换成JsonStr以及传向下一级函数 */
        zpSubMetaIf->OpsId = 0;
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->CommitId = zVecCnter;
        zpSubMetaIf->FileId = -1;
        zpSubMetaIf->HostId = -1;
        zpSubMetaIf->CacheId = zpMetaIf->CacheId;
        zpSubMetaIf->DataType = zpMetaIf->DataType;
        zpSubMetaIf->p_TimeStamp = &(zRes[41]);
        zpSubMetaIf->p_data = zpTopVecWrapIf->p_RefDataIf[zVecCnter].p_data;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, zpSubMetaIf);

        /* 将JsonStr内容存放到iov_base中 */
        zVecDataLen = strlen(zJsonBuf);
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base, zJsonBuf, zVecDataLen);
        zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len = zVecDataLen;

        /* 新生成的缓存本来就是有序的，不需要额外排序 */
        if (zIsCommitDataType ==zpMetaIf->DataType) {
            zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_base = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base;
            zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_len = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len;
        }

        /* 必须在上一个 zRes 使用完之后才能执行 */
        zpRes = zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler);
        /* >>>>检测是否是最后一次循环 */
        zCcur_Fin_Mark(NULL, zpRes, zCacheSiz - 1, zVecCnter);
        /* 生成下一级缓存 */
        zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubMetaIf);
    }
    /* >>>>等待分发出去的所有任务全部完成 */
    zCcur_Wait();

    pclose(zpShellRetHandler);  // 关闭popen打开的FILE指针

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
        zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf.VecSiz = zpTopVecWrapIf->VecSiz;
        zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_base = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_base;
        zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf.p_VecIf[zVecCnter].iov_len = zpTopVecWrapIf->p_VecIf[zVecCnter].iov_len;
    }

    // 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置
    zppGlobRepoIf[zpMetaIf->RepoId]->CommitCacheQueueHeadId = zCacheSiz - 1;
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
    sprintf(zShellBuf, "cd %s && git log server -1 --format=\"%%H_%%ct\"",
            zppGlobRepoIf[zpObjIf->RepoId]->p_RepoPath);

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
    zCcur_Init(zpObjIf->RepoId);
    /* >>>>填充必要的线程间同步数据 */
    zpSubMetaIf = zalloc_cache(zpObjIf->RepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf);
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
    zCcur_Fin_Mark(0, 0, 0, 0);
    /* 生成下一级缓存 */
    zAdd_To_Thread_Pool( zget_file_list_and_diff_content, zpSubMetaIf );
    /* >>>>等待分发出去的所有任务全部完成 */
    zCcur_Wait();

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
    sprintf(zShellBuf, "cd %s && git log -1 CURRENT --format=\"%%H_%%ct\"", zppGlobRepoIf[zRepoId]->p_RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zget_one_line(zRes, zCommonBufSiz, zpFile);
    zLen = 1 + strlen(zRes);  // 此处不能去掉换行符，保证与直接从命令行读出的数据格式一致

    if (zLen != write(zppGlobRepoIf[zRepoId]->LogFd, zRes, zLen)) {
        zPrint_Err(0, NULL, "日志写入失败： <.git_shadow/log/deploy/meta> !");
        exit(1);
    }
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
    zCheck_Negative_Exit(zFd[0] = open(zppGlobRepoIf[zRepoId]->p_RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zppGlobRepoIf[zRepoId]->TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_DpResList, struct zDeployResInfo, zppGlobRepoIf[zRepoId]->TotalHost);  // 分配数组空间，用于顺序读取

    for (_i j = 0; j < zppGlobRepoIf[zRepoId]->TotalHost; j++) {
        zppGlobRepoIf[zRepoId]->p_DpResList[j].RepoId = zRepoId;  // 写入代码库索引值
        zppGlobRepoIf[zRepoId]->p_DpResList[j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zppGlobRepoIf[zRepoId]->p_DpResList[j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zppGlobRepoIf[zRepoId]->p_DpResList[j].ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zppGlobRepoIf[zRepoId]->p_DpResHash[(zppGlobRepoIf[zRepoId]->p_DpResList[j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zppGlobRepoIf[zRepoId]->p_DpResHash[(zppGlobRepoIf[zRepoId]->p_DpResList[j].ClientAddr) % zDeployHashSiz] = &(zppGlobRepoIf[zRepoId]->p_DpResList[j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zppGlobRepoIf[zRepoId]->p_DpResList[j]);
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

    zCheck_Negative_Exit(zFd[0] = open(zppGlobRepoIf[zRepoId]->p_RepoPath, O_RDONLY));

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
            fprintf(stderr, "\033[31;01m[%s]-[Line %d]: Invalid entry!\033[00m\n", zAllIpTxtPath, i);
            exit(1);
        }

        zIpv4Addr = zconvert_ipv4_str_to_bin(zpPCREResIf->p_rets[0]);
        zpcre_free_tmpsource(zpPCREResIf);

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

/************
 * INIT OPS *
 ************/
/*
 * 参数：
 *   新建项目基本信息五个字段
 *   初次启动标记(zInitMark: 1 表示为初始化时调用，0 表示动态更新时调用)
 * 返回值:
 *   -1：信息错误，无法解析
 *   -2：指定的项目ID已存在
 *   -3：指定的源版本控制系统(VCS)类型错误
 */
_i
zadd_one_repo_env(char *zpRepoStrIf, _i zInitMark) {
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;
    char zPullCmdBuf[zCommonBufSiz];

    struct zMetaInfo *zpMetaIf;
    struct zObjInfo *zpObjIf;
    struct stat zStatIf;
    void **zppPrev;

    _i zRepoId,zFd[2];

    /* 正则匹配项目基本信息（5个字段） */
    zpInitIf = zpcre_init("(\\w|[[:punct:]])+");
    zpRetIf = zpcre_match(zpInitIf, zpRepoStrIf, 1);
    if (5 != zpRetIf->cnt) {
        zPrint_Time();
        fprintf(stderr, "\033[31m\"%s\": 新项目信息错误!\033[00m\n", zpRepoStrIf);
        return -1;
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
            return -2;
        }
    }
    /* 检测并生成项目代码定期更新命令 */
    if (0 == strcmp("git", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/%s && git pull --force %s %s:server",
                zpRetIf->p_rets[1],
                zpRetIf->p_rets[2],
                zpRetIf->p_rets[3]);
    } else if (0 == strcmp("svn", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/%s/sync_svn_to_git && svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server",
                zpRetIf->p_rets[1]);
    } else {
        zPrint_Err(0, NULL, "无法识别的远程版本管理系统：不是git也不是svn!");
        return -4;
    }
    /* 分配项目信息的存储空间 */
    zMem_Alloc(zppGlobRepoIf[zRepoId], struct zRepoInfo, 1);
    zppGlobRepoIf[zRepoId]->RepoId = zRepoId;
    /* 提取项目绝对路径 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, 1 + strlen("/home/git/") + strlen(zpRetIf->p_rets[1]));
    strcpy(zppGlobRepoIf[zRepoId]->p_RepoPath, "/home/git/");
    strcat(zppGlobRepoIf[zRepoId]->p_RepoPath, zpRetIf->p_rets[1]);
    /* 检测代码库路径是否存在，根据是否是初始化时被调用，进行创建或报错 */
    errno = 0;
    if (-1 == (zFd[0] = open(zppGlobRepoIf[zRepoId]->p_RepoPath, O_RDONLY | O_DIRECTORY))) {
        if (ENOENT == errno) {
            char *zpCmd = "/home/git/zgit_shadow/scripts/zmaster_init_repo.sh";
            char *zppArgv[] = {"", zpRetIf->p_rets[0], zpRetIf->p_rets[1], zpRetIf->p_rets[2], zpRetIf->p_rets[3], zpRetIf->p_rets[4], NULL};
            zfork_do_exec(zpCmd, zppArgv);
        } else {
            free(zppGlobRepoIf[zRepoId]->p_RepoPath);
            free(zppGlobRepoIf[zRepoId]);
            return -3;
        }
    } else {
        if (0 == zInitMark) {
            free(zppGlobRepoIf[zRepoId]->p_RepoPath);
            free(zppGlobRepoIf[zRepoId]);
            return -3;
        }
    }
    /* 清理资源占用 */
    close(zFd[0]);
    zpcre_free_tmpsource(zpRetIf);
    zpcre_free_metasource(zpInitIf);
    /* 存储项目代码定期更新命令 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zppGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zppGlobRepoIf[zRepoId]->MemLock), NULL) );
    zMap_Alloc( zppGlobRepoIf[zRepoId]->p_MemPool, char, zMemPoolSiz );
    zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    /* 打开代码库顶层目录，生成目录fd供接下来的openat使用 */
    zCheck_Negative_Exit( zFd[0] = open(zppGlobRepoIf[zRepoId]->p_RepoPath, O_RDONLY) );
    /* inotify */
    zMem_Alloc( zpObjIf, char, sizeof(struct zObjInfo) + 1 + strlen("/.git/logs") + strlen(zppGlobRepoIf[zRepoId]->p_RepoPath) );
    zpObjIf->RepoId = zRepoId;
    zpObjIf->RecursiveMark = 1;
    zpObjIf->CallBack = zupdate_one_commit_cache;
    zpObjIf->UpperWid = -1; /* 填充 -1，提示 zinotify_add_sub_watch 函数这是顶层监控对象 */
    strcpy(zpObjIf->p_path, zppGlobRepoIf[zRepoId]->p_RepoPath);
    strcat(zpObjIf->p_path, "/.git/logs");
    zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);
    /* 必要的文件路径检测与创建 */
    #define zCheck_Status_Exit(zRet) do {\
        if (-1 == (zRet) && errno != EEXIST) {\
            zPrint_Err(errno, NULL, "Can't create directory!");\
            exit(1);\
        }\
    } while(0)
    zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow", 0755) );
    zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/info", 0755) );
    zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/log", 0755) );
    zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/log/deploy", 0755) );
    zCheck_Status_Exit( zFd[1] = openat(zFd[0], zAllIpTxtPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
    close(zFd[1]);
    zCheck_Status_Exit( zFd[1] = openat(zFd[0], zMajorIpTxtPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
    close(zFd[1]);
    zCheck_Status_Exit( zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
    close(zFd[1]);
    zCheck_Status_Exit( zFd[1] = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
    close(zFd[1]);
    #undef zCheck_Dir_Status_Exit
    /* 在每个代码库的<.git_shadow/info/repo_id>文件中写入所属代码库的ID */
    zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_TRUNC | O_CREAT, 0644) );
    if (sizeof(zRepoId) != write(zFd[1], &zRepoId, sizeof(zRepoId))) {
        zPrint_Err(0, NULL, "项目ID写入失败!");
        exit(1);
    }
    /* 为每个代码库生成一把读写锁，锁属性设置写者优先 */
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_init(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_setkind_np(&(zppGlobRepoIf[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) );
    zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zppGlobRepoIf[zRepoId]->RwLock), &(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );
    zCheck_Pthread_Func_Exit( pthread_rwlockattr_destroy(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)) );
    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&zppGlobRepoIf[zRepoId]->MutexLock, NULL) );
    /* 更新 TotalHost、zpppDpResHash、zppDpResList */
    zupdate_ipv4_db(&zRepoId);
    /* 用于收集布署尚未成功的主机列表，第一个元素用于存放实时时间戳，因此要多分配一个元素的空间 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_FailingList, _ui, 1 + zppGlobRepoIf[zRepoId]->TotalHost);
    /* 初始化日志下一次写入偏移量并找开日志文件 */
    zCheck_Negative_Exit( fstatat(zFd[0], zLogPath, &zStatIf, 0) );
    zppGlobRepoIf[zRepoId]->zDeployLogOffSet = zStatIf.st_size;
    zCheck_Negative_Exit( zppGlobRepoIf[zRepoId]->LogFd = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_APPEND, 0755) );

    close(zFd[1]);
    close(zFd[0]);

    /* 缓存版本初始化 */
    zppGlobRepoIf[zRepoId]->CacheId = 1000000000;
    /* 用于标记提交记录缓存中的下一个可写位置 */
    zppGlobRepoIf[zRepoId]->CommitCacheQueueHeadId = zCacheSiz - 1;
    /* 指针指向自身的静态数据项 */
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->CommitRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedCommitVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->DeployVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->DeployRefDataIf;
    /* 生成提交记录缓存 */
    zpMetaIf = zalloc_cache(zRepoId, sizeof(struct zMetaInfo));
    zpMetaIf->RepoId = zRepoId;
    zpMetaIf->CacheId = zppGlobRepoIf[zRepoId]->CacheId;
    zpMetaIf->DataType = zIsCommitDataType;
    zgenerate_cache(zpMetaIf);
    /* 生成布署记录缓存 */
    zpMetaIf = zalloc_cache(zRepoId, sizeof(struct zMetaInfo));
    zpMetaIf->RepoId = zRepoId;
    zpMetaIf->CacheId = zppGlobRepoIf[zRepoId]->CacheId;
    zpMetaIf->DataType = zIsDeployDataType;
    zgenerate_cache(zpMetaIf);

    zGlobMaxRepoId = zRepoId;
    return 0;
}

/* 读取项目信息 */
void
zinit_env(const char *zpConfPath) {
    FILE *zpFile;
    char zRes[zCommonBufSiz];

    zCheck_Null_Exit( zpFile = fopen(zpConfPath, "r") );
    while (NULL != zget_one_line(zRes, zCommonBufSiz, zpFile)) {
        zadd_one_repo_env(zRes, 1);
    }

    if (0 > zGlobMaxRepoId) {
        zPrint_Err(0, NULL, "未读取到有效代码库信息!");
    }
    fclose(zpFile);
}
