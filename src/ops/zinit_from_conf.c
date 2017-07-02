#ifndef _Z
    #include "../zmain.c"
#endif

jmp_buf zJmpEnv;

/*************************
 * DEAL WITH CONFIG FILE *
 *************************/
// 取 [REPO] 区域配置条目
void
zparse_conf_REPO(FILE *zpFile, char **zppRes, _i *zpLineNum) {
    zPCREInitInfo *zpInitIf[4];
    zPCRERetInfo *zpRetIf[4];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+/\\S+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("\\d+(?=\\s+/\\S+\\s*($|#))");  // 取代码库编号
    zpInitIf[3] = zpcre_init("/\\S+(?=\\s*($|#))");  // 取代码库路径
	
	zMem_Alloc(zppRepoPathList, char *, zMaxRepoNum); // 预分配足够大的内存空间，待获取实际的代码库数量后，再缩减到实际所需空间

	_i zRealRepoNum = 0;
    while (NULL != (*zppRes = zget_one_line_from_FILE(zpFile))) {
		(*zpLineNum)++;  // 维持行号
        (*zppRes)[strlen(*zppRes) - 1] = '\0';  // 清除行末的换行符 '\n'

        zpRetIf[0] = zpcre_match(zpInitIf[0], *zppRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
		} else {  // 若是空白行或注释行，跳过
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
		}

        zpRetIf[1] = zpcre_match(zpInitIf[1], *zppRes, 0);
        if (0 == zpRetIf[1]->cnt) {  // 若检测到格式有误的语句，报错后退出
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 语法格式错误\033[00m\n", *zpLineNum ,*zppRes);
            zpcre_free_tmpsource(zpRetIf[1]);
			exit(1);
		} else {
            zpcre_free_tmpsource(zpRetIf[1]);
		}

		zRealRepoNum++;

        zpRetIf[2] = zpcre_match(zpInitIf[2], *zppRes, 0);
        zpRetIf[3] = zpcre_match(zpInitIf[3], *zppRes, 0);
		zppRepoPathList[atoi(zpRetIf[2]->p_rets[0])] = zpRetIf[3]->p_rets[0];

		zpcre_free_tmpsource(zpRetIf[2]);
		zpcre_free_tmpsource(zpRetIf[3]);
	}

	zpcre_free_metasource(zpInitIf[0]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[3]);

	zRepoNum = zRealRepoNum;
	zppRepoPathList = realloc(zppRepoPathList, zRepoNum * sizeof(char *));  // 缩减到实际所需空间
	zCheck_Null_Exit(zppRepoPathList);

	longjmp(zJmpEnv, 1);  // 跳回到上层函数继续执行
}

// 取 [INOTIFY] 区域配置条目
zObjInfo *
zparse_conf_INOTIFY(FILE *zpFile, char **zppRes, _i *zpLineNum) {
	zObjInfo *zpObjIf;
	_i zRepoId;
    zPCREInitInfo *zpInitIf[7];
    zPCRERetInfo *zpRetIf[7];

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("\\s*\\d+\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#)");  // 检测整体格式是否合法
    zpInitIf[2] = zpcre_init("\\d+(?=\\s+\\S+\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#)");  // 取所属代码库编号ID
    zpInitIf[3] = zpcre_init("\\S+(?=\\s+\\S+\\s+\\S+\\s+\\d+\\s*($|#)");  // 取被监控对象路径
    zpInitIf[4] = zpcre_init("\\S+(?=\\s+\\S+\\s+\\d+\\s*($|#)");  // 取正则表达式子符串
    zpInitIf[5] = zpcre_init("\\S+(?=\\s+\\d+\\s*($|#)");  // 取是否递归的标志位，可以为：Y/N/YES/NO/yes/y/n/no 等
    zpInitIf[6] = zpcre_init("\\S+(?=\\s*($|#))");  // 回调函数ID
	
    while (NULL != (*zppRes = zget_one_line_from_FILE(zpFile))) {
		(*zpLineNum)++;  // 维持行号
        (*zppRes)[strlen(*zppRes) - 1] = '\0';  // 清除行末的换行符 '\n'

        zpRetIf[0] = zpcre_match(zpInitIf[0], *zppRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
		} else {  // 若是空白行或注释行，跳过
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
		}

        zpRetIf[1] = zpcre_match(zpInitIf[1], *zppRes, 0);
        if (0 == zpRetIf[1]->cnt) {  // 若检测到格式有误的语句，报错后退出
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 语法格式错误\033[00m\n", *zpLineNum ,*zppRes);
            zpcre_free_tmpsource(zpRetIf[1]);
			exit(1);
		} else {
            zpcre_free_tmpsource(zpRetIf[1]);
		}

        zpRetIf[2] = zpcre_match(zpInitIf[2], *zppRes, 0);
        zpRetIf[3] = zpcre_match(zpInitIf[3], *zppRes, 0);
        zpRetIf[4] = zpcre_match(zpInitIf[4], *zppRes, 0);
        zpRetIf[5] = zpcre_match(zpInitIf[5], *zppRes, 0);
        zpRetIf[6] = zpcre_match(zpInitIf[6], *zppRes, 0);

		zRepoId = strtol(zpRetIf[2]->p_rets[0], NULL, 10);
		if ('/' == zpRetIf[3]->p_rets[0][0]) {
			zpObjIf = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[3]->p_rets[0]));  // 为新条目分配内存
			zCheck_Null_Exit(zpObjIf);
			strcpy(zpObjIf->path, zpRetIf[3]->p_rets[0]); // 被监控对象绝对路径
		} else {
			zpObjIf = malloc(sizeof(zObjInfo) + 2 + strlen(zpRetIf[3]->p_rets[0]) + strlen(zppRepoPathList[zRepoId]));  // 为新条目分配内存
			zCheck_Null_Exit(zpObjIf);
			strcpy(zpObjIf->path, zppRepoPathList[zRepoId]);
			strcat(zpObjIf->path, "/");
			strcat(zpObjIf->path, zpRetIf[3]->p_rets[0]); // 被监控对象绝对路径
		}

		zpObjIf->RepoId = zRepoId;  // 所属版本库ID
		zMem_Alloc(zpObjIf->zpRegexPattern, char, 1 + strlen(zpRetIf[4]->p_rets[0]));
		strcpy(zpObjIf->zpRegexPattern, zpRetIf[4]->p_rets[0]); // 正则字符串
		zpObjIf->RecursiveMark = ('y' == tolower(zpRetIf[5]->p_rets[0][0])) ? 1 : 0; // 递归标识
		zpObjIf->CallBack = zCallBackList[strtol(zpRetIf[6]->p_rets[0], NULL, 10)];  // 回调函数

		zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);  // 检测到有效条目，加入inotify监控队列

		zpcre_free_tmpsource(zpRetIf[2]);
		zpcre_free_tmpsource(zpRetIf[3]);
		zpcre_free_tmpsource(zpRetIf[4]);
		zpcre_free_tmpsource(zpRetIf[5]);
		zpcre_free_tmpsource(zpRetIf[6]);
	}

	zpcre_free_metasource(zpInitIf[0]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[3]);
	zpcre_free_metasource(zpInitIf[4]);
	zpcre_free_metasource(zpInitIf[5]);
	zpcre_free_metasource(zpInitIf[6]);

	longjmp(zJmpEnv, 1);  // 跳回到上层函数继续执行
}

// 读取主配置文件
void
zparse_conf_and_add_top_watch(const char *zpConfPath) {
    zPCREInitInfo *zpInitIf[2];
    zPCRERetInfo *zpRetIf[2];

    char *zpRes = NULL;
	zObjInfo *zpTopObjIf;

    FILE *zpFile = fopen(zpConfPath, "r");
	zCheck_Null_Exit(zpFile);

    zpInitIf[0] = zpcre_init("^\\s*($|#)");  // 匹配空白行或注释行
    zpInitIf[1] = zpcre_init("(?<=^\\[)\\S+(?=\\]\\s*($|#))");  // 匹配区块标题：[REPO] 或 [INOTIFY]

    for (_i zLineNum = 1; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); zLineNum++) {
        zpRes[strlen(zpRes) - 1] = '\0';  // 清除行末的换行符 '\n'

        zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);  // 若是空白行或注释行，跳过
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
		} else {
            zpcre_free_tmpsource(zpRetIf[0]);
            continue;
		}

		setjmp(zJmpEnv);  // 解析函数据行完毕后，跳转到此处
        zpRetIf[1] = zpcre_match(zpInitIf[1], zpRes, 0); // 匹配区块标题，根据标题名称调用对应的解析函数
        if (0 == zpRetIf[1]->cnt) {  // 若在区块标题之前检测到其它语句，报错后退出
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": 区块标题之前不能有其它语句\033[00m\n", zLineNum ,zpRes);
            zpcre_free_tmpsource(zpRetIf[0]);
			exit(1);
        } else {
			if (0 == strcmp("REPO", zpRetIf[1]->p_rets[0])) {
        	    zpcre_free_tmpsource(zpRetIf[1]);
				zparse_conf_REPO(zpFile, &zpRes, &zLineNum);
			} else if (0 == strcmp("INOTIFY", zpRetIf[1]->p_rets[0])) {
        	    zpcre_free_tmpsource(zpRetIf[1]);
				zpTopObjIf = zparse_conf_INOTIFY(zpFile, &zpRes, &zLineNum);
				zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpTopObjIf);
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

void
ztest_func(void *_) {
	printf("Success!\n");
}

_i
main(void) {
    zInotifyFD = inotify_init();  // 生成inotify master fd
	zthread_poll_init();
    zCallBackList[0] = ztest_func;
	zparse_conf_and_add_top_watch("/tmp/sample.conf");
    zAdd_To_Thread_Pool(zinotify_wait, NULL);  // 主线程等待事件发生
	sleep(90);
}
