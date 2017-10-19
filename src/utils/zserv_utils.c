#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zpGlobRepoIf[zRepoId]->MemLock));\
    \
    void **zppPrev = zpGlobRepoIf[zRepoId]->p_MemPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        munmap(zpGlobRepoIf[zRepoId]->p_MemPool, zMemPoolSiz);\
        zpGlobRepoIf[zRepoId]->p_MemPool = zppPrev;\
    }\
    zpGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);\
    \
    pthread_mutex_unlock(&(zpGlobRepoIf[zRepoId]->MemLock));\
} while(0)

/**************
 * NATIVE OPS *
 **************/
/* 用于提取深层对象 */
#define zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf)
#define zGet_OneFileVecWrapIf(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_SubVecWrapIf)

#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_data)

/*
 * 功能：生成单个文件的差异内容缓存
 */
void *
zget_diff_content(void *zpParam) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpParam;
    zVecWrapInfo *zpTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zpGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 40 + 40 + zpGlobRepoIf[zpMetaIf->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf, "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zGet_OneFilePath(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );

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
    zpNodeIf->pp_ResHash[zpNodeIf->LineNum] = zpNodeIf;\
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
} while (0)

void *
zdistribute_task(void *zpParam) {
    zMetaInfo *zpNodeIf = (zMetaInfo *)zpParam;
    zMetaInfo **zppKeepPtr = zpNodeIf->pp_ResHash;

    do {
        /* 分发直连的子节点 */
        if (NULL != zpNodeIf->p_FirstChild) {
            zpNodeIf->p_FirstChild->pp_ResHash = zppKeepPtr;
            zdistribute_task(zpNodeIf->p_FirstChild);  // 暂时以递归处理，线程模型会有收集不齐全部任务的问题
        }

        /* 自身及所有的左兄弟 */
        zGenerate_Graph(zpNodeIf);
        zpNodeIf = zpNodeIf->p_left;
    } while ((NULL != zpNodeIf) && (zpNodeIf->pp_ResHash = zppKeepPtr));

    return NULL;
}

#define zGenerate_Tree_Node() do {\
    zpTmpNodeIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
\
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
    zpTmpNodeIf[0]->CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
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
\
        zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_FirstChild;\
\
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
        zpTmpNodeIf[0]->CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
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
        zSubMetaIf.CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;
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
zget_file_list(void *zpParam) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpParam;
    zVecWrapInfo *zpTopVecWrapIf;
    FILE *zpShellRetHandler;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 4 * 40 + zpGlobRepoIf[zpMetaIf->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */

    sprintf(zCommonBuf, "cd \"%s\" && git diff --shortstat \"%s\" \"%s\" | grep -oP '\\d+(?=\\s*file)' && git diff --name-only \"%s\" \"%s\"",
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );

    /* 差异文件数量 >24 时使用 git 原生视图，避免占用太多资源，同时避免爆栈 */
    if (NULL == zget_one_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        pclose(zpShellRetHandler);
        return (void *) -1;
    } else {
        if (24 < strtol(zCommonBuf, NULL, 10)) {
            zget_file_list_large(zpMetaIf, zpTopVecWrapIf, zpShellRetHandler, zCommonBuf, zMaxBufLen);
            goto zMarkLarge;
        }
    }

    /* 差异文件数量 <=24 生成Tree图 */
    zMetaInfo zSubMetaIf;
    _ui zVecDataLen, zBaseDataLen, zNodeCnter, zLineCnter;
    zMetaInfo *zpRootNodeIf, *zpTmpNodeIf[3];  // [0]：本体    [1]：记录父节点    [2]：记录兄长节点
    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1] = {{.RepoId = zpMetaIf->RepoId}};  // 使用项目内存池

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
        }
    }
    zReg_Free_Metasource(zRegInitIf);
    pclose(zpShellRetHandler);

    if (NULL == zpRootNodeIf) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = NULL;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec));

        zSubMetaIf.OpsId = 0;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;
        zSubMetaIf.CommitId = zpMetaIf->CommitId;
        zSubMetaIf.FileId = -1;  // 置为 -1，不允许再查询下一级内容
        zSubMetaIf.CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zSubMetaIf.DataType = zpMetaIf->DataType;
        zSubMetaIf.p_data = (0 == strcmp(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId))) ? "===> 最新的已布署版本 <===" : "=> 无差异 <=";
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

        /* Tree 图 */
        zdistribute_task(zpRootNodeIf);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zRefDataInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(struct iovec));

        for (_ui zCnter = 0; zCnter < zLineCnter; zCnter++) {
            zconvert_struct_to_json_str(zCommonBuf, zpRootNodeIf->pp_ResHash[zCnter]); /* 将 zMetaInfo 转换为 json 文本 */

            zVecDataLen = strlen(zCommonBuf);
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[zCnter].iov_len = zVecDataLen;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[zCnter].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[zCnter].iov_base, zCommonBuf, zVecDataLen);

            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[zCnter].p_data = zpRootNodeIf->pp_ResHash[zCnter]->p_ExtraData;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[zCnter].p_SubVecWrapIf = NULL;
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
zgenerate_cache(void *zpParam) {
    zMetaInfo *zpMetaIf, zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    zpMetaIf = (zMetaInfo *)zpParam;

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + 12;
    char zCommonBuf[zMaxBufLen];

    FILE *zpShellRetHandler;
    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        sprintf(zCommonBuf, "cd \"%s\" && git log server%d --format=\"%%H_%%ct\"", zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zpMetaIf->RepoId); // 取 server 分支的提交记录
        zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->SortedDpVecWrapIf);
        // 调用外部命令 cat，而不是用 fopen 打开，如此可用统一的 pclose 关闭
        sprintf(zCommonBuf, "cat \"%s\"\"%s\"", zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zDpSigLogPath);
        zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    /* 第一行单独处理，避免后续每次判断是否是第一行 */
    zCnter = 0;
    if (NULL != zget_one_line(zCommonBuf, zGlobBufSiz, zpShellRetHandler)) {
        /* 只提取比最近一次布署版本更新的提交记录 */
        if ((zIsCommitDataType == zpMetaIf->DataType)
                && (0 == (strncmp(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
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
                    && (0 == (strncmp(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
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
            zSubMetaIf.CacheId =  zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;
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
            strcpy(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zpTopVecWrapIf->p_RefDataIf[zCnter - 1].p_data);
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
 *         -35：
 *         -36：请求创建的项目路径已存在，且项目ID不同
 *         -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *         -38：拉取远程代码库失败（git clone 失败）
 *         -39：项目元数据创建失败，如：无法打开或创建布署日志文件meta等原因
 */
#define zFree_Source() do {\
    free(zpGlobRepoIf[zRepoId]->p_RepoPath);\
    free(zpGlobRepoIf[zRepoId]);\
    zpGlobRepoIf[zRepoId] = NULL;\
    zReg_Free_Tmpsource(zRegResIf);\
    zPrint_Time();\
} while(0)

_i
zinit_one_repo_env(char *zpRepoMetaData) {
    zRegInitInfo zRegInitIf[2];
    zRegResInfo zRegResIf[2] = {{.RepoId = -1}, {.RepoId = -1}};  // 使用系统 *alloc 函数分配内存

    _i zRepoId, zErrNo;

    /* 正则匹配项目基本信息（5个字段） */
    zreg_compile(zRegInitIf, "(\\w|[[:punct:]])+");
    zreg_match(zRegResIf, zRegInitIf, zpRepoMetaData);
    zReg_Free_Metasource(zRegInitIf);
    if (5 > zRegResIf->cnt) {
        zReg_Free_Tmpsource(zRegResIf);
        zPrint_Time();
        return -34;
    }

    /* 提取项目ID，调整 zGlobMaxRepoId */
    zRepoId = strtol(zRegResIf->p_rets[0], NULL, 10);
    if ((zGlobRepoIdLimit > zRepoId) && (0 < zRepoId)) {} else {
        zReg_Free_Tmpsource(zRegResIf);
        zPrint_Time();
        return -32;
    }

    if (NULL != zpGlobRepoIf[zRepoId]) {
        zReg_Free_Tmpsource(zRegResIf);
        zPrint_Time();
        return -35;
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zpGlobRepoIf[zRepoId], zRepoInfo, 1);
    zpGlobRepoIf[zRepoId]->RepoId = zRepoId;
    zpGlobRepoIf[zRepoId]->SelfPushMark = (6 == zRegResIf->cnt) ? 1 : 0;

    /* 提取项目绝对路径，结果格式：/home/git/`dirname($Path_On_Host)`/.____DpSystem/`basename($Path_On_Host)` */
    zreg_compile(zRegInitIf + 1, "[^/]+[/]*$");
    zreg_match(zRegResIf + 1, zRegInitIf + 1, zRegResIf->p_rets[1]);
    zReg_Free_Metasource(zRegInitIf + 1);
    /* 去掉 basename 部分 */
    zRegResIf->p_rets[1][zRegResIf->ResLen[1] - (zRegResIf + 1)->ResLen[0]] = '\0';
    /* 拼接结果字符串 */
    while ('/' == zRegResIf->p_rets[1][0]) { zRegResIf->p_rets[1]++; }  // 去除多余的 '/'
    zMem_Alloc(zpGlobRepoIf[zRepoId]->p_RepoPath, char, sizeof("/home/git/.____DpSystem/") + zRegResIf->ResLen[1]);
    zpGlobRepoIf[zRepoId]->RepoPathLen = sprintf(zpGlobRepoIf[zRepoId]->p_RepoPath, "%s%s%s%s", "/home/git/", zRegResIf->p_rets[1], ".____DpSystem/", (zRegResIf + 1)->p_rets[0]);
    zReg_Free_Tmpsource(zRegResIf + 1);

    /* 取出本项目所在路径的最大路径长度（用于度量 git 输出的差异文件相对路径长度） */
    zpGlobRepoIf[zRepoId]->MaxPathLen = pathconf(zpGlobRepoIf[zRepoId]->p_RepoPath, _PC_PATH_MAX);

    /* 调用SHELL执行检查和创建 */
    char zCommonBuf[zGlobBufSiz + zpGlobRepoIf[zRepoId]->RepoPathLen];
    sprintf(zCommonBuf, "sh -x ${zGitShadowPath}/tools/zmaster_init_repo.sh \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", zRegResIf->p_rets[0], zpGlobRepoIf[zRepoId]->p_RepoPath + 9, zRegResIf->p_rets[2], zRegResIf->p_rets[3], zRegResIf->p_rets[4]);

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
    sprintf(zPathBuf, "%s%s", zpGlobRepoIf[zRepoId]->p_RepoPath, zDpSigLogPath);
    zpGlobRepoIf[zRepoId]->DpSigLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zpGlobRepoIf[zRepoId]->p_RepoPath, zDpTimeSpentLogPath);
    zpGlobRepoIf[zRepoId]->DpTimeSpentLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    if ((-1 == zpGlobRepoIf[zRepoId]->DpSigLogFd) || (-1 == zpGlobRepoIf[zRepoId]->DpTimeSpentLogFd)) {
        close(zpGlobRepoIf[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -39;
    }

    /* 检测并生成项目代码定期更新命令 */
    char zPullCmdBuf[zGlobBufSiz];
    if (0 == strcmp("git", zRegResIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && rm -f .git/index.lock; git pull --force \"%s\" \"%s\":server%d",
                zpGlobRepoIf[zRepoId]->p_RepoPath,
                zRegResIf->p_rets[2],
                zRegResIf->p_rets[3],
                zRepoId);
    } else if (0 == strcmp("svn", zRegResIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; rm -f .git/index.lock; svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server%d",
                zpGlobRepoIf[zRepoId]->p_RepoPath,
                zRepoId);
    } else {
        close(zpGlobRepoIf[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -37;
    }

    zMem_Alloc(zpGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zpGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 清理资源占用 */
    zReg_Free_Tmpsource(zRegResIf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zpGlobRepoIf[zRepoId]->p_MemPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Time();
        fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
        exit(1);
    }
    void **zppPrev = zpGlobRepoIf[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    zpGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepoIf[zRepoId]->MemLock), NULL) );

    /* 布署重试锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepoIf[zRepoId]->DpRetryLock), NULL) );

    /* libssh2 并发锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepoIf[zRepoId]->DpSyncLock), NULL) );
    zCheck_Pthread_Func_Exit( pthread_cond_init(&(zpGlobRepoIf[zRepoId]->DpSyncCond), NULL) );

    /* 为每个代码库生成一把读写锁 */
    zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zpGlobRepoIf[zRepoId]->RwLock), NULL) );
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_init(&(zpGlobRepoIf[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_setkind_np(&(zpGlobRepoIf[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zpGlobRepoIf[zRepoId]->RwLock), &(zpGlobRepoIf[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_destroy(&(zpGlobRepoIf[zRepoId]->zRWLockAttr)));

    /* 读写锁生成之后，立刻拿写锁 */
    pthread_rwlock_wrlock(&(zpGlobRepoIf[zRepoId]->RwLock));

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepoIf[zRepoId]->ReplyCntLock, NULL));
    /* 用于保证 "git pull" 原子性拉取的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepoIf[zRepoId]->PullLock, NULL));

    /* 布署并发流量控制 */
    zCheck_Negative_Exit( sem_init(&(zpGlobRepoIf[zRepoId]->DpTraficControl), 0, zDpTraficLimit) );

    /* 缓存版本初始化 */
    zpGlobRepoIf[zRepoId]->CacheId = 1000000000;
    /* 上一次布署结果状态初始化 */
    zpGlobRepoIf[zRepoId]->RepoState = zRepoGood;

    /* 提取最近一次布署的SHA1 sig，日志文件不会为空，初创时即会以空库的提交记录作为第一条布署记录 */
    sprintf(zCommonBuf, "cat %s%s | tail -1", zpGlobRepoIf[zRepoId]->p_RepoPath, zDpSigLogPath);
    FILE *zpShellRetHandler;
    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    if (zBytes(40) != zget_str_content(zpGlobRepoIf[zRepoId]->zLastDpSig, zBytes(40), zpShellRetHandler)) {
        zpGlobRepoIf[zRepoId]->zLastDpSig[40] = '\0';
    }
    pclose(zpShellRetHandler);

    /* 指针指向自身的静态数据项 */
    zpGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zpGlobRepoIf[zRepoId]->CommitVecIf;
    zpGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zpGlobRepoIf[zRepoId]->CommitRefDataIf;
    zpGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zpGlobRepoIf[zRepoId]->CommitVecIf;  // 提交记录总是有序的，不需要再分配静态空间

    zpGlobRepoIf[zRepoId]->DpVecWrapIf.p_VecIf = zpGlobRepoIf[zRepoId]->DpVecIf;
    zpGlobRepoIf[zRepoId]->DpVecWrapIf.p_RefDataIf = zpGlobRepoIf[zRepoId]->DpRefDataIf;
    zpGlobRepoIf[zRepoId]->SortedDpVecWrapIf.p_VecIf = zpGlobRepoIf[zRepoId]->SortedDpVecIf;

    zpGlobRepoIf[zRepoId]->p_DpCcurIf = zpGlobRepoIf[zRepoId]->DpCcurIf;

    /* 生成缓存 */
    zMetaInfo zMetaIf;
    zMetaIf.RepoId = zRepoId;

    zMetaIf.DataType = zIsCommitDataType;
    zgenerate_cache(&zMetaIf);

    zMetaIf.DataType = zIsDpDataType;
    zgenerate_cache(&zMetaIf);

    /* 全局 libgit2 Handler 初始化 */
    zCheck_Null_Exit( zpGlobRepoIf[zRepoId]->p_GitRepoHandler = zgit_env_init(zpGlobRepoIf[zRepoId]->p_RepoPath) );  // 目标库

    /* 放锁 */
    pthread_rwlock_unlock(&(zpGlobRepoIf[zRepoId]->RwLock));

    /* 标记初始化动作已全部完成 */
    zpGlobRepoIf[zRepoId]->zInitRepoFinMark = 1;

    /* 全局实际项目 ID 最大值调整 */
    pthread_mutex_lock(&zGlobCommonLock);
    zGlobMaxRepoId = zRepoId > zGlobMaxRepoId ? zRepoId : zGlobMaxRepoId;
    pthread_mutex_unlock(&zGlobCommonLock);

    return 0;
}
#undef zFree_Source


/* 用于线程并发执行的外壳函数 */
void *
zinit_one_repo_env_thread_wraper(void *zpParam) {
    char *zpOrigStr = ((char *) zpParam) + sizeof(void *);
    _i zErrNo;
    if (0 > (zErrNo = zinit_one_repo_env(zpOrigStr))) {
        fprintf(stderr, "[zinit_one_repo_env] ErrNo: %d\n", zErrNo);
    }

    return NULL;
}


#ifndef _Z_BSD
/* 定时获取系统全局负载信息 */
void *
zsys_load_monitor(void *zpParam) {
    _ul zTotalMem, zAvalMem;
    FILE *zpHandler;

    zCheck_Null_Exit( zpHandler = fopen("/proc/meminfo", "r") );

    while(1) {
        fscanf(zpHandler, "%*s %ld %*s %*s %*ld %*s %*s %ld", &zTotalMem, &zAvalMem);
        zGlobMemLoad = 100 * (zTotalMem - zAvalMem) / zTotalMem;
        fseek(zpHandler, 0, SEEK_SET);

        /*
         * 此处不拿锁，直接通知，否则锁竞争太甚
         * 由于是无限循环监控任务，允许存在无效的通知
         * 工作线程等待在 80% 的水平线上，此处降到 70% 才通知
         */
        if (70 > zGlobMemLoad) { pthread_cond_signal(&zSysLoadCond); }

        zsleep(0.1);
    }
    return zpParam;  // 消除编译警告信息
}
#endif


/* 读取项目信息，初始化配套环境 */
void *
zinit_env(const char *zpConfPath) {
    FILE *zpFile = NULL;
    static char zConfBuf[zGlobRepoNumLimit][zGlobBufSiz];  // 预置 128 个静态缓存区
    char zCpuNumBuf[8];
    _i zCnter = 0;

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
    while (zGlobRepoNumLimit > zCnter) {
        if (NULL == zget_one_line(zConfBuf[zCnter] + sizeof(void *), zGlobBufSiz, zpFile)) {
            goto zMarkFin;
        } else {
            zAdd_To_Thread_Pool(zinit_one_repo_env_thread_wraper, zConfBuf[zCnter++]);
        }
    }

    /* 若代码为数量超过可以管理的上限，报错退出 */
    zPrint_Err(0, NULL, "代码库数量超出上限，布署系统已退出");
    exit(1);

zMarkFin:
    fclose(zpFile);

#ifndef _Z_BSD
//    zpFile = NULL;
//    zCheck_Null_Exit( zpFile = popen("cat /proc/cpuinfo | grep -c 'processor[[:blank:]]\\+:'", "r") );
//    zCheck_Null_Exit( zget_one_line(zCpuNumBuf, 8, zpFile) );
//    zSysCpuNum = strtol(zCpuNumBuf, NULL, 10);
//    fclose(zpFile);

    zAdd_To_Thread_Pool(zsys_load_monitor, NULL);
#endif

    return NULL;
}


/* 去除json标识符:  ][}{\",:  */
void
zclear_json_identifier(char *zpStr, _i zStrLen) {
    char zDb[256] = {0};
    zDb['['] = 1;
    zDb[']'] = 1;
    zDb['{'] = 1;
    zDb['}'] = 1;
    zDb[','] = 1;
    zDb[':'] = 1;
    zDb['\"'] = 1;

    for (_i zCnter = 0; zCnter < zStrLen; zCnter++) {
        if (1 == zDb[(_i)zpStr[zCnter]]) {
            zpStr[zCnter] = '=';
        }
    }
}
