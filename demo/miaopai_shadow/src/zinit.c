#ifndef _Z
    #include "zmain.c"
#endif

void
zinit_env(void) {
// TEST: PASS
    struct zMetaInfo *zpMetaIf;
    struct zObjInfo *zpObjIf;
    _i zFd[2];

    for (_i i = 0; i < zGlobRepoNum; i++) {
        // 初始化每个代码库的内存池
        zpGlobRepoIf[i].MemPoolHeadId = 0;
        zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepoIf[i].MemLock), NULL) );
        zpGlobRepoIf[i].MemPoolSiz = zBytes(24 * 1024 * 1024);  // 24M
        zMap_Alloc( zpGlobRepoIf[i].p_MemPool, char, zpGlobRepoIf[i].MemPoolSiz );

        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zCheck_Negative_Exit( zFd[0] = open(zpGlobRepoIf[i].RepoPath, O_RDONLY) );

        /* 启动 inotify */
        zMem_Alloc( zpObjIf, char, sizeof(struct zObjInfo) + 1 + strlen("/.git/logs") + strlen(zpGlobRepoIf[i].RepoPath) );
        strcpy(zpObjIf->path, zpGlobRepoIf[i].RepoPath);
        strcat(zpObjIf->path, "/.git/logs");

        zpObjIf->RepoId = i;
        zpObjIf->RecursiveMark = 1;
        zpObjIf->zpRegexPattern = "^\\.{1,2}$";
        zpObjIf->CallBack = zupdate_one_commit_cache;
        zpObjIf->UpperWid = -1;  // 填充负数，提示 zinotify_add_sub_watch 函数这是顶层监控对象

        zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);

        #define zCheck_Status_Exit(zRet) do {\
            if (-1 == (zRet) && errno != EEXIST) {\
                zPrint_Err(errno, NULL, "Can't create directory!");\
                exit(1);\
            }\
        } while(0)
        // 如果 .git_shadow 路径不存在，创建之
        zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow", 0755) );
        zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/info", 0755) );
        zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/log", 0755) );
        zCheck_Status_Exit( mkdirat(zFd[0], ".git_shadow/log/deploy", 0755) );

        // 若对应的文件不存在，创建之
        zCheck_Status_Exit( zFd[1] = openat(zFd[0], zAllIpTxtPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
        close(zFd[1]);
        zCheck_Status_Exit( zFd[1] = openat(zFd[0], zMajorIpTxtPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
        close(zFd[1]);
        zCheck_Status_Exit( zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
        close(zFd[1]);
        zCheck_Status_Exit( zFd[1] = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_EXCL, 0644) );
        close(zFd[1]);
        #undef zCheck_Dir_Status_Exit

        // 在每个代码库的 .git_shadow/info/repo_id 文件中写入自身的代码库ID
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_TRUNC | O_CREAT, 0644) );
        if (sizeof(i) != write(zFd[1], &i, sizeof(i))) {
            zPrint_Err(0, NULL, "[write]: update REPO ID failed!");
            exit(1);
        }
        close(zFd[1]);

        // 为每个代码库生成一把读写锁，锁属性设置写者优先
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_init(&(zpGlobRepoIf[i].zRWLockAttr)) );
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_setkind_np(&(zpGlobRepoIf[i].zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) );
        zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zpGlobRepoIf[i].RwLock), &(zpGlobRepoIf[i].zRWLockAttr)) );
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_destroy(&(zpGlobRepoIf[i].zRWLockAttr)) );
        // 互斥锁初始化
        zCheck_Pthread_Func_Exit( pthread_mutex_init(&zpGlobRepoIf[i].MutexLock, NULL) );
        // 更新 TotalHost、zpppDpResHash、zppDpResList
        zupdate_ipv4_db(&i);
        // 用于收集布署尚未成功的主机列表，第一个元素用于存放实时时间戳，因此要多分配一个元素的空间
        zMem_Alloc(zpGlobRepoIf[i].p_FailingList, _ui, 1 + zpGlobRepoIf[i].TotalHost);
        // 打开日志文件
        zCheck_Negative_Exit( zpGlobRepoIf[i].LogFd = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_APPEND, 0755) );
        close(zFd[0]);
        // 缓存版本初始化
        zpGlobRepoIf[i].CacheId = 1000000000;
        //CommitCacheQueueHeadId
        zpGlobRepoIf[i].CommitCacheQueueHeadId = zCacheSiz;
        /* 指针指向自身的实体静态数据项 */
        zpGlobRepoIf[i].CommitVecWrapIf.p_VecIf = zpGlobRepoIf[i].CommitVecIf;
        zpGlobRepoIf[i].CommitVecWrapIf.p_RefDataIf = zpGlobRepoIf[i].CommitRefDataIf;

        zpGlobRepoIf[i].SortedCommitVecWrapIf.p_VecIf = zpGlobRepoIf[i].SortedCommitVecIf;

        zpGlobRepoIf[i].DeployVecWrapIf.p_VecIf = zpGlobRepoIf[i].DeployVecIf;
        zpGlobRepoIf[i].DeployVecWrapIf.p_RefDataIf = zpGlobRepoIf[i].DeployRefDataIf;

        /* 生成缓存 */
        zpMetaIf = zalloc_cache(i, sizeof(struct zMetaInfo));
        zpMetaIf->RepoId = i;
        zpMetaIf->CacheId = zpGlobRepoIf[i].CacheId;
        zpMetaIf->DataType = zIsCommitDataType;
        zpMetaIf->CcurSwitch = zCcurOn;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);

        zpMetaIf = zalloc_cache(i, sizeof(struct zMetaInfo));
        zpMetaIf->RepoId = i;
        zpMetaIf->CacheId = zpGlobRepoIf[i].CacheId;
        zpMetaIf->DataType = zIsDeployDataType;
        zpMetaIf->CcurSwitch = zCcurOn;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);
    }
}

// 取代码库条目
void
zparse_REPO(FILE *zpFile, char *zpRes, _i *zpLineNum) {
// TEST: PASS
    _i zRepoId, zFd[2];
    zPCREInitInfo *zpInitIf[4];
    zPCRERetInfo *zpRetIf[4];
    _i zRepoNum = 4;

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+/\\S+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("\\d+(?=\\s+/\\S+\\s*($|#))");  // 取代码库编号
    zpInitIf[3] = zpcre_init("/\\S+(?=\\s*($|#))");  // 取代码库路径

    zMem_C_Alloc(zpGlobRepoIf, struct zRepoInfo, zRepoNum);

    _i zRealRepoNum = 0;
    do {
        if (zRealRepoNum == zRepoNum) {
            zRepoNum *= 2;
            zMem_Re_Alloc(zpGlobRepoIf, struct zRepoInfo, zRepoNum, zpGlobRepoIf);
        }

        (*zpLineNum)++;  // 维持行号
        zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
        } else {  // 若是空白行或注释行，跳过
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
        }

        zpRetIf[1] = zpcre_match(zpInitIf[1], zpRes, 0);
        if (0 == zpRetIf[1]->cnt) {
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 语法格式错误\033[00m\n", *zpLineNum ,zpRes);
            exit(1);
        } else {
            zpcre_free_tmpsource(zpRetIf[1]);
        }

        zpRetIf[2] = zpcre_match(zpInitIf[2], zpRes, 0);
        zpRetIf[3] = zpcre_match(zpInitIf[3], zpRes, 0);

        zRepoId = strtol(zpRetIf[2]->p_rets[0], NULL, 10);
        strcpy(zpGlobRepoIf[zRepoId].RepoPath, zpRetIf[3]->p_rets[0]);

        zRealRepoNum++;
        // 检测代码库路径合法性
        if (-1 == (zFd[0] = open(zpRetIf[3]->p_rets[0], O_RDONLY | O_DIRECTORY))) {
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 指定的代码库地址不存在或不是目录!\033[00m\n", *zpLineNum ,zpRes);
            exit(1);
        }

        close(zFd[1]);
        close(zFd[0]);
        zpcre_free_tmpsource(zpRetIf[2]);
        zpcre_free_tmpsource(zpRetIf[3]);
    } while (NULL != (zpRes = zget_one_line(zpRes, zCommonBufSiz, zpFile)));

    zpcre_free_metasource(zpInitIf[0]);
    zpcre_free_metasource(zpInitIf[1]);
    zpcre_free_metasource(zpInitIf[2]);
    zpcre_free_metasource(zpInitIf[3]);

    if (0 == (zGlobRepoNum = zRealRepoNum)) {
        zPrint_Err(0, NULL, "未读取到有效代码库信息!");
        exit(1);
    }

    zMem_Re_Alloc(zpGlobRepoIf, struct zRepoInfo, zGlobRepoNum, zpGlobRepoIf);  // 缩减到实际所需空间
}

/* 读取主配置文件(正则取词有问题!!! 后续排查) */
void
zparse_conf(const char *zpConfPath) {
// TEST: PASS
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;
    char zBuf[zCommonBufSiz];
    char *zpRes = NULL;
    FILE *zpFile;

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));

    zpInitIf = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行

    for (_i zLineNum = 1; NULL != (zpRes = zget_one_line(zBuf, zCommonBufSiz, zpFile)); zLineNum++) {
        zpRetIf = zpcre_match(zpInitIf, zpRes, 0);  // 若是空白行或注释行，跳过
        if (0 != zpRetIf->cnt) {
            zpcre_free_tmpsource(zpRetIf);
            continue;
        }
        zpcre_free_tmpsource(zpRetIf);

        zparse_REPO(zpFile, zpRes, &zLineNum);
    }

    fclose(zpFile);
}

// 监控主配置文件的变动
void
zconfig_file_monitor(const char *zpConfPath) {
// TEST: PASS
    _i zConfFD = inotify_init();
    zCheck_Negative_Return(inotify_add_watch(zConfFD, zpConfPath, IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF),);

    char zBuf[zCommonBufSiz]
    __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t zLen;

    const struct inotify_event *zpEv;
    char *zpOffset;

    for (;;) {
        zLen = read(zConfFD, zBuf, zSizeOf(zBuf));
        zCheck_Negative_Return(zLen,);

        for (zpOffset = zBuf; zpOffset < zBuf + zLen;
            zpOffset += zSizeOf(struct inotify_event) + zpEv->len) {
            zpEv = (const struct inotify_event *)zpOffset;
            if (zpEv->mask & (IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) {
                return;
            }
        }
    }
}
