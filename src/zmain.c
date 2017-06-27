#define _Z
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
//#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <sys/inotify.h>
#include <sys/epoll.h>

#include <pthread.h>
#include <sys/mman.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>

#include "zutils.h"

#define zCommonBufSiz 4096 

/**********************
 * DATA STRUCT DEFINE *
 **********************/
typedef struct zFileDiffInfo {
	_ui CacheVersion;
	_us RepoId;  // correspond to the name of code repository
	_us FileIndex;  // index in iovec array

	struct iovec *p_DiffContent;
	_ui VecSiz;

	_ui PathLen;
	char path[];  // the path relative to code repo
} zFileDiffInfo;

typedef struct zDeployLogInfo {  // write to log.meta
	_ui RepoId;  // correspond to the name of code repository
	_ui index;  // index of deploy history, used as hash
	_ul offset;
	_ui len;
	_ul TimeStamp;
} zDeployLogInfo;

typedef struct zDeployResInfo {
	_ui ClientAddr;  // 0xffffffff
	_us RepoId;  // correspond to the name of code repository
	_us DeployState;
} zDeployResInfo;

/*************************/
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
#define zDeployHashSiz 1024
_i zRepoNum;  // how many repositories
char **zppRepoList;  // each repository's absolute path

struct  iovec **zppCacheVec;  // each repository's Global cache for git diff content
_i *zpCacheVecSiz;

_i *zpSpecWid;  // watch ID for each repository's .git/logs

char **zppCurTagSig;  // each repository's CURRENT(git tag) SHA1 sig
_i *zpLogFd[3];  // opened log fd for each repo: one for metadata, one for filenames, one for commit sig

pthread_mutex_t *zpDeployLock;
_i *zpTotalHost;
_i *zpReplyCnt;

zDeployResInfo ***zpppDpResHash, **zppDpResList;  // base on the 32bit IPv4 addr, store as _ui

#define zWatchHashSiz 8192
_i zInotifyFD;
zSubObjInfo *zpPathHash[zWatchHashSiz];
char zBitHash[zWatchHashSiz];
char *zpShellCommand;  // What to do when get events, two extra VAR available: $zEvType and $zEvPath

/****************
 * SUB MODULERS *
 ****************/
#include "zbase_utils.c"
#include "zpcre.c"
#include "zthread_pool.c"
#include "znetwork.c"
#include "zinotify_callback.c"
#include "zinotify.c"

/*************************
 * DEAL WITH CONFIG FILE *
 *************************/
zObjInfo *
zread_conf_file(const char *zpConfPath) {
//TEST: PASS
	zObjInfo *zpObjIf[3] = {NULL};

	zPCREInitInfo *zpInitIf[4] = {NULL};
	zPCRERetInfo *zpRetIf[4] = {NULL};

	_i zCnt = 0;
	char *zpRes = NULL;
	FILE *zpFile = fopen(zpConfPath, "r");

	struct stat zStatBuf;

	zpInitIf[0] = zpcre_init("^\\s*\\d\\s*/[/\\w]+");
	zpInitIf[1] = zpcre_init("^\\d(?=\\s+)");
	zpInitIf[2] = zpcre_init("[/\\w]+(?=\\s*$)");
	zpInitIf[3] = zpcre_init("^\\s*($|#)");

	for (_i i = 1; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); i++) {
		zpRes[strlen(zpRes) - 1] = '\0';

		zpRetIf[3] = zpcre_match(zpInitIf[3], zpRes, 0);
		if (0 < zpRetIf[3]->cnt) {
			zpcre_free_tmpsource(zpRetIf[3]);
			continue;
		}
		zpcre_free_tmpsource(zpRetIf[3]);

		zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
		if (0 == zpRetIf[0]->cnt) {
			zpcre_free_tmpsource(zpRetIf[0]);
			zPrint_Time();
			fprintf(stderr, "\033[31m[Line %d] \"%s\": Invalid entry.\033[00m\n", i ,zpRes);
			continue;
		} else {
			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);
			if (-1 == lstat(zpRetIf[2]->p_rets[0], &zStatBuf) 
					|| !S_ISDIR(zStatBuf.st_mode)) {
				zpcre_free_tmpsource(zpRetIf[2]);
				zpcre_free_tmpsource(zpRetIf[1]);
				zpcre_free_tmpsource(zpRetIf[0]);
				zPrint_Time();
				fprintf(stderr, "\033[31m[Line %d] \"%s\": NO such directory or NOT a directory.\033[00m\n", i, zpRes);
				continue;
			}

			zpObjIf[0] = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];
			zpObjIf[0]->p_next = NULL;  // Must here!!!

			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);

			zpcre_free_tmpsource(zpRetIf[2]);
			zpcre_free_tmpsource(zpRetIf[1]);
			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[3]);
	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[0]);

	fclose(zpFile);
	return zpObjIf[2];
}

void
zconfig_file_monitor(const char *zpConfPath) {
//TEST: PASS
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

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;
			if (zpEv->mask & (IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) { return; }
		}
	}
}

/********
 * MAIN *
 ********/
_i
main(_i zArgc, char **zppArgv) {
//TEST: PASS
	char zPathBuf[zCommonBufSiz];
	struct stat zStatIf;
	_i zFd, zSiz, zErr;
	zFd = zSiz = zErr = 0;

	//extern char *optarg;
	//extern int optind, opterr, optopt;
	opterr = 0;  // prevent getopt to print err info
	for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "CSf:x:h:p:"));) {
		switch (zOpt) {
		case 'f':
			if (-1 == stat(optarg, &zStatIf) || !S_ISREG(zStatIf.st_mode)) {
				zPrint_Time();
				fprintf(stderr, "\033[31;01mConfig file not exists or is not a regular file!\n"
						"Usage: %s -f <Config File Path>\033[00m\n", zppArgv[0]);
				exit(1);
			}
			break;
		case 'x':
			zpShellCommand = optarg;
			break;
		case 'h':  // host ip addr
			// TO DO
			break;
		case 'p':  // port
			// TO DO
			break;
		case 'C':  // Client
			// TO DO
			break;
		case 'S':  // Server
			// TO DO
			break;
		default: // zOpt == '?'
			zPrint_Time();
		 	fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
			exit(1);
		}
	}

	if (optind >= zArgc) {
		zPrint_Time();
		zPrint_Err(0, NULL, "\033[31;01mNeed at least one argument to special CODE REPO path!\033[00m");
		exit(1);
	}

	zRepoNum = 1 + zArgc - optind;

	zMem_Alloc(zppCurTagSig, char *, zRepoNum);
	zMem_Alloc(zpSpecWid, _i, zRepoNum);

	zMem_Alloc(zppCacheVec, struct iovec *, zRepoNum);
	zMem_Alloc(zpCacheVecSiz, _i, zRepoNum);

	zMem_Alloc(zpLogFd[0], _i, zRepoNum);
	zMem_Alloc(zpLogFd[1], _i, zRepoNum);
	zMem_Alloc(zpLogFd[2], _i, zRepoNum);

	zMem_Alloc(zpTotalHost, _i, zRepoNum );
	zMem_Alloc(zpReplyCnt, _i, zRepoNum );
/**/ 
	zMem_Alloc(zppRepoList, char *, zRepoNum);
	zMem_Alloc(zpDeployLock, pthread_mutex_t, zRepoNum);
	zMem_Alloc(zppDpResList, zDeployResInfo *, zRepoNum);
	zMem_Alloc(zpppDpResHash, zDeployResInfo **, zRepoNum);

	for (_i i = 0; i < zRepoNum; i++) { 
		zppRepoList[i] = zppArgv[optind];

		sprintf(zPathBuf, "%s/.git_shadow/log.meta", zppArgv[optind]);
		zpLogFd[0][i] = open(zPathBuf, O_RDWR | O_CREAT | O_APPEND, 0600);  // log metadata
		zCheck_Negative_Exit(zpLogFd[0][i]);

		sprintf(zPathBuf, "%s/.git_shadow/log.data", zppArgv[optind]);
		zpLogFd[1][i] = open(zPathBuf, O_RDWR | O_CREAT | O_APPEND, 0600);  // log filename
		zCheck_Negative_Exit(zpLogFd[1][i]);

		sprintf(zPathBuf, "%s/.git_shadow/log.sig", zppArgv[optind]);
		zpLogFd[2][i] = open(zPathBuf, O_RDWR | O_CREAT | O_APPEND, 0600);  // log filename
		zCheck_Negative_Exit(zpLogFd[1][i]);

		if (0 != (zErr =pthread_mutex_init(&(zpDeployLock[i]), NULL))) {
			zPrint_Err(zErr, NULL, "Init deploy lock failed!");
			exit(1);
		}

		sprintf(zPathBuf, "%s/.git_shadow/client_list_all", zppArgv[optind]);
		zFd = open(zPathBuf, O_RDONLY);
		zCheck_Negative_Exit(fstat(zFd, &zStatIf));

		zSiz = zStatIf.st_size / sizeof(zDeployResInfo);
		zMem_Alloc(zppDpResList[i], zDeployResInfo, zSiz);
		for (_i j = 0; j < zSiz; j++) {
			errno = 0;
			if (sizeof(zDeployResInfo) != read(zFd, &zppDpResList[i][j], sizeof(zDeployResInfo))) {
				zPrint_Err(errno, NULL, "read client info failed!");
				exit(1);
			}
		}
		close(zFd);
	}

	zdaemonize("/");

zReLoad:;
	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zthread_poll_init();

	zObjInfo *zpObjIf = NULL;
	if (NULL == (zpObjIf = zread_conf_file(zppArgv[2]))) {
		zPrint_Time();
		fprintf(stderr, "\033[31;01mNo valid entry found in config file!!!\n\033[00m\n");
	}
	else {
		do {
			zAdd_To_Thread_Pool(zinotify_add_top_watch, zpObjIf);
			zpObjIf = zpObjIf->p_next;
		} while (NULL != zpObjIf);
	}

	zAdd_To_Thread_Pool(zinotify_wait, NULL);
	zconfig_file_monitor(zppArgv[2]);  // Robustness

	close(zInotifyFD);

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);
	if (0 == zPid) { goto zReLoad; }
	else { exit(0); }
}
