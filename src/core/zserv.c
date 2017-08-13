#ifndef _Z
    #include "../zmain.c"
#endif

/***********
 * NET OPS *
 ***********/
/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if ((0 > zpMetaIf->CommitId) || ((zCacheSiz - 1) < zpMetaIf->CommitId) || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf)) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "CommitId 不存在或内容为空（空提交）");\
        return -3;\
    }\
} while(0)

/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMetaIf->FileId) || ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) < zpMetaIf->FileId)) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "差异文件ID不存在");\
        return -4;\
    }\
} while(0)

/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zppGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) {\
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );\
        zPrint_Err(0, NULL, "前端发送的缓存ID已失效");\
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
zlist_repo(zMetaInfo *_, _i zSd) {
    return 0;
}

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(zMetaInfo *zpMetaIf, _i zSd) {
    _i zErrNo;

    if (0 > (zErrNo = zinit_one_repo_env(zpMetaIf->p_data))) {
        return zErrNo;
    } else {
        zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
        return 0;
    }
}

/*
 * 13：删除项目（代码库）
 */
_i
zdelete_repo(zMetaInfo *zpMetaIf, _i zSd) {
    return 0;
}

/*
 * 6：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    zVecWrapInfo *zpSortedTopVecWrapIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型不存在");
        return -10;
    }

    /* 若上一次布署是失败的，则返回其 SHA1 sig，提示其尝试重新布署 */
    if (zRepoDamaged == zppGlobRepoIf[zpMetaIf->RepoId]->RepoState) {
        zpMetaIf->p_data = zppGlobRepoIf[zpMetaIf->RepoId]->zFailDeploySig;
        return -13;
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
zprint_diff_files(zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    zVecWrapInfo *zpTopVecWrapIf;

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

    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁

    if (NULL != zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf) {
        zsendmsg(zSd, zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf, 0, NULL);
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
zprint_diff_content(zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    zVecWrapInfo *zpTopVecWrapIf;

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

    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁
    zCheck_CommitId();  // 宏内部会解锁
    zCheck_FileId();  // 宏内部会解锁

    /* 差异文件内容直接是文本格式，在此处临时拼装成 json 样式 */
    if (NULL != zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->p_RefDataIf[zpMetaIf->FileId].p_SubVecWrapIf) {
        zsendto(zSd, "[{\"OpsId\":0,\"data\":\"", zBytes(20), 0, NULL);
        zsendmsg(zSd, zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->p_RefDataIf[zpMetaIf->FileId].p_SubVecWrapIf, 0, NULL);
        zsendto(zSd, "\"}]", zBytes(3), 0, NULL);  // 前端 PHP 需要的二级json结束符
    }

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return 0;
}

/*
 * 4：更新中转机 IPv4
 */
_i
zupdate_ipv4_db_major(zMetaInfo *zpMetaIf, _i zSd) {
    char zShellBuf[zCommonBufSiz];
    sprintf(zShellBuf, "sh -x %s_SHADOW/scripts/zhost_init_repo_major.sh %s %s",  // $1:MajorHostAddr；$2:PathOnHost
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpMetaIf->p_data,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9);  // 指定代码库在布署目标机上的绝对路径，即：去掉最前面的 "/home/git" 合计 9 个字符

    /* 此处取读锁权限即可，因为只需要排斥布署动作，并不影响查询类操作 */
    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        return -11;
    };

    if (0 != system(zShellBuf)) {
        pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
        return -27;
    }

    zppGlobRepoIf[zpMetaIf->RepoId]->MajorHostAddr = zconvert_ipv4_str_to_bin(zpMetaIf->p_data);

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );

    zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    return 0;
}

/*
 * 5：更新集群中所有主机的 ip 列表
 */
_i
zupdate_ipv4_db_all(zMetaInfo *zpMetaIf, _i zSd) {
    zMetaInfo *zpSubMetaIf;
    zPCREInitInfo *zpPcreInitIf;
    zPCRERetInfo *zpPcreResIf;
    zDeployResInfo *zpDpResListIf, *zpTmpDpResIf;
    char *zpIpStrList;
    _ui zOffSet = 0;

    if (NULL == zpMetaIf->p_ExtraData) { return -24; }

    zpPcreInitIf = zpcre_init("(\\d{1,3}\\.){3}\\d{1,3}");
    zpPcreResIf = zpcre_match(zpPcreInitIf, zpMetaIf->p_data, 1);

    if (strtol(zpMetaIf->p_ExtraData, NULL, 10) != zpPcreResIf->cnt) {
        zpcre_free_tmpsource(zpPcreResIf);
        zpcre_free_metasource(zpPcreInitIf);
        return -28;
    }

    /* 此处取读锁权限即可，因为只需要排斥布署动作，并不影响查询类操作 */
    if (EBUSY == pthread_rwlock_tryrdlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        zpcre_free_tmpsource(zpPcreResIf);
        zpcre_free_metasource(zpPcreInitIf);
        return -11;
    };

    /* 更新项目目标主机总数 */
    zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost = zpPcreResIf->cnt;

    /* 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配 */
    zMem_C_Alloc(zpDpResListIf, zDeployResInfo, zpPcreResIf->cnt);

    /* 加空格最长16字节，如："123.123.123.123 " */
    zpIpStrList = zalloc_cache(zpMetaIf->RepoId, zBytes(16) * zpPcreResIf->cnt);

    /* 并发同步环境初始化 */
    zCcur_Init(zpMetaIf->RepoId, A);
    for (_i i = 0; i < zpPcreResIf->cnt; i++) {
        /* 检测是否是最后一次循环 */
        zCcur_Fin_Mark((zpPcreResIf->cnt - 1) == i, A);

        /* 转换字符串点分格式 IPv4 为 _ui 型 */
        zpDpResListIf[i].ClientAddr = zconvert_ipv4_str_to_bin(zpPcreResIf->p_rets[i]);
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
    if (NULL != zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf) {
        free(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf);
    }
    zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf = zpDpResListIf;
    zppGlobRepoIf[zpMetaIf->RepoId]->p_HostAddrList = zpIpStrList;  // 存放于项目内存池中，不可 free()

    /* 更新对应的 HASH 散列，用于加快查询速度 */
    for (_i zCnter = 0; zCnter < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
        zpTmpDpResIf = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDeployHashSiz];
        if (NULL == zpTmpDpResIf) {
            /* 若顶层为空，直接指向数组中对应的位置 */
            zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDeployHashSiz]
                = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        } else {
            /* 将线性数组影射成 HASH 结构 */
            while (NULL != zpTmpDpResIf->p_next) { zpTmpDpResIf = zpTmpDpResIf->p_next; }
            zpTmpDpResIf->p_next = &(zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        }
    }

    /* 初始化远端新主机可能耗时较长，因此在更靠后的位置等待信号，以防长时间阻塞其它操作 */
    zCcur_Wait(A);

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );

    zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    return 0;
}

/*
 * 5：布署
 * 6：撤销
 */
_i
zdeploy(zMetaInfo *zpMetaIf, _i zSd) {
    zVecWrapInfo *zpTopVecWrapIf;
    zMetaInfo *zpSubMetaIf[2];

    _ui zMajorHostAddr;
    char zMajorHostStrAddrBuf[16];

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        return -10;
    }

    // 若检查条件成立，如下三个宏的内部会解锁
    zCheck_Lock_State();
    zCheck_CacheId();
    zCheck_CommitId();

    /*
     * 检查中转机 IPv4 存在性
     * 优先取用传入的 HostId 字段
     * 与单独调用 zupdate_ipv4_db_major 函数的区别是，此处并不会去初始化中转机的环境，适合除了新建项目外的所有场景
     */
    if (0 == zpMetaIf->HostId) { zMajorHostAddr = zppGlobRepoIf[zpMetaIf->RepoId]->MajorHostAddr; }
    else { zMajorHostAddr = zpMetaIf->HostId; }
    if (0 == zMajorHostAddr) { return -25; }

    /* 转换成点分格式 IPv4 地址 */
    zconvert_ipv4_bin_to_str(zMajorHostAddr, zMajorHostStrAddrBuf);

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMetaIf->p_data[0]) {zupdate_ipv4_db_all(zpMetaIf, zSd); }
    if (NULL == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf) { return -26; }

    /* 加写锁排斥一切相关操作 */
    if (EBUSY == pthread_rwlock_trywrlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) { return -11; };

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
            zMajorHostStrAddrBuf,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_HostAddrList);  // 集群主机的点分格式文本 IPv4 列表

    /* 调用 git 命令执行布署，暂不检查返回值 */
    system(zShellBuf);

    //等待所有主机的状态都得到确认，10 秒超时
    for (_i zTimeCnter = 0; zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost > zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt; zTimeCnter++) {
        zsleep(0.2);
        if (50 < zTimeCnter) {
            /* 若为部分布署失败，代码库状态置为 "损坏" 状态，并更新最近一次失败的 SHA1 sig；若为全部布署失败，则不必置位 */
            if (0 < zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt) {
                zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoDamaged;
                strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zFailDeploySig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));
            }
            pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
            return -12;
        }
    }

    // 布署成功，向前端确认成功，并复位代码库状态
    zsendto(zSd, "[{\"OpsId\":0}]", zBytes(13), 0, NULL);
    zppGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoGood;

    /* 将本次布署信息写入日志 */
    zwrite_log(zpMetaIf->RepoId);

    /* 重置内存池状态 */
    zReset_Mem_Pool_State(zpMetaIf->RepoId);

    /* 如下部分：更新全局缓存 */
    zppGlobRepoIf[zpMetaIf->RepoId]->CacheId = time(NULL);
    /* 同步锁初始化 */
    zCcur_Init(zpMetaIf->RepoId, A);  //___
    zCcur_Fin_Mark(1 == 1, A);  //___
    zCcur_Init(zpMetaIf->RepoId, B);  //___
    zCcur_Fin_Mark(1 == 1, B);  //___
    /* 生成提交记录缓存 */
    zpSubMetaIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf[0], A);  //___
    zpSubMetaIf[0]->RepoId = zpMetaIf->RepoId;
    zpSubMetaIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
    zpSubMetaIf[0]->DataType = zIsCommitDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpSubMetaIf[0]);
    /* 生成布署记录缓存 */
    zpSubMetaIf[1] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpSubMetaIf[1], B);  //___
    zpSubMetaIf[1]->RepoId = zpMetaIf->RepoId;
    zpSubMetaIf[1]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
    zpSubMetaIf[1]->DataType = zIsDeployDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpSubMetaIf[1]);
    /* 等待两批任务完成，之后释放同步锁的资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    pthread_rwlock_unlock( &(zppGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
    return 0;
}

/*
 * 7：回复尚未确认成功的主机列表
 */
_i
zprint_failing_list(zMetaInfo *zpMetaIf, _i zSd) {
    _ui *zpFailingList = zppGlobRepoIf[zpMetaIf->RepoId]->p_FailingList;
    memset(zpFailingList, 0, sizeof(_ui) * zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost);

    if (zppGlobRepoIf[zpMetaIf->RepoId]->ReplyCnt == zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        zsendto(zSd, "[{\"OpsId\":0,\"data\":\"\"}]", zBytes(23), 0, NULL);
    } else {
        _i zDataLen = 16 * zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost;
        char *zpBasePtr, zpDataBuf[zDataLen];
        zpBasePtr = zpDataBuf;

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        for (_i i = 0, zUnReplyCnt = 0; i < zppGlobRepoIf[zpMetaIf->RepoId]->TotalHost; i++) {
            if (0 == zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].DeployState) {
                zpFailingList[zUnReplyCnt] = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[i].ClientAddr;
                zpBasePtr += sprintf(zpBasePtr, "%u|", zpFailingList[zUnReplyCnt]);  // sprintf 将返回除 ‘\0’ 之外的字符总数，与 strle() 取得的值相同
                zUnReplyCnt++;
            }
        }
        (--zpBasePtr)[0] = '\0';  // 去掉最后一根竖线

        /* 拼装二维 json */
        zsendto(zSd, "[{\"OpsId\":-12,\"data\":\"", zBytes(22), 0, NULL);
        zsendto(zSd, zpDataBuf, strlen(zpDataBuf), 0, NULL);
        zsendto(zSd, "\"}]", zBytes(3), 0, NULL);
    }

    return 0;
}

/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
_i
zstate_confirm(zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
    zDeployResInfo *zpTmp = zppGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[zpMetaIf->HostId % zDeployHashSiz];

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
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
_i
zlock_repo(zMetaInfo *zpMetaIf, _i zSd) {
// TEST:PASS
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
void
zops_route(void *zpSd) {
// TEST:PASS
    _i zSd = *((_i *)zpSd);
    _i zBufSiz = zCommonBufSiz;
    _i zRecvdLen;
    _i zErrNo;
    zMetaInfo zMetaIf;
    char zJsonBuf[zCommonBufSiz] = {'\0'};
    char *zpJsonBuf = zJsonBuf;

    /* 必须清零，以防脏栈数据导致问题 */
    memset(&zMetaIf, 0, sizeof(zMetaInfo));

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
        return;
    }

    char zDataBuf[zRecvdLen], zExtraDataBuf[zRecvdLen];
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

    if ((1 != zMetaIf.OpsId) && ((zGlobMaxRepoId < zMetaIf.RepoId) || (0 > zMetaIf.RepoId) || (NULL == zppGlobRepoIf[zMetaIf.RepoId]))) {
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
}

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
 * -9：服务端错误：接收缓冲区为空或容量不足，无法解析数据
 * -10：前端请求的数据类型错误
 * -11：正在布署／撤销过程中（请稍后重试？）
 * -12：布署失败（超时？未全部返回成功状态）
 * -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确（此时前端需要再收取一次数据）
 *
 * -24：更新全量IP列表时，没有在 ExtraData 字段指明IP总数量
 * -25：集群主节点(与中控机直连的主机)IP地址数据库不存在
 * -26：集群全量节点(所有主机)IP地址数据库不存在
 * -27：主节点IP数据库更新失败
 * -28：全量节点IP数据库更新失败
 * -29：更新IP数据库时集群中有一台或多台主机初始化失败（每次更新IP地址库时，需要检测每一个IP所指向的主机是否已具备布署条件，若是新机器，则需要推送初始化脚本而后执行之）
 *
 * -33：权限不足，无法创建请求的路径
 * -34：请求创建的新项目信息格式错误（合法字段数量不是5个）
 * -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 * -36：请求创建的项目路径已存在
 * -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 * -38：无法创建新项目路径（如：git clone失败等原因）
 * -39：项目ID写入配置文件失败(repo_id)
 *
 * -100：不确定IP数据库是否准确更新，需要前端验证MD5_checksum（若验证不一致，则需要尝试重新更交IP数据库）
 */

void
zstart_server(void *zpIf) {
// TEST:PASS
    // 顺序不可变
    zNetServ[0] = zlist_repo;  // 显示项目ID及其在中控机上的绝对路径
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = zupdate_ipv4_db_major;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[5] = zupdate_ipv4_db_all;  // 更新集群中所有主机的 ip 列表
    zNetServ[6] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[7] = zstate_confirm;  // 布署成功状态人工确认
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
