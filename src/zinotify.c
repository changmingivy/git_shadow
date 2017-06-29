#ifndef _Z
	#include "zmain.c"
#endif

#define zBaseWatchBit \
	IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF

/*************
 * ADD WATCH *
 *************/
void
zinotify_add_sub_watch(void *zpIf) {
	zSubObjInfo *zpCurIf = (zSubObjInfo *) zpIf;

	if (-1 == chdir(zpCurIf->path)) { return; }  // Robustness
	_i zWid = inotify_add_watch(zInotifyFD, zpCurIf->path, zBaseWatchBit | IN_DONT_FOLLOW);
	zCheck_Negative_Exit(zWid);

	if (NULL != zpPathHash[zWid]) { free(zpPathHash[zWid]); }  // Free old memory before using the same index again.
	zpPathHash[zWid] = zpCurIf;

	if (-1 == chdir(zpCurIf->path)) { return; }  // Robustness
	DIR *zpDir = opendir(zpCurIf->path);
	zCheck_Null_Exit(zpDir);

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
			zSubObjInfo *zpSubIf = malloc(zSizeOf(zSubObjInfo) + 2 + zLen + strlen(zpEntry->d_name));
			zCheck_Null_Exit(zpSubIf);

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

//	free(zpCurIf);  // Can safely free, but NOT free it!
	closedir(zpDir);
}

void
zinotify_add_top_watch(void *zpIf) {
	zObjInfo *zpObjIf = (zObjInfo *) zpIf;
	char *zpPath = zpObjIf->StrBuf + zpObjIf->ObjPathOffset;
	size_t zLen = strlen(zpPath);

	zSubObjInfo *zpTopIf = malloc(zSizeOf(zSubObjInfo) + 1 + zLen);
	zCheck_Null_Exit(zpTopIf);

	zpTopIf->p_PCREInitIf = zpcre_init(zpObjIf->StrBuf + zpObjIf->RegexStrOffset);
	zpTopIf->RecursiveMark = zpObjIf->RecursiveMark;
	zpTopIf->CallBack = (-1 == zpObjIf->CallBackId) ? NULL : zCallBackList[zpObjIf->CallBackId];

	zpTopIf->UpperWid = inotify_add_watch(zInotifyFD, zpPath, zBaseWatchBit | IN_DONT_FOLLOW);
	zCheck_Negative_Exit(zpTopIf->UpperWid);

	strcpy(zpTopIf->path, zpPath);
	zpPathHash[zpTopIf->UpperWid] = zpTopIf;

	if (((zObjInfo *) zpObjIf)->RecursiveMark) {
		struct dirent *zpEntry;

		DIR *zpDir = opendir(zpPath);
		zCheck_Null_Exit(zpDir);

		zPCRERetInfo *zpRetIf = NULL;
		while (NULL != (zpEntry = readdir(zpDir))) {
			if (DT_DIR == zpEntry->d_type) {
				zpRetIf = zpcre_match(zpTopIf->p_PCREInitIf, zpEntry->d_name, 0);
				if (0 != zpRetIf->cnt) {
					zpcre_free_tmpsource(zpRetIf);
					continue;
				}

				// Must do "malloc" here.
				zSubObjInfo *zpSubIf = malloc(zSizeOf(zSubObjInfo) 
						+ 2 + zLen + strlen(zpEntry->d_name));
				zCheck_Null_Exit(zpSubIf);
	
				zpSubIf->RecursiveMark = zpTopIf->RecursiveMark;
				zpSubIf->RepoId = zpTopIf->RepoId;
				zpSubIf->CallBack = zpTopIf->CallBack;
				zpSubIf->UpperWid = zpTopIf->UpperWid;
				zpSubIf->p_PCREInitIf = zpTopIf->p_PCREInitIf;
	
				strcpy(zpSubIf->path, zpPath);
				strcat(zpSubIf->path, "/");
				strcat(zpSubIf->path, zpEntry->d_name);

				zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
			}
		}
		closedir(zpDir);
	}
}

/********************
 * DEAL WITH EVENTS *
 ********************/
void
zinotify_wait(void *_) {
	char zBuf[zCommonBufSiz] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;

	for (;;) {
		zLen = read(zInotifyFD, zBuf, zSizeOf(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; zpOffset += zSizeOf(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			if (1 == zpPathHash[zpEv->wd]->RecursiveMark && (zpEv->mask & IN_ISDIR) && ( (zpEv->mask & IN_CREATE) || (zpEv->mask & IN_MOVED_TO) )) {
		  		// If a new subdir is created or moved in, add it to the watch list.
				// Must do "malloc" here.
				zSubObjInfo *zpSubIf = malloc(zSizeOf(zSubObjInfo) 
						+ 2 + strlen(zpPathHash[zpEv->wd]->path) + zpEv->len);
				zCheck_Null_Exit(zpSubIf);
	
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

//#define zNewDirEv 2
//#define zNewFileEv 1
//#define zModEv 0
//#define zDelFileEv -1
//#define zDelDirEv -2
//#define zDelFileSelfEv -3
//#define zDelDirSelfEv -4
//#define zUnknownEv -5
