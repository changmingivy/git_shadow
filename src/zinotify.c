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
#endif

#define zHashSiz 8192

#define zBaseWatchBit \
	IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF

#define zNewDirMark 2
#define zNewFileMark 1
#define zModMark 0
#define zDelFileMark -1
#define zDelDirMark -2
#define zDelFileSelfMark -3
#define zDelDirSelfMark -4
#define zUnknownMark -5

/**********************
 * DATA STRUCT DEFINE *
 **********************/
typedef struct zObjInfo {
	struct zObjInfo *p_next;
	_i RecursiveMark;  // Mark recursive monitor.
	char path[];  // The directory to be monitored.
}zObjInfo;

typedef struct zSubObjInfo {
	_i UpperWid;
	_s RecursiveMark;
	_s ActionMark;
	char path[];
}zSubObjInfo;

typedef struct zFileDiffInfo {
	_i CacheVersion;
	_us FileIndex;  // index in iovec array

	struct iovec *p_DiffContent;
	_us VecSiz;

	char path[];  // the path relative to code repo
} zFileDiffInfo;

typedef struct zDeployLogInfo {
	_i index;  // deploy index, used as hash
	char BaseTagSig[40];  // where to come back
	_ul offset;
	_i len;
} zDeployLogInfo;

/********************
 * FUNCTION DECLARE *
 ********************/
extern void zdaemonize(const char *);
extern void zfork_do_exec(const char *, char **);
extern char * zget_one_line_from_FILE(FILE *);

static void * zthread_func(void *);

static void zthread_poll_init(void);
//static void zthread_poll_destroy(void);

/**************
 * GLOBAL VAR *
 **************/
static _i zInotifyFD;

static pthread_mutex_t zCommonLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t zCommonCond = PTHREAD_COND_INITIALIZER;

static zSubObjInfo *zpPathHash[zHashSiz];
static char zBitHash[zHashSiz];

static _i zSpecWid;  // @For special purpose: $REPO/.git/logs

static char CurTagSig[40];  // git SHA1 sig

static char *zpShellCommand;  // What to do when get events, two extra VAR available: $zEventType and $zEventPath

static struct  iovec *zpCacheVec;  // Global cache for git diff content
static _i zpCacheVecSiz;

/*************
 * ADD WATCH *
 *************/
static void *
zinotify_add_sub_watch(void *zpIf) {
//TEST: PASS
	zSubObjInfo *zpCurIf = (zSubObjInfo *) zpIf;

	if (-1 == chdir(zpCurIf->path)) { return NULL; }  // Robustness
	_i zWid = inotify_add_watch(zInotifyFD, zpCurIf->path, zBaseWatchBit | IN_DONT_FOLLOW);
	zCheck_Negative_Exit(zWid);

	if (NULL != zpPathHash[zWid]) { free(zpPathHash[zWid]); }  // Free old memory before using the same index again.
	zpPathHash[zWid] = zpCurIf;

	if (-1 == chdir(zpCurIf->path)) { return NULL; }  // Robustness
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
			zpSubIf->UpperWid = zpCurIf->UpperWid;

			strcpy(zpSubIf->path, zpCurIf->path);
			strcat(zpSubIf->path, "/");
			strcat(zpSubIf->path, zpEntry->d_name);

			zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
		}
	}

//	free(zpCurIf);  // Can safely free, but NOT free it!
	closedir(zpDir);
	return NULL;
}

static void *
zinotify_add_top_watch(void *zpIf) {
//TEST: PASS
	zObjInfo *zpObjIf = (zObjInfo *) zpIf;
	char *zpPath = zpObjIf->path;

	zSubObjInfo *zpTopIf = malloc(
			sizeof(zSubObjInfo) + 1 + strlen(zpPath)
			);

	zpTopIf->RecursiveMark = zpObjIf->RecursiveMark;
	zpTopIf->UpperWid = inotify_add_watch(zInotifyFD, zpPath, zBaseWatchBit | IN_DONT_FOLLOW);
	zCheck_Negative_Exit(zpTopIf->UpperWid);

	if (0 == strcmp(strlen(zpPath) - strlen(".git/logs") - 1 + zpPath, ".git/logs")) {
		zSpecWid = zpTopIf->UpperWid;  // @Used for special match ".git/logs"
	}

	strcpy(zpTopIf->path, zpPath);
	zpPathHash[zpTopIf->UpperWid] = zpTopIf;

	if (((zObjInfo *) zpObjIf)->RecursiveMark) {
		size_t zLen = strlen(zpPath);
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
				zpSubIf->UpperWid = zpTopIf->UpperWid;
	
				strcpy(zpSubIf->path, zpPath);
				strcat(zpSubIf->path, "/");
				strcat(zpSubIf->path, zpEntry->d_name);

				zAdd_To_Thread_Pool(zinotify_add_sub_watch, zpSubIf);
			}
		}
		closedir(zpDir);
	}
	return NULL;
}

/****************
 * UPDATE CACHE *
 ****************/
struct iovec *
zgenerate_cache(void) {
	_i zNewVersion = (_i)time(NULL);

	struct iovec *zpNewCacheVec[2] = {NULL};

	FILE *zpShellRetHandler[2] = {NULL};
	_i zTotalLine[2] = {0};

	char *zpRes[2] = {NULL};
	size_t zResLen[2] = {0};
	_i zBaseSiz = sizeof(zFileDiffInfo);
	char zShellBuf[zCommonBufSiz];

	zpShellRetHandler[0] = popen("git diff --name-only HEAD CURRENT | wc -l && git diff --name-only HEAD CURRENT", "r");
	zCheck_Null_Exit(zpShellRetHandler);

	if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) { return 0; }
	else {
		if (0 == (zTotalLine[0] = atoi(zpRes[0]))) { return  NULL; }
		zMem_Alloc(zpNewCacheVec[0], struct iovec, zTotalLine[0]);
		zpCacheVecSiz = zTotalLine[0];  // Global Var

		for (_i i = 0; NULL != (zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0])); i++) {
			zResLen[0] = strlen(zpRes[0]);
			zCheck_Null_Exit(
					zpNewCacheVec[0][i].iov_base = malloc(1 + zResLen[0] + zBaseSiz)
					);
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->CacheVersion = zNewVersion;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->FileIndex = i;
			strcpy(((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->path, zpRes[0]);

			sprintf(zShellBuf, "git diff HEAD CURRENT -- %s | wc -l && git diff --name-only HEAD CURRENT -- %s", zpRes[0], zpRes[0]);
			zpShellRetHandler[1] = popen(zShellBuf, "r");
			zCheck_Null_Exit(zpShellRetHandler);

			zMem_Alloc(zpNewCacheVec[1], struct iovec, atoi(zpRes[1]));

			for (_i j = 0; NULL != (zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])); j++) {
				zResLen[1] = strlen(zpRes[1]);
				zMem_Alloc(zpNewCacheVec[j]->iov_base, char, 1 + zResLen[1]);
				strcpy(((char *)(zpNewCacheVec[1][j].iov_base)), zpRes[1]);
			}
			pclose(zpShellRetHandler[1]);
		}
		pclose(zpShellRetHandler[0]);
	}
	return zpNewCacheVec[0];
}

static void *
zupdate_cache(void *_) {
	struct iovec *zpOldCacheIf = zpCacheVec;
	if (NULL == (zpCacheVec = zgenerate_cache())) {
		zpCacheVec = zpOldCacheIf;  // Global Var
	}
	else {
		//struct iovec *zpOldCacheIf = (struct iovec *)zpIf;
		for (size_t i = 0; i < zpOldCacheIf->iov_len; i++) { 
			for (_i j = 0; j < ((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->VecSiz; j++) {
				free((((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent[j]).iov_base);
			}
			free(((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent);
			free(zpOldCacheIf[i].iov_base); 
		}
		free(zpOldCacheIf);
	}
	return NULL;
}

/********************
 * DEAL WITH EVENTS *
 ********************/
static void *
zcallback_common_action(void *zpCurIf) {
//TEST: PASS
	zSubObjInfo *zpSubIf = (zSubObjInfo *)zpCurIf;

	pthread_mutex_lock(&zCommonLock);
	while (0 != zBitHash[zpSubIf->UpperWid]) {
		pthread_cond_wait(&zCommonCond, &zCommonLock);
	}
	zBitHash[zpSubIf->UpperWid] = 1;
	pthread_mutex_unlock(&zCommonLock);

	char zShellBuf[1 + strlen("zEventPath=;zEventType=;") + strlen(zpSubIf->path) + strlen(zpShellCommand)];
	sprintf(zShellBuf, "zEventPath=%s;zEventMark=%d;%s", zpSubIf->path, zpSubIf->ActionMark, zpShellCommand);

	system(zShellBuf);

	pthread_mutex_lock(&zCommonLock);
	zBitHash[zpSubIf->UpperWid] = 0;
	pthread_cond_signal(&zCommonCond);
	pthread_mutex_unlock(&zCommonLock);

	return NULL;
}

static void *
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

			if (zpEv->wd == zSpecWid) {
				zAdd_To_Thread_Pool(zupdate_cache, NULL);
			}
//			else if (zpEv->mask & (IN_CREATE | IN_MOVED_TO)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->ActionMark = zNewDirMark; }
//				else { zpPathHash[zpEv->wd]->ActionMark = zNewFileMark; }
//			}
//			else if (zpEv->mask & (IN_DELETE | IN_MOVED_FROM)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->ActionMark = zDelDirMark; }
//				else { zpPathHash[zpEv->wd]->ActionMark = zDelFileMark; }
//			}
//			else if (zpEv->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) { 
//				if (zpEv->mask & IN_ISDIR) { zpPathHash[zpEv->wd]->ActionMark = zDelDirSelfMark; }
//				else { zpPathHash[zpEv->wd]->ActionMark = zDelFileSelfMark; }
//			}
//			else if (zpEv->mask & IN_MODIFY) {
//				zpPathHash[zpEv->wd]->ActionMark = zModMark;
//			}
//			else if (zpEv->mask & IN_Q_OVERFLOW) {  // Robustness
//				zpPathHash[zpEv->wd]->ActionMark = zUnknownMark;
//				zPrint_Err(0, NULL, "\033[31;01mQueue overflow, some events may be lost!!\033[00m\n");
//				goto zMark;
//			}
//			else if (zpEv->mask & IN_IGNORED){ goto zMark; }
//			else {
//				zpPathHash[zpEv->wd]->ActionMark = zUnknownMark;
//				zPrint_Err(0, NULL, "\033[31;01mUnknown event occur!!\033[00m\n");
//				goto zMark;
//			}
//
//			zAdd_To_Thread_Pool(zcallback_common_action, zpPathHash[zpEv->wd]);
//zMark:;
		}
	}
}

#undef zHashSiz
#undef zBaseWatchBit 
#undef zNewDirMark
#undef zNewFileMark
#undef zModMark
#undef zDelFileMark
#undef zDelDirMark
#undef zDelFileSelfMark
#undef zDelDirSelfMark
#undef zUnknownMark
