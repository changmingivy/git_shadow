#ifndef _Z
    #include "../zmain.c"
#endif

/*************************
 * DEAL WITH CONFIG FILE *
 *************************/

// 读取主配置文件
void
zparse_conf_and_add_top_watch(const char *zpConfPath) {
    zObjInfo *zpObjIf;
    zPCREInitInfo *zpInitIf[8];
    zPCRERetInfo *zpRetIf[8];
    char *zpRes = NULL;
    FILE *zpFile = fopen(zpConfPath, "r");
    _i zFd[2];

    zpInitIf[0] = zpcre_init("^\\s*(\\S+\\s+){4}\\S+\\s*(#|$)");  // 检测每一行五个字段是否齐全
    zpInitIf[1] = zpcre_init("^\\s*($|#)");  // 匹配空白行（跳过空白行）
    zpInitIf[2] = zpcre_init("\\S+$");  // 匹配目标文本的最末一个字段

    zpInitIf[3] = zpcre_init("^\\s*/[/\\S]*");  // 匹配代码库绝对路径
    zpInitIf[4] = zpcre_init("^\\s*/[/\\S]*\\s+[/\\S]+");  // 匹配要监控的目标的路径（绝对路径或相对于代码库的相对路径）
    zpInitIf[5] = zpcre_init("^\\s*/[/\\S]*\\s+[/\\S]+\\s+\\d+");  // 匹配是否递归监控的标识数字（0 或 非0）
    zpInitIf[6] = zpcre_init("^\\s*/[/\\S]*\\s+[/\\S]+\\s+\\d+\\s+\\d+");  // 匹配监控到事件后触发的回调函数索引
    zpInitIf[7] = zpcre_init("^\\s*/[/\\S]*\\s+[/\\S]+\\s+\\d+\\s+\\d+\\s+\\S+");  // 匹配正则表达式字符串（用于表示将被inotify忽略的路径名称规则）

    for (_i i = 0; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); i++) {
        zpRes[strlen(zpRes) - 1] = '\0';  // 清除行末的换行符 '\n'

        zpRetIf[1] = zpcre_match(zpInitIf[1], zpRes, 0);  // 若是空白行，跳过
        if (0 < zpRetIf[1]->cnt) {
            zpcre_free_tmpsource(zpRetIf[1]);
            continue;
        }
        zpcre_free_tmpsource(zpRetIf[1]);

        zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
        if (0 == zpRetIf[0]->cnt) {
            zpcre_free_tmpsource(zpRetIf[0]);
            zPrint_Time();
            fprintf(stderr, "\033[31m[Line %d] \"%s\": Invalid entry.\033[00m\n", i ,zpRes);  // 若不符合整体格式，打印错误信息后跳过此行
            continue;
        } else {
            zpRetIf[3] = zpcre_match(zpInitIf[3], zpRetIf[0]->p_rets[0], 0);
            if ((-1 == (zFd[0] = open(zpRetIf[3]->p_rets[0], O_RDONLY | O_DIRECTORY)))) {
                zPrint_Time();
                fprintf(stderr, "\033[31m[Line %d] \"%s\": %s\033[00m\n", i, zpRes, strerror(errno));

                zpcre_free_tmpsource(zpRetIf[3]);
                zpcre_free_tmpsource(zpRetIf[0]);
                close(zFd[0]);
                continue;  // 若代码库绝对路径无效，跳过
            }

            zpRetIf[4] = zpcre_match(zpInitIf[4], zpRetIf[0]->p_rets[0], 0);
            if ((-1 == (zFd[1] = openat(zFd[0], zpRetIf[4]->p_rets[0], O_RDONLY)))) {
                zPrint_Time();
                fprintf(stderr, "\033[31m[Line %d] \"%s\": %s\033[00m\n", i, zpRes, strerror(errno));

                zpcre_free_tmpsource(zpRetIf[4]);
                zpcre_free_tmpsource(zpRetIf[0]);
                close(zFd[1]);
                close(zFd[0]);
                continue;  // 若指定的被监控目标路径无效，跳过
            }

            zpRetIf[5] = zpcre_match(zpInitIf[5], zpRetIf[0]->p_rets[0], 0);
            zpRetIf[6] = zpcre_match(zpInitIf[6], zpRetIf[0]->p_rets[0], 0);
            zpRetIf[7] = zpcre_match(zpInitIf[7], zpRetIf[0]->p_rets[0], 0);

			// 为全局变量（代码库总数及对应的路径名称）斌值
            zRepoNum = i + 1;
			zppRepoPathList = realloc(zppRepoPathList, zRepoNum * sizeof(char *));
			zCheck_Null_Exit(zppRepoPathList);

			// (1)取代码库路径   (2)为RepoId字段斌值
            zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[3]->p_rets[0],0);
			zMem_Alloc(zppRepoPathList[i], char, 1 + strlen(zpRetIf[2]->p_rets[0]));
			strcpy(zppRepoPathList[i], zpRetIf[2]->p_rets[0]);
			zpObjIf->RepoId = i;  // 为被监控对象的代码库ID字段斌值

			zpObjIf = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
            strcpy(zpObjIf->path, zpRetIf[2]->p_rets[0]);  // 将代码库绝对路径名称复制到被监控对象名称缓存区
            zpcre_free_tmpsource(zpRetIf[2]);

			// 取被监控对象路径
            zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[4]->p_rets[0],0);
			if ('/' == zpRetIf[2]->p_rets[0][0]) {  // 若被监控对象路径名称是绝对路径，覆盖先前复制过来的代码库路径
				zpObjIf = realloc(zpObjIf, sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
				zCheck_Null_Exit(zpObjIf);
            	strcpy(zpObjIf->path, zpRetIf[2]->p_rets[0]);
			} else {
				zpObjIf = realloc(zpObjIf, sizeof(zObjInfo) + 2 + strlen(zpObjIf->path) + strlen(zpRetIf[2]->p_rets[0]));
				zCheck_Null_Exit(zpObjIf);
            	strcat(zpObjIf->path, "/");  // 若被监控对象的路径名称是相对路径，则首先在代码库路径后拼接一个'/'
            	strcat(zpObjIf->path, zpRetIf[2]->p_rets[0]);  // 最后拼接被监控对象的相对路径
			}
            zpcre_free_tmpsource(zpRetIf[2]);

			// 取递归标志
            zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[5]->p_rets[0],0);
            zpObjIf->RecursiveMark = atoi(zpRetIf[2]->p_rets[0]);  // 递归标志
            zpcre_free_tmpsource(zpRetIf[2]);

			// 取回调函数指针
            zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[6]->p_rets[0],0);
            zpObjIf->CallBack = zCallBackList[atoi(zpRetIf[2]->p_rets[0])];  // 以配置文件中的ID索引函数指针
            zpcre_free_tmpsource(zpRetIf[2]);

			// 取正则表达式结构体指针
            zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[7]->p_rets[0],0);
			zpObjIf->p_PCREInitIf = zpcre_init(zpRetIf[2]->p_rets[0]);  // 首先编译正则表达式字符串
            zpcre_free_tmpsource(zpRetIf[2]);

			// 释放本次匹配使用的所有临时资源
            zpcre_free_tmpsource(zpRetIf[7]);
            zpcre_free_tmpsource(zpRetIf[6]);
            zpcre_free_tmpsource(zpRetIf[5]);
            zpcre_free_tmpsource(zpRetIf[4]);
            zpcre_free_tmpsource(zpRetIf[3]);
            zpcre_free_tmpsource(zpRetIf[1]);
            zpcre_free_tmpsource(zpRetIf[0]);

			// 执行到此处已生成有效信息，将其添加到监控队列
			zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpObjIf);
        }
    }

    zpcre_free_metasource(zpInitIf[7]);
    zpcre_free_metasource(zpInitIf[6]);
    zpcre_free_metasource(zpInitIf[5]);
    zpcre_free_metasource(zpInitIf[4]);
    zpcre_free_metasource(zpInitIf[3]);
    zpcre_free_metasource(zpInitIf[2]);
    zpcre_free_metasource(zpInitIf[1]);
    zpcre_free_metasource(zpInitIf[0]);

    fclose(zpFile);
}

// 监控主配置文件的变动
void
zconfig_file_monitor(const char *zpConfPath) {
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
