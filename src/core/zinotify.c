#ifndef _Z
    #include "../zmain.c"
#endif

#define zBaseWatchBit \
    IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF

/*************
 * ADD WATCH *
 *************/
void
zinotify_add_sub_watch(void *zpIf) {
// TEST: PASS
    struct zObjInfo *zpCurIf = (struct zObjInfo *) zpIf;

    _i zWid = inotify_add_watch(zInotifyFD, zpCurIf->path, zBaseWatchBit | IN_DONT_FOLLOW);
    zCheck_Negative_Return(zWid,);
    if (0 > zpCurIf->UpperWid) {
        zpCurIf->UpperWid = zWid;
    }
    if (NULL != zpObjHash[zWid]) {
        free(zpObjHash[zWid]);  // Free old memory before using the same index again.
    }
    zpObjHash[zWid] = zpCurIf;

    if (0 == zpCurIf->RecursiveMark) {
        return;  // 如果不需要递归监控子目录，到此返回，不再往下执行
    }

    DIR *zpDir = opendir(zpCurIf->path);
    zCheck_Null_Return(zpDir,);

    size_t zLen = strlen(zpCurIf->path);
    struct dirent *zpEntry;

    zPCRERetInfo *zpRetIf = NULL;
    zPCREInitInfo *zpPCREInitIf = zpcre_init(zpCurIf->zpRegexPattern);  // 暂时在此处编译正则表达式，后续优化，当前效率较低
    while (NULL != (zpEntry = readdir(zpDir))) {
        if (DT_DIR == zpEntry->d_type) {
            zpRetIf = zpcre_match(zpPCREInitIf, zpEntry->d_name, 0);
            if (0 == zpRetIf->cnt) {
                zpcre_free_tmpsource(zpRetIf);
            } else {
                zpcre_free_tmpsource(zpRetIf);
                continue;
            }

            // Must do "malloc" here.
            struct zObjInfo *zpSubIf = malloc(zSizeOf(struct zObjInfo) + 2 + zLen + strlen(zpEntry->d_name));
            zCheck_Null_Return(zpSubIf,);

            // 为新监控目标填充基本信息
            zpSubIf->RepoId = zpCurIf->RepoId;
            zpSubIf->UpperWid = zpCurIf->UpperWid;
            zpSubIf->zpRegexPattern = zpCurIf->zpRegexPattern;
            zpSubIf->CallBack = zpCurIf->CallBack;
            zpSubIf->RecursiveMark = zpCurIf->RecursiveMark;

            strcpy(zpSubIf->path, zpCurIf->path);
            strcat(zpSubIf->path, "/");
            strcat(zpSubIf->path, zpEntry->d_name);

            zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
        }
    }
//  free(zpCurIf);  // Can safely free, but NOT free it!
    closedir(zpDir);
}

/********************
 * DEAL WITH EVENTS *
 ********************/
void
zinotify_wait(void *_) {
// TEST: PASS
    char zBuf[zCommonBufSiz]
    __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t zLen;

    const struct inotify_event *zpEv;
    char *zpOffset;

    for (;;) {
    zCheck_Negative_Exit( zLen = read(zInotifyFD, zBuf, zSizeOf(zBuf)) );

        for (zpOffset = zBuf; zpOffset < zBuf + zLen;
            zpOffset += zSizeOf(struct inotify_event) + zpEv->len) {
            zpEv = (struct inotify_event *)zpOffset;

            if (1 == zpObjHash[zpEv->wd]->RecursiveMark
                && (zpEv->mask & IN_ISDIR)
                && ((zpEv->mask & IN_CREATE) || (zpEv->mask & IN_MOVED_TO))) {

                // If a new subdir is created or moved in, add it to the watch list.
                zPCRERetInfo *zpRetIf = NULL;  // 首先检测是否在被忽略的范围之内
                zPCREInitInfo *zpPCREInitIf = zpcre_init(zpObjHash[zpEv->wd]->zpRegexPattern);  // 暂时在此处编译正则表达式，后续优化，当前效率较低
                zpRetIf = zpcre_match(zpPCREInitIf, zpEv->name, 0);
                if (0 == zpRetIf->cnt) {
                    zpcre_free_tmpsource(zpRetIf);
                } else {
                    zpcre_free_tmpsource(zpRetIf);
                    continue;
                }

                // Must do "malloc" here.
                // 分配的内存包括路径名称长度
                struct zObjInfo *zpSubIf = malloc(zSizeOf(struct zObjInfo) + 2 + strlen(zpObjHash[zpEv->wd]->path) + zpEv->len);
                zCheck_Null_Return(zpSubIf,);

                // 为新监控目标填冲基本信息
                zpSubIf->RepoId = zpObjHash[zpEv->wd]->RepoId;
                zpSubIf->UpperWid = zpObjHash[zpEv->wd]->UpperWid;
                zpSubIf->zpRegexPattern = zpObjHash[zpEv->wd]->zpRegexPattern;
                zpSubIf->CallBack = zpObjHash[zpEv->wd]->CallBack;
                zpSubIf->RecursiveMark = zpObjHash[zpEv->wd]->RecursiveMark;

                strcpy(zpSubIf->path, zpObjHash[zpEv->wd]->path);
                strcat(zpSubIf->path, "/");
                strcat(zpSubIf->path, zpEv->name);

                zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
            }

            if (NULL != zpObjHash[zpEv->wd]->CallBack) {
                zAdd_To_Thread_Pool(zpObjHash[zpEv->wd]->CallBack, zpObjHash[zpEv->wd]);
            }
        }
    }
}
