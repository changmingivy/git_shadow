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
    zObjInfo *zpCurIf = (zObjInfo *) zpIf;

    _i zWid = inotify_add_watch(zInotifyFD, zpCurIf->path, zBaseWatchBit | IN_DONT_FOLLOW);
    zCheck_Negative_Return(zWid,);

    if (NULL != zpPathHash[zWid]) {
        free(zpPathHash[zWid]);  // Free old memory before using the same index again.
    }
    zpPathHash[zWid] = zpCurIf;

    if (0 == zpCurIf->RecursiveMark) {
        return;  // 如果不需要递归监控子目录，到此返回，不再往下执行
    }

    DIR *zpDir = opendir(zpCurIf->path);
    zCheck_Null_Return(zpDir,);

    size_t zLen = strlen(zpCurIf->path);
    struct dirent *zpEntry;

    zPCRERetInfo *zpRetIf = NULL;
    while (NULL != (zpEntry = readdir(zpDir))) {
        if (DT_DIR == zpEntry->d_type) {
            zpRetIf = zpcre_match(zpCurIf->p_PCREInitIf, zpEntry->d_name, 0);
            if (0 != zpRetIf->cnt) {
                zpcre_free_tmpsource(zpRetIf);
                continue;
            }

            // Must do "malloc" here.
            zObjInfo *zpSubIf = malloc(zSizeOf(zObjInfo)
                    + 2
                    + zLen
                    + strlen(zpEntry->d_name));
            zCheck_Null_Return(zpSubIf,);

            // 为新监控目标填冲基本信息
            zpSubIf->RepoId = zpCurIf->RepoId;
            zpSubIf->UpperWid = zpCurIf->UpperWid;
            zpSubIf->CallBack = zpCurIf->CallBack;
            zpSubIf->RecursiveMark = zpCurIf->RecursiveMark;
            zpSubIf->p_PCREInitIf = zpCurIf->p_PCREInitIf;

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
    char zBuf[zCommonBufSiz]
        __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t zLen;

    const struct inotify_event *zpEv;
    char *zpOffset;

    for (;;) {
        zLen = read(zInotifyFD, zBuf, zSizeOf(zBuf));
        zCheck_Negative_Return(zLen,);

        for (zpOffset = zBuf; zpOffset < zBuf + zLen;
                zpOffset += zSizeOf(struct inotify_event) + zpEv->len) {
            zpEv = (struct inotify_event *)zpOffset;

            if (1 == zpPathHash[zpEv->wd]->RecursiveMark
                    && (zpEv->mask & IN_ISDIR)
                    && ((zpEv->mask & IN_CREATE) || (zpEv->mask & IN_MOVED_TO))) {
                // If a new subdir is created or moved in, add it to the watch list.
                // Must do "malloc" here.
                // 分配的内存包括路径名称长度
                zObjInfo *zpSubIf = malloc(zSizeOf(zObjInfo)
                        + 2
                        + strlen(zpPathHash[zpEv->wd]->path)
                        + zpEv->len);
                zCheck_Null_Return(zpSubIf,);

                // 为新监控目标填冲基本信息
                zpSubIf->RepoId = zpPathHash[zpEv->wd]->RepoId;
                zpSubIf->UpperWid = zpPathHash[zpEv->wd]->UpperWid;
                zpSubIf->CallBack = zpPathHash[zpEv->wd]->CallBack;
                zpSubIf->RecursiveMark = zpPathHash[zpEv->wd]->RecursiveMark;
                zpSubIf->p_PCREInitIf = zpPathHash[zpEv->wd]->p_PCREInitIf;

                strcpy(zpSubIf->path, zpPathHash[zpEv->wd]->path);
                strcat(zpSubIf->path, "/");
                strcat(zpSubIf->path, zpEv->name);

                zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
            }

            if (NULL != zpPathHash[zpEv->wd]->CallBack) {
                zAdd_To_Thread_Pool(zpPathHash[zpEv->wd]->CallBack, NULL);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////
//void
//ztest_func(void *_) {
//    fprintf(stderr, "Success!\n");
//}
//
//_i
//main(void) {
//    zObjInfo *zpObjIf = malloc(zSizeOf(zObjInfo) + zBytes(22));
//    zpObjIf->ObjPathOffset = 5;  // test
//    zpObjIf->RegexStrOffset = 10;  // ^[.]{1,2}$
//    zpObjIf->CallBackId = 3;
//    zpObjIf->RecursiveMark = 1;
//    strcpy(zpObjIf->StrBuf, "/tmp");
//    strcpy(zpObjIf->StrBuf + 5, "/tmp");
//    strcpy(zpObjIf->StrBuf + 10, "^[.]{1,2}$");
//
//    zInotifyFD = inotify_init();  // 生成inotify master fd
//    zinotify_add_top_watch(zpObjIf);
//    zinotify_wait(NULL);
//    return 0;
//}
