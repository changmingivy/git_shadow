#ifndef _Z
    #include "zmain.c"
#endif

//jmp_buf zJmpEnv;

/**************************************
 * DEAL WITH CONFIG FILE AND INIT ENV *
 **************************************/
void
zinit_env(void) {
    _i zFd[2] = {0}, zRet = 0;

    zRet = pthread_rwlockattr_setkind_np(&zRWLockAttr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP); // 设置读写锁属性为写优先，如：正在更新缓存、正在布署过程中、正在撤销过程中等，会阻塞查询请求
    if (0 > zRet) {
        zPrint_Err(zRet, NULL, "rwlock set attr failed!");
        exit(1);
    }

    // 每个代码库近期布署日志信息的缓存
    zMem_C_Alloc(zppPreLoadLogVecIf, struct iovec *, zRepoNum);
    zMem_C_Alloc(zpPreLoadLogVecSiz, _i, zRepoNum);

    // 保存各个代码库的CURRENT标签所对应的SHA1 sig
    zMem_C_Alloc(zppCurTagSig, char *, zRepoNum);
    // 缓存'git diff'文件路径列表及每个文件内容变动的信息，与每个代码库一一对应
    zMem_C_Alloc(zppCacheVecIf, struct iovec *, zRepoNum);
    zMem_C_Alloc(zpCacheVecSiz, _i, zRepoNum);
    // 每个代码库对应meta、data、sig三个日志文件
    zMem_C_Alloc(zpLogFd[0], _i, zRepoNum);
    zMem_C_Alloc(zpLogFd[1], _i, zRepoNum);
    zMem_C_Alloc(zpLogFd[2], _i, zRepoNum);
    // 存储每个代码库对应的主机总数
    zMem_C_Alloc(zpTotalHost, _i, zRepoNum );
    // 即时存储已返回布署成功信息的主机总数
    zMem_C_Alloc(zpReplyCnt, _i, zRepoNum );
    // 索引每个代码库的读写锁
    zMem_C_Alloc(zpRWLock, pthread_rwlock_t, zRepoNum);

    // 每个代码库对应一个线性数组，用于接收每个ECS返回的确认信息
    // 同时基于这个线性数组建立一个HASH索引，以提高写入时的定位速度
    zMem_C_Alloc(zppDpResList, zDeployResInfo *, zRepoNum);
    zMem_C_Alloc(zpppDpResHash, zDeployResInfo **, zRepoNum);

    for (_i i = 0; i < zRepoNum; i++) {
        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zFd[0] = open(zppRepoPathList[i], O_RDONLY);
        zCheck_Negative_Exit(zFd[0]);

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
        if (0 != (zRet =pthread_rwlock_init(&(zpRWLock[i]), &zRWLockAttr))) {
            zPrint_Err(zRet, NULL, "Init deploy lock failed!");
            exit(1);
        }

        // 打开meta日志文件
        zpLogFd[0][i] = openat(zFd[0], zMetaLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[0][i]);
        // 打开data日志文件
        zpLogFd[1][i] = openat(zFd[0], zDataLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[1][i]);
        // 打开sig日志文件
        zpLogFd[2][i] = openat(zFd[0], zSigLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[2][i]);

        close(zFd[0]);  // zFd[0] 用完关闭

        zupdate_ipv4_db_all(&i);
        zppCacheVecIf[i] = zgenerate_cache(i);
    }
}

// 取 [REPO] 区域配置条目
void
zparse_REPO(FILE *zpFile, char **zppRes, _i *zpLineNum) {
// TEST: PASS
    _i zRepoId, zFd;
    zPCREInitInfo *zpInitIf[5];
    zPCRERetInfo *zpRetIf[5];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+/\\S+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("^\\s*\\[\\S+\\]\\s*($|#)");  // 检测是否已到下一个区块标题
    zpInitIf[3] = zpcre_init("\\d+(?=\\s+/\\S+\\s*($|#))");  // 取代码库编号
    zpInitIf[4] = zpcre_init("/\\S+(?=\\s*($|#))");  // 取代码库路径

    zMem_Alloc(zppRepoPathList, char *, zMaxRepoNum); // 预分配足够大的内存空间，待获取实际的代码库数量后，再缩减到实际所需空间

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

        zCheck_Negative_Exit( // 检测代码库路径合法性
                zFd = open(zpRetIf[4]->p_rets[0], O_RDONLY | O_DIRECTORY)
                );
        close(zFd);

        zRepoId = atoi(zpRetIf[3]->p_rets[0]);
        zMem_Alloc(zppRepoPathList[zRepoId], char, 1 + strlen(zpRetIf[4]->p_rets[0]));
        strcpy(zppRepoPathList[zRepoId], zpRetIf[4]->p_rets[0]);

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
    zppRepoPathList = realloc(zppRepoPathList, zRepoNum * sizeof(char *));  // 缩减到实际所需空间
    zCheck_Null_Exit(zppRepoPathList);
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
            zpObjIf = malloc(sizeof(zObjInfo) + 2 + strlen(zpRetIf[4]->p_rets[0]) + strlen(zppRepoPathList[zRepoId]));  // 为新条目分配内存
            zCheck_Null_Exit(zpObjIf);
            strcpy(zpObjIf->path, zppRepoPathList[zRepoId]);
            strcat(zpObjIf->path, "/");
            strcat(zpObjIf->path, zpRetIf[4]->p_rets[0]); // 被监控对象绝对路径
        }

        zCheck_Negative_Exit( // 检测被监控目标的路径合法性
                zFd = open(zpObjIf->path, O_RDONLY)
                );
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

    FILE *zpFile = fopen(zpConfPath, "r");
    zCheck_Null_Exit(zpFile);

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
    zCheck_Negative_Return(
            inotify_add_watch(
                zConfFD,
                zpConfPath,
                IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF
                ),
            );

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
