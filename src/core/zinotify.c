#ifndef _Z
    #include "../zmain.c"
#endif

#define zBaseWatchBit \
    IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF

/*************
 * ADD WATCH *
 *************/
void *
zinotify_add_sub_watch(void *zpIf) {
    struct zObjInfo *zpCurIf, *zpSubIf;
    _i zWid;

    zpCurIf = (struct zObjInfo *) zpIf;
    zCheck_Negative_Exit(zWid = inotify_add_watch(zppGlobRepoIf[zpCurIf->RepoId]->InotifyFd, zpCurIf->p_path, zBaseWatchBit | IN_DONT_FOLLOW));

    // 判断是否是顶层被监控对象
    if (-1 == zpCurIf->UpperWid) { zpCurIf->UpperWid = zWid; }

    zppGlobRepoIf[zpCurIf->RepoId]->p_ObjHash[zWid] = zpCurIf;

    // 如果不需要递归监控子目录，到此返回，不再往下执行
    if (0 == zpCurIf->RecursiveMark) { return NULL; }

    DIR *zpDir = opendir(zpCurIf->p_path);
    zCheck_Null_Return(zpDir, NULL);

    size_t zLen = strlen(zpCurIf->p_path);
    struct dirent *zpEntry;

    while (NULL != (zpEntry = readdir(zpDir))) {
        if (DT_DIR == zpEntry->d_type) {  // _BSD_SOURCE
            if (0 == strcmp(".", zpEntry->d_name) || 0 == strcmp("..", zpEntry->d_name)) {
                continue;  /* 忽略 '.' 与 '..' 两个路径，否则会陷入死循环 */
            }

            // Must do "alloc" here.
            zpSubIf = zalloc_cache(zpCurIf->RepoId, sizeof(struct zObjInfo) + 2 + zLen + strlen(zpEntry->d_name));

            // 为新监控目标填充基本信息
            zpSubIf->RepoId = zpCurIf->RepoId;
            zpSubIf->UpperWid = zpCurIf->UpperWid;
            zpSubIf->CallBack = zpCurIf->CallBack;
            zpSubIf->RecursiveMark = zpCurIf->RecursiveMark;

            strcpy(zpSubIf->p_path, zpCurIf->p_path);
            strcat(zpSubIf->p_path, "/");
            strcat(zpSubIf->p_path, zpEntry->d_name);

            zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
        }
    }

    closedir(zpDir);
    return NULL;
}

/********************
 * DEAL WITH EVENTS *
 ********************/
void *
zinotify_wait(void *zpIf) {
// TEST: PASS
    char zBuf[zCommonBufSiz] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t zLen;

    const struct inotify_event *zpEv;
    char *zpOffset;

    _i zRepoId = * ((_i *) zpIf);

    for (;;) {
    zCheck_Negative_Exit(zLen = read(zppGlobRepoIf[zRepoId]->InotifyFd, zBuf, zSizeOf(zBuf)));

        for (zpOffset = zBuf; zpOffset < zBuf + zLen;
            zpOffset += zSizeOf(struct inotify_event) + zpEv->len) {
            zpEv = (struct inotify_event *)zpOffset;

            if (NULL != zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->CallBack) {
                zAdd_To_Thread_Pool(zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->CallBack, zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]);
            }

            /* If a new subdir is created or moved in, add it to the watch list */
            if (1 == zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->RecursiveMark
                && (zpEv->mask & IN_ISDIR)
                && ((zpEv->mask & IN_CREATE) || (zpEv->mask & IN_MOVED_TO))) {

                if (0 == strcmp(".", zpEv->name) || 0 == strcmp("..", zpEv->name)) {
                    continue;  /* 忽略 '.' 与 '..' 两个路径，否则会陷入死循环 */
                }

                // Must do "alloc" here; 分配的内存包括路径名称长度
                struct zObjInfo *zpSubIf = zalloc_cache(zRepoId, zSizeOf(struct zObjInfo) + 2 + strlen(zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->p_path) + zpEv->len);

                // 为新监控目标填冲基本信息
                zpSubIf->RepoId = zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->RepoId;
                zpSubIf->UpperWid = zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->UpperWid;
                zpSubIf->CallBack = zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->CallBack;
                zpSubIf->RecursiveMark = zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->RecursiveMark;

                strcpy(zpSubIf->p_path, zppGlobRepoIf[zRepoId]->p_ObjHash[zpEv->wd]->p_path);
                strcat(zpSubIf->p_path, "/");
                strcat(zpSubIf->p_path, zpEv->name);

                zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
            }
        }
    }

    return NULL;
}
