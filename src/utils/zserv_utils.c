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
#define zCcur_Init(zRepoId, zTotalTask, zSuffix) \
    _i *zpFinMark##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpTotalTask##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpTaskCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpThreadCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    *zpFinMark##zSuffix = 0;\
    *zpTotalTask##zSuffix = zTotalTask;\
    *zpTaskCnter##zSuffix = 0;\
    *zpThreadCnter##zSuffix = 0;\
\
    pthread_cond_t *zpCondVar##zSuffix = zalloc_cache(zRepoId, sizeof(pthread_cond_t));\
    pthread_cond_init(zpCondVar##zSuffix, NULL);\
\
    pthread_mutex_t *zpMutexLock##zSuffix = zalloc_cache(zRepoId, 3 * sizeof(pthread_mutex_t));\
    pthread_mutex_init(zpMutexLock##zSuffix, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix + 1, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix + 2, NULL);\
\
    pthread_mutex_lock(zpMutexLock##zSuffix);

/* 配置将要传递给工作线程的参数(结构体) */
#define zCcur_Sub_Config(zpSubIf, zSuffix) \
    zpSubIf->p_FinMark = zpFinMark##zSuffix;\
    zpSubIf->p_TotalTask = zpTotalTask##zSuffix;\
    zpSubIf->p_TaskCnter = zpTaskCnter##zSuffix;\
    zpSubIf->p_ThreadCnter = zpThreadCnter##zSuffix;\
    zpSubIf->p_CondVar = zpCondVar##zSuffix;\
    zpSubIf->p_MutexLock[0] = zpMutexLock##zSuffix;\
    zpSubIf->p_MutexLock[1] = zpMutexLock##zSuffix + 1;\
    zpSubIf->p_MutexLock[2] = zpMutexLock##zSuffix + 2;

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Sub_Config_Thread(zpSubIf, zpIf) \
    zpSubIf->p_FinMark = zpIf->p_FinMark;\
    zpSubIf->p_TotalTask = zpIf->p_TotalTask;\
    zpSubIf->p_TaskCnter = zpIf->p_TaskCnter;\
    zpSubIf->p_ThreadCnter = zpIf->p_ThreadCnter;\
    zpSubIf->p_CondVar = zpIf->p_CondVar;\
    zpSubIf->p_MutexLock[0] = zpIf->p_MutexLock[0];\
    zpSubIf->p_MutexLock[1] = zpIf->p_MutexLock[1];\
    zpSubIf->p_MutexLock[2] = zpIf->p_MutexLock[2];

/* 放置于调用者每次分发任务之前(即调用工作线程之前)，其中zStopExpression指最后一次循环的判断条件，如：A > B && C < D */
#define zCcur_Fin_Mark(zStopExpression, zSuffix) do {\
        pthread_mutex_lock(zpMutexLock##zSuffix + 2);\
        (*zpTaskCnter##zSuffix)++;\
        pthread_mutex_unlock(zpMutexLock##zSuffix + 2);\
        if (zStopExpression) {\
            *(zpFinMark##zSuffix) = 1;\
        }\
    } while(0)

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Fin_Mark_Thread(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[2]);\
        (*(zpIf->p_TaskCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[2]);\
        if (*(zpIf->p_TaskCnter) == *(zpIf->p_TotalTask)) {\
            *(zpIf->p_FinMark) = 1;\
        }\
    } while(0)

/*
 * 用于存在条件式跳转的循环场景
 * 每次跳过时，都必须让同步计数器递减一次
 */
#define zCcur_Cnter_Subtract(zSuffix) do {\
        (*(zpTaskCnter##zSuffix))--;\
} while(0)
/*
 * 当调用者任务分发完成之后执行，之后释放资源占用
 * 不能使用while，而要使用 do...while，至少让调用者有一次收信号的机会
 * 否则可能导致在下层通知未执行之前条件变量被销毁，从而带来不确定的后果
 */
#define zCcur_Wait(zSuffix) do {\
        do {\
            pthread_cond_wait(zpCondVar##zSuffix, zpMutexLock##zSuffix);\
        } while (*(zpTaskCnter##zSuffix) != *(zpThreadCnter##zSuffix));\
        pthread_mutex_unlock(zpMutexLock##zSuffix);\
        pthread_cond_destroy(zpCondVar##zSuffix);\
        pthread_mutex_destroy(zpMutexLock##zSuffix + 2);\
        pthread_mutex_destroy(zpMutexLock##zSuffix + 1);\
        pthread_mutex_destroy(zpMutexLock##zSuffix);\
    } while(0)

/* 放置于工作线程的回调函数末尾 */
#define zCcur_Fin_Signal(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[1]);\
        (*(zpIf->p_ThreadCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[1]);\
        if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_TaskCnter) == *(zpIf->p_ThreadCnter))) {\
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
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    zVecWrapInfo *zpTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zShellBuf[zCommonBufSiz], zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return;
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s %s -- %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zGet_OneFilePath(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    for (zCnter = 0; 0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
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
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = 0;  // 先赋为 0
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_RefDataIf = NULL;
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_base = zpTmpBaseDataIf[2]->p_data;
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_len = zpTmpBaseDataIf[2]->DataLen;
        }

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = zCnter;
    }
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表
 */
void
zgenerate_graph(void *zpIf) {
    zMetaInfo *zpNodeIf, *zpTmpNodeIf;
    _i zOffSet;

    zpNodeIf = (zMetaInfo *)zpIf;
    zpNodeIf->pp_ResHash[zpNodeIf->LineNum] = zpNodeIf;
    zOffSet = 6 * zpNodeIf->OffSet + 10;

    zpNodeIf->p_data[--zOffSet] = ' ';
    zpNodeIf->p_data[--zOffSet] = '\200';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';
    zpNodeIf->p_data[--zOffSet] = '\200';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';
    zpNodeIf->p_data[--zOffSet] = (NULL == zpNodeIf->p_left) ? '\224' : '\234';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';

    zpTmpNodeIf = zpNodeIf;
    for (_i i = 0; i < zpNodeIf->OffSet; i++) {
        zpNodeIf->p_data[--zOffSet] = ' ';
        zpNodeIf->p_data[--zOffSet] = ' ';
        zpNodeIf->p_data[--zOffSet] = ' ';

        if (NULL == zpTmpNodeIf->p_left) {
            zpNodeIf->p_data[--zOffSet] = ' ';
        } else {
            zpNodeIf->p_data[--zOffSet] = '\202';
            zpNodeIf->p_data[--zOffSet] = '\224';
            zpNodeIf->p_data[--zOffSet] = '\342';
        }

        zpTmpNodeIf = zpTmpNodeIf->p_father;
    }

    zpNodeIf->p_data = zpNodeIf->p_data + zOffSet;

    zCcur_Fin_Signal(zpNodeIf);
}

void
zdistribute_task(void *zpIf) {
    zMetaInfo *zpNodeIf, *zpTmpNodeIf;
    zpNodeIf = (zMetaInfo *)zpIf;

    zpTmpNodeIf = zpNodeIf->p_left;
    if (NULL != zpTmpNodeIf) {  // 不能用循环，会导致重复发放
        zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;

        zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
        zCcur_Fin_Mark_Thread(zpTmpNodeIf);
        zAdd_To_Thread_Pool(zdistribute_task, zpTmpNodeIf);
    }

    zpTmpNodeIf = zpNodeIf->p_FirstChild;
    if (NULL != zpTmpNodeIf) {  // 不能用循环，会导致重复发放
        zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;

        zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
        zCcur_Fin_Mark_Thread(zpTmpNodeIf);
        zAdd_To_Thread_Pool(zdistribute_task, zpTmpNodeIf);
    }

    zAdd_To_Thread_Pool(zgenerate_graph, zpNodeIf);
}

#define zGenerate_Tree_Node() do {\
    zpTmpNodeIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
    zpTmpNodeIf[0]->LineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNodeIf[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNodeIf[0]->p_FirstChild = NULL;\
    zpTmpNodeIf[0]->p_left = NULL;\
    zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + strlen(zpPcreRetIf->p_rets[zNodeCnter]));\
    strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter]);\
\
    zpTmpNodeIf[0]->OpsId = 0;\
    zpTmpNodeIf[0]->RepoId = zpMetaIf->RepoId;\
    zpTmpNodeIf[0]->CommitId = zpMetaIf->CommitId;\
    zpTmpNodeIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
    zpTmpNodeIf[0]->DataType = zpMetaIf->DataType;\
\
    if (zNodeCnter == (zpPcreRetIf->cnt - 1)) {\
        zpTmpNodeIf[0]->FileId = zpTmpNodeIf[0]->LineNum;\
        zpTmpNodeIf[0]->p_ExtraData = zalloc_cache(zpMetaIf->RepoId, zBaseDataLen);\
        memcpy(zpTmpNodeIf[0]->p_ExtraData, zRes, zBaseDataLen);\
    } else {\
        zpTmpNodeIf[0]->FileId = -1;\
        zpTmpNodeIf[0]->p_ExtraData = NULL;\
    }\
\
    if (0 == zNodeCnter) {\
        zpTmpNodeIf[0]->p_father = NULL;\
        if (NULL == zpRootNodeIf) {\
            zpRootNodeIf = zpTmpNodeIf[0];\
        } else {\
            for (zpTmpNodeIf[2] = zpRootNodeIf; NULL != zpTmpNodeIf[2]->p_left; zpTmpNodeIf[2] = zpTmpNodeIf[2]->p_left) {}\
            zpTmpNodeIf[2]->p_left = zpTmpNodeIf[0];\
        }\
    } else {\
        zpTmpNodeIf[0]->p_father = zpTmpNodeIf[1];\
        if (NULL == zpTmpNodeIf[2]) {\
            zpTmpNodeIf[1]->p_FirstChild = zpTmpNodeIf[0];\
        } else {\
            zpTmpNodeIf[2]->p_left = zpTmpNodeIf[0];\
        }\
    }\
\
    zNodeCnter++;\
    for (; zNodeCnter < zpPcreRetIf->cnt; zNodeCnter++) {\
        zpTmpNodeIf[0]->p_FirstChild = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
        zpTmpNodeIf[1] = zpTmpNodeIf[0];\
        zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_FirstChild;\
        zpTmpNodeIf[0]->p_father = zpTmpNodeIf[1];\
        zpTmpNodeIf[0]->p_FirstChild = NULL;\
        zpTmpNodeIf[0]->p_left = NULL;\
\
        zpTmpNodeIf[0]->LineNum = zLineCnter;  /* 横向偏移 */\
        zLineCnter++;  /* 每个节点会占用一行显示输出 */\
        zpTmpNodeIf[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
        zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + strlen(zpPcreRetIf->p_rets[zNodeCnter]));\
        strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter]);\
\
        zpTmpNodeIf[0]->OpsId = 0;\
        zpTmpNodeIf[0]->RepoId = zpMetaIf->RepoId;\
        zpTmpNodeIf[0]->CommitId = zpMetaIf->CommitId;\
        zpTmpNodeIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
        zpTmpNodeIf[0]->DataType = zpMetaIf->DataType;\
\
        zpTmpNodeIf[0]->FileId = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNodeIf[0]->p_ExtraData = NULL;\
    }\
    zpTmpNodeIf[0]->FileId = zpTmpNodeIf[0]->LineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNodeIf[0]->p_ExtraData = zalloc_cache(zpMetaIf->RepoId, zBaseDataLen);\
    memcpy(zpTmpNodeIf[0]->p_ExtraData, zRes, zBaseDataLen);\
} while(0)

void
zget_file_list(void *zpIf) {
    zMetaInfo *zpMetaIf, zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf;
    _i zVecDataLen, zBaseDataLen, zNodeCnter, zLineCnter;

    zMetaInfo *zpRootNodeIf, *zpTmpNodeIf[3];  // [0]：本体    [1]：记录父节点    [2]：记录兄长节点
    zPCREInitInfo *zpPcreInitIf;
    zPCRERetInfo *zpPcreRetIf;

    FILE *zpShellRetHandler;
    char zShellBuf[zCommonBufSiz], zJsonBuf[zBytes(256)], zRes[zBytes(1024)];

    zpMetaIf = (zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return;
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff --name-only %s %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    /* 在生成树节点之前分配空间，以使其不为 NULL，防止多个查询文件列的的请求导致重复生成同一缓存 */
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 0;  // 先赋为 0，知会同类请求缓存正在生成过程中

    zpRootNodeIf = NULL;
    zLineCnter = 0;
    zpPcreInitIf = zpcre_init("[^/]+");
    if (NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler)) {
        zBaseDataLen = strlen(zRes);

        zRes[zBaseDataLen - 1] = '/';  // 由于 '非' 逻辑匹配无法取得最后一个字符，此处为适为 pcre 临时添加末尾标识
        zpPcreRetIf = zpcre_match(zpPcreInitIf, zRes, 1);
        zRes[zBaseDataLen - 1] = '\0';  // 去除临时的多余字符

        zNodeCnter = 0;
        zpTmpNodeIf[2] = zpTmpNodeIf[1] = zpTmpNodeIf[0] = NULL;
        zGenerate_Tree_Node(); /* 添加树节点 */
        zpcre_free_tmpsource(zpPcreRetIf);

        while (NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler)) {
            zBaseDataLen = strlen(zRes);

            zRes[zBaseDataLen - 1] = '/';  // 由于 '非' 逻辑匹配无法取得最后一个字符，此处为适为 pcre 临时添加末尾标识
            zpPcreRetIf = zpcre_match(zpPcreInitIf, zRes, 1);
            zRes[zBaseDataLen - 1] = '\0';  // 去除临时的多余字符

            zpTmpNodeIf[0] = zpRootNodeIf;
            zpTmpNodeIf[2] = zpTmpNodeIf[1] = NULL;
            for (zNodeCnter = 0; zNodeCnter < zpPcreRetIf->cnt;) {
                do {
                    if (0 == strcmp(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter])) {
                        zpTmpNodeIf[1] = zpTmpNodeIf[0];
                        zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_FirstChild;
                        zpTmpNodeIf[2] = NULL;
                        zNodeCnter++;
                        if (NULL == zpTmpNodeIf[0]) {
                            goto zMarkOuter;
                        } else {
                            goto zMarkInner;
                        }
                    }
                    zpTmpNodeIf[2] = zpTmpNodeIf[0];
                    zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_left;
                } while (NULL != zpTmpNodeIf[0]);
                break;
zMarkInner:;
            }
zMarkOuter:;
            zGenerate_Tree_Node(); /* 添加树节点 */
            zpcre_free_tmpsource(zpPcreRetIf);
        }
    }
    zpcre_free_metasource(zpPcreInitIf);
    pclose(zpShellRetHandler);

    if (NULL == zpRootNodeIf) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = NULL;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec));

        zSubMetaIf.OpsId = 0;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;
        zSubMetaIf.CommitId = zpMetaIf->CommitId;
        zSubMetaIf.FileId = -1;  // 置为 -1，不允许再查询下一级内容
        zSubMetaIf.CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zSubMetaIf.DataType = zpMetaIf->DataType;
        zSubMetaIf.p_data = "==> 最新的已布署版本 <==";
        zSubMetaIf.p_ExtraData = NULL;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, &zSubMetaIf);
        zJsonBuf[0] = '[';  // 逗号替换为 '['

        zVecDataLen = strlen(zJsonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base, zJsonBuf, zVecDataLen);

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 1;
    } else {
        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNodeIf->pp_ResHash = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zMetaInfo *));

        /* Tree 图生成过程的并发控制 */
        zCcur_Init(zpMetaIf->RepoId, zLineCnter, A);
        zCcur_Sub_Config(zpRootNodeIf, A);
        zCcur_Fin_Mark(zpRootNodeIf->p_TotalTask == zpRootNodeIf->p_TaskCnter, A);
        zAdd_To_Thread_Pool(zdistribute_task, zpRootNodeIf);
        zCcur_Wait(A);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zRefDataInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(struct iovec));

        for (_i i = 0; i < zLineCnter; i++) {
            zconvert_struct_to_json_str(zJsonBuf, zpRootNodeIf->pp_ResHash[i]); /* 将 zMetaInfo 转换为 json 文本 */

            zVecDataLen = strlen(zJsonBuf);
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);

            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpRootNodeIf->pp_ResHash[i]->p_ExtraData;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zLineCnter;
    }
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zgenerate_cache(void *zpIf) {
    zMetaInfo *zpMetaIf, zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[zCommonBufSiz], zJsonBuf[zBytes(256)];

    zpMetaIf = (zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        sprintf(zShellBuf, "cd %s && git log server --format=\"%%H_%%ct\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath); // 取 server 分支的提交记录
        zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDeployVecWrapIf);
        // 调用外部命令 cat，而不是用 fopen 打开，如此可用统一的 pclose 关闭
        sprintf(zShellBuf, "cat %s%s", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zLogPath);
        zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    for (zCnter = 0; (zCnter < zCacheSiz) && (NULL != zget_one_line(zRes, zBytes(1024), zpShellRetHandler)); zCnter++) {
        /* 只提取比最近一次布署版本更新的提交记录 */
        if ((zIsCommitDataType == zpMetaIf->DataType)
                && (0 == (strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zRes, zBytes(40))))) { break; }
        zBaseDataLen = strlen(zRes);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
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
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zpTmpBaseDataIf[2]->p_data[40] = '\0';

            /* 用于转换成JsonStr */
            zSubMetaIf.OpsId = 0;
            zSubMetaIf.RepoId = zpMetaIf->RepoId;
            zSubMetaIf.CommitId = i;
            zSubMetaIf.FileId = -1;
            zSubMetaIf.CacheId =  zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
            zSubMetaIf.DataType = zpMetaIf->DataType;
            zSubMetaIf.p_data = zpTmpBaseDataIf[2]->p_data;
            zSubMetaIf.p_ExtraData = &(zpTmpBaseDataIf[2]->p_data[41]);

            /* 将zMetaInfo转换为JSON文本 */
            zconvert_struct_to_json_str(zJsonBuf, &zSubMetaIf);

            zVecDataLen = strlen(zJsonBuf);
            zpTopVecWrapIf->p_VecIf[i].iov_len = zVecDataLen;
            zpTopVecWrapIf->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zpTopVecWrapIf->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);

            zpTopVecWrapIf->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;
            zpTopVecWrapIf->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        if (zIsDeployDataType == zpMetaIf->DataType) {
            // 存储最近一次布署的 SHA1 sig，执行布署是首先对比布署目标与最近一次布署，若相同，则直接返回成功
            strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zpTopVecWrapIf->p_RefDataIf[zCnter - 1].p_data);
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
    memset(zpTopVecWrapIf->p_RefDataIf + zpTopVecWrapIf->VecSiz, 0, sizeof(zRefDataInfo) * (zCacheSiz - zpTopVecWrapIf->VecSiz));

    /* >>>>任务完成，尝试通知上层调用者 */
    zCcur_Fin_Signal(zpMetaIf);
}

/*
 * 当监测到有新的代码提交时，为新版本代码生成缓存
 * 此函数在 inotify 中使用，传入的参数是 zObjInfo 数型
 */
void
zupdate_one_commit_cache(void *zpIf) {
    zObjInfo *zpObjIf;
    zMetaInfo zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;

    char zJsonBuf[zBytes(256)];  // iov_base
    _i zVecDataLen, *zpHeadId;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[zCommonBufSiz];

    zpObjIf = (zObjInfo*)zpIf;
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

    /* 转换成JsonStr */
    zSubMetaIf.OpsId = 0;
    zSubMetaIf.RepoId = zpObjIf->RepoId;
    zSubMetaIf.CommitId = *zpHeadId;  // 逆向循环索引号更新
    zSubMetaIf.FileId = -1;
    zSubMetaIf.CacheId = zppGlobRepoIf[zpObjIf->RepoId]->CacheId;
    zSubMetaIf.DataType = zIsCommitDataType;
    zSubMetaIf.p_data = zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data;
    zSubMetaIf.p_ExtraData = &(zRes[41]);
    /* 生成下一级缓存，新提交的单条记录直接生成下一级缓存 */
    zget_file_list(&zSubMetaIf);

    /* 将zMetaInfo转换为JSON文本 */
    zconvert_struct_to_json_str(zJsonBuf, &zSubMetaIf);

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
//     zObjInfo *zpObjIf = (zObjInfo *) zpIf;
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
    char zShellBuf[zCommonBufSiz], zRes[zCommonBufSiz];
    FILE *zpFile;
    _i zLen;

    /* write last deploy SHA1_sig and it's timestamp to: <_SHADOW/log/meta> */
    sprintf(zShellBuf, "cd %s && git log %s -1 --format=\"%%H_%%ct\"",
            zppGlobRepoIf[zRepoId]->p_RepoPath,
            zppGlobRepoIf[zRepoId]->zLastDeploySig);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zget_one_line(zRes, zCommonBufSiz, zpFile);
    zLen = strlen(zRes);  // 写入文件时，不能写入最后的 '\0'

    if (zLen != write(zppGlobRepoIf[zRepoId]->LogFd, zRes, zLen)) {
        zPrint_Err(0, NULL, "日志写入失败： <_SHADOW/log/deploy/meta> !");
        exit(1);
    }
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
    zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zGlobMaxRepoId + 1, zppGlobRepoIf);\
    zpcre_free_tmpsource(zpRetIf);\
    zpcre_free_metasource(zpInitIf);\
} while(0)

_i
zinit_one_repo_env(char *zpRepoMetaData) {
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;

    zMetaInfo *zpMetaIf[2];
    zObjInfo *zpObjIf;
    char zShellBuf[zCommonBufSiz];

    _i zRepoId,zFd;

    /* 正则匹配项目基本信息（5个字段） */
    zpInitIf = zpcre_init("(\\w|[[:punct:]])+");
    zpRetIf = zpcre_match(zpInitIf, zpRepoMetaData, 1);
    if (5 != zpRetIf->cnt) {
        zPrint_Time();
        fprintf(stderr, "\033[31m\"%s\": 新项目信息错误!\033[00m\n", zpRepoMetaData);
        return -34;
    }

    /* 提取项目ID */
    zRepoId = strtol(zpRetIf->p_rets[0], NULL, 10);
    if (zRepoId > zGlobMaxRepoId) {
        zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zRepoId + 1, zppGlobRepoIf);
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
    zMem_C_Alloc(zppGlobRepoIf[zRepoId], zRepoInfo, 1);
    zppGlobRepoIf[zRepoId]->RepoId = zRepoId;

    /* 提取项目绝对路径 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, 1 + strlen("/home/git/") + strlen(zpRetIf->p_rets[1]));
    sprintf(zppGlobRepoIf[zRepoId]->p_RepoPath, "%s%s", "/home/git/", zpRetIf->p_rets[1]);

    /* 调用SHELL执行检查和创建 */
    sprintf(zShellBuf, "sh -x /home/git/zgit_shadow/scripts/zmaster_init_repo.sh %s", zpRepoMetaData);
    system(zShellBuf);

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
        sprintf(zPullCmdBuf, "cd /home/git/%s && rm -rf * && git pull --force %s %s:server",
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

    /* inotify */
    zpObjIf = zalloc_cache(zRepoId, sizeof(zObjInfo) + 1 + strlen(zInotifyObjRelativePath) + strlen(zppGlobRepoIf[zRepoId]->p_RepoPath));
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
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&zppGlobRepoIf[zRepoId]->ReplyCntLock, NULL) );

    /* 缓存版本初始化 */
    zppGlobRepoIf[zRepoId]->CacheId = 1000000000;

    /* 提取最近一次布署的SHA1 sig */
    sprintf(zShellBuf, "cat %s%s | tail -1", zppGlobRepoIf[zRepoId]->p_RepoPath, zLogPath);
    FILE *zpShellRetHandler = popen(zShellBuf, "r");
    if (zBytes(40) == zget_str_content(zppGlobRepoIf[zRepoId]->zLastDeploySig, zBytes(40), zpShellRetHandler)) {
        zppGlobRepoIf[zRepoId]->zLastDeploySig[40] = '\0';
    } else {
        pclose(zpShellRetHandler);
        sprintf(zShellBuf, "cd %s && git log BASEXXXXXXXX -1 --format=%%H", zppGlobRepoIf[zRepoId]->p_RepoPath);
        zpShellRetHandler = popen(zShellBuf, "r");
        zget_str_content(zppGlobRepoIf[zRepoId]->zLastDeploySig, zBytes(40), zpShellRetHandler);
    }
    pclose(zpShellRetHandler);

    /* 指针指向自身的静态数据项 */
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->CommitRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedCommitVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->DeployVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->DeployRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedDeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedDeployVecIf;

    /* 初始化任务分发环境 */
    zCcur_Init(zRepoId, 0, A);  //___
    zCcur_Fin_Mark(1 == 1, A);  //___
    zCcur_Init(zRepoId, 0, B);  //___
    zCcur_Fin_Mark(1 == 1, B);  //___

    /* 生成提交记录缓存 */
    zpMetaIf[0] = zalloc_cache(zRepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[0], A);  //___
    zpMetaIf[0]->RepoId = zRepoId;
    zpMetaIf[0]->DataType = zIsCommitDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[0]);

    /* 生成布署记录缓存 */
    zpMetaIf[1] = zalloc_cache(zRepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[1], B);  //___
    zpMetaIf[1]->RepoId = zRepoId;
    zpMetaIf[1]->DataType = zIsDeployDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[1]);

    /* 等待两批任务完成，之后释放相关资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    zGlobMaxRepoId = zRepoId;
    return 0;
}
#undef zFree_Source

/* 读取项目信息，初始化配套环境 */
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
        = zparse_digit;
    zJsonParseOps['d']  // data
        = zJsonParseOps['E']  // ExtraData
        = zparse_str;

    zCheck_Null_Exit( zpFile = fopen(zpConfPath, "r") );
    while (NULL != zget_one_line(zRes, zCommonBufSiz, zpFile)) {
        zinit_one_repo_env(zRes);
    }

    if (0 > zGlobMaxRepoId) {
        zPrint_Err(0, NULL, "未读取到有效代码库信息!");
    }
    fclose(zpFile);
}

/* 通过 SSH 远程初始化一个目标主机，完成任务后通知上层调用者 */
void
zinit_one_remote_host(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    char zShellBuf[zCommonBufSiz];
    char zMajorHostStrAddrBuf[16], zHostStrAddrBuf[16];

    zconvert_ipv4_bin_to_str(zppGlobRepoIf[zpMetaIf->RepoId]->MajorHostAddr, zMajorHostStrAddrBuf);
    zconvert_ipv4_bin_to_str(zpMetaIf->HostId, zHostStrAddrBuf);

    sprintf(zShellBuf, "sh -x /home/git/zgit_shadow/scripts/zhost_init_repo.sh %s %s %s",
            zMajorHostStrAddrBuf,
            zHostStrAddrBuf,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9);  // 去掉最前面的 "/home/git" 共计 9 个字符
    system(zShellBuf);

    zCcur_Fin_Signal(zpMetaIf);
}
