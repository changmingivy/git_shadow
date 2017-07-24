#ifndef _Z
    #include "zmain.c"
#endif

void
zinit_env(struct zNetServInfo *zpNetServIf) {
    struct zCacheMetaInfo *zpMetaIf;
    struct zObjInfo *zpObjIf;
    struct stat zStatIf;
    char zLastTimeStampStr[11] = {'0', '\0'};  // 存放最后一次布署的时间戳
    _i zFd[2];

    for (_i i = 0; i < zGlobRepoNum; i++) {
        // 初始化每个代码库的内存池
        zpGlobRepoIf[i].MemPoolSiz = zBytes(8 * 1024 * 1024);
        zCheck_Null_Exit( zpGlobRepoIf[i].p_MemPool = malloc(zpGlobRepoIf[i].MemPoolSiz) );
        zpGlobRepoIf[i].MemPoolHeadId = 0;
        zCheck_Pthread_Func_Exit( pthread_mutex_init(&zpGlobRepoIf[i].MemLock, NULL) );

        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[i].RepoPath, O_RDONLY));

        /* 启动 inotify */
        zCheck_Null_Exit( zpObjIf = malloc(sizeof(struct zObjInfo) + 1 + strlen("/.git/logs") + strlen(zpGlobRepoIf[i].RepoPath)) );
        strcpy(zpObjIf->path, zpGlobRepoIf[i].RepoPath);
        strcat(zpObjIf->path, "/.git/logs");

        zpObjIf->RepoId = i;
        zpObjIf->RecursiveMark = 1;
        zpObjIf->zpRegexPattern = "^\\.{1,2}$";
        zpObjIf->CallBack = zupdate_one_commit_cache;
        zpObjIf->UpperWid = -1;  // 填充负数，提示 zinotify_add_sub_watch 函数这是顶层监控对象

        zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);

        #define zCheck_Dir_Status_Exit(zRet) do {\
            if (-1 == (zRet) && errno != EEXIST) {\
                zPrint_Err(errno, NULL, "Can't create directory!");\
                exit(1);\
            }\
        } while(0)
        // 如果 .git_shadow 路径不存在，创建之
        zCheck_Dir_Status_Exit( mkdirat(zFd[0], ".git_shadow", 0700) );
        zCheck_Dir_Status_Exit( mkdirat(zFd[0], ".git_shadow/info", 0700) );
        zCheck_Dir_Status_Exit( mkdirat(zFd[0], ".git_shadow/log", 0700) );
        zCheck_Dir_Status_Exit( mkdirat(zFd[0], ".git_shadow/log/deploy", 0700) );
        #undef zCheck_Dir_Status_Exit

        // 若对应的文件不存在，创建之
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zAllIpTxtPath, O_WRONLY | O_CREAT) );
        close(zFd[1]);
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zMajorIpTxtPath, O_WRONLY | O_CREAT) );
        close(zFd[1]);
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_CREAT) );
        close(zFd[1]);
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT) );
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
        // 第一个元素用于存放实时时间戳，因此要多分配一个元素的空间
        zMem_Alloc(zpGlobRepoIf[i].p_FailingList, _ui, 1 + zpGlobRepoIf[i].TotalHost);
        // 打开日志文件
        zCheck_Negative_Exit( zpGlobRepoIf[i].LogFd = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_APPEND, 0600) );
        /* 获取当前日志文件属性，不能基于全局 LogFd 打开（以 append 方式打开，会导致 fstat 结果中 st_size 为0）*/
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zLogPath, O_RDONLY) );
        zCheck_Negative_Exit( fstat(zFd[1], &zStatIf) );
        if (0 != zStatIf.st_size) {
            zCheck_Negative_Exit( pread(zFd[1], zLastTimeStampStr, zBytes(11), zStatIf.st_size - zBytes(11)) );
        }
        close(zFd[0]);
        close(zFd[1]);
        // 读取日志中最后一条记录的时间戳作为初始的缓存版本，若日志大小为0，则缓存版本赋0值
        zpGlobRepoIf[i].CacheId = strtol(zLastTimeStampStr, NULL, 10);
        /* 指针指向自身的实体静态数据项 */
        zpGlobRepoIf[i].CommitVecWrapIf.p_VecIf = zpGlobRepoIf[i].CommitVecIf;
        zpGlobRepoIf[i].CommitVecWrapIf.p_RefDataIf = zpGlobRepoIf[i].CommitRefDataIf;

        zpGlobRepoIf[i].SortedCommitVecWrapIf.p_VecIf = zpGlobRepoIf[i].SortedCommitVecIf;

        zpGlobRepoIf[i].DeployVecWrapIf.p_VecIf = zpGlobRepoIf[i].DeployVecIf;
        zpGlobRepoIf[i].DeployVecWrapIf.p_RefDataIf = zpGlobRepoIf[i].DeployRefDataIf;

        /* 生成缓存，必须在堆上分配内存，因为 zgenerate_cache 会进行free */
        zMem_Alloc(zpMetaIf, struct zCacheMetaInfo, 1);
        zpMetaIf->TopObjTypeMark = zIsCommitCacheType;
        zpMetaIf->RepoId = i;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);

        zMem_Alloc(zpMetaIf, struct zCacheMetaInfo, 1);
        zpMetaIf->TopObjTypeMark = zIsDeployCacheType;
        zpMetaIf->RepoId = i;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);
    }

    zAdd_To_Thread_Pool( zstart_server, zpNetServIf );  // 启动网络服务
    zAdd_To_Thread_Pool( zinotify_wait, NULL );  // 等待事件发生
}

// 取代码库条目
void
zparse_REPO(FILE *zpFile, char *zpRes, _i *zpLineNum) {
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
            zpcre_free_tmpsource(zpRetIf[1]);
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
        zCheck_Negative_Exit(zFd[0] = open(zpRetIf[3]->p_rets[0], O_RDONLY | O_DIRECTORY));
        // 在每个代码库的 .git_shadow/info/repo_id 文件中写入自身的代码库ID
        zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));
        if (sizeof(zRepoId) != write(zFd[1], &zRepoId, sizeof(zRepoId))) {
            zPrint_Err(0, NULL, "[write]: update REPO ID failed!");
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

/* 读取主配置文件(正则取词有问题!!! 暂不影响功能，后续排查) */
void
zparse_conf(const char *zpConfPath) {
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;
    char zBuf[zCommonBufSiz];
    char *zpRes = NULL;
    FILE *zpFile;

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));

    zpInitIf = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行

    for (_i zLineNum = 1; NULL != (zpRes = zget_one_line(zBuf, zCommonBufSiz, zpFile)); zLineNum++) {
        //zpRes[strlen(zpRes) - 1 ] = '\0';
        zpRetIf = zpcre_match(zpInitIf, zpRes, 0);  // 若是空白行或注释行，跳过
        if (0 != zpRetIf->cnt) {
            zpcre_free_tmpsource(zpRetIf);
            continue;
        }
        zpcre_free_tmpsource(zpRetIf);

        zparse_REPO(zpFile, zpRes, &zLineNum);
        fclose(zpFile);
    }
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
