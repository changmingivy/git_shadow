#ifndef _Z
    #include "zmain.c"
#endif

void
zinit_env(void) {
	struct zCacheMetaInfo zCommitMetaIf = {.TopObjTypeMark = 0};
	struct zCacheMetaInfo zDeployMetaIf = {.TopObjTypeMark = 1};
    struct stat zStatIf;
	char zLastTimeStampStr[11];  // 存放最后一次布署的时间戳
    _i zFd[2];

    for (_i i = 0; i < zRepoNum; i++) {
        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[i].RepoPath, O_RDONLY));

        #define zCheck_Dir_Status_Exit(zRet) do {\
            if (-1 == (zRet) && errno != EEXIST) {\
                zPrint_Err(errno, NULL, "Can't create directory!");\
                exit(1);\
            }\
        } while(0)

        // 如果 .git_shadow 路径不存在，创建之，并从远程拉取该代码库的客户端ipv4列表
        // 需要--主动--从远程拉取该代码库的客户端ipv4列表 ???
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow", 0700));
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow/info", 0700));
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow/log", 0700));
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow/log/deploy", 0700));

        // 为每个代码库生成一把读写锁，锁属性设置写者优先
    	zCheck_Pthread_Func_Exit( pthread_rwlockattr_init(&(zpRepoGlobIf[i].zRWLockAttr)) );
    	zCheck_Pthread_Func_Exit( pthread_rwlockattr_setkind_np(&(zpRepoGlobIf[i].zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) );
        zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zpRepoGlobIf[i].RwLock), &zRWLockAttr) );
    	zCheck_Pthread_Func_Exit( pthread_rwlockattr_destroy(&(zpRepoGlobIf[i].zRWLockAttr)) );

        zupdate_ipv4_db_all(i);  // 更新 zpppDpResHash 与 zppDpResList，读写锁的操作在此函数内部，外部调用方不能再加解锁

        zCheck_Negative_Exit( zpRepoGlobIf[i].LogFd = openat(zFd[0], zSigLogPath, O_WRONLY | O_CREAT | O_APPEND, 0600) );  // 打开日志文件

		/* 获取当前日志文件属性，不能基于 zpLogFd[0][i] 打开（以 append 方式打开，会导致 fstat 结果中 st_size 为0）*/
        zCheck_Negative_Exit( zFd[1] = openat(zFd[0], zMetaLogPath, O_RDONLY) );
        zCheck_Negative_Exit( fstat(zFd[1], &zStatIf) );
        zCheck_Negative_Exit( pread(zFd[1], zLastTimeStampStr, zBytes(11), zStatIf.st_size - zBytes(11)) );  // 10 位数字 ＋ '\n'
        close(zFd[0]);
        close(zFd[1]);

		zpRepoGlobIf[i].CacheId = strtol(zLastTimeStampStr, NULL, 10);  // 读取日志中最后一条记录的时间戳作为初始的缓存版本

		/* 指针指向自身的实体静态数据项 */
		zpRepoGlobIf[i].CommitWrapVecIf.p_VecIf = zpRepoGlobIf[i].CommitVecIf;
		zpRepoGlobIf[i].CommitWrapVecIf.p_RefDataIf = zpRepoGlobIf[i].CommitRefDataIf;

		zpRepoGlobIf[i].SortedCommitWrapVecIf.p_VecIf = zpRepoGlobIf[i].SortedCommitVecIf;

		zpRepoGlobIf[i].DeployWrapVecIf.p_VecIf = zpRepoGlobIf[i].DeployVecIf;
		zpRepoGlobIf[i].DeployWrapVecIf.p_RefDataIf = zpRepoGlobIf[i].DeployRefDataIf;
		/* 生成缓存 */
		zCommitMetaIf.RepoId = i;
		zDeployMetaIf.RepoId = i;
		zAdd_To_Thread_Pool(zgenerate_cache, &zCommitMetaIf);
		zAdd_To_Thread_Pool(zgenerate_cache, &zDeployMetaIf);
    }
}

// 取 [REPO] 区域配置条目
void
zparse_REPO(FILE *zpFile, char **zppRes, _i *zpLineNum) {
// TEST: PASS
    _i zRepoId, zFd[2];
    zPCREInitInfo *zpInitIf[5];
    zPCRERetInfo *zpRetIf[5];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+/\\S+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("^\\s*\\[\\S+\\]\\s*($|#)");  // 检测是否已到下一个区块标题
    zpInitIf[3] = zpcre_init("\\d+(?=\\s+/\\S+\\s*($|#))");  // 取代码库编号
    zpInitIf[4] = zpcre_init("/\\S+(?=\\s*($|#))");  // 取代码库路径

    zMem_C_Alloc(zpRepoGlobIf, zRepoInfo, zMaxRepoNum); // 预分配足够大的内存空间，待获取实际的代码库数量后，再缩减到实际所需空间

    _i zRealRepoNum = 0;
    while (NULL != (*zppRes = zget_one_line_from_FILE(zpFile))) {
        (*zpLineNum)++;  // 维持行号
        zpRetIf[0] = zpcre_match(zpInitIf[0], *zppRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
        } else {  // 若是空白行或注释行，跳过
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
        }

        if (strlen(*zppRes) == 0) { continue; }

        zpRetIf[1] = zpcre_match(zpInitIf[1], *zppRes, 0);
        if (0 == zpRetIf[1]->cnt) {
            zpRetIf[2] = zpcre_match(zpInitIf[2], *zppRes, 0);
            if (0 == zpRetIf[2]->cnt) {  // 若检测到格式有误的语句，报错后退出
                zPrint_Time();
                fprintf(stderr, "\033[31m[Line %d] \"%s\": 语法格式错误\033[00m\n", *zpLineNum ,*zppRes);
                zpcre_free_tmpsource(zpRetIf[1]);
                zpcre_free_tmpsource(zpRetIf[2]);
                exit(1);
            } else {
                zpcre_free_tmpsource(zpRetIf[1]);
                zpcre_free_tmpsource(zpRetIf[2]);
                goto zMark;
            }
        } else {
            zpcre_free_tmpsource(zpRetIf[1]);
        }

        zRealRepoNum++;

        zpRetIf[3] = zpcre_match(zpInitIf[3], *zppRes, 0);
        zpRetIf[4] = zpcre_match(zpInitIf[4], *zppRes, 0);

        zCheck_Negative_Exit(zFd[0] = open(zpRetIf[4]->p_rets[0], O_RDONLY | O_DIRECTORY)); // 检测代码库路径合法性
        zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zRepoIdPath, O_WRONLY | O_TRUNC | O_CREAT, 0600)); // 在每个代码库的 .git_shadow/info/repo_id 文件中写入自身的代码库ID
        if (sizeof(zRepoId) != write(zFd[1], &zRepoId, sizeof(zRepoId))) {
            zPrint_Err(0, NULL, "[write]: update REPO ID failed!");
            exit(1);
        }
        close(zFd[1]);
        close(zFd[0]);

        zRepoId = strtol(zpRetIf[3]->p_rets[0], NULL, 10);
        strcpy(zpRepoGlobIf[zRepoId].RepoPath, zpRetIf[4]->p_rets[0]);

        zpcre_free_tmpsource(zpRetIf[3]);
        zpcre_free_tmpsource(zpRetIf[4]);
    }

zMark:
    zpcre_free_metasource(zpInitIf[0]);
    zpcre_free_metasource(zpInitIf[1]);
    zpcre_free_metasource(zpInitIf[2]);
    zpcre_free_metasource(zpInitIf[3]);
    zpcre_free_metasource(zpInitIf[4]);

    zRepoNum = zRealRepoNum;
    zMem_Re_Alloc(zpRepoGlobIf, zRepoInfo, zRepoNum, zpRepoGlobIf);  // 缩减到实际所需空间
}

// 取 [INOTIFY] 区域配置条目
void
zparse_INOTIFY_and_add_watch(FILE *zpFile, char **zppRes, _i *zpLineNum) {
// TEST: PASS
    zObjInfo *zpObjIf;
    _i zRepoId, zFd;
    zPCREInitInfo *zpInitIf[8];
    zPCRERetInfo *zpRetIf[8];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("^\\s*\\[\\S+\\]\\s*($|#)");  // 检测是否已到下一个区块标题
    zpInitIf[3] = zpcre_init("\\d+(?=\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#))");  // 取所属代码库编号ID
    zpInitIf[4] = zpcre_init("\\S+(?=\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#))");  // 取被监控对象路径
    zpInitIf[5] = zpcre_init("\\S+(?=\\s+\\S+\\s+\\d+\\s*($|#))");  // 取正则表达式子符串
    zpInitIf[6] = zpcre_init("\\S+(?=\\s+\\d+\\s*($|#))");  // 取是否递归的标志位，可以为：Y/N/YES/NO/yes/y/n/no 等
    zpInitIf[7] = zpcre_init("\\S+(?=\\s*($|#))");  // 回调函数ID

    while (NULL != (*zppRes = zget_one_line_from_FILE(zpFile))) {
        (*zpLineNum)++;  // 维持行号
        zpRetIf[0] = zpcre_match(zpInitIf[0], *zppRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
        } else {  // 若是空白行或注释行，跳过
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
        }

        if (strlen(*zppRes) == 0) { continue; }

        zpRetIf[1] = zpcre_match(zpInitIf[1], *zppRes, 0);
        if (0 == zpRetIf[1]->cnt) {  // 若检测到格式有误的语句，报错后退出
            zpRetIf[2] = zpcre_match(zpInitIf[2], *zppRes, 0);
            if (0 == zpRetIf[2]->cnt) {
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 语法格式错误\033[00m\n", *zpLineNum ,*zppRes);
            zpcre_free_tmpsource(zpRetIf[1]);
            zpcre_free_tmpsource(zpRetIf[2]);
            exit(1);
            } else {
            zpcre_free_tmpsource(zpRetIf[1]);
            zpcre_free_tmpsource(zpRetIf[2]);
            goto zMark;
            }
        } else {
            zpcre_free_tmpsource(zpRetIf[1]);
        }

        zpRetIf[3] = zpcre_match(zpInitIf[3], *zppRes, 0);
        zpRetIf[4] = zpcre_match(zpInitIf[4], *zppRes, 0);
        zpRetIf[5] = zpcre_match(zpInitIf[5], *zppRes, 0);
        zpRetIf[6] = zpcre_match(zpInitIf[6], *zppRes, 0);
        zpRetIf[7] = zpcre_match(zpInitIf[7], *zppRes, 0);

        zRepoId = strtol(zpRetIf[3]->p_rets[0], NULL, 10);
        if ('/' == zpRetIf[4]->p_rets[0][0]) {
            zpObjIf = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[4]->p_rets[0]));  // 为新条目分配内存
            zCheck_Null_Exit(zpObjIf);
            strcpy(zpObjIf->path, zpRetIf[4]->p_rets[0]); // 被监控对象绝对路径
        } else {
            zpObjIf = malloc(sizeof(zObjInfo) + 2 + strlen(zpRetIf[4]->p_rets[0]) + strlen(zpRepoGlobIf[zRepoId].RepoPath));  // 为新条目分配内存
            zCheck_Null_Exit(zpObjIf);
            strcpy(zpObjIf->path, zpRepoGlobIf[zRepoId].RepoPath);
            strcat(zpObjIf->path, "/");
            strcat(zpObjIf->path, zpRetIf[4]->p_rets[0]); // 被监控对象绝对路径
        }

        zCheck_Negative_Exit(zFd = open(zpObjIf->path, O_RDONLY));  // 检测被监控目标的路径合法性
        close(zFd);

        zpObjIf->RepoId = zRepoId;  // 所属版本库ID
        zMem_Alloc(zpObjIf->zpRegexPattern, char, 1 + strlen(zpRetIf[5]->p_rets[0]));
        strcpy(zpObjIf->zpRegexPattern, zpRetIf[5]->p_rets[0]); // 正则字符串
        zpObjIf->RecursiveMark = ('y' == tolower(zpRetIf[6]->p_rets[0][0])) ? 1 : 0; // 递归标识
        zpObjIf->CallBack = zCallBackList[strtol(zpRetIf[7]->p_rets[0], NULL, 10)];  // 回调函数

        zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);  // 检测到有效条目，加入inotify监控队列

        zpcre_free_tmpsource(zpRetIf[3]);
        zpcre_free_tmpsource(zpRetIf[4]);
        zpcre_free_tmpsource(zpRetIf[5]);
        zpcre_free_tmpsource(zpRetIf[6]);
        zpcre_free_tmpsource(zpRetIf[7]);
    }

zMark:
    zpcre_free_metasource(zpInitIf[0]);
    zpcre_free_metasource(zpInitIf[1]);
    zpcre_free_metasource(zpInitIf[2]);
    zpcre_free_metasource(zpInitIf[3]);
    zpcre_free_metasource(zpInitIf[4]);
    zpcre_free_metasource(zpInitIf[5]);
    zpcre_free_metasource(zpInitIf[6]);
    zpcre_free_metasource(zpInitIf[7]);
}

// 读取主配置文件(正则取词有问题!!! 暂不影响功能，后续排查)
void
zparse_conf_and_init_env(const char *zpConfPath) {
// TEST: PASS
    zPCREInitInfo *zpInitIf[2];
    zPCRERetInfo *zpRetIf[2];
    char *zpRes = NULL;
    FILE *zpFile;

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("(?<=^\\[)\\S+(?=\\]\\s*($|#))");  // 匹配区块标题：[REPO] 或 [INOTIFY]

    for (_i zLineNum = 1; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); zLineNum++) {
        zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);  // 若是空白行或注释行，跳过
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
        } else {
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
        }

        if (strlen(zpRes) == 0) { continue; }

zMark:  // 解析函数据行完毕后，跳转到此处
        if (NULL == zpRes) { return; }
        zpRetIf[1] = zpcre_match(zpInitIf[1], zpRes, 0); // 匹配区块标题，根据标题名称调用对应的解析函数
        if (0 == zpRetIf[1]->cnt) {  // 若在区块标题之前检测到其它语句，报错后退出
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 区块标题之前不能有其它语句\033[00m\n", zLineNum ,zpRes);
            zpcre_free_tmpsource(zpRetIf[1]);
            exit(1);
        } else {
            if (0 == strcmp("REPO", zpRetIf[1]->p_rets[0])) {
                zparse_REPO(zpFile, &zpRes, &zLineNum);
                zpcre_free_tmpsource(zpRetIf[1]);
                goto zMark;
            } else if (0 == strcmp("INOTIFY", zpRetIf[1]->p_rets[0])) {
                zinit_env();  // 代码库信息读取完毕后，初始化git_shadow整体运行环境
                zparse_INOTIFY_and_add_watch(zpFile, &zpRes, &zLineNum);
                zpcre_free_tmpsource(zpRetIf[1]);
                goto zMark;
            } else {  // 若检测到无效区块标题，报错后退出
                zPrint_Time();
                fprintf(stderr, "\033[31m[Line %d] \"%s\": 无效的区块标题\033[00m\n", zLineNum ,zpRes);
                zpcre_free_tmpsource(zpRetIf[1]);
                exit(1);
            }
        }
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
