#ifndef _Z
    #include "zmain.c"
#endif

void
zinit_env(void) {
// TEST: PASS
    struct zMetaInfo *zpMetaIf;
    struct zObjInfo *zpObjIf;
    struct stat zStatIf;
    void **zppPrev;
    _i zFd[2];

    for (_i i = 0; i <= zGlobMaxRepoId; i++) {
        if (NULL == zppGlobRepoIf[i]) { continue; }

        // 初始化每个代码库的内存池
        zppGlobRepoIf[i]->MemPoolOffSet = sizeof(void *);  // 开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存map区
        zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zppGlobRepoIf[i]->MemLock), NULL) );
        zMap_Alloc( zppGlobRepoIf[i]->p_MemPool, char, zMemPoolSiz );
        zppPrev = zppGlobRepoIf[i]->p_MemPool;
        zppPrev[0] = NULL;

        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zCheck_Negative_Exit( zFd[0] = open(zppGlobRepoIf[i]->p_RepoPath, O_RDONLY) );

        /* 启动 inotify */
        zMem_Alloc( zpObjIf, char, sizeof(struct zObjInfo) + 1 + strlen("/.git/logs") + strlen(zppGlobRepoIf[i]->p_RepoPath) );
        strcpy(zpObjIf->path, zppGlobRepoIf[i]->p_RepoPath);
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
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_init(&(zppGlobRepoIf[i]->zRWLockAttr)) );
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_setkind_np(&(zppGlobRepoIf[i]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) );
        zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zppGlobRepoIf[i]->RwLock), &(zppGlobRepoIf[i]->zRWLockAttr)) );
        zCheck_Pthread_Func_Exit( pthread_rwlockattr_destroy(&(zppGlobRepoIf[i]->zRWLockAttr)) );
        // 互斥锁初始化
        zCheck_Pthread_Func_Exit( pthread_mutex_init(&zppGlobRepoIf[i]->MutexLock, NULL) );
        // 更新 TotalHost、zpppDpResHash、zppDpResList
        zupdate_ipv4_db(&i);
        // 用于收集布署尚未成功的主机列表，第一个元素用于存放实时时间戳，因此要多分配一个元素的空间
        zMem_Alloc(zppGlobRepoIf[i]->p_FailingList, _ui, 1 + zppGlobRepoIf[i]->TotalHost);
        // 初始化日志下一次写入偏移量并找开日志文件
        zCheck_Negative_Exit( fstatat(zFd[0], zLogPath, &zStatIf, 0) );
        zppGlobRepoIf[i]->zDeployLogOffSet = zStatIf.st_size;
        zCheck_Negative_Exit( zppGlobRepoIf[i]->LogFd = openat(zFd[0], zLogPath, O_WRONLY | O_CREAT | O_APPEND, 0755) );
        close(zFd[0]);
        // 缓存版本初始化
        zppGlobRepoIf[i]->CacheId = 1000000000;
        //CommitCacheQueueHeadId
        zppGlobRepoIf[i]->CommitCacheQueueHeadId = zCacheSiz;
        /* 指针指向自身的实体静态数据项 */
        zppGlobRepoIf[i]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[i]->CommitVecIf;
        zppGlobRepoIf[i]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[i]->CommitRefDataIf;

        zppGlobRepoIf[i]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[i]->SortedCommitVecIf;

        zppGlobRepoIf[i]->DeployVecWrapIf.p_VecIf = zppGlobRepoIf[i]->DeployVecIf;
        zppGlobRepoIf[i]->DeployVecWrapIf.p_RefDataIf = zppGlobRepoIf[i]->DeployRefDataIf;

        /* 生成缓存 */
        zpMetaIf = zalloc_cache(i, sizeof(struct zMetaInfo));
        zpMetaIf->RepoId = i;
        zpMetaIf->CacheId = zppGlobRepoIf[i]->CacheId;
        zpMetaIf->DataType = zIsCommitDataType;
        zpMetaIf->CcurSwitch = zCcurOn;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);

        zpMetaIf = zalloc_cache(i, sizeof(struct zMetaInfo));
        zpMetaIf->RepoId = i;
        zpMetaIf->CacheId = zppGlobRepoIf[i]->CacheId;
        zpMetaIf->DataType = zIsDeployDataType;
        zpMetaIf->CcurSwitch = zCcurOn;
        zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf);
    }
}

// 解析代码库条目信息
void
zparse_REPO(FILE *zpFile, char *zpRes, _i *zpLineNum) {
// TEST: PASS
    _i zRepoId, zFd;
    char zPullCmdBuf[zCommonBufSiz];
    zPCREInitInfo *zpInitIf[7];
    zPCRERetInfo *zpRetIf[7];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+/\\S+\\s*\\S+\\s*\\S+\\s*\\S+(?=\\s*($|#))");  // 检测整体格式是否合法

    zpInitIf[2] = zpcre_init("\\d+(?=\\s+/\\S+\\s*\\S+\\s*\\S+\\s*\\S+\\s*($|#))");  // 取代码库编号
    zpInitIf[3] = zpcre_init("/\\S+(?=\\s*\\S+\\s*\\S+\\s*\\S+\\s*($|#))");  // 取代码库路径（生产机上的绝对路径，中控机需要加前缀/home/git）
    zpInitIf[4] = zpcre_init("\\S+(?=\\s*\\S+\\s*\\S+\\s*($|#))");  // 取代码库远程地址（pull地址）
    zpInitIf[5] = zpcre_init("\\S+(?=\\s*\\S+\\s*($|#))");  // 取远程代码库生产（主）分支名称
    zpInitIf[6] = zpcre_init("\\S+(?=\\s*($|#))");  // 远程（源）代码服务器所使用的版本控制系统：svn或git

    zppGlobRepoIf = NULL;
    _i zMaxRepoId = -1;
    do {
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
        }

        zpRetIf[2] = zpcre_match(zpInitIf[2], zpRes, 0);
        zpRetIf[3] = zpcre_match(zpInitIf[3], zpRes, 0);
        zpRetIf[4] = zpcre_match(zpInitIf[4], zpRes, 0);
        zpRetIf[5] = zpcre_match(zpInitIf[5], zpRes, 0);
        zpRetIf[6] = zpcre_match(zpInitIf[6], zpRes, 0);

        zRepoId = strtol(zpRetIf[2]->p_rets[0], NULL, 10);
        if (zRepoId > zMaxRepoId) {
            zMem_Re_Alloc(zppGlobRepoIf, struct zRepoInfo *, zRepoId + 1, zppGlobRepoIf);
            for (_i i = zMaxRepoId + 1; i < zRepoId; i++) {
                zppGlobRepoIf[i] = NULL;
            }
            zMaxRepoId = zRepoId;
        }
        zMem_Alloc(zppGlobRepoIf[zRepoId], struct zRepoInfo, 1);
        zppGlobRepoIf[zRepoId]->RepoId = zRepoId;
        zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, 1 + strlen("/home/git/") + strlen(zpRetIf[3]->p_rets[0]));
        strcpy(zppGlobRepoIf[zRepoId]->p_RepoPath, "/home/git/");
        strcat(zppGlobRepoIf[zRepoId]->p_RepoPath, zpRetIf[3]->p_rets[0]);

        if (0 == strcmp("git", zpRetIf[6]->p_rets[0])) {
            sprintf(zPullCmdBuf, "cd %s && git pull --force %s %s:server",
                    zppGlobRepoIf[zRepoId]->p_RepoPath,
                    zpRetIf[4]->p_rets[0],
                    zpRetIf[5]->p_rets[0]);
        } else if (0 == strcmp("svn", zpRetIf[6]->p_rets[0])) {
            sprintf(zPullCmdBuf, "cd %s/sync_svn_to_git && svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server",
                    zppGlobRepoIf[zRepoId]->p_RepoPath);
        } else {
            zPrint_Err(0, NULL, "无法识别的远程版本管理系统：不是git也不是svn!");
            exit(1);
        }

        zMem_Alloc(zppGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
        strcpy(zppGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

        /* 检测代码库路径是否存在，不存在尝试初始化之 */
        if (-1 == (zFd = open(zpRetIf[3]->p_rets[0], O_RDONLY | O_DIRECTORY))) {
            char *zpCmd = "/home/git/zgit_shadow/scripts/zmaster_init_repo.sh";
            char *zppArgv[] = {"", zpRetIf[2]->p_rets[0], zpRetIf[3]->p_rets[0], zpRetIf[4]->p_rets[0], zpRetIf[5]->p_rets[0], zpRetIf[6]->p_rets[0], NULL};
            zfork_do_exec(zpCmd, zppArgv);
        }
        close(zFd);

        zpcre_free_tmpsource(zpRetIf[1]);
        zpcre_free_tmpsource(zpRetIf[2]);
        zpcre_free_tmpsource(zpRetIf[3]);
        zpcre_free_tmpsource(zpRetIf[4]);
        zpcre_free_tmpsource(zpRetIf[5]);
        zpcre_free_tmpsource(zpRetIf[6]);
    } while (NULL != (zpRes = zget_one_line(zpRes, zCommonBufSiz, zpFile)));

    zpcre_free_metasource(zpInitIf[0]);
    zpcre_free_metasource(zpInitIf[1]);
    zpcre_free_metasource(zpInitIf[2]);
    zpcre_free_metasource(zpInitIf[3]);
    zpcre_free_metasource(zpInitIf[4]);
    zpcre_free_metasource(zpInitIf[5]);
    zpcre_free_metasource(zpInitIf[6]);

    if (0 > (zGlobMaxRepoId = zMaxRepoId)) {
        zPrint_Err(0, NULL, "未读取到有效代码库信息!");
        exit(1);
    }
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
    _i zLineNum;

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));

    zpInitIf = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行

    for (zLineNum = 1; NULL != (zpRes = zget_one_line(zBuf, zCommonBufSiz, zpFile)); zLineNum++) {
        zpRetIf = zpcre_match(zpInitIf, zpRes, 0);  // 若是空白行或注释行，跳过
        if (0 != zpRetIf->cnt) {
            zpcre_free_tmpsource(zpRetIf);
            continue;
        }
        zpcre_free_tmpsource(zpRetIf);

        zparse_REPO(zpFile, zpRes, &zLineNum);
    }

    if (1 == zLineNum) {
        zPrint_Err(0, NULL, "配置文件为空!");
        exit(1);
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
