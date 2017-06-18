#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>

#include <libgen.h>

#include "zpcre2.h"

#define zCommonBufSiz 4096 

#define zHashSiz 8192
#define zThreadPollSiz 64

#define zWatchBit \
	IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF | IN_EXCL_UNLINK | IN_DONT_FOLLOW

/*******************************
 * DATA STRUCT DEFINE
 ******************************/
typedef struct zObjInfo {
	struct zObjInfo *p_next;
	_i RecursiveMark;  // Mark recursive monitor.
	char path[];  // The directory to be monitored.
}zObjInfo;

typedef struct zSubObjInfo {
	_i UpperWid;
	char path[];
}zSubObjInfo;

/***********************
 * FUNCTION DECLARE
 **********************/
extern char * zget_one_line_from_FILE(FILE *);
static void * zthread_func(void *zpIndex);

void zthread_poll_init(void);
void zthread_poll_destroy(void);


/***************
 * Global var
 **************/
static _i zInotifyFD;

static pthread_mutex_t zGitLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t zGitCond = PTHREAD_COND_INITIALIZER;

static zSubObjInfo *zpPathHash[zHashSiz];
static char zBitHash[zHashSiz];

/**********************
 * Simple thread pool
 **********************/
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
		pthread_mutex_lock(&(zLock[0]));\
		while (-1 == zJobQueue) {\
			pthread_cond_wait(&(zCond[0]), &(zLock[0]));\
		}\
\
		zThreadPoll[zJobQueue].OpsFunc = zFunc;\
		zThreadPoll[zJobQueue].p_param = zParam;\
		zThreadPoll[zJobQueue].MarkStart = 1;\
\
		pthread_mutex_lock(&(zLock[1]));\
		zJobQueue = -1;\
		pthread_cond_signal(&(zCond[1]));\
		pthread_mutex_unlock(&(zLock[1]));\
\
		pthread_mutex_unlock(&(zLock[0]));\
\
		pthread_mutex_lock(&(zLock[2]));\
		pthread_mutex_unlock(&(zLock[2]));\
		pthread_cond_signal(&(zCond[2]));\
}while(0)

typedef void * (* zThreadOps) (void *);

typedef struct zThreadJobInfo {
	pthread_t Tid;
	_i MarkStart;
	zThreadOps OpsFunc;
	void *p_param;
}zThreadJobInfo;

static zThreadJobInfo zThreadPoll[zThreadPollSiz];
static _i zIndex[zThreadPollSiz];
static _i zJobQueue = -1;

static pthread_mutex_t zLock[3] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t zCond[3] = {PTHREAD_COND_INITIALIZER};

void
zthread_poll_init(void) {
	for (_i i = 0; i < zThreadPollSiz; i++) {
		zIndex[i] = i;
		zThreadPoll[i].MarkStart= 0;
		zCheck_Pthread_Func_Exit(
				pthread_create(&(zThreadPoll[i].Tid), NULL, zthread_func, &(zIndex[i]))
				);
	}
}

void
zthread_poll_destroy(void) {
	for (_i i = 0; i < zThreadPollSiz; i++) {
		pthread_cancel(zThreadPoll[i].Tid);
	}
}

static void *
zthread_func(void *zpIndex) {
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);
	_i i = *((_i *)zpIndex);

zMark:;
	pthread_mutex_lock(&(zLock[1]));
	while (-1 != zJobQueue) {  // -1: no other thread is ahead of me.
		pthread_cond_wait(&zCond[1], &(zLock[1]));
	}

	pthread_mutex_lock(&(zLock[0]));
	zJobQueue = i;
	pthread_cond_signal(&zCond[0]);
	pthread_mutex_unlock(&(zLock[0]));

	pthread_mutex_unlock(&(zLock[1]));

	// 0: param is not ready, can not start; 1: param ready, can start.
	pthread_mutex_lock(&(zLock[2]));
	while (1 != zThreadPoll[i].MarkStart) {
		pthread_cond_wait(&zCond[2], &(zLock[2]));
	}

	zThreadPoll[i].MarkStart = 0;
	pthread_mutex_unlock(&(zLock[2]));

	zThreadPoll[i].OpsFunc(zThreadPoll[i].p_param);
	
	goto zMark;
	return NULL;
}

/*********************
 * Major 
 ********************/
zObjInfo *
zread_conf_file(const char *zpConfPath) {
//TEST: PASS
	zObjInfo *zpObjIf[3] = {NULL};

	zPCREInitInfo *zpInitIf[3] = {NULL};
	zPCRERetInfo *zpRetIf[3] = {NULL};

	_i zCnt = 0;
	char *zpRes = NULL;
	FILE *zpFile = fopen(zpConfPath, "r");

	zpInitIf[0] = zpcre_init("^\\s*\\d\\s*/[/\\w]+");
	zpInitIf[1] = zpcre_init("\\d(?=\\s+)");
	zpInitIf[2] = zpcre_init("[/\\w]+(?=\\s*$)");

	while (NULL != (zpRes = zget_one_line_from_FILE(zpFile))) {
		zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
		if (0 == zpRetIf[0]->cnt) {
			zpcre_free_tmpsource(zpRetIf[0]);
			continue;
		} else {
			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);

			zpObjIf[0] = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
			zpObjIf[0]->p_next = NULL;
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];

			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);

			zpcre_free_tmpsource(zpRetIf[2]);
			zpcre_free_tmpsource(zpRetIf[1]);
			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[0]);

	fclose(zpFile);
	return zpObjIf[2];
}

void *
zinotify_add_watch_recursively(void *zpIf) {
//TEST: PASS
	zSubObjInfo *zpCurIf = (zSubObjInfo *) zpIf;

	if (-1 == chdir(zpCurIf->path)) { return NULL; }  // Robustness
	_i zWid = inotify_add_watch(zInotifyFD, zpCurIf->path, zWatchBit);
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

			zpSubIf->UpperWid = zpCurIf->UpperWid;

			strcpy(zpSubIf->path, zpCurIf->path);
			strcat(zpSubIf->path, "/");
			strcat(zpSubIf->path, zpEntry->d_name);

			zAdd_To_Thread_Pool(zinotify_add_watch_recursively, zpSubIf);
		}
	}

//	free(zpCurIf);  // Can safely free, but NOT free it!
	closedir(zpDir);
	return NULL;
}

static void *
zinotify_add_top_watch(void *zpObjIf) {
//TEST: PASS
	char *zpPath = ((zObjInfo *) zpObjIf)->path;

	zSubObjInfo *zpTopIf = malloc(
			sizeof(zSubObjInfo) + 1 + strlen(zpPath)
			);

	zpTopIf->UpperWid = inotify_add_watch(zInotifyFD, zpPath, zWatchBit);
	zCheck_Negative_Exit(zpTopIf->UpperWid);

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
	
				zpSubIf->UpperWid = zpTopIf->UpperWid;
	
				strcpy(zpSubIf->path, zpPath);
				strcat(zpSubIf->path, "/");
				strcat(zpSubIf->path, zpEntry->d_name);

				zAdd_To_Thread_Pool(zinotify_add_watch_recursively, zpSubIf);
			}
		}
		closedir(zpDir);
	}
	return NULL;
}

void *
zgit_action(void *zpCurIf) {
//TEST: PASS
	zSubObjInfo *zpSubIf = (zSubObjInfo *)zpCurIf;

	pthread_mutex_lock(&zGitLock);
	while (0 != zBitHash[zpSubIf->UpperWid]) {
		pthread_cond_wait(&zGitCond, &zGitLock);
	}
	zBitHash[zpSubIf->UpperWid] = 1;
	pthread_mutex_unlock(&zGitLock);

	size_t zLen = strlen(zpPathHash[zpSubIf->UpperWid]->path);

	do {
		if (-1 == chdir(zpSubIf->path)) {  // Robustness
			zpSubIf->path[strlen(dirname(zpSubIf->path))] = '\0';
		}
		else {
			char Buf[strlen(zpPathHash[zpSubIf->UpperWid]->path) + 64];
			strcpy(Buf, "sh ");
			strcat(Buf, zpPathHash[zpSubIf->UpperWid]->path);
			strcat(Buf, "/git_action.sh");
			system(Buf);

			break;
		}
	} while (zLen <= strlen(zpSubIf->path)) ;

	pthread_mutex_lock(&zGitLock);
	zBitHash[zpSubIf->UpperWid] = 0;
	pthread_cond_signal(&zGitCond);
	pthread_mutex_unlock(&zGitLock);

	return NULL;
}

void *
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

			if ((zpEv->mask & IN_ISDIR) 
					&& (zpEv->mask & IN_CREATE || zpEv->mask & IN_MOVED_TO)) {
		  			// If a new subdir is created or moved in, add it to the watch list.
					// Must do "malloc" here.
					zSubObjInfo *zpSubIf = malloc(sizeof(zSubObjInfo) 
							+ 2 + strlen(zpPathHash[zpEv->wd]->path) + zpEv->len);
					zCheck_Null_Exit(zpSubIf);
	
					zpSubIf->UpperWid = zpPathHash[zpEv->wd]->UpperWid;
	
					strcpy(zpSubIf->path, zpPathHash[zpEv->wd]->path);
					strcat(zpSubIf->path, "/");
					strcat(zpSubIf->path, zpEv->name);

					zAdd_To_Thread_Pool(zinotify_add_watch_recursively, zpSubIf);
					goto zMark;
			}
			else if ((zpEv->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY | IN_IGNORED))) { 
zMark:
				zAdd_To_Thread_Pool(zgit_action, zpPathHash[zpEv->wd]);
			}
			else if (zpEv->mask & IN_Q_OVERFLOW) {  // Robustness
				fprintf(stderr, "Queue overflow, some events may be lost!!");
			}
		}
	}
}

void
zconfig_file_monitor(const char *zpConfPath) {
	_i zConfFD = inotify_init();
	zCheck_Negative_Exit(
			inotify_add_watch(
				zConfFD,
				zpConfPath, 
				IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF
				)
			); 

	char zBuf[zCommonBufSiz] 
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;

	for (;;) {
		zLen = read(zConfFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; 
				zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			if (zpEv->mask & (IN_MODIFY | IN_IGNORED | IN_MOVE_SELF | IN_DELETE_SELF)) { return; }
		}
	}
}

/***********************************************************************************/
_i
main(_i zArgc, char **zppArgv) {
	//zdaemonize(NULL);
	if (3 == zArgc && 0 == strcmp("-f", zppArgv[1])) {
		struct stat zStat[1];
		zCheck_Negative_Exit(stat(zppArgv[2], zStat));
		if (!S_ISREG(zStat->st_mode)) {
			fprintf(stderr, "Need a regular file!\n"
					"Usage: file_monitor -f <Config File Path>\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "Usage: file_monitor -f <Config File Path>\n");
		exit(1);
	}

zReLoad:;
	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zthread_poll_init();

	zObjInfo *zpObjIf[2] = {NULL};
	for (zpObjIf[0] = zpObjIf[1] = zread_conf_file(zppArgv[2]); 
			NULL != zpObjIf[0]; zpObjIf[0] = zpObjIf[0]->p_next) {

		zAdd_To_Thread_Pool(zinotify_add_top_watch, zpObjIf[0]);
	}

	zAdd_To_Thread_Pool(zinotify_wait, NULL);

	zconfig_file_monitor(zppArgv[2]);  // Robustness

	//close(zInotifyFD);
	//zthread_poll_destroy();

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (0 == zPid) { goto zReLoad; }
	else { exit(0); }
}
/***********************************************************************************/
