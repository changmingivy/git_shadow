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
    if (zDeployLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock) {\
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        return -6;\
    }\
} while(0)

/*
 * 0：列出所有有效项目ID及其所在路径
 */
_i
zzero(zMetaInfo *_, _i __) { return 0; }

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(zMetaInfo *zpMetaIf, _i zSd) {
    _i zErrNo;
    if (0 == (zErrNo = zinit_one_repo_env(zpMetaIf->p_data))) {
        zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    }

    return zErrNo;
}

/*
 * 13：删除项目（代码库）
 */
_i
zdelete_repo(zMetaInfo *zpIf, _i zSd) {
    _i zErrNo;
    char zShellBuf[zCommonBufSiz];
    zMetaInfo *zpMetaIf = (zMetaInfo *) zpIf;

    /* 取 Destroy 锁 */
    pthread_mutex_lock(&zDestroyLock);

    /* 
     * 取项目写锁
     * 元数据指针置为NULL
     * 销毁读写锁
     */
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    zRepoInfo *zpRepoIf = zppGlobRepoIf[zpMetaIf->RepoId];
    zppGlobRepoIf[zpMetaIf->RepoId] = NULL;
    pthread_rwlock_unlock(&(zpRepoIf->RwLock));
    pthread_rwlock_destroy(&(zpRepoIf->RwLock));

    /* 生成待执行的外部动作指令 */
    sprintf(zShellBuf, "sh -x %s_SHADOW/scripts/zdelete_repo.sh %s %s %s",
            zpRepoIf->p_RepoPath,  // 指定代码库的绝对路径
            zpRepoIf->p_RepoPath + 9,  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符
            NULL == zpRepoIf->p_ProxyHostStrAddr ? "" : zpRepoIf->p_ProxyHostStrAddr,
            NULL == zpRepoIf->p_HostStrAddrList ? "" : zpRepoIf->p_HostStrAddrList);  // 集群主机的点分格式文本 IPv4 列表

    /* 执行动作，清理本地及所有远程主机上的项目文件，system返回值是wait状态，不是错误码，错误码需要用WEXITSTATUS宏提取 */
    zErrNo = WEXITSTATUS(system(zShellBuf));

    /* 清理该项目占用的资源 */
    pthread_kill(zppGlobRepoIf[zpMetaIf->RepoId]->InotifyWaitTid, SIGKILL);
    close(zpRepoIf->InotifyFd);

    void **zppPrev = zpRepoIf->p_MemPool;
    do {
        zppPrev = zppPrev[0];
        munmap(zpRepoIf->p_MemPool, zMemPoolSiz);
        zpRepoIf->p_MemPool = zppPrev;
    } while(NULL != zpRepoIf->p_MemPool);

    free(zpRepoIf->p_TopObjIf);
    free(zpRepoIf->p_RepoPath);
    free(zpRepoIf);

    /* 放 Destroy 锁 */
    pthread_mutex_unlock(&zDestroyLock);

    if (0 != zErrNo) {
        return -16;
    } else {
        zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
        return 0;
    }
}

/*
 * 5：显示所有项目及其元信息
 * 6：显示单个项目及其元信息
 */
_i
zshow_all_repo_meta(zMetaInfo *_, _i zSd) {
    char zSendBuf[zCommonBufSiz];

    zsendto(zSd, "[", zBytes(1), 0, NULL);  // 凑足json格式
    for(_i zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
        if (NULL == zppGlobRepoIf[zCnter] || 0 == zppGlobRepoIf[zCnter]->zInitRepoFinMark) { continue; }

        if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zCnter]->RwLock))) {
            sprintf(zSendBuf, "{\"OpsId\":-11,\"data\":\"Id: %d\"},", zCnter);
            zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);
            continue;
        };

        sprintf(zSendBuf, "{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDeploy: %s\nLastDeployedRev: %s\nLastDeployState: %s\nProxyHostIp: %s\nTotalHost: %d\nHostIPs: %s\"},",
                zCnter,
                zppGlobRepoIf[zCnter]->p_RepoPath,
                zDeployLocked == zppGlobRepoIf[zCnter]->DpLock ? "No" : "Yes",
                '\0' == zppGlobRepoIf[zCnter]->zLastDeploySig[0] ? "_" : zppGlobRepoIf[zCnter]->zLastDeploySig,
                zRepoDamaged == zppGlobRepoIf[zCnter]->RepoState ? "fail" : "success",
                NULL == zppGlobRepoIf[zCnter]->p_ProxyHostStrAddr ? "_" : zppGlobRepoIf[zCnter]->p_ProxyHostStrAddr,
                zppGlobRepoIf[zCnter]->TotalHost,
                NULL == zppGlobRepoIf[zCnter]->p_HostStrAddrList ? "_" : zppGlobRepoIf[zCnter]->p_HostStrAddrList
                );
        zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

        pthread_rwlock_unlock(&(zppGlobRepoIf[zCnter]->RwLock));
    }
    zsendto(zSd, "{\"OpsId\":0,\"data\":\"__END__\"}]", zBytes(1), 0, NULL);  // 凑足json格式，同时防止内容为空时，前端无法解析

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

    sprintf(zSendBuf, "[{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDeploy: %s\nLastDeployedRev: %s\nLastDeployState: %s\nProxyHostIp: %s\nTotalHost: %d\nHostIPs: %s\"}]",
            zpMetaIf->RepoId,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zDeployLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock ? "No" : "Yes",
            '\0' == zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig[0] ? "_" : zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zRepoDamaged == zppGlobRepoIf[zpMetaIf->RepoId]->RepoState ? "fail" : "success",
            NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr ? "_" : zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr,
            zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost,
            NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList ? "_" : zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList
            );
    zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 7：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpSortedTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        return -11;
    };

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrapIf->VecSiz) {
        if (0 < zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL)) {
            zsendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符
        } else {
            zsendto(zSd, "[{\"OpsId\":-14}]", zBytes(15), 0, NULL);
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
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    /* get rdlock */
    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        return -11;
    }

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        zget_file_list(zpMetaIf);
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (0 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
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
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        zget_file_list(zpMetaIf);
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (0 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -11;
        }
    }

    zCheck_FileId();  // 宏内部会解锁
    if (NULL == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)) {
        zget_diff_content(zpMetaIf);
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (0 == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz) {
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
zupdate_ipv4_db_major(zMetaInfo *zpMetaIf, _i zSd) {
    if (NULL == zpMetaIf->p_data) { return -27; }
    if (NULL != zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr
            && 0 == strcmp(zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr, zpMetaIf->p_data)) {
        goto zMark;
    }

    char zShellBuf[zCommonBufSiz];
    sprintf(zShellBuf, "sh -x %s_SHADOW/scripts/zhost_init_repo_major.sh %s %s",  // $1:MajorHostAddr；$2:PathOnHost
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpMetaIf->p_data,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9);  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符

    /* 此处取读锁权限即可，因为只需要排斥布署动作，并不影响查询类操作 */
    if (0 > pthread_rwlock_tryrdlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        return -11;
    }

    /* system返回值是wait状态，不是错误码，错误码需要用WEXITSTATUS宏提取 */
    if (0 != WEXITSTATUS(system(zShellBuf))) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -27;
    }

    zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr = zalloc_cache(zpMetaIf->RepoId, 1 + strlen(zpMetaIf->p_data));
    strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr, zpMetaIf->p_data);

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

zMark:
    zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    return 0;
}

/*
 * 注：完全内嵌于 zdeploy() 中，不再需要读写锁
 */
_i
zupdate_ipv4_db_all(zMetaInfo *zpMetaIf, _i _) {
    zMetaInfo *zpSubMetaIf;
    zPCREInitInfo *zpPcreInitIf;
    zPCRERetInfo *zpPcreResIf;
    zDeployResInfo *zpDpResListIf, *zpTmpDpResIf;
    char *zpIpStrList;
    _ui zOffSet = 0;

    if (NULL == zpMetaIf->p_ExtraData) {
        return -24;
    }

    zpPcreInitIf = zpcre_init("(\\d{1,3}\\.){3}\\d{1,3}");
    zpPcreResIf = zpcre_match(zpPcreInitIf, zpMetaIf->p_data, 1);

    if (strtol(zpMetaIf->p_ExtraData, NULL, 10) != zpPcreResIf->cnt) {
        zpcre_free_tmpsource(zpPcreResIf);
        zpcre_free_metasource(zpPcreInitIf);
        return -28;
    }

    /* 更新项目目标主机总数 */
    zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost = zpPcreResIf->cnt;

    /* 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配 */
    zMem_Alloc(zpDpResListIf, zDeployResInfo, zpPcreResIf->cnt);

    /* 加空格最长16字节，如："123.123.123.123 " */
    zpIpStrList = zalloc_cache(zpMetaIf->RepoId, zBytes(16) * zpPcreResIf->cnt);

    /* 并发同步环境初始化 */
    zCcur_Init(zpMetaIf->RepoId, 0, A);
    for (_i i = 0; i < zpPcreResIf->cnt; i++) {
        /* 检测是否是最后一次循环 */
        zCcur_Fin_Mark((zpPcreResIf->cnt - 1) == i, A);

        /* 转换字符串点分格式 IPv4 为 _ui 型 */
        zpDpResListIf[i].ClientAddr = zconvert_ipv4_str_to_bin(zpPcreResIf->p_rets[i]);
        zpDpResListIf[i].p_next = NULL;
        zpTmpDpResIf = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[zpMetaIf->HostId % zDeployHashSiz];
        while (NULL != zpTmpDpResIf) {
            /* 若 IPv4 address 已存在，则跳过初始化远程主机的环节 */
            if (zpTmpDpResIf->ClientAddr == zpDpResListIf[i].ClientAddr) {
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
        zpSubMetaIf->HostId = zpDpResListIf[i].ClientAddr;

        zAdd_To_Thread_Pool(zinit_one_remote_host, zpSubMetaIf);
zMark:
        /*
         * 非定长字符串不好动态调整，因此无论是否已存在都要执行
         * 生成将要传递给布署脚本的参数：空整分隔的字符串形式的 IPv4 列表
         */
        strcpy(zpIpStrList + zOffSet, zpPcreResIf->p_rets[i]);
        zOffSet += 1 + strlen(zpPcreResIf->p_rets[i]);
        zpIpStrList[zOffSet - 1] = ' ';
    }
    zpIpStrList[zOffSet - 1] = '\0';

    zpcre_free_tmpsource(zpPcreResIf);
    zpcre_free_metasource(zpPcreInitIf);

    /* 更新项目元信息 */
    if (NULL != zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf) { free(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf); }
    zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf = zpDpResListIf;
    zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList = zpIpStrList;  // 存放于项目内存池中，不可 free()

    /* 将线性数组影射成 HASH 结构,clear hash buf before reuse it!!! */
    memset(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, 0, zDeployHashSiz * sizeof(zDeployResInfo *));
    for (_i zCnter = 0; zCnter < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
        zpTmpDpResIf = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDeployHashSiz];
        if (NULL == zpTmpDpResIf) {
            /* 若顶层为空，直接指向数组中对应的位置 */
            zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDeployHashSiz]
                = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        } else {
            while (NULL != zpTmpDpResIf->p_next) { zpTmpDpResIf = zpTmpDpResIf->p_next; }
            zpTmpDpResIf->p_next = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        }
    }

    /* 初始化远端新主机可能耗时较长，因此在更靠后的位置等待信号，以防长时间阻塞其它操作 */
    zCcur_Wait(A);
    return 0;
}

/*
 * 12：布署／撤销
 */
_i
zdeploy(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpTopVecWrapIf;
    zMetaInfo *zpSubMetaIf[2];

    _i zErrNo;

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        return -10;
    }

    /* 加写锁排斥一切相关操作 */
    if (0 > pthread_rwlock_trywrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        return -11;
    }

    // 若检查条件成立，如下三个宏的内部会解锁，必须放在加锁之后的位置
    zCheck_Lock_State();
    zCheck_CacheId();
    zCheck_CommitId();

    /* 若项目状态是 zRepoGood，并且请求布署的版本号与最近一次布署的相同，直接返回成功 */
    if (zRepoGood == zppGlobRepoIf[zpMetaIf->RepoId]->RepoState 
            && 0 == (strcmp(zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig))) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
        return 0;
    }

    /* 检查中转机 IPv4 存在性 */
    if (NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -25;
    }

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMetaIf->p_data[0]) {
        zErrNo = zupdate_ipv4_db_all(zpMetaIf, zSd);

        if (0 > zErrNo) {
            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return zErrNo;
        }
    }

    if (NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf) {
        pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -26;
    }

    /* 重置布署状态 */
    zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt = 0;
    for (_i i = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].DeployState = 0;
    }

    /* 执行外部脚本使用 git 进行布署 */
    sprintf(zShellBuf, "sh -x %s_SHADOW/scripts/zdeploy.sh %s %s %s %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 指定代码库的绝对路径
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),  // 指定40位SHA1  commit sig
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符
            zppGlobRepoIf[zpMetaIf->RepoId]->p_ProxyHostStrAddr,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_HostStrAddrList);  // 集群主机的点分格式文本 IPv4 列表

    /* 调用 git 命令执行布署 */
    zAdd_To_Thread_Pool(zthread_system, zShellBuf );

    /* 等待所有主机的状态都得到确认，5 秒超时 */
    for (_i zTimeCnter = 0; zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost > zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt; zTimeCnter++) {
        zsleep(0.2);
        if (25 < zTimeCnter) {
            /* 若为部分布署失败，代码库状态置为 "损坏" 状态；若为全部布署失败，则无需此步 */
            if (0 < zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt) {
                zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig[0] = '\0';
                zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoDamaged;
            }

            pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -12;
        }
    }

    /* 布署成功：向前端确认成功，更新最近一次布署的版本号到项目元信息中，复位代码库状态 */
    zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));
    zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoGood;

    /* 将本次布署信息写入日志 */
    zwrite_log(zpMetaIf->RepoId);

    /* 停掉inotify wait线程，重置内存池状态，之后重新添加受监控对象、重启inotify wait */
    pthread_kill(zppGlobRepoIf[zpMetaIf->RepoId]->InotifyWaitTid, SIGKILL);
    zReset_Mem_Pool_State(zpMetaIf->RepoId);
    zAdd_To_Thread_Pool(zinotify_add_sub_watch, zppGlobRepoIf[zpMetaIf->RepoId]->p_TopObjIf);  // 添加监控目标
    zCheck_Pthread_Func_Exit(pthread_create(&(zppGlobRepoIf[zpMetaIf->RepoId]->InotifyWaitTid), NULL, zinotify_wait, &(zpMetaIf->RepoId)));

    /* 如下部分：更新全局缓存 */
    zppGlobRepoIf[zpMetaIf->RepoId]->CacheId = time(NULL);
    /* 同步锁初始化 */
    zCcur_Init(zpMetaIf->RepoId, 0, A);  //___
    zCcur_Fin_Mark(1 == 1, A);  //___
    zCcur_Init(zpMetaIf->RepoId, 0, B);  //___
    zCcur_Fin_Mark(1 == 1, B);  //___
    /* 生成提交记录缓存 */
    zpSubMetaIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf[0], A);  //___
    zpSubMetaIf[0]->RepoId = zpMetaIf->RepoId;
    zpSubMetaIf[0]->DataType = zIsCommitDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpSubMetaIf[0]);
    /* 生成布署记录缓存 */
    zpSubMetaIf[1] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf[1], B);  //___
    zpSubMetaIf[1]->RepoId = zpMetaIf->RepoId;
    zpSubMetaIf[1]->DataType = zIsDeployDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpSubMetaIf[1]);
    /* 等待两批任务完成，之后释放同步锁的资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 7：回复尚未确认成功的主机列表
 */
_i
zprint_failing_list(zMetaInfo *zpMetaIf, _i zSd) {
    if (zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt == zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        zsendto(zSd, "[{\"OpsId\":0,\"data\":\"\"}]", zBytes(23), 0, NULL);
    } else {
        _i zDataLen = 16 * zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost;
        char *zpBasePtr, zDataBuf[zDataLen], zIpv4StrAddrBuf[INET_ADDRSTRLEN];
        zpBasePtr = zDataBuf;

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        for (_i i = 0, zUnReplyCnt = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
            if (0 == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].DeployState) {
                zconvert_ipv4_bin_to_str(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].ClientAddr, zIpv4StrAddrBuf);
                zpBasePtr += sprintf(zpBasePtr, "%s,", zIpv4StrAddrBuf);  // sprintf 将返回除 ‘\0’ 之外的字符总数，与 strle() 取得的值相同
                zUnReplyCnt++;
            }
        }
        (--zpBasePtr)[0] = '\0';  // 去掉最后一个逗号

        /* 拼装二维 json */
        zsendto(zSd, "[{\"OpsId\":-12,\"data\":\"", zBytes(22), 0, NULL);
        zsendto(zSd, zDataBuf, strlen(zDataBuf), 0, NULL);
        zsendto(zSd, "\"}]", zBytes(3), 0, NULL);
    }

    return 0;
}

/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
_i
zstate_confirm(zMetaInfo *zpMetaIf, _i _) {
    zDeployResInfo *zpTmp = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[zpMetaIf->HostId % zDeployHashSiz];

    for (; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (0 == zpTmp->DeployState && zpTmp->ClientAddr == zpMetaIf->HostId) {
            zpTmp->DeployState = 1;
            // 需要原子性递增
            pthread_mutex_lock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));
            zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt++;
            pthread_mutex_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCntLock));
            return 0;
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
    char zJsonBuf[64];
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    if (2 == zpMetaIf->OpsId) {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDeployLocked;
    } else {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDeployUnLock;
    }

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    sprintf(zJsonBuf, "[{\"OpsId\":0}]");
    zsendto(zSd, zJsonBuf, zBytes(13), 0, NULL);

    return 0;
}

/*
 * 网络服务路由函数
 */
void *
zops_route(void *zpSd) {
    _i zSd = *((_i *)zpSd);
    _i zBufSiz = zCommonBufSiz;
    _i zRecvdLen;
    _i zErrNo;
    zMetaInfo zMetaIf;
    char zJsonBuf[zCommonBufSiz] = {'\0'};
    char *zpJsonBuf = zJsonBuf;

    /* 必须清零，以防脏栈数据导致问题 */
    memset(&zMetaIf, 0, sizeof(zMetaInfo));
//    zMetaIf.RepoId = -1;  // 提定为 -1，用作判断条件，防止与0号库冲突???

    if (zBufSiz == (zRecvdLen = recv(zSd, zpJsonBuf, zBufSiz, 0))) {
        _i zRecvSiz, zOffSet;
        zRecvSiz = zOffSet = zBufSiz;
        zBufSiz *= 2;
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
    }

    if (zBytes(6) > zRecvdLen) {
        shutdown(zSd, SHUT_RDWR);
        return NULL;
    }

    char zDataBuf[zRecvdLen], zExtraDataBuf[zRecvdLen];
    memset(zDataBuf, 0, zRecvdLen);
    memset(zExtraDataBuf, 0, zRecvdLen);
    zMetaIf.p_data = zDataBuf;
    zMetaIf.p_ExtraData = zExtraDataBuf;
    if (0 > (zErrNo = zconvert_json_str_to_struct(zpJsonBuf, &zMetaIf))) {
        zMetaIf.OpsId = zErrNo;
        goto zMarkCommonAction;
    }

    if (0 > zMetaIf.OpsId || zServHashSiz <= zMetaIf.OpsId) {
        zMetaIf.OpsId = -1;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if ((1 != zMetaIf.OpsId) && (5 != zMetaIf.OpsId) && ((zGlobMaxRepoId < zMetaIf.RepoId) || (0 >= zMetaIf.RepoId) || (NULL == zppGlobRepoIf[zMetaIf.RepoId]))) {
        zMetaIf.OpsId = -2;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if (0 > (zErrNo = zNetServ[zMetaIf.OpsId](&zMetaIf, zSd))) {
        if (-12 == zErrNo) {
            zprint_failing_list(&zMetaIf, zSd);
            goto zMarkEnd;
        }

        zMetaIf.OpsId = zErrNo;  // 此时代表错误码
zMarkCommonAction:
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zpJsonBuf[0] = '[';
        zsendto(zSd, zpJsonBuf, strlen(zpJsonBuf), 0, NULL);
        zsendto(zSd, "]", zBytes(1), 0, NULL);
    }
zMarkEnd:
    shutdown(zSd, SHUT_RDWR);
    if (zCommonBufSiz <= zRecvdLen) { free(zpJsonBuf); }

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
 *  -14：服务器缓存内容错误
 *  -15：最近的布署记录之后，无新的提交记录
 *  -16：清理远程主机上的项目文件失败（删除项目时）
 *
 *  -24：更新全量IP列表时，没有在 ExtraData 字段指明IP总数量
 *  -25：集群主节点(与中控机直连的主机)IP地址数据库不存在
 *  -26：集群全量节点(所有主机)IP地址数据库不存在
 *  -27：主节点IP数据库更新失败
 *  -28：全量节点IP数据库更新失败
 *  -29：更新IP数据库时集群中有一台或多台主机初始化失败（每次更新IP地址库时，需要检测每一个IP所指向的主机是否已具备布署条件，若是新机器，则需要推送初始化脚本而后执行之）
 *
 *  -33：无法创建请求的项目路径
 *  -34：请求创建的新项目信息格式错误（合法字段数量不是5个）
 *  -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 *  -36：请求创建的项目路径已存在，且项目ID不同
 *  -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *  -38：拉取远程代码库失败（git clone 失败）
 *  -39：项目元数据创建失败，如：项目ID无法写入repo_id、无法打开或创建布署日志文件meta等原因
 */

void
zstart_server(void *zpIf) {
    // 顺序不可变
    zNetServ[0] = zzero;  // 显示项目ID及其在中控机上的绝对路径
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = zupdate_ipv4_db_major;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[5] = zshow_all_repo_meta;  // 显示当前有效项目的元信息
    zNetServ[6] = zshow_one_repo_meta;  // 不再提供人工状态确认接口
    zNetServ[7] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[8] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[9] = zprint_record;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zNetServ[10] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[11] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[12] = zdeploy;  // 布署或撤销(如果 zMetaInfo 中 IP 地址数据段不为0，则表示仅布署到指定的单台主机，更多的适用于测试场景，仅需一台机器的情形)
    zNetServ[13] = zdelete_repo;  // 删除指定项目及其所属的所有文件

    /* 如下部分配置网络服务 */
    zNetServInfo *zpNetServIf = (zNetServInfo *)zpIf;
    _i zMajorSd, zConnSd;
    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    for (;;) {
        if (-1 != (zConnSd = accept(zMajorSd, NULL, 0))) {
            zAdd_To_Thread_Pool(zops_route, &zConnSd);
        }
    }
}
