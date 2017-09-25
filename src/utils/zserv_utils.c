#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
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
    _i *____zpFinMark##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *____zpTotalTask##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *____zpTaskCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *____zpThreadCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    *____zpFinMark##zSuffix = 0;\
    *____zpTotalTask##zSuffix = zTotalTask;\
    *____zpTaskCnter##zSuffix = 0;\
    *____zpThreadCnter##zSuffix = 0;\
\
    pthread_mutex_t *____zpMutexLock##zSuffix = zalloc_cache(zRepoId, 2 * sizeof(pthread_mutex_t));\
    pthread_mutex_init(____zpMutexLock##zSuffix, NULL);\
    pthread_mutex_init(____zpMutexLock##zSuffix + 1, NULL);\

//#define zCcur_Init(zRepoId, zTotalTask, zSuffix) \
//    _i *____zpFinMark##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
//    _i *____zpTotalTask##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
//    _i *____zpTaskCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
//    _i *____zpThreadCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
//    *____zpFinMark##zSuffix = 0;\
//    *____zpTotalTask##zSuffix = zTotalTask;\
//    *____zpTaskCnter##zSuffix = 0;\
//    *____zpThreadCnter##zSuffix = 0;\
//\
//    pthread_cond_t *____zpCondVar##zSuffix = zalloc_cache(zRepoId, sizeof(pthread_cond_t));\
//    pthread_cond_init(____zpCondVar##zSuffix, NULL);\
//\
//    pthread_mutex_t *____zpMutexLock##zSuffix = zalloc_cache(zRepoId, 3 * sizeof(pthread_mutex_t));\
//    pthread_mutex_init(____zpMutexLock##zSuffix, NULL);\
//    pthread_mutex_init(____zpMutexLock##zSuffix + 1, NULL);\
//    pthread_mutex_init(____zpMutexLock##zSuffix + 2, NULL);\
//\
//    pthread_mutex_lock(____zpMutexLock##zSuffix)

/* 配置将要传递给工作线程的参数(结构体) */
#define zCcur_Sub_Config(zpSubIf, zSuffix) do {\
    zpSubIf->p_FinMark = ____zpFinMark##zSuffix;\
    zpSubIf->p_TotalTask = ____zpTotalTask##zSuffix;\
    zpSubIf->p_TaskCnter = ____zpTaskCnter##zSuffix;\
    zpSubIf->p_ThreadCnter = ____zpThreadCnter##zSuffix;\
    zpSubIf->p_MutexLock[0] = ____zpMutexLock##zSuffix;\
    zpSubIf->p_MutexLock[1] = ____zpMutexLock##zSuffix + 1;\
} while (0)

//#define zCcur_Sub_Config(zpSubIf, zSuffix) do {\
//    zpSubIf->p_FinMark = ____zpFinMark##zSuffix;\
//    zpSubIf->p_TotalTask = ____zpTotalTask##zSuffix;\
//    zpSubIf->p_TaskCnter = ____zpTaskCnter##zSuffix;\
//    zpSubIf->p_ThreadCnter = ____zpThreadCnter##zSuffix;\
//    zpSubIf->p_CondVar = ____zpCondVar##zSuffix;\
//    zpSubIf->p_MutexLock[0] = ____zpMutexLock##zSuffix;\
//    zpSubIf->p_MutexLock[1] = ____zpMutexLock##zSuffix + 1;\
//    zpSubIf->p_MutexLock[2] = ____zpMutexLock##zSuffix + 2;\
//} while (0)

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Sub_Config_Thread(zpSubIf, zpIf) do {\
    zpSubIf->p_FinMark = zpIf->p_FinMark;\
    zpSubIf->p_TotalTask = zpIf->p_TotalTask;\
    zpSubIf->p_TaskCnter = zpIf->p_TaskCnter;\
    zpSubIf->p_ThreadCnter = zpIf->p_ThreadCnter;\
    zpSubIf->p_CondVar = zpIf->p_CondVar;\
    zpSubIf->p_MutexLock[0] = zpIf->p_MutexLock[0];\
    zpSubIf->p_MutexLock[1] = zpIf->p_MutexLock[1];\
} while (0)

//#define zCcur_Sub_Config_Thread(zpSubIf, zpIf) do {\
//    zpSubIf->p_FinMark = zpIf->p_FinMark;\
//    zpSubIf->p_TotalTask = zpIf->p_TotalTask;\
//    zpSubIf->p_TaskCnter = zpIf->p_TaskCnter;\
//    zpSubIf->p_ThreadCnter = zpIf->p_ThreadCnter;\
//    zpSubIf->p_CondVar = zpIf->p_CondVar;\
//    zpSubIf->p_MutexLock[0] = zpIf->p_MutexLock[0];\
//    zpSubIf->p_MutexLock[1] = zpIf->p_MutexLock[1];\
//    zpSubIf->p_MutexLock[2] = zpIf->p_MutexLock[2];\
//} while (0)

/* 放置于调用者每次分发任务之前(即调用工作线程之前)，其中zStopExpression指最后一次循环的判断条件，如：A > B && C < D */
#define zCcur_Fin_Mark(zStopExpression, zSuffix) do {\
        pthread_mutex_lock(____zpMutexLock##zSuffix);\
        (*____zpTaskCnter##zSuffix)++;\
        pthread_mutex_unlock(____zpMutexLock##zSuffix);\
        if (zStopExpression) { *(____zpFinMark##zSuffix) = 1; }\
    } while(0)

//#define zCcur_Fin_Mark(zStopExpression, zSuffix) do {\
//        pthread_mutex_lock(____zpMutexLock##zSuffix + 2);\
//        (*____zpTaskCnter##zSuffix)++;\
//        pthread_mutex_unlock(____zpMutexLock##zSuffix + 2);\
//        if (zStopExpression) { *(____zpFinMark##zSuffix) = 1; }\
//    } while(0)

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Fin_Mark_Thread(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[0]);\
        (*(zpIf->p_TaskCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[0]);\
        if (*(zpIf->p_TotalTask) == *(zpIf->p_TaskCnter)) { *(zpIf->p_FinMark) = 1; }\
    } while(0)

//#define zCcur_Fin_Mark_Thread(zpIf) do {\
//        pthread_mutex_lock(zpIf->p_MutexLock[2]);\
//        (*zpIf->p_TaskCnter)++;\
//        pthread_mutex_unlock(zpIf->p_MutexLock[2]);\
//        if (*(zpIf->p_TotalTask) == *(zpIf->p_TaskCnter)) { *(zpIf->p_FinMark) = 1; }\
//    } while(0)

/*
 * 用于存在条件式跳转的循环场景
 * 每次跳过时，都必须让同步计数器递减一次
 */
#define zCcur_Cnter_Subtract(zSuffix) do {\
        (*____zpTaskCnter##zSuffix)--;\
} while (0)

/*
 * 当调用者任务分发完成之后执行，之后释放资源占用
 */
#define zCcur_Wait(zSuffix) do {\
        while ((1 != *____zpFinMark##zSuffix) || (*____zpTaskCnter##zSuffix != *____zpThreadCnter##zSuffix)) {\
            zsleep(0.001);\
        }\
        pthread_mutex_destroy(____zpMutexLock##zSuffix);\
        pthread_mutex_destroy(____zpMutexLock##zSuffix + 1);\
    } while (0)

/*
 ***************************************************************************************
 * The futex facility returned an unexpected error code.!!!!!!!!!!!!!!!!!!!!!!!!!!
 ***************************************************************************************
 */

//#define zCcur_Wait(zSuffix) do {\
//        while ((1 != *____zpFinMark##zSuffix) || (*____zpTaskCnter##zSuffix != *____zpThreadCnter##zSuffix)) {\
//            pthread_cond_wait(____zpCondVar##zSuffix, ____zpMutexLock##zSuffix);\
//        }\
//        pthread_mutex_unlock(____zpMutexLock##zSuffix);\
//\
//        pthread_cond_destroy(____zpCondVar##zSuffix);\
//        pthread_mutex_destroy(____zpMutexLock##zSuffix);\
//        pthread_mutex_destroy(____zpMutexLock##zSuffix + 1);\
//        pthread_mutex_destroy(____zpMutexLock##zSuffix + 2);\
//    } while (0)

/* 放置于工作线程的回调函数末尾 */
#define zCcur_Fin_Signal(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[1]);\
        (*(zpIf->p_ThreadCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[1]);\
    } while (0)

//#define zCcur_Fin_Signal(zpIf) do {\
//        pthread_mutex_lock(zpIf->p_MutexLock[1]);\
//        (*(zpIf->p_ThreadCnter))++;\
//        pthread_mutex_unlock(zpIf->p_MutexLock[1]);\
//        if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_TaskCnter) == *(zpIf->p_ThreadCnter))) {\
//            pthread_mutex_lock(zpIf->p_MutexLock[0]);\
//            pthread_mutex_unlock(zpIf->p_MutexLock[0]);\
//            pthread_cond_signal(zpIf->p_CondVar);\
//        }\
//    } while (0)

/* 用于提取深层对象 */
#define zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf)
#define zGet_OneFileVecWrapIf(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_SubVecWrapIf)

#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_data)

/*
 * 功能：生成单个文件的差异内容缓存
 */
void *
zget_diff_content(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    zVecWrapInfo *zpTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zppGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 40 + 40 + zppGlobRepoIf[zpMetaIf->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf, "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zGet_OneFilePath(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId));

    zpShellRetHandler = popen(zCommonBuf, "r");

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    zCnter = 0;
    if (0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler))) {
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
            zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);

            zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
            zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        }

        pclose(zpShellRetHandler);
    } else {
        pclose(zpShellRetHandler);
        return (void *) -1;
    }

    if (0 == zCnter) {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = NULL;
    } else {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = -7;  // 先赋为 -7
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_RefDataIf = NULL;
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_base = zpTmpBaseDataIf[2]->p_data;
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_len = zpTmpBaseDataIf[2]->DataLen;
        }

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = zCnter;
    }

    return NULL;
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表
 */
#define zGenerate_Graph(zpNodeIf) do {\
    zMetaInfo *____zpTmpNodeIf;\
    _i ____zOffSet;\
\
    zpNodeIf = (zMetaInfo *)zpIf;\
    zpNodeIf->pp_ResHash[zpNodeIf->LineNum] = zpIf;\
    ____zOffSet = 6 * zpNodeIf->OffSet + 10;\
\
    zpNodeIf->p_data[--____zOffSet] = ' ';\
    zpNodeIf->p_data[--____zOffSet] = '\200';\
    zpNodeIf->p_data[--____zOffSet] = '\224';\
    zpNodeIf->p_data[--____zOffSet] = '\342';\
    zpNodeIf->p_data[--____zOffSet] = '\200';\
    zpNodeIf->p_data[--____zOffSet] = '\224';\
    zpNodeIf->p_data[--____zOffSet] = '\342';\
    zpNodeIf->p_data[--____zOffSet] = (NULL == zpNodeIf->p_left) ? '\224' : '\234';\
    zpNodeIf->p_data[--____zOffSet] = '\224';\
    zpNodeIf->p_data[--____zOffSet] = '\342';\
\
    ____zpTmpNodeIf = zpNodeIf;\
    for (_i i = 0; i < zpNodeIf->OffSet; i++) {\
        zpNodeIf->p_data[--____zOffSet] = ' ';\
        zpNodeIf->p_data[--____zOffSet] = ' ';\
        zpNodeIf->p_data[--____zOffSet] = ' ';\
\
        ____zpTmpNodeIf = ____zpTmpNodeIf->p_father;\
        if (NULL == ____zpTmpNodeIf->p_left) {\
            zpNodeIf->p_data[--____zOffSet] = ' ';\
        } else {\
            zpNodeIf->p_data[--____zOffSet] = '\202';\
            zpNodeIf->p_data[--____zOffSet] = '\224';\
            zpNodeIf->p_data[--____zOffSet] = '\342';\
        }\
    }\
\
    zpNodeIf->p_data = zpNodeIf->p_data + ____zOffSet;\
\
    zCcur_Fin_Mark_Thread(zpNodeIf);\
    zCcur_Fin_Signal(zpNodeIf);\
} while (0)

void *
zdistribute_task(void *zpIf) {
    zMetaInfo *zpNodeIf, *zpTmpNodeIf;
    zpNodeIf = (zMetaInfo *)zpIf;

    /* 自身信息 */
    zGenerate_Graph(zpNodeIf);

    /* 第一个左兄弟；不能用循环，会导致重复发放 */
    if (NULL != zpNodeIf->p_left) {
        zpNodeIf->p_left->pp_ResHash = zpNodeIf->pp_ResHash;
        zCcur_Sub_Config_Thread(zpNodeIf->p_left, zpNodeIf);
        zAdd_To_Thread_Pool(zdistribute_task, zpNodeIf->p_left);
    }

    /* 嫡系长子直接处理；各级的左兄弟另行分发 */
    for (zpTmpNodeIf = zpNodeIf->p_FirstChild; NULL != zpTmpNodeIf; zpTmpNodeIf = zpNodeIf->p_FirstChild) {
        zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;
        zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
        zGenerate_Graph(zpTmpNodeIf);

        if (NULL != zpTmpNodeIf->p_left) {
            zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;
            zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
            zAdd_To_Thread_Pool(zdistribute_task, zpTmpNodeIf->p_left);
        }
    }

    return NULL;
}

#define zGenerate_Tree_Node() do {\
    zpTmpNodeIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
    zpTmpNodeIf[0]->LineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNodeIf[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNodeIf[0]->p_FirstChild = NULL;\
    zpTmpNodeIf[0]->p_left = NULL;\
    zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + zRegResIf->ResLen[zNodeCnter]);\
    strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zRegResIf->p_rets[zNodeCnter]);\
\
    zpTmpNodeIf[0]->OpsId = 0;\
    zpTmpNodeIf[0]->RepoId = zpMetaIf->RepoId;\
    zpTmpNodeIf[0]->CommitId = zpMetaIf->CommitId;\
    zpTmpNodeIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
    zpTmpNodeIf[0]->DataType = zpMetaIf->DataType;\
\
    if (zNodeCnter == (zRegResIf->cnt - 1)) {\
        zpTmpNodeIf[0]->FileId = zpTmpNodeIf[0]->LineNum;\
        zpTmpNodeIf[0]->p_ExtraData = zalloc_cache(zpMetaIf->RepoId, zBaseDataLen);\
        memcpy(zpTmpNodeIf[0]->p_ExtraData, zCommonBuf, zBaseDataLen);\
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
    for (; zNodeCnter < zRegResIf->cnt; zNodeCnter++) {\
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
        zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + zRegResIf->ResLen[zNodeCnter]);\
        strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zRegResIf->p_rets[zNodeCnter]);\
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
    memcpy(zpTmpNodeIf[0]->p_ExtraData, zCommonBuf, zBaseDataLen);\
} while(0)

/* 差异文件数量 >128 时，调用此函数，以防生成树图损耗太多性能；此时无需检查无差的性况 */
void
zget_file_list_large(zMetaInfo *zpMetaIf, zVecWrapInfo *zpTopVecWrapIf, FILE *zpShellRetHandler, char *zpCommonBuf, _i zMaxBufLen) {
    zMetaInfo zSubMetaIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    for (zCnter = 0; NULL != zget_one_line(zpCommonBuf, zMaxBufLen, zpShellRetHandler); zCnter++) {
        zBaseDataLen = strlen(zpCommonBuf);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0]; }
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zpCommonBuf, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[0] = zpTmpBaseDataIf[0]->p_next;
    }
    pclose(zpShellRetHandler);

    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zCnter;
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(zRefDataInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));

    for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;

        /* 用于转换成JsonStr */
        zSubMetaIf.OpsId = 0;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;
        zSubMetaIf.CommitId = zpMetaIf->CommitId;
        zSubMetaIf.FileId = i;
        zSubMetaIf.CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zSubMetaIf.DataType = zpMetaIf->DataType;
        zSubMetaIf.p_data = zpTmpBaseDataIf[2]->p_data;
        zSubMetaIf.p_ExtraData = NULL;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zpCommonBuf, &zSubMetaIf);

        zVecDataLen = strlen(zpCommonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zpCommonBuf, zVecDataLen);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_SubVecWrapIf = NULL;
    }

    /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
    ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';
}

void *
zget_file_list(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    zVecWrapInfo *zpTopVecWrapIf;
    FILE *zpShellRetHandler;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zppGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 4 * 40 + zppGlobRepoIf[zpMetaIf->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */

    sprintf(zCommonBuf, "cd \"%s\" && git diff --shortstat \"%s\" \"%s\" | grep -oP '\\d+(?=\\s*file)' && git diff --name-only \"%s\" \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zpShellRetHandler = popen(zCommonBuf, "r");

    /* 差异文件数量 >128 时使用 git 原生视图 */
    if (NULL == zget_one_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        pclose(zpShellRetHandler);
        return (void *) -1;
    } else {
//        if (128 < strtol(zCommonBuf, NULL, 10)) {
            zget_file_list_large(zpMetaIf, zpTopVecWrapIf, zpShellRetHandler, zCommonBuf, zMaxBufLen);
            goto zMarkLarge;
//        }
    }

    /* 差异文件数量 <=128 生成Tree图 */
    zMetaInfo zSubMetaIf;
    _i zVecDataLen, zBaseDataLen, zNodeCnter, zLineCnter;
    zMetaInfo *zpRootNodeIf, *zpTmpNodeIf[3];  // [0]：本体    [1]：记录父节点    [2]：记录兄长节点
    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1];

    /* 在生成树节点之前分配空间，以使其不为 NULL，防止多个查询文件列的的请求导致重复生成同一缓存 */
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = -7;  // 先赋为 -7，知会同类请求缓存正在生成过程中

    zpRootNodeIf = NULL;
    zLineCnter = 0;
    zreg_compile(zRegInitIf, "[^/]+");
    if (NULL != zget_one_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        zBaseDataLen = strlen(zCommonBuf);

        zCommonBuf[zBaseDataLen - 1] = '\0';  // 去掉换行符
        zreg_match(zRegResIf, zRegInitIf, zCommonBuf);

        zNodeCnter = 0;
        zpTmpNodeIf[2] = zpTmpNodeIf[1] = zpTmpNodeIf[0] = NULL;
        zGenerate_Tree_Node(); /* 添加树节点 */
        zreg_free_tmpsource(zRegResIf);

        while (NULL != zget_one_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
            zBaseDataLen = strlen(zCommonBuf);

            zCommonBuf[zBaseDataLen - 1] = '\0';  // 去掉换行符
            zreg_match(zRegResIf, zRegInitIf, zCommonBuf);

            zpTmpNodeIf[0] = zpRootNodeIf;
            zpTmpNodeIf[2] = zpTmpNodeIf[1] = NULL;
            for (zNodeCnter = 0; zNodeCnter < zRegResIf->cnt;) {
                do {
                    if (0 == strcmp(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zRegResIf->p_rets[zNodeCnter])) {
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
            zreg_free_tmpsource(zRegResIf);
        }
    }
    zreg_free_metasource(zRegInitIf);
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
        zSubMetaIf.p_data = (0 == strcmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId))) ? "===> 最新的已布署版本 <===" : "=> 无差异 <=";
        zSubMetaIf.p_ExtraData = NULL;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zCommonBuf, &zSubMetaIf);
        zCommonBuf[0] = '[';  // 逗号替换为 '['

        zVecDataLen = strlen(zCommonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base, zCommonBuf, zVecDataLen);

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 1;
    } else {
        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNodeIf->pp_ResHash = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zMetaInfo *));

        /* Tree 图生成过程的并发控制 */
        zCcur_Init(zpMetaIf->RepoId, zLineCnter, A);
        zCcur_Sub_Config(zpRootNodeIf, A);
        zAdd_To_Thread_Pool(zdistribute_task, zpRootNodeIf);
        zCcur_Wait(A);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zRefDataInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(struct iovec));

        for (_i i = 0; i < zLineCnter; i++) {
            zconvert_struct_to_json_str(zCommonBuf, zpRootNodeIf->pp_ResHash[i]); /* 将 zMetaInfo 转换为 json 文本 */

            zVecDataLen = strlen(zCommonBuf);
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zCommonBuf, zVecDataLen);

            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpRootNodeIf->pp_ResHash[i]->p_ExtraData;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zLineCnter;
    }

zMarkLarge:
    return NULL;
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void *
zgenerate_cache(void *zpIf) {
    zMetaInfo *zpMetaIf, zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    zpMetaIf = (zMetaInfo *)zpIf;

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zppGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 12;
    char zCommonBuf[zMaxBufLen];

    FILE *zpShellRetHandler;
    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        sprintf(zCommonBuf, "cd \"%s\" && git log server%d --format=\"%%H_%%ct\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zpMetaIf->RepoId); // 取 server 分支的提交记录
        zpShellRetHandler = popen(zCommonBuf, "r");
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDpVecWrapIf);
        // 调用外部命令 cat，而不是用 fopen 打开，如此可用统一的 pclose 关闭
        sprintf(zCommonBuf, "cat \"%s\"\"%s\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zDpSigLogPath);
        zpShellRetHandler = popen(zCommonBuf, "r");
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    /* 第一行单独处理，避免后续每次判断是否是第一行 */
    zCnter = 0;
    if (NULL != zget_one_line(zCommonBuf, zGlobBufSiz, zpShellRetHandler)) {
        /* 只提取比最近一次布署版本更新的提交记录 */
        if ((zIsCommitDataType == zpMetaIf->DataType)
                && (0 == (strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
        zBaseDataLen = strlen(zCommonBuf);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zCommonBuf, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1]->p_next = NULL;

        zCnter++;
        for (; (zCnter < zCacheSiz) && (NULL != zget_one_line(zCommonBuf, zGlobBufSiz, zpShellRetHandler)); zCnter++) {
            /* 只提取比最近一次布署版本更新的提交记录 */
            if ((zIsCommitDataType == zpMetaIf->DataType)
                    && (0 == (strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
            zBaseDataLen = strlen(zCommonBuf);
            zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
            zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseDataIf[0]->p_data, zCommonBuf, zBaseDataLen);
            zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

            zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
            zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        }
    }
zMarkSkip:
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
            zconvert_struct_to_json_str(zCommonBuf, &zSubMetaIf);

            zVecDataLen = strlen(zCommonBuf);
            zpTopVecWrapIf->p_VecIf[i].iov_len = zVecDataLen;
            zpTopVecWrapIf->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zpTopVecWrapIf->p_VecIf[i].iov_base, zCommonBuf, zVecDataLen);

            zpTopVecWrapIf->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;
            zpTopVecWrapIf->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        if (zIsDpDataType == zpMetaIf->DataType) {
            // 存储最近一次布署的 SHA1 sig，执行布署是首先对比布署目标与最近一次布署，若相同，则直接返回成功
            strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zpTopVecWrapIf->p_RefDataIf[zCnter - 1].p_data);
            /* 将布署记录按逆向时间排序（新记录显示在前面） */
            for (_i i = 0; i < zpTopVecWrapIf->VecSiz; i++) {
                zCnter--;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_base = zpTopVecWrapIf->p_VecIf[i].iov_base;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_len = zpTopVecWrapIf->p_VecIf[i].iov_len;
            }
        } else {
            /* 提交记录缓存本来就是有序的，不需要额外排序 */
            zpSortedTopVecWrapIf->p_VecIf = zpTopVecWrapIf->p_VecIf;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
    }

    /* 防止意外访问导致的程序崩溃 */
    memset(zpTopVecWrapIf->p_RefDataIf + zpTopVecWrapIf->VecSiz, 0, sizeof(zRefDataInfo) * (zCacheSiz - zpTopVecWrapIf->VecSiz));

    return NULL;
}

/************
 * INIT OPS *
 ************/
/*
 * 参数：
 *   新建项目基本信息五个字段
 *   初次启动标记(zInitMark: 1 表示为初始化时调用，0 表示动态更新时调用)
 * 返回值:
 *         -33：无法创建请求的项目路径
 *         -34：请求创建的新项目信息格式错误（合法字段数量不是五个）
 *         -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 *         -36：请求创建的项目路径已存在，且项目ID不同
 *         -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *         -38：拉取远程代码库失败（git clone 失败）
 *         -39：项目元数据创建失败，如：项目ID无法写入repo_id、无法打开或创建布署日志文件meta等原因
 */
#define zFree_Source() do {\
    free(zppGlobRepoIf[zRepoId]->p_RepoPath);\
    free(zppGlobRepoIf[zRepoId]);\
    zppGlobRepoIf[zRepoId] = NULL;\
    if (-1 != zGlobMaxRepoId) {\
        zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zGlobMaxRepoId + 1, zppGlobRepoIf);\
    }\
    zreg_free_tmpsource(zRegResIf);\
} while(0)

_i
zinit_one_repo_env(char *zpRepoMetaData) {
    zRegInitInfo zRegInitIf[2];
    zRegResInfo zRegResIf[2];

    char zCommonBuf[zGlobBufSiz];

    _i zRepoId, zFd, zErrNo;

    /* 正则匹配项目基本信息（5个字段） */
    zreg_compile(zRegInitIf, "(\\w|[[:punct:]])+");
    zreg_match(zRegResIf, zRegInitIf, zpRepoMetaData);
    zreg_free_metasource(zRegInitIf);
    if (5 > zRegResIf->cnt) {
        zPrint_Time();
        return -34;
    }

    /* 提取项目ID */
    zRepoId = strtol(zRegResIf->p_rets[0], NULL, 10);
    if (zRepoId > zGlobMaxRepoId) {
        zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zRepoId + 1, zppGlobRepoIf);
        for (_i i = zGlobMaxRepoId + 1; i < zRepoId; i++) {
            zppGlobRepoIf[i] = NULL;
        }
    } else {
        if (NULL != zppGlobRepoIf[zRepoId]) {
            zreg_free_tmpsource(zRegResIf);
            return -35;
        }
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zppGlobRepoIf[zRepoId], zRepoInfo, 1);
    zppGlobRepoIf[zRepoId]->RepoId = zRepoId;
    zppGlobRepoIf[zRepoId]->SelfPushMark = (6 == zRegResIf->cnt) ? 1 : 0;

    /* 提取项目绝对路径，结果格式：/home/git/`dirname($Path_On_Host)`/.____DpSystem/`basename($Path_On_Host)` */
    zreg_compile(zRegInitIf + 1, "[^/]+[/]*$");
    zreg_match(zRegResIf + 1, zRegInitIf + 1, zRegResIf->p_rets[1]);
    zreg_free_metasource(zRegInitIf + 1);
    /* 去掉 basename 部分 */
    zRegResIf->p_rets[1][zRegResIf->ResLen[1] - (zRegResIf + 1)->ResLen[0]] = '\0';
    /* 拼接结果字符串 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, sizeof("/home/git/.____DpSystem/") + zRegResIf->ResLen[1]);
    zppGlobRepoIf[zRepoId]->RepoPathLen = sprintf(zppGlobRepoIf[zRepoId]->p_RepoPath, "%s%s%s%s", "/home/git/", zRegResIf->p_rets[1], ".____DpSystem/", (zRegResIf + 1)->p_rets[0]);
    zreg_free_tmpsource(zRegResIf + 1);

    /* 取出本项目所在路径的最大路径长度（用于度量 git 输出的差异文件相对路径长度） */
    zppGlobRepoIf[zRepoId]->MaxPathLen = pathconf(zppGlobRepoIf[zRepoId]->p_RepoPath, _PC_PATH_MAX);

    /* 调用SHELL执行检查和创建 */
    sprintf(zCommonBuf, "sh -x /home/git/zgit_shadow/tools/zmaster_init_repo.sh \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", zRegResIf->p_rets[0], zppGlobRepoIf[zRepoId]->p_RepoPath + 9, zRegResIf->p_rets[2], zRegResIf->p_rets[3], zRegResIf->p_rets[4]);

    /* system 返回的是与 waitpid 中的 status 一样的值，需要用宏 WEXITSTATUS 提取真正的错误码 */
    zErrNo = WEXITSTATUS( system(zCommonBuf) );
    if (255 == zErrNo) {
        zFree_Source();
        return -36;
    } else if (254 == zErrNo) {
        zFree_Source();
        return -33;
    } else if (253 == zErrNo) {
        zFree_Source();
        return -38;
    }

    /* 打开日志文件 */
    char zPathBuf[zGlobBufSiz];
    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zDpSigLogPath);
    zppGlobRepoIf[zRepoId]->DpSigLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zDpTimeSpentLogPath);
    zppGlobRepoIf[zRepoId]->DpTimeSpentLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zRepoIdPath);
    zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0644);

    if ((-1 == zFd)
            || (-1 == zppGlobRepoIf[zRepoId]->DpSigLogFd)
            || (-1 == zppGlobRepoIf[zRepoId]->DpTimeSpentLogFd)) {
        close(zFd);
        close(zppGlobRepoIf[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -39;
    }

    /* 在每个代码库的<_SHADOW/info/repo_id>文件中写入所属代码库的ID */
    char zRepoIdBuf[12];  // 足以容纳整数最大值即可
    _i zRepoIdStrLen = sprintf(zRepoIdBuf, "%d", zRepoId);
    if (zRepoIdStrLen != write(zFd, zRepoIdBuf, zRepoIdStrLen)) {
        close(zFd);
        close(zppGlobRepoIf[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -39;
    }
    close(zFd);

    /* 检测并生成项目代码定期更新命令 */
    char zPullCmdBuf[zGlobBufSiz];
    if (0 == strcmp("git", zRegResIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; rm -f .git/index.lock; git pull --force \"%s\" \"%s\":server%d",
                zppGlobRepoIf[zRepoId]->p_RepoPath,
                zRegResIf->p_rets[2],
                zRegResIf->p_rets[3],
                zRepoId);
    } else if (0 == strcmp("svn", zRegResIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; rm -f .git/index.lock; svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server%d",
                zppGlobRepoIf[zRepoId]->p_RepoPath,
                zRepoId);
    } else {
        close(zppGlobRepoIf[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -37;
    }

    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zppGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 清理资源占用 */
    zreg_free_tmpsource(zRegResIf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zppGlobRepoIf[zRepoId]->p_MemPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Time();
        fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
        exit(1);
    }
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&(zppGlobRepoIf[zRepoId]->MemLock), NULL));

    /* 为每个代码库生成一把读写锁 */
    zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zppGlobRepoIf[zRepoId]->RwLock), NULL));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_init(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_setkind_np(&(zppGlobRepoIf[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zppGlobRepoIf[zRepoId]->RwLock), &(zppGlobRepoIf[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_destroy(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)));

    /* 读写锁生成之后，立刻拿写锁 */
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zRepoId]->RwLock));

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zppGlobRepoIf[zRepoId]->ReplyCntLock, NULL));
    /* 用于保证 "git pull" 原子性拉取的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zppGlobRepoIf[zRepoId]->PullLock, NULL));

    /* 缓存版本初始化 */
    zppGlobRepoIf[zRepoId]->CacheId = 1000000000;
    /* 上一次布署结果状态初始化 */
    zppGlobRepoIf[zRepoId]->RepoState = zRepoGood;

    /* 提取最近一次布署的SHA1 sig，日志文件不会为空，初创时即会以空库的提交记录作为第一条布署记录 */
    sprintf(zCommonBuf, "cat %s%s | tail -1", zppGlobRepoIf[zRepoId]->p_RepoPath, zDpSigLogPath);
    FILE *zpShellRetHandler = popen(zCommonBuf, "r");
    if (zBytes(40) != zget_str_content(zppGlobRepoIf[zRepoId]->zLastDpSig, zBytes(40), zpShellRetHandler)) {
        zppGlobRepoIf[zRepoId]->zLastDpSig[40] = '\0';
    }
    pclose(zpShellRetHandler);

    /* 指针指向自身的静态数据项 */
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->CommitRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;  // 提交记录总是有序的，不需要再分配静态空间

    zppGlobRepoIf[zRepoId]->DpVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->DpVecIf;
    zppGlobRepoIf[zRepoId]->DpVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->DpRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedDpVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedDpVecIf;

    zppGlobRepoIf[zRepoId]->p_HostStrAddrList[0] = zppGlobRepoIf[zRepoId]->HostStrAddrList[0];
    zppGlobRepoIf[zRepoId]->p_HostStrAddrList[1] = zppGlobRepoIf[zRepoId]->HostStrAddrList[1];

    /* 生成缓存 */
    zMetaInfo zMetaIf;
    zMetaIf.RepoId = zRepoId;

    zMetaIf.DataType = zIsCommitDataType;
    zgenerate_cache(&zMetaIf);

    zMetaIf.DataType = zIsDpDataType;
    zgenerate_cache(&zMetaIf);

    /**/
    zGlobMaxRepoId = zRepoId > zGlobMaxRepoId ? zRepoId : zGlobMaxRepoId;
    zppGlobRepoIf[zRepoId]->zInitRepoFinMark = 1;

    /* 放锁 */
    pthread_rwlock_unlock(&(zppGlobRepoIf[zRepoId]->RwLock));
    return 0;
}
#undef zFree_Source

/* 读取项目信息，初始化配套环境 */
void *
zinit_env(const char *zpConfPath) {
    FILE *zpFile;
    char zRes[zGlobBufSiz];
    _i zErrNo;

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

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));
    while (NULL != zget_one_line(zRes, zGlobBufSiz, zpFile)) {
        if (0 > (zErrNo = zinit_one_repo_env(zRes))) {
            fprintf(stderr, "ERROR[zinit_one_repo_env]: %d\n", zErrNo);
        }
    }

    if (0 > zGlobMaxRepoId) { zPrint_Err(0, NULL, "未读取到有效代码库信息!"); }

    fclose(zpFile);
    return NULL;
}
