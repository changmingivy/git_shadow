#ifndef _Z
    #include "../zmain.c"
#endif

/***********
 * NET OPS *
 ***********/
/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if (0 > zpMetaIf->CommitId || (zCacheSiz - 1) < zpMetaIf->CommitId || NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "Commit ID 不存在!");\
        return -3;\
    }\
} while(0)

/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMetaIf->FileId)\
            || ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].zUnitCnt - 1) < (zpMetaIf->FileId / zUnitSiz))\
            || ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].pp_UnitVecWrapIf[zpMetaIf->FileId / zUnitSiz]->VecSiz - 1) < (zpMetaIf->FileId % zUnitSiz))) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "差异文件ID不存在!");\
        return -4;\
    }\
} while(0)

/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zppGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "前端发送的缓存ID已失效!");\
        return -8;\
    }\
} while(0)

/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDeployLocked == zppGlobRepoIf[zpMetaIf->RepoId]->DpLock) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        return -6;\
    }\
} while(0)

/*
 * 0：列出所有有效项目ID及其所在路径
 */
_i
zlist_repo(struct zMetaInfo *_, _i zSd) {
    return 0;
}

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(struct zMetaInfo *zpMetaIf, _i zSd) {
    char zJsonBuf[128];
    _i zErrNo = zadd_one_repo_env(zpMetaIf->p_data, 0);

    switch (zErrNo) {
        case -1:
            return -34;  // 请求创建的新项目信息格式错误（合法字段数量不是5个）
        case -2:
            return -35;  // 请求创建的项目ID已存在或不合法（创建项目代码库时出错）
        case -3:
            return -36;  // 请求创建的项目路径已存在
        case -4:
            return -37;  // 请求创建项目时指定的源版本控制系统错误（非git或svn）
        default:
            sprintf(zJsonBuf, "{\"OpsId\":0,\"RepoId\":%d}", zpMetaIf->RepoId);
            zsendto(zSd, zJsonBuf, strlen(zJsonBuf), 0, NULL);
            return 0;
    }
}

/*
 * 6：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    struct zVecWrapInfo *zpSortedTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX，不使用链表 */
    if (0 < zpSortedTopVecWrapIf->VecSiz) {
        zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL);
        zsendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符
    }

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return 0;
}

/*
 * 5：开发人员已提交的版本号列表
 * 6：历史布署版本号列表
 * 10：显示差异文件路径列表
 */
_i
zprint_diff_files(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    struct zVecWrapInfo *zpTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁

    if (0 < zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].zUnitCnt) {
        for (_i i = 0; i < zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].zUnitCnt; i++) {
            zsendmsg(zSd, zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].pp_UnitVecWrapIf[i], 0, NULL);
        }
        zsendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符
    }

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return 0;
}

/*
 * 6：版本号列表
 * 10：显示差异文件路径列表
 * 11：显示差异文件内容
 */
_i
zprint_diff_content(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    struct zVecWrapInfo *zpTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁
    zCheck_FileId();  // 宏内部会解锁

    /* 差异文件内容直接是文本格式，不是json，因此最后不必追加 ']' */
    for (struct zVecWrapInfo *zpTmpVecWrapIf = zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_RefDataIf[zpMetaIf->FileId % zUnitSiz].p_SubVecWrapIf;
			NULL != zpTmpVecWrapIf;
			zpTmpVecWrapIf = zpTmpVecWrapIf->p_next) {
        zsendmsg(zSd, zpTmpVecWrapIf, 0, NULL);
    }

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return 0;
}

/*
 * 5：布署
 * 6：撤销
 */
_i
zdeploy(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    struct stat zStatIf;
    _i zFd;

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char zIpv4AddrStr[INET_ADDRSTRLEN] = "\0";
    char zJsonBuf[64];
    char *zpFilePath;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDeployDataType == zpMetaIf->DataType){
        zpTopVecWrapIf = zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpMetaIf->DataType = zIsDeployDataType;
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    if (EBUSY == pthread_rwlock_trywrlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {  // 加写锁
        return -11;
    };

    zCheck_Lock_State();  // 这个宏内部会释放写锁
    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁

    // 减 2 是为适应 json 二维结构，最后有一个 ']' 也会计入 VecSiz
    if (0 > zpMetaIf->FileId) {
        zpFilePath = "";
    } else if ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 2) < zpMetaIf->FileId) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "差异文件ID不存在!");\
        return -4;\
    } else {
        zpFilePath = zGet_OneFilePath(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId);
    }

    zCheck_Negative_Exit( zFd = open(zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, O_RDONLY) );

    zCheck_Negative_Exit( fstatat(zFd, zMajorIpTxtPath, &zStatIf, 0) );
    if (0 == zStatIf.st_size) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "集群主节点IP地址数据库不存在!");
        return -25;
    }

    zCheck_Negative_Exit( fstatat(zFd, zAllIpPath, &zStatIf, 0) );
    close(zFd);
    if (0 == zStatIf.st_size
            || (0 != (zStatIf.st_size % sizeof(_ui)))
            || (zStatIf.st_size / zSizeOf(_ui)) != zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "集群全量IP地址数据库异常!");
        return -26;
    }

    /* 重置布署状态 */
    zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt = 0;
    for (_i i = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
        zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResList[i].DeployState = 0;
    }

    /* 若前端指定了HostId，则本次操作为单主机布署 */
    if (0 != zpMetaIf->HostId) {
        zconvert_ipv4_bin_to_str(zpMetaIf->HostId, zIpv4AddrStr);
    }

    /* 执行外部脚本使用 git 进行布署 */
    sprintf(zShellBuf, "%s/.git_shadow/scripts/zdeploy.sh -p %s -i %s -f %s -h %s -P %s",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 指定代码库的绝对路径
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 指定代码库的绝对路径
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),  // 指定40位SHA1  commit sig
            zpFilePath,  // 指定目标文件相对于代码库的路径
            zIpv4AddrStr,  // 点分格式的ipv4地址
            zMajorIpTxtPath);  // Host 主节点 IP 列表相对于代码库的路径

    /* 调用 git 命令执行布署，脚本中设定的异常退出码均为 255 */
    if (255 == system(zShellBuf)) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );  // 释放写锁
        zPrint_Err(0, NULL, "Shell 布署命令执行错误!");
        return -12;
    }

    //等待所有主机的状态都得到确认，10 秒超时
    for (_i zTimeCnter = 0; zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost > zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt; zTimeCnter++) {
        sleep(1);
        //zsleep(0.2);

        fprintf(stderr, "DEBUG: TOTAL HOST: %d, Reply Cnt: %d\n",zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost, zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt);
        if (10 < zTimeCnter) {
            // 如果布署失败，代码库状态置为 "损坏" 状态
            zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoDamaged;
            pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );  // 释放写锁
            zPrint_Err(0, NULL, "布署超时(>10s)!");
            return -12;
        }
    }
    // 布署成功，向前端确认成功，并复位代码库状态
    sprintf(zJsonBuf, "{\"OpsId\":0,\"RepoId\":%d}", zpMetaIf->RepoId);
    zsendto(zSd, zJsonBuf, strlen(zJsonBuf), 0, NULL);
    zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoGood;

    /* 将本次布署信息写入日志 */
    zwrite_log(zpMetaIf->RepoId);

    /* 重置内存池状态 */
    zReset_Mem_Pool_State(zpMetaIf->RepoId);
    //zppGlobRepoIf[zpMetaIf->RepoId]->MemPoolOffSet = sizeof(void *);

    /* 如下部分：更新全局缓存 */
    zppGlobRepoIf[zpMetaIf->RepoId]->CacheId = time(NULL);
    /* 同步锁初始化 */
    zCcur_Init(zpMetaIf->RepoId, A);  //___
    zCcur_Init(zpMetaIf->RepoId, B);  //___
    /* 生成提交记录缓存 */
    zpMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpMetaIf, A);  //___
    zpMetaIf->RepoId = zpMetaIf->RepoId;
    zpMetaIf->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
    zpMetaIf->DataType = zIsCommitDataType;
    zCcur_Fin_Mark(1 == 1, A);  //___
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);
    /* 生成布署记录缓存 */
    zpMetaIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct zMetaInfo));
    zCcur_Sub_Config(zpMetaIf, B);  //___
    zpMetaIf->RepoId = zpMetaIf->RepoId;
    zpMetaIf->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
    zpMetaIf->DataType = zIsDeployDataType;
    zCcur_Fin_Mark(1 == 1, B);  //___
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);
    /* 等待两批任务完成，之后释放相关资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );

    return 0;
}

/*
 * 7：回复尚未确认成功的主机列表
 */
_i
zprint_failing_list(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    _ui *zpFailingList = zppGlobRepoIf[zpMetaIf->RepoId]->p_FailingList;
    memset(zpFailingList, 0, sizeof(_ui) * zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost);
    /* 第一个元素写入实时时间戳 */
    zpFailingList[0] = time(NULL);

    if (zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt == zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        zsendto(zSd, zpFailingList, sizeof(zpFailingList[0]), 0, NULL);
    } else {
        _i zUnReplyCnt = 1;

        char *zpJsonStrBuf, *zpBasePtr;
        _i zDataLen = 16 * zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost;

        zMem_Alloc(zpMetaIf->p_data, char, zDataLen);
        zMem_Alloc(zpJsonStrBuf, char, 256 + zDataLen);
        zpBasePtr = zpMetaIf->p_data;

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        for (_i i = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
            if (0 == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResList[i].DeployState) {
                zpFailingList[zUnReplyCnt] = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResList[i].ClientAddr;

                if (0 == i) {
                    sprintf(zpBasePtr, "%u", zpFailingList[zUnReplyCnt]);
                } else {
                    sprintf(zpBasePtr, "|%u", zpFailingList[zUnReplyCnt]);
                }
                zpBasePtr += 1 + strlen(zpBasePtr);

                zUnReplyCnt++;
            }
        }

        zconvert_struct_to_json_str(zpJsonStrBuf, zpMetaIf);
        zsendto(zSd, &(zpJsonStrBuf[1]), strlen(zpJsonStrBuf) - 1, 0, NULL);  // 不发送开头的逗号

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
zstate_confirm(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    struct zDeployResInfo *zpTmp = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHash[zpMetaIf->HostId % zDeployHashSiz];

    for (; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (0 == zpTmp->DeployState && zpTmp->ClientAddr == zpMetaIf->HostId) {
            zpTmp->DeployState = 1;
            // 需要原子性递增
            pthread_mutex_lock( &(zppGlobRepoIf[zpMetaIf->RepoId]->MutexLock) );
            zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt++;
            pthread_mutex_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->MutexLock) );
            return 0;
        }
    }
    return 0;
}

/*
 * 4：仅更新集群中负责与中控机直接通信的主机的 ip 列表
 * 5：更新集群中所有主机的 ip 列表
 */
_i
zupdate_ipv4_db_glob(struct zMetaInfo *zpMetaIf, _i zSd) {
    char zShellBuf[256], zPathBuf[128], *zpWritePath;
    _i zFd, zStrDbLen;

    zpWritePath = (4 == zpMetaIf->OpsId) ? zMajorIpTxtPath : zAllIpTxtPath;

    strcpy(zPathBuf, zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWritePath);

    /* 此处取读锁权限即可，因为只需要排斥布署动作，并不影响查询类操作 */
    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        zpMetaIf->p_data = "";
        return -11;
    };

    /* 将接收到的IP地址库写入文件 */
    zCheck_Negative_Exit( zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600) );
    zStrDbLen = strlen(zpMetaIf->p_data);  // 不能把最后的 '\0' 写入文件
    if (zStrDbLen != write(zFd, zpMetaIf->p_data, zStrDbLen)) {
        zpMetaIf->p_data = "";
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
        zPrint_Err(errno, NULL, "写入IPv4数据库失败(点分格式，文本文件)!");
        return (4 == zpMetaIf->OpsId) ? -27 : -28;
    }
    close(zFd);

    /* 生成 MD5_checksum 作为data回发给前端 */
    zpMetaIf->p_data = zgenerate_file_sig_md5(zPathBuf);

    /* 更新集群整体IP数据库时，检测新机器并进行初始化 */
    sprintf(zShellBuf, "/home/git/zgit_shadow/scripts/zhost_init_repo.sh %s", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
    if ((5 == zpMetaIf->OpsId) && (255 == system(zShellBuf))) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
        zPrint_Err(errno, NULL, "集群主机布署环境初始化失败!");
        return -29;
    }

    zupdate_ipv4_db( &(zpMetaIf->RepoId) );

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return -100;  // 提示前端验证 MD5_checksum
}

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
_i
zlock_repo(struct zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    char zJsonBuf[64];
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    if (2 == zpMetaIf->OpsId) {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDeployLocked;
    } else {
        zppGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDeployUnLock;
    }

    pthread_rwlock_unlock(&(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    sprintf(zJsonBuf, "{\"OpsId\":0,\"RepoId\":%d}", zpMetaIf->RepoId);
    zsendto(zSd, zJsonBuf, strlen(zJsonBuf), 0, NULL);

    return 0;
}

/*
 * 网络服务路由函数
 */
#define zCheck_Errno_12() do {\
    if (-12 == zErrNo) {\
        char *zpJsonStrBuf, *zpBasePtr;\
        _i zUnReplyCnt = 0;\
        _i zDataLen = 16 * zppGlobRepoIf[zMetaIf.RepoId]->TotalHost;\
\
        zMem_Alloc(zMetaIf.p_data, char, zDataLen);\
        zMem_Alloc(zpJsonStrBuf, char, 256 + zDataLen);\
        zpBasePtr = zMetaIf.p_data;\
\
        memset(zppGlobRepoIf[zMetaIf.RepoId]->p_FailingList, 0, zppGlobRepoIf[zMetaIf.RepoId]->TotalHost);\
        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */\
        for (_i i = 0; i < zppGlobRepoIf[zMetaIf.RepoId]->TotalHost; i++) {\
            if (0 == zppGlobRepoIf[zMetaIf.RepoId]->p_DpResList[i].DeployState) {\
                zppGlobRepoIf[zMetaIf.RepoId]->p_FailingList[zUnReplyCnt] = zppGlobRepoIf[zMetaIf.RepoId]->p_DpResList[i].ClientAddr;\
\
                zconvert_ipv4_bin_to_str(zppGlobRepoIf[zMetaIf.RepoId]->p_FailingList[zUnReplyCnt], zpBasePtr);\
                if (0 != i) {\
                    (zpBasePtr - 1)[0]  = ',';\
                }\
\
                zpBasePtr += 1 + strlen(zpBasePtr);\
                zUnReplyCnt++;\
            }\
        }\
        zconvert_struct_to_json_str(zpJsonStrBuf, &zMetaIf);\
        zsendto(zSd, &(zpJsonStrBuf[1]), strlen(zpJsonStrBuf) - 1, 0, NULL);\
\
        free(zMetaIf.p_data);\
        free(zpJsonStrBuf);\
\
        goto zMark;\
    }\
} while(0)

#define zSizMark 256
void
zops_route(void *zpSd) {
// TEST:PASS
    _i zSd = *((_i *)zpSd);
    pthread_mutex_unlock(&zNetServLock);
    _i zBufSiz = zSizMark;
    _i zRecvdLen;
    _i zErrNo;
    char zJsonBuf[zBufSiz];
    char *zpJsonBuf = zJsonBuf;

    struct zMetaInfo zMetaIf;
    cJSON *zpJsonRootObj;

    /* 用于接收IP地址列表的场景 */
    if (zBufSiz == (zRecvdLen = recv(zSd, zpJsonBuf, zBufSiz, 0))) {
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

    if (zBytes(4) > zRecvdLen) {
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (NULL == (zpJsonRootObj = zconvert_json_str_to_struct(zpJsonBuf, &zMetaIf))) {
        // 此时因为解析失败，zMetaIf处于未初始化状态，需要手动赋值
        memset(&zMetaIf, 0, sizeof(zMetaIf));
        zMetaIf.OpsId = -7;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, &(zpJsonBuf[1]), strlen(zpJsonBuf) - 1, 0, NULL);
        shutdown(zSd, SHUT_RDWR);
        zPrint_Err(0, NULL, "接收到的数据无法解析!");
        goto zMark;
    }

    if (0 > zMetaIf.OpsId || zServHashSiz <= zMetaIf.OpsId) {
        zMetaIf.OpsId = -1;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, &(zpJsonBuf[1]), strlen(zpJsonBuf) - 1, 0, NULL);
        zPrint_Err(0, NULL, "接收到的指令ID不存在!");
        goto zMark;
    }

    // 应对初始化配置为空或指定的ID不合法等场景
    if ((1 != zMetaIf.OpsId) && ((-1 == zGlobMaxRepoId) || (0 > zMetaIf.RepoId) || (NULL == zppGlobRepoIf[zMetaIf.RepoId]))) {
        zMetaIf.OpsId = -2;  // 此时代表错误码
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, &(zpJsonBuf[1]), strlen(zpJsonBuf) - 1, 0, NULL);
        zPrint_Err(0, NULL, "项目ID不存在!");
        goto zMark;
    }

    if (0 > (zErrNo = zNetServ[zMetaIf.OpsId](&zMetaIf, zSd))) {
        zMetaIf.OpsId = zErrNo;  // 此时代表错误码

        zCheck_Errno_12();

        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zsendto(zSd, &(zpJsonBuf[1]), strlen(zpJsonBuf) - 1, 0, NULL);
    }

zMark:
    if (zSizMark < zBufSiz) { free(zpJsonBuf); }
    zjson_obj_free(zpJsonRootObj);
    shutdown(zSd, SHUT_RDWR);
}
#undef zSizMark
#undef zCheck_Errno_12

/************
 * 网络服务 *
 ************/
/* 执行结果状态码对应表
 * -1：操作指令不存在（未知／未定义）
 * -2：项目ID不存在
 * -3：代码版本ID不存在或与其相关联的内容为空（空提交记录）
 * -4：差异文件ID不存在
 * -5：指定的主机 IP 不存在
 * -6：项目布署／撤销／更新ip数据库的权限被锁定
 * -7：后端接收到的数据无法解析，要求前端重发
 * -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 * -10：前端请求的数据类型错误
 * -11：正在布署／撤销过程中（请稍后重试？）
 * -12：布署失败（超时？未全部返回成功状态）
 * -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确（此时前端需要再收取一次数据）
 *
 * -25：集群主节点(与中控机直连的主机)IP地址数据库不存在
 * -26：集群全量节点(所有主机)IP地址数据库不存在或数据异常，需要更新
 * -27：主节点IP数据库更新失败
 * -28：全量节点IP数据库更新失败
 * -29：更新IP数据库时集群中有一台或多台主机初始化失败（每次更新IP地址库时，需要检测每一个IP所指向的主机是否已具备布署条件，若是新机器，则需要推送初始化脚本而后执行之）
 *
 * -34：请求创建的新项目信息格式错误（合法字段数量不是5个）
 * -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 * -36：请求创建的项目路径已存在
 * -37：请求创建项目时指定的源版本控制系统错误（非git与svn）
 * -38：拉取远程代码失败
 *
 * -100：不确定IP数据库是否准确更新，需要前端验证MD5_checksum（若验证不一致，则需要尝试重新更交IP数据库）
 */

void
zstart_server(void *zpIf) {
// TEST:PASS
#define zMaxEvents 64
    // 顺序不可变
    zNetServ[0] = zlist_repo;  // 显示项目ID及其在中控机上的绝对路径
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = zupdate_ipv4_db_glob;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[5] = zupdate_ipv4_db_glob;  // 更新集群中所有主机的 ip 列表
    zNetServ[6] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[7] = zstate_confirm;  // 布署成功状态人工确认
    zNetServ[8] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[9] = zprint_record;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zNetServ[10] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[11] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[12] = zdeploy;  // 布署(如果 zMetaInfo 中 IP 地址数据段不为0，则表示仅布署到指定的单台主机，更多的适用于测试场景，仅需一台机器的情形)
    zNetServ[13] = zdeploy;  // 撤销(如果 zMetaInfo 中 IP 地址数据段不为0，则表示仅布署到指定的单台主机)

    /* 如下部分配置网络服务 */
    struct zNetServInfo *zpNetServIf = (struct zNetServInfo *)zpIf;
    _i zMajorSd, zConnSd;
    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    for (;;) {
        pthread_mutex_lock(&zNetServLock);
        zCheck_Negative_Exit( zConnSd = accept(zMajorSd, NULL, 0) );
        zAdd_To_Thread_Pool(zops_route, &zConnSd);
    }
#undef zMaxEvents
}
