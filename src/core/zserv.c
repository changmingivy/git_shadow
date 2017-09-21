#ifndef _Z
    #include "../zmain.c"
#endif

/***********
 * NET OPS *
 ***********/
/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if ((0 > zpMetaIf->CommitId)\
            || ((zCacheSiz - 1) < zpMetaIf->CommitId)\
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_data)) {\
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zPrint_Err(0, NULL, "CommitId 不存在或内容为空（空提交）");\
        return -3;\
    }\
} while(0)

/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMetaIf->FileId)\
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf)\
            || ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) < zpMetaIf->FileId)) {\
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zPrint_Err(0, NULL, "差异文件ID不存在");\
        return -4;\
    }\
} while(0)

/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zppGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) {\
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zPrint_Err(0, NULL, "前端发送的缓存ID已失效");\
        return -8;\
    }\
} while(0)

/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDpLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock) {\
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        return -6;\
    }\
} while(0)

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(zMetaInfo *zpMetaIf, _i zSd) {
    _i zErrNo;
    if (0 == (zErrNo = zinit_one_repo_env(zpMetaIf->p_data))) {
        zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    }

    return zErrNo;
}

/*
 * 7：重置指定项目为原始状态（删除所有主机上的所有项目文件，保留中控机上的 _SHADOW 元文件）
 */
_i
zreset_repo(zMetaInfo *zpMetaIf, _i zSd) {
    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1];

    zreg_compile(zRegInitIf, "([0-9]{1,3}\\.){3}[0-9]{1,3}");
    zreg_match(zRegResIf, zRegInitIf, zpMetaIf->p_data);
    zreg_free_metasource(zRegInitIf);

    if (strtol(zpMetaIf->p_ExtraData, NULL, 10) != zRegResIf->cnt) {
        zreg_free_tmpsource(zRegResIf);
        return -28;
    }

    _i zOffSet = 0;
    for (_i zCnter = 0; zCnter < zRegResIf->cnt; zCnter++) {
        strcpy(zpMetaIf->p_data + zOffSet, zRegResIf->p_rets[zCnter]);
        zOffSet += 1 + zRegResIf->ResLen[zCnter];
        zpMetaIf->p_data[zOffSet - 1] = ' ';
    }
    if (0 < zOffSet) { zpMetaIf->p_data[zOffSet - 1] = '\0'; }
    else { zpMetaIf->p_data[0] = '\0'; }
    zreg_free_tmpsource(zRegResIf);

    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    /* 检查中转机 IPv4 存在性 */
    if ('\0' == zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr[0]) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -25;
    }

    /* 生成待执行的外部动作指令 */
    char zShellBuf[zCommonBufSiz];
    sprintf(zShellBuf, "sh -x %s_SHADOW/tools/zreset_repo.sh %s %s %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 指定代码库的绝对路径
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符
            zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr,
            zpMetaIf->p_data);  // 集群主机的点分格式文本 IPv4 列表

    /* 执行动作，清理本地及所有远程主机上的项目文件，system返回值是wait状态，不是错误码，错误码需要用WEXITSTATUS宏提取 */
    if (255 == WEXITSTATUS( system(zShellBuf)) ) {  // 中转机清理动作出错会返回 255 错误码，其它机器暂不处理错误返回
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -60;
    }

    /* 中转机元数据重置 */
    zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr[0] = '\0';

    /* 目标机元数据重置 */
    memset(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf, 0, zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost * sizeof(zDpResInfo));
    memset(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, 0, zDpHashSiz * sizeof(zDpResInfo *));

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    return 0;
}

/*
 * 存在较大风险，暂不使用!!!!
 * 13：删除项目（代码库）
 */

/* 删除项目与拉取远程代码两个动作需要互斥执行 */
//pthread_mutex_t zDestroyLock = PTHREAD_MUTEX_INITIALIZER;

//_i
//zdelete_repo(zMetaInfo *zpMetaIf, _i zSd) {
//    _i zErrNo;
//    char zShellBuf[zCommonBufSiz];
//
//    /* 取 Destroy 锁 */
//    pthread_mutex_lock(&zDestroyLock);
//
//    /* 
//     * 取项目写锁
//     * 元数据指针置为NULL
//     * 销毁读写锁
//     */
//    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
//    zRepoInfo *zpRepoIf = zppGlobRepoIf[zpMetaIf->RepoId];
//    zppGlobRepoIf[zpMetaIf->RepoId] = NULL;
//    pthread_rwlock_unlock(&(zpRepoIf->RwLock));
//    pthread_rwlock_destroy(&(zpRepoIf->RwLock));
//
//    /* 生成待执行的外部动作指令 */
//    sprintf(zShellBuf, "sh -x %s_SHADOW/tools/zdelete_repo.sh %s %s %s",
//            zpRepoIf->p_RepoPath,  // 指定代码库的绝对路径
//            zpRepoIf->p_RepoPath + 9,  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符
//            zpRepoIf->ProxyHostStrAddr,
//            NULL == zpRepoIf->p_HostStrAddrList ? "" : zpRepoIf->p_HostStrAddrList);  // 集群主机的点分格式文本 IPv4 列表
//
//    /* 执行动作，清理本地及所有远程主机上的项目文件，system返回值是wait状态，不是错误码，错误码需要用WEXITSTATUS宏提取 */
//    zErrNo = WEXITSTATUS(system(zShellBuf));
//
//    /* 清理该项目占用的资源 */
//    void **zppPrev = zpRepoIf->p_MemPool;
//    do {
//        zppPrev = zppPrev[0];
//        munmap(zpRepoIf->p_MemPool, zMemPoolSiz);
//        zpRepoIf->p_MemPool = zppPrev;
//    } while(NULL != zpRepoIf->p_MemPool);
//
//    free(zpRepoIf->p_RepoPath);
//    free(zpRepoIf);
//
//    /* 放 Destroy 锁 */
//    pthread_mutex_unlock(&zDestroyLock);
//
//    if (0 != zErrNo) {
//        return -16;
//    } else {
//        zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
//        return 0;
//    }
//}

/*
 * 5：显示所有项目及其元信息
 * 6：显示单个项目及其元信息
 */
_i
zshow_all_repo_meta(zMetaInfo *zpMetaIf, _i zSd) {
    char zSendBuf[zCommonBufSiz];

    zsendto(zSd, "[", zBytes(1), 0, NULL);  // 凑足json格式
    for(_i zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
        if (NULL == zppGlobRepoIf[zCnter] || 0 == zppGlobRepoIf[zCnter]->zInitRepoFinMark) { continue; }

        if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zCnter]->RwLock))) {
            sprintf(zSendBuf, "{\"OpsId\":-11,\"data\":\"Id: %d\"},", zCnter);
            zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);
            continue;
        };

        sprintf(zSendBuf, "{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nProxyHostIp: %s\nTotalHost: %d\nHostIPs: %s\"},",
                zCnter,
                zppGlobRepoIf[zCnter]->p_RepoPath,
                zDpLocked == zppGlobRepoIf[zCnter]->DpLock ? "No" : "Yes",
                '\0' == zppGlobRepoIf[zCnter]->zLastDpSig[0] ? "_" : zppGlobRepoIf[zCnter]->zLastDpSig,
                zRepoDamaged == zppGlobRepoIf[zCnter]->RepoState ? "fail" : "success",
                zppGlobRepoIf[zCnter]->ProxyHostStrAddr,
                zppGlobRepoIf[zCnter]->TotalHost,
                NULL == zppGlobRepoIf[zCnter]->p_HostStrAddrList ? "_" : zppGlobRepoIf[zCnter]->p_HostStrAddrList
                );
        zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

        pthread_rwlock_unlock(&(zppGlobRepoIf[zCnter]->RwLock));
    }

    zsendto(zSd, "{\"OpsId\":0,\"data\":\"__END__\"}]", sizeof("{\"OpsId\":0,\"data\":\"__END__\"}]") - 1, 0, NULL);  // 凑足json格式，同时防止内容为空时，前端无法解析
    return 0;
}

/*
 * 6：显示单个项目及其元信息
 */
_i
zshow_one_repo_meta(zMetaInfo *zpIf, _i zSd) {
    zMetaInfo *zpMetaIf = (zMetaInfo *) zpIf;
    char zSendBuf[zCommonBufSiz];

    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) { return -11; };

    sprintf(zSendBuf, "[{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nProxyHostIp: %s\nTotalHost: %d\nHostIPs: %s\"}]",
            zpMetaIf->RepoId,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zDpLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock ? "No" : "Yes",
            '\0' == zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig[0] ? "_" : zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zRepoDamaged == zppGlobRepoIf[zpMetaIf->RepoId]->RepoState ? "fail" : "success",
            zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr,
            zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost,
            NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList ? "_" : zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList
            );
    zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 全量刷新：只刷新版本号列表
 * 需要继承下层已存在的缓存
 */
_i
zrefresh_cache(zMetaInfo *zpMetaIf) {
//    _i zCnter[2];
//    struct iovec zOldVecIf[zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz];
//    zRefDataInfo zOldRefDataIf[zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz];
//
//    for (zCnter[0] = 0; zCnter[0] < zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz; zCnter[0]++) {
//        zOldVecIf[zCnter[0]].iov_base = zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[0]].iov_base;
//        zOldVecIf[zCnter[0]].iov_len = zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[0]].iov_len;
//        zOldRefDataIf[zCnter[0]].p_data  = zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[0]].p_data;
//        zOldRefDataIf[zCnter[0]].p_SubVecWrapIf = zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[0]].p_SubVecWrapIf;
//    }

    zgenerate_cache(zpMetaIf);  // 复用了 zops_route 函数传下来的 MetaInfo 结构体(栈内存)

//    zCnter[1] = zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz;
//    if (zCnter[1] > zCnter[0]) {
//        for (zCnter[0]--, zCnter[1]--; zCnter[0] >= 0; zCnter[0]--, zCnter[1]--) {
//            if (NULL == zOldRefDataIf[zCnter[0]].p_SubVecWrapIf) { continue; }
//            if (NULL == zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_SubVecWrapIf) { break; }  // 若新内容为空，说明已经无法一一对应，后续内容无需再比较
//            if (0 == (strcmp(zOldRefDataIf[zCnter[0]].p_data, zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_data))) {
//                zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[1]].iov_base = zOldVecIf[zCnter[0]].iov_base;
//                zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[1]].iov_len = zOldVecIf[zCnter[0]].iov_len;
//                zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_SubVecWrapIf = zOldRefDataIf[zCnter[0]].p_SubVecWrapIf;
//            } else {
//                break;  // 若不能一一对应，则中断
//            }
//        }
//    }

    return 0;
}

/*
 * 7：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpSortedTopVecWrapIf;

    if (0 > pthread_rwlock_trywrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) { return -11; };

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        /*
         * 如果该项目被标记为被动拉取模式（相对的是主动推送模式），则：
         *     若距离最近一次 “git pull“ 的时间间隔超过 10 秒，尝试拉取远程代码
         *     放在取得读写锁之后执行，防止与布署过程中的同类运作冲突
         *     取到锁，则拉取；否则跳过此步，直接打印列表
         *     打印布署记录时不需要执行
         */
        if (10 < (time(NULL) - zppGlobRepoIf[zpMetaIf->RepoId]->LastPullTime)) {
            if ((0 == zppGlobRepoIf[zpMetaIf->RepoId]->SelfPushMark)
                    && (0 == pthread_mutex_trylock( &(zppGlobRepoIf[zpMetaIf->RepoId]->PullLock))) ) {

                system(zppGlobRepoIf[zpMetaIf->RepoId]->p_PullCmd);
                zppGlobRepoIf[zpMetaIf->RepoId]->LastPullTime = time(NULL); /* 以取完远程代码的时间重新赋值 */

                char zShellBuf[zCommonBufSiz];
                FILE *zpShellRetHandler;

                sprintf(zShellBuf, "cd %s && git log server -1 --format=%%H", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
                zpShellRetHandler = popen(zShellBuf, "r");
                zget_str_content(zShellBuf, zBytes(40), zpShellRetHandler);
                pclose(zpShellRetHandler);

                if ((NULL == zppGlobRepoIf[zpMetaIf->RepoId]->CommitRefDataIf[0].p_data)
                        || (0 != strncmp(zShellBuf, zppGlobRepoIf[zpMetaIf->RepoId]->CommitRefDataIf[0].p_data, 40))) {
                    zpMetaIf->DataType = zIsCommitDataType;
                    zrefresh_cache(zpMetaIf);
                }

                pthread_mutex_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->PullLock));
            }
        }
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDpVecWrapIf);
    } else {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -10;
    }

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrapIf->VecSiz) {
        if (0 < zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL)) {
            zsendto(zSd, "]", zBytes(1), 0, NULL);  // 二维json结束符
        } else {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -70;
        }
    }

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 10：显示差异文件路径列表
 */
_i
zprint_diff_files(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpTopVecWrapIf, zSendVecWrapIf;
    _i zSplitCnt;

    /* 若上一次布署是部分失败的，返回 -13 错误 */
    if (zRepoDamaged == zppGlobRepoIf[zpMetaIf->RepoId]->RepoState) { return -13; }

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpMetaIf->DataType = zIsDpDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    /* get rdlock */
    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) { return -11; }

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        if ((void *) -1 == zget_file_list(zpMetaIf)) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -11;
        }
    }

    zSendVecWrapIf.VecSiz = 0;
    zSendVecWrapIf.p_VecIf = zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf;
    zSplitCnt = (zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrapIf.VecSiz = (zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrapIf.VecSiz = zSendUnitSiz;
        }

        zsendmsg(zSd, &zSendVecWrapIf, 0, NULL);
        zSendVecWrapIf.p_VecIf += zSendVecWrapIf.VecSiz;
    }
    zsendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 11：显示差异文件内容
 */
_i
zprint_diff_content(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpTopVecWrapIf, zSendVecWrapIf;
    _i zSplitCnt;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpMetaIf->DataType = zIsDpDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) { return -11; };

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        if ((void *) -1 == zget_file_list(zpMetaIf)) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -11;
        }
    }

    zCheck_FileId();  // 宏内部会解锁
    if (NULL == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)) {
        if ((void *) -1 == zget_diff_content(zpMetaIf)) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -11;
        }
    }

    zSendVecWrapIf.VecSiz = 0;
    zSendVecWrapIf.p_VecIf = zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf;
    zSplitCnt = (zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrapIf.VecSiz = (zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz - 1) % zSendUnitSiz + 1;
        }
        else {
            zSendVecWrapIf.VecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zsendmsg(zSd, &zSendVecWrapIf, 0, NULL);
        zSendVecWrapIf.p_VecIf += zSendVecWrapIf.VecSiz;
    }

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 4：更新中转机 IPv4
 */
_i
zupdate_ipv4_db_proxy(zMetaInfo *zpMetaIf, _i zSd) {
    if (NULL == zpMetaIf->p_data || zBytes(15) < strlen(zpMetaIf->p_data) || zBytes(7) > strlen(zpMetaIf->p_data)) { return -22; }
    if (0 == strcmp(zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr, zpMetaIf->p_data)) { goto zMark; }

    char zShellBuf[zCommonBufSiz];
    sprintf(zShellBuf, "sh -x %s_SHADOW/tools/zhost_init_repo_proxy.sh \"%s\" \"%s\"",  // $1:MajorHostAddr；$2:PathOnHost
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpMetaIf->p_data,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9);  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符

    /* 此处取读锁权限即可，因为只需要排斥布署动作，并不影响查询类操作 */
    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) { return -11; }

    /* system 返回值是 waitpid 状态，不是错误码，错误码需要用 WEXITSTATUS 宏提取 */
    if (0 != WEXITSTATUS(system(zShellBuf))) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -27;
    }

    strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr, zpMetaIf->p_data);

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

zMark:
    zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    return 0;
}

/*
 * 注：完全内嵌于 zdeploy() 中，不再需要读写锁
 */
_i
zupdate_ipv4_db_all(zMetaInfo *zpMetaIf) {
    zMetaInfo *zpSubMetaIf;
    zDpResInfo *zpOldDpResListIf, *zpTmpDpResIf, *zpOldDpResHashIf[zDpHashSiz];
    _ui zOffSet = 0;

    if (NULL == zpMetaIf->p_ExtraData) {
        zpMetaIf->p_data = NULL;
        return -24;
    }

    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1];
    zreg_compile(zRegInitIf , "([0-9]{1,3}\\.){3}[0-9]{1,3}");
    zreg_match(zRegResIf, zRegInitIf, zpMetaIf->p_data);
    zreg_free_metasource(zRegInitIf);

    if (strtol(zpMetaIf->p_ExtraData, NULL, 10) != zRegResIf->cnt) {
        zreg_free_tmpsource(zRegResIf);
        return -28;
    }

    /* 用于统计分析初始化远程主机的耗时，文件：Init_Remote_Host.TimeCnt */
    strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, "Init_Remote_Host");

    /* 检测上一次的内存是否需要释放 */
    if (zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList != &(zppGlobRepoIf[zpMetaIf->RepoId]->HostStrAddrList[0])) {
        free(zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList);
    }

    if (zForecastedHostNum < zRegResIf->cnt) {
        /* 若指定的目标主机数量大于预测的主机数量，则另行分配内存 */
        /* 加空格最长16字节，如："123.123.123.123 " */
        zMem_Alloc(zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList, char, 16 * zRegResIf->cnt);
    } else {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList = zppGlobRepoIf[zpMetaIf->RepoId]->HostStrAddrList;
    }

    /* 更新项目目标主机总数 */
    zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost = zRegResIf->cnt;

    /* 暂留旧数据 */
    zpOldDpResListIf = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf;
    memcpy(zpOldDpResHashIf, zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, zDpHashSiz * sizeof(zDpResInfo *));

    /* 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配 */
    zMem_Alloc(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf, zDpResInfo, zRegResIf->cnt);

    /* 重置状态 */
    memset(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, 0, zDpHashSiz * sizeof(zDpResInfo *));  /* Clear hash buf before reuse it!!! */
    zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt = 0;
    zppGlobRepoIf[zpMetaIf->RepoId]->DpStartTime = time(NULL);

    /* 并发同步环境初始化 */
    zCcur_Init(zpMetaIf->RepoId, zRegResIf->cnt, A);
    for (_i zCnter = 0; zCnter < zRegResIf->cnt; zCnter++) {
        /* 检测是否是最后一次循环 */
        zCcur_Fin_Mark((zRegResIf->cnt - 1) == zCnter, A);

        /* 线性链表斌值；转换字符串点分格式 IPv4 为 _ui 型 */
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr = zconvert_ipv4_str_to_bin(zRegResIf->p_rets[zCnter]);
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState = -1;
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].p_next = NULL;

        /* 更新HASH */
        zpTmpDpResIf = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDpHashSiz];
        if (NULL == zpTmpDpResIf) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDpHashSiz]
                = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        } else {
            while (NULL != zpTmpDpResIf->p_next) { zpTmpDpResIf = zpTmpDpResIf->p_next; }
            zpTmpDpResIf->p_next = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        }

        zpTmpDpResIf = zpOldDpResHashIf[zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr % zDpHashSiz];
        while (NULL != zpTmpDpResIf) {
            /* 若 IPv4 address 已存在，则跳过初始化远程主机的环节 */
            if (zpTmpDpResIf->ClientAddr == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) {
                /* 先前已被初始化过的主机，状态置1，防止后续收集结果时误报失败，同时计数递增 */
                zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState = 1;
                pthread_mutex_lock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));
                zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt++;
                pthread_mutex_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));

                /* 每次条件式跳过时，都必须让同步计数器递减一次 */
                zCcur_Cnter_Subtract(A);
                goto zMark;
            }
            zpTmpDpResIf = zpTmpDpResIf->p_next;
        }

        zpSubMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));
        /* 调度一个新的线程执行初始化远程主机的任务 */
        zCcur_Sub_Config(zpSubMetaIf, A);
        /* 仅需要 RepoId 与 HostId 两个字段 */
        zpSubMetaIf->RepoId = zpMetaIf->RepoId;
        zpSubMetaIf->HostId = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr;

        zAdd_To_Thread_Pool(zinit_one_remote_host, zpSubMetaIf);
zMark:
        /*
         * 非定长字符串不好动态调整，因此无论是否已存在都要执行
         * 生成将要传递给布署脚本的参数：空整分隔的字符串形式的 IPv4 列表
         */
        strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList+ zOffSet, zRegResIf->p_rets[zCnter]);
        zOffSet += 1 + zRegResIf->ResLen[zCnter];
        zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[zOffSet - 1] = ' ';
    }

    if (0 < zOffSet) {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[zOffSet - 1] = '\0';
    } else {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[0] = '\0';
    }

    if (NULL != zpOldDpResListIf) { free(zpOldDpResListIf); }

    /* 初始化远端新主机可能耗时较长，因此在更靠后的位置等待信号，以防长时间阻塞其它操作 */
    zCcur_Wait(A);

    if (zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        char *zpBasePtr, zIpv4StrAddrBuf[INET_ADDRSTRLEN];
        zpMetaIf->p_data[0] = '\0';
        zOffSet = 0;
        zpBasePtr = zpMetaIf->p_data;
        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        for (_i zCnter = 0, zUnReplyCnt = 0; zCnter < zRegResIf->cnt; zCnter++) {
            if (-1 == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState) {
                zconvert_ipv4_bin_to_str(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr, zIpv4StrAddrBuf);
                zpBasePtr += sprintf(zpBasePtr, "%s,", zIpv4StrAddrBuf);  // sprintf 将返回除 ‘\0’ 之外的字符总数，与 strlen() 取得的值相同
                zUnReplyCnt++;

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行；同时主机总数递减 */
                zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr = 0;
                zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost--;
            } else {
                /* 此处重新生成有效的全量主机IP地址字符串，过滤掉失败的部分 */
                strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList+ zOffSet, zRegResIf->p_rets[zCnter]);
                zOffSet += 1 + zRegResIf->ResLen[zCnter];
                zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[zOffSet - 1] = ' ';
            }
        }
        if (zpBasePtr > zpMetaIf->p_data) { (--zpBasePtr)[0] = '\0'; }  // 若至少取到一个值，则需要去掉最后一个逗号

        if (0 < zOffSet) {
            zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[zOffSet - 1] = '\0';
        } else {
            zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[0] = '\0';
        }

        zreg_free_tmpsource(zRegResIf);
        return -23;
    } else {
        zreg_free_tmpsource(zRegResIf);
        return 0;
    }
}

/*
 * 实际的布署函数，由外壳函数调用
 * 12：布署／撤销
 * 13：新加入的主机请求布署自身
 */
_i
zdeploy(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpTopVecWrapIf;
    _i zErrNo;

    if (zIsCommitDataType == zpMetaIf->DataType) { zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf); }
    else if (zIsDpDataType == zpMetaIf->DataType) { zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf); }
    else { return -10; }

    /* 检查是否允许布署 */
    if (zDpLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock) { return -6; }
    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zppGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) { return -8; }
    /* 检查指定的版本号是否有效 */
    if ((0 > zpMetaIf->CommitId)
            || ((zCacheSiz - 1) < zpMetaIf->CommitId)
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_data)) { 
        return -3;
    }

    /* 检查中转机 IPv4 存在性 */
    if ('\0' == zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr[0]) { return -25; }

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMetaIf->p_data[0]) {
        if (0 > (zErrNo = zupdate_ipv4_db_all(zpMetaIf))) { return zErrNo; }
    }

    /* 检查部署目标主机集合是否存在 */
    if (0 == zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost
            || NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList
            || '\0' == zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList[0]) {
        return -26;
    }

    /* 正在布署的版本号，用于布署耗时分析 */
    strncpy(zppGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zBytes(41));

    /* 执行外部脚本使用 git 进行布署；因为要传递给新线程执行，故而不能用栈内存 */
    char *zpShellBuf = zalloc_cache(zpMetaIf->RepoId, zBytes(256));
    sprintf(zpShellBuf, "sh -x %s_SHADOW/tools/zdeploy.sh \"%s\" \"%s\" \"%s\" \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 指定代码库的绝对路径
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),  // 指定40位SHA1  commit sig
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符
            zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList);  // 集群主机的点分格式文本 IPv4 列表

    /* 重置布署状态 */
    for (_i i = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].DpState = -1;
    }
    zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt = 0;
    zppGlobRepoIf[zpMetaIf->RepoId]->DpStartTime = time(NULL);

    /* 调用 git 命令执行布署 */
    zAdd_To_Thread_Pool(zthread_system, zpShellBuf);

    /* 等待所有主机的状态都得到确认，120 秒超时 */
    for (_i zTimeCnter = 0; zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost > zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt; zTimeCnter++) {
        zsleep(0.2);
        if (600 < zTimeCnter) {
            /* 若为部分布署失败，代码库状态置为 "损坏" 状态；若为全部布署失败，则无需此步 */
            if (0 < zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt) {
                zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig[0] = '\0';
                zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoDamaged;
            }

            char *zpBasePtr, zIpv4StrAddrBuf[INET_ADDRSTRLEN];
            zpBasePtr = zpMetaIf->p_data;
            /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
            for (_i zCnter = 0, zUnReplyCnt = 0; zCnter < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
                if (-1 == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState) {
                    zconvert_ipv4_bin_to_str(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr, zIpv4StrAddrBuf);
                    zpBasePtr += sprintf(zpBasePtr, "%s,", zIpv4StrAddrBuf);  // sprintf 将返回除 ‘\0’ 之外的字符总数，与 strlen() 取得的值相同
                    zUnReplyCnt++;
                }
            }
            if (zpBasePtr > zpMetaIf->p_data) { (--zpBasePtr)[0] = '\0'; }  // 若至少取到一个值，则需要去掉最后一个逗号

            return -12;
        }
    }

    /* 布署成功：向前端确认成功，更新最近一次布署的版本号到项目元信息中，复位代码库状态 */
    zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoGood;

    /* 若请求布署的版本号与最近一次布署的相同，则不必再重复生成缓存 */
    if (0 != strcmp(zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig)) {
        /* 更新最新一次布署版本号，并将本次布署信息写入日志 */
        strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));
        zwrite_log(zpMetaIf->RepoId);

        /* 重置内存池状态 */
        zReset_Mem_Pool_State(zpMetaIf->RepoId);

        /* 如下部分：更新全局缓存 */
        zppGlobRepoIf[zpMetaIf->RepoId]->CacheId = time(NULL);

        zMetaInfo zSubMetaIf;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;

        zSubMetaIf.DataType = zIsCommitDataType;
        zgenerate_cache(&zSubMetaIf);
        zSubMetaIf.DataType = zIsDpDataType;
        zgenerate_cache(&zSubMetaIf);
    }

    return 0;
}

/*
 * 外壳函数
 * 12：布署／撤销
 * 13：新加入的主机请求布署自身
 */
_i
zcommon_deploy(zMetaInfo *zpMetaIf, _i zSd) {
    _i zErrNo;

    if (13 == zpMetaIf->OpsId) {
        zpMetaIf->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zpMetaIf->DataType = 1;
        zpMetaIf->CommitId = zppGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf.VecSiz - 1;

        /* 若为目标主机请求布署自身的请求，则实行阻塞式等待 */
        pthread_rwlock_wrlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    } else {
        if (0 > pthread_rwlock_trywrlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) { return -11; }
    }

    zErrNo = zdeploy(zpMetaIf, zSd);

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return zErrNo;
}

/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
_i
zstate_confirm(zMetaInfo *zpMetaIf, _i zSd) {
    zDpResInfo *zpTmp = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[zpMetaIf->HostId % zDpHashSiz];

    for (; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if ((-1 == zpTmp->DpState) && (zpTmp->ClientAddr == zpMetaIf->HostId)) {
            /* 'A' 标识初始化远程主机的结果回复，'B' 标识布署状态回复 */
            if (('B' == zpMetaIf->p_ExtraData[0]) && (0 != strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zpMetaIf->p_data, 40))) { break; }
            zpTmp->DpState = 1;

            pthread_mutex_lock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));

            /* 需要原子性递增 */
            zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt++;

            /* 调试功能：布署耗时统计，必须在锁内执行 */
            zwrite_analysis_data(zpMetaIf->RepoId, zppGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zpMetaIf->HostId, zreal_time() - zppGlobRepoIf[zpMetaIf->RepoId]->DpStartTime);

            pthread_mutex_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));
            break;
        }
    }
    return 0;
}

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
_i
zlock_repo(zMetaInfo *zpMetaIf, _i zSd) {
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    if (2 == zpMetaIf->OpsId) {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDpLocked;
    } else {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDpUnLock;
    }

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);

    return 0;
}

/*
 * 网络服务路由函数
 */
void *
zops_route(void *zpSd) {
    _i zSd = *((_i *)zpSd);
    _i zRecvdLen;
    _i zErrNo;
    zMetaInfo zMetaIf;
    char zJsonBuf[zCommonBufSiz] = {'\0'};
    char *zpJsonBuf = zJsonBuf;

    /* 必须清零，以防脏栈数据导致问题 */
    memset(&zMetaIf, 0, sizeof(zMetaInfo));

    /* 若收到大体量数据，直接一次性扩展为1024倍的缓冲区，以简化逻辑 */
    if (zCommonBufSiz == (zRecvdLen = recv(zSd, zpJsonBuf, zCommonBufSiz, 0))) {
        zMem_Alloc(zpJsonBuf, char, zCommonBufSiz * 1024);
        strcpy(zpJsonBuf, zJsonBuf);
        zRecvdLen += recv(zSd, zpJsonBuf + zRecvdLen, zCommonBufSiz * 1024 - zRecvdLen, 0);
    }

    if (zBytes(6) > zRecvdLen) {
        shutdown(zSd, SHUT_RDWR);
        return NULL;
    }

    zRecvdLen *= 1.25;  // 扩展用于保留布署耗时秒数的空间
    char zDataBuf[zRecvdLen], zExtraDataBuf[zRecvdLen];
    memset(zDataBuf, 0, zRecvdLen);
    memset(zExtraDataBuf, 0, zRecvdLen);
    zMetaIf.p_data = zDataBuf;
    zMetaIf.p_ExtraData = zExtraDataBuf;
    if (0 > (zErrNo = zconvert_json_str_to_struct(zpJsonBuf, &zMetaIf))) {
        zMetaIf.OpsId = zErrNo;
        goto zMarkCommonAction;
    }

    if (0 > zMetaIf.OpsId || zServHashSiz <= zMetaIf.OpsId || NULL == zNetServ[zMetaIf.OpsId]) {
        zMetaIf.OpsId = -1;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if ((1 != zMetaIf.OpsId) && (5 != zMetaIf.OpsId)
            && ((zGlobMaxRepoId < zMetaIf.RepoId) || (0 >= zMetaIf.RepoId) || (NULL == zppGlobRepoIf[zMetaIf.RepoId]))) {
        zMetaIf.OpsId = -2;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if (0 > (zErrNo = zNetServ[zMetaIf.OpsId](&zMetaIf, zSd))) {
        zMetaIf.OpsId = zErrNo;  // 此时代表错误码
zMarkCommonAction:
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zpJsonBuf[0] = '[';
        zsendto(zSd, zpJsonBuf, strlen(zpJsonBuf), 0, NULL);
        zsendto(zSd, "]", zBytes(1), 0, NULL);
    }

    shutdown(zSd, SHUT_RDWR);
    if (zpJsonBuf != &(zJsonBuf[0])) { free(zpJsonBuf); }

    return NULL;
}

/************
 * 网络服务 *
 ************/
/*  执行结果状态码对应表
 *  -1：操作指令不存在（未知／未定义）
 *  -2：项目ID不存在
 *  -3：代码版本ID不存在或与其相关联的内容为空（空提交记录）
 *  -4：差异文件ID不存在
 *  -5：指定的主机 IP 不存在
 *  -6：项目布署／撤销／更新ip数据库的权限被锁定
 *  -7：后端接收到的数据无法解析，要求前端重发
 *  -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 *  -9：服务端错误：接收缓冲区为空或容量不足，无法解析数据
 *  -10：前端请求的数据类型错误
 *  -11：正在布署／撤销过程中（请稍后重试？）
 *  -12：布署失败（超时？未全部返回成功状态）
 *  -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确
 *  -14：
 *  -15：最近的布署记录之后，无新的提交记录
 *  -16：清理远程主机上的项目文件失败（删除项目时）
 *
 *  -22：指定的代理分发主机IP地址格式错误
 *  -23：更新全量IP列表时：部分或全部目标初始化失败
 *  -24：更新全量IP列表时，没有在 ExtraData 字段指明IP总数量
 *  -25：集群主节点(与中控机直连的主机)IP地址数据库不存在
 *  -26：集群全量节点(所有主机)IP地址数据库不存在，或为空
 *  -27：代理分发节点主机初始化失败
 *  -28：前端指定的IP数量与实际解析出的数量不一致
 *  -29：更新IP数据库时集群中有一台或多台主机初始化失败（每次更新IP地址库时，需要检测每一个IP所指向的主机是否已具备布署条件，若是新机器，则需要推送初始化脚本而后执行之）
 *
 *  -33：无法创建请求的项目路径
 *  -34：请求创建的新项目信息格式错误（合法字段数量少于 5 个或大于 6 个，第6个字段用于标记是被动拉取代码还是主动推送代码）
 *  -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 *  -36：请求创建的项目路径已存在，且项目ID不同
 *  -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *  -38：拉取远程代码库失败（git clone 失败）
 *  -39：项目元数据创建失败，如：项目ID无法写入repo_id、无法打开或创建布署日志文件meta等原因
 *
 *  -60：中转机项目文件清理失败
 *
 *  -70：服务器版本号列表缓存存在错误
 *  -71：服务器差异文件列表缓存存在错误
 *  -72：服务器单个文件的差异内容缓存存在错误
 */

void
zstart_server(void *zpIf) {
    zNetServ[0] = NULL;
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = zupdate_ipv4_db_proxy;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[5] = zshow_all_repo_meta;  // 显示所有有效项目的元信息
    zNetServ[6] = zshow_one_repo_meta;  // 显示单个有效项目的元信息
    zNetServ[7] = NULL;
    zNetServ[8] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[9] = zprint_record;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zNetServ[10] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[11] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[12] = zcommon_deploy;  // 布署或撤销
    zNetServ[13] = zcommon_deploy;  // 用于新加入某个项目的主机每次启动时主动请求中控机向自己承载的所有项目同目最近一次已布署版本代码
    zNetServ[14] = zreset_repo;  // 重置指定项目为原始状态（删除所有主机上的所有项目文件，保留中控机上的 _SHADOW 元文件）
    zNetServ[15] = NULL;  // 删除指定项目及其所属的所有文件

    /* 如下部分配置网络服务 */
    zNetServInfo *zpNetServIf = (zNetServInfo *)zpIf;
    _i zMajorSd;
    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    /* 会传向新线程，使用静态变量；使用数组防止密集型网络防问导致在新线程取到套接字之前，其值已变化的情况(此法不够严谨，权宜之计) */
    static _i zConnSd[64];
    for (_ui zIndex = 0;;zIndex++) {  // 务必使用无符号整型，防止溢出错乱
        if (-1 != (zConnSd[zIndex % 64] = accept(zMajorSd, NULL, 0))) {
            zAdd_To_Thread_Pool(zops_route, zConnSd + (zIndex % 64));
        }
    }
}
