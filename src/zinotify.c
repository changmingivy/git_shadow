#ifndef _Z
	#define _XOPEN_SOURCE 700
	#define _BSD_SOURCE
	#include <sys/types.h>
	#include <sys/uio.h>
	#include <unistd.h>
	#include <sys/wait.h>
	#include <sys/stat.h>
	#include <sys/inotify.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <dirent.h>
	#include <errno.h>
	#include <time.h>
	#include <libgen.h>
	#include "zthread_pool.c"
	void zupdate_cache(void *zpIf);
	_i zRepoNum;  // how many repositories
	char **zppRepoList;  // each repository's absolute path
	char **zppCurTagSig;  // each repository's CURRENT(git tag) SHA1 sig
	struct  iovec **zppCacheVec;  // each repository's Global cache for git diff content
	_i *zpCacheVecSiz;
	_i *zpSpecWid;
#endif

#include <sys/inotify.h>

#define zHashSiz 8192
#define zBaseWatchBit \
	IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF

#define zNewDirEv 2
#define zNewFileEv 1
#define zModEv 0
#define zDelFileEv -1
#define zDelDirEv -2
#define zDelFileSelfEv -3
#define zDelDirSelfEv -4
#define zUnknownEv -5

/**********************
 * DATA STRUCT DEFINE *
 **********************/
typedef struct zObjInfo {
	struct zObjInfo *p_next;
	_i RecursiveMark;  // Mark recursive monitor.
	char path[];  // The directory to be monitored.
}zObjInfo;

typedef struct zSubObjInfo {
	_i RepoId;
	_i UpperWid;  // Maybe, used as REPO ID ?
	_s RecursiveMark;
	_s EvType;
	char path[];
}zSubObjInfo;

/**************
 * GLOBAL VAR *
 **************/
_i zInotifyFD;

pthread_mutex_t zCommonLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t zCommonCond = PTHREAD_COND_INITIALIZER;

zSubObjInfo *zpPathHash[zHashSiz];
char zBitHash[zHashSiz];

char *zpShellCommand;  // What to do when get events, two extra VAR available: $zEvType and $zEvPath

/*************
 * ADD WATCH *
 *************/
void
zinotify_add_sub_watch(void *zpIf) {
//TEST: PASS
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

	while (NULL != (zpEntry = readdir(zpDir))) {
		if (DT_DIR == zpEntry->d_type
				&& strcmp(".", zpEntry->d_name) 
				&& strcmp("..", zpEntry->d_name) 
				&& strcmp(".git", zpEntry->d_name)) {
			// Must do "malloc" here.
			zSubObjInfo *zpSubIf = malloc(sizeof(zSubObjInfo) 
					+ 2 + zLen + strlen(zpEntry->d_name));
			zCheck_Null_Exit(zpSubIf);

			zpSubIf->RecursiveMark = 1;
			zpSubIf->RepoId = zpCurIf->RepoId;
			zpSubIf->UpperWid = zpCurIf->UpperWid;

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
//TEST: PASS
	zObjInfo *zpObjIf = (zObjInfo *) zpIf;
	char *zpPath = zpObjIf->path;
	size_t zLen = strlen(zpPath);

	zSubObjInfo *zpTopIf = malloc(sizeof(zSubObjInfo) + 1 + zLen);
	zCheck_Null_Exit(zpTopIf);

	zpTopIf->RecursiveMark = zpObjIf->RecursiveMark;
	for (_i i = 0; i < zRepoNum; i++) {
		if (0 == strncmp(zppRepoList[i], zpPath, strlen(zppRepoList[i]))) {
			zpTopIf->RepoId = i;
		}
		if (0 == strcmp(zpPath + strlen(zppRepoList[i]), ".git/logs") || 0 == strcmp(zpPath + strlen(zppRepoList[i]), "/.git/logs")) {
			zpSpecWid[i] = zpTopIf->UpperWid;  // @Used for special match ".git/logs"
		}
		break;
	}
	zpTopIf->UpperWid = inotify_add_watch(zInotifyFD, zpPath, zBaseWatchBit | IN_DONT_FOLLOW);
	zCheck_Negative_Exit(zpTopIf->UpperWid);

	strcpy(zpTopIf->path, zpPath);
	zpPathHash[zpTopIf->UpperWid] = zpTopIf;

	if (((zObjInfo *) zpObjIf)->RecursiveMark) {
		struct dirent *zpEntry;

		DIR *zpDir = opendir(zpPath);
		zCheck_Null_Exit(zpDir);

		while (NULL != (zpEntry = readdir(zpDir))) {
			if (DT_DIR == zpEntry->d_type
					&& strcmp(".", zpEntry->d_name) 
					&& strcmp("..", zpEntry->d_name) 
					&& strcmp(".git", zpEntry->d_name)) {

				// Must do "malloc" here.
				zSubObjInfo *zpSubIf = malloc(sizeof(zSubObjInfo) 
						+ 2 + zLen + strlen(zpEntry->d_name));
				zCheck_Null_Exit(zpSubIf);
	
				zpSubIf->RecursiveMark = 1;
				zpSubIf->RepoId = zpTopIf->RepoId;
				zpSubIf->UpperWid = zpTopIf->UpperWid;
	
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
zcallback_common_action(void *zpCurIf) {
//TEST: PASS
	zSubObjInfo *zpSubIf = (zSubObjInfo *)zpCurIf;

	pthread_mutex_lock(&zCommonLock);
	while (0 != zBitHash[zpSubIf->UpperWid]) {
		pthread_cond_wait(&zCommonCond, &zCommonLock);
	}
	zBitHash[zpSubIf->UpperWid] = 1;
	pthread_mutex_unlock(&zCommonLock);

	char zShellBuf[1 + strlen("zEvPath=;zEvType=;") + strlen(zpSubIf->path) + strlen(zpShellCommand)];
	sprintf(zShellBuf, "zEvPath=%s;zEvMark=%d;%s", zpSubIf->path, zpSubIf->EvType, zpShellCommand);

	system(zShellBuf);

	pthread_mutex_lock(&zCommonLock);
	zBitHash[zpSubIf->UpperWid] = 0;
	pthread_cond_signal(&zCommonCond);
	pthread_mutex_unlock(&zCommonLock);
}

void
zinotify_wait(void *_) {
//TEST: PASS
	char zBuf[zCommonBufSiz]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;

	for (;;) {
		zLen = read(zInotifyFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			if (1 == zpPathHash[zpEv->wd]->RecursiveMark 
					&& (zpEv->mask & IN_ISDIR) 
					&& ( (zpEv->mask & IN_CREATE) || (zpEv->mask & IN_MOVED_TO) )
					) {
		  		// If a new subdir is created or moved in, add it to the watch list.
				// Must do "malloc" here.
				zSubObjInfo *zpSubIf = malloc(sizeof(zSubObjInfo) 
						+ 2 + strlen(zpPathHash[zpEv->wd]->path) + zpEv->len);
				zCheck_Null_Exit(zpSubIf);
	
				zpSubIf->RecursiveMark = 1;
				zpSubIf->UpperWid = zpPathHash[zpEv->wd]->UpperWid;
	
				strcpy(zpSubIf->path, zpPathHash[zpEv->wd]->path);
				strcat(zpSubIf->path, "/");
				strcat(zpSubIf->path, zpEv->name);

				zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
			}

			if (zpPathHash[zpEv->wd]->UpperWid == zpSpecWid[zpPathHash[zpEv->wd]->RepoId]) {
				zAdd_To_Thread_Pool(zupdate_cache, &(zpPathHash[zpEv->wd]->RepoId));
			}

//			if (zpEv->mask & (IN_CREATE | IN_MOVED_TO)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->EvType = zNewDirEv; }
//				else { zpPathHash[zpEv->wd]->EvType = zNewFileEv; }
//			}
//			else if (zpEv->mask & (IN_DELETE | IN_MOVED_FROM)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->EvType = zDelDirEv; }
//				else { zpPathHash[zpEv->wd]->EvType = zDelFileEv; }
//			}
//			else if (zpEv->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->EvType = zDelDirSelfEv; }
//				else { zpPathHash[zpEv->wd]->EvType = zDelFileSelfEv; }
//			}
//			else if (zpEv->mask & IN_MODIFY) {
//				zpPathHash[zpEv->wd]->EvType = zModEv;
//			}
//			else if (zpEv->mask & IN_Q_OVERFLOW) {  // Robustness
//				zpPathHash[zpEv->wd]->EvType = zUnknownEv;
//				zPrint_Err(0, NULL, "\033[31;01mQueue overflow, some events may be lost!!\033[00m\n");
//				goto zEv;
//			}
//			else if (zpEv->mask & IN_IGNORED){ goto zEv; }
//			else {
//				zpPathHash[zpEv->wd]->EvType = zUnknownEv;
//				zPrint_Err(0, NULL, "\033[31;01mUnknown event occur!!\033[00m\n");
//				goto zEv;
//			}
//
//			zAdd_To_Thread_Pool(zcallback_common_action, zpPathHash[zpEv->wd]);
//zEv:;
		}
	}
}

#undef zHashSiz
#undef zBaseWatchBit 
#undef zNewDirEv
#undef zNewFileEv
#undef zModEv
#undef zDelFileEv
#undef zDelDirEv
#undef zDelFileSelfEv
#undef zDelDirSelfEv
#undef zUnknownEv
