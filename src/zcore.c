#ifndef _Z
	#define _XOPEN_SOURCE 700
	#define _BSD_SOURCE
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netdb.h>
	#include <pthread.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <sys/stat.h>
	#include <sys/signal.h>
	#include <sys/inotify.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <time.h>
	#include <errno.h>
	#include <dirent.h>
	#include <libgen.h>
	#include <sys/mman.h>
	#include "zutils.h"
	#include "zcommon.c"
	#include "zthread_pool.c"
	#define zCommonBufSiz 4096 
	char **zppRepoList;  // each repository's absolute path
	_i zRepoNum;  // how many repositories
	char **zppCurTagSig;  // each repository's CURRENT(git tag) SHA1 sig
	struct  iovec **zppCacheVec;  // each repository's Global cache for git diff content
	_i *zpCacheVecSiz;
	_i *zpLogFd[2];  // opened log fd for each repo
#endif

#include <sys/epoll.h>

#define UDP 0
#define TCP 1
#define zMaxEvents 64

/**********************
 * DATA STRUCT DEFINE *
 **********************/
typedef struct zFileDiffInfo {
	_i CacheVersion;
	_us RepoId;  // correspond to the name of code repository
	_us FileIndex;  // index in iovec array

	struct iovec *p_DiffContent;
	_us VecSiz;

	_us PathLen;
	char path[];  // the path relative to code repo
} zFileDiffInfo;

typedef struct zDeployLogInfo {
	_i index;  // deploy index, used as hash
	char BaseTagSig[40];  // where to come back
	_ul TimeStamp;
	_ul offset;
	_i len;
} zDeployLogInfo;

typedef struct zDeployResInfo {
	_us CacheVersion;
	_us DeployState;
} zDeployResInfo;

/*
 * return contents from cache directly
 * use zsendmsg
 * msg form(sent from frontend, to git_shadow):
 * [OpsMark(l/d/D/R)][struct zFileDiffInfo]
 * Meaning:
 * 		-l:list modified file list, default behavior
 * 		-d:file content diff
 * 		-D:deploy, need shell script
 * 		-R:revoke, need shell script
 */
void
zlist_diff_files(_i zSd, zFileDiffInfo *zpIf){
	zsendmsg(zSd, zppCacheVec[zpIf->RepoId], zpCacheVecSiz[zpIf->RepoId], 0, NULL);
}

void
zlist_diff_contents(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVec[zpIf->RepoId]->iov_base))->CacheVersion) {
		zsendmsg(zSd, zpIf->p_DiffContent, zpIf->VecSiz, 0, NULL);
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);  // if cache version has changed, return a '!' to frontend
	}
}

void
zdeploy_new(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVec[zpIf->RepoId]->iov_base))->CacheVersion) {
		char zShellBuf[4096];
		sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -D -p %s", zppRepoList[zpIf->RepoId]);
		system(zShellBuf);
		// TO DO: receive deploy results from ECS, 
		// and send it to frontend,
		// recv confirm information from frontend,
		// and write deploy log.
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);  // if cache version has changed, return a '!' to frontend
	}
}

void
zlist_log(_i zSd, zFileDiffInfo *zpIf) {
	struct stat zStatBuf;
	zCheck_Negative_Exit(fstat(zpLogFd[0][zpIf->RepoId], &(zStatBuf)));

	_i zVecNum = 2 * zStatBuf.st_size / sizeof(zDeployLogInfo);
	struct iovec zVec[zVecNum];

	zDeployLogInfo *zpMetaLog = mmap(NULL, zStatBuf.st_size, PROT_READ, MAP_PRIVATE, zpLogFd[0][zpIf->RepoId], 0);
	zCheck_Null_Exit(zpMetaLog);
	madvise(zpMetaLog, zStatBuf.st_size, MADV_WILLNEED);

	_ul zDataLogSiz = (zpMetaLog + zStatBuf.st_size - sizeof(zDeployLogInfo))->offset + (zpMetaLog + zStatBuf.st_size - sizeof(zDeployLogInfo))->len;
	char *zpDataLog = mmap(NULL, zDataLogSiz, PROT_READ, MAP_PRIVATE, zpLogFd[1][zpIf->RepoId], 0);
	zCheck_Null_Exit(zpDataLog);
	madvise(zpDataLog, zDataLogSiz, MADV_WILLNEED);

	for (_i i = 0; i < zVecNum; i++) {
		if (0 == i % 2) {
			zVec[i].iov_base = zpMetaLog + i / 2;
			zVec[i].iov_len = sizeof(zDeployLogInfo);
		}
		else {
			zVec[i].iov_base = zpDataLog + (zpMetaLog + i / 2)->offset;
			zVec[i].iov_len = (zpMetaLog + i / 2)->len;
		}
	}

	zsendmsg(zSd, zVec, zVecNum, 0, NULL);	

	munmap(zpMetaLog, zStatBuf.st_size);
	munmap(zpDataLog, zDataLogSiz);
}

void
zrevoke_from_log(_i zSd, zDeployLogInfo *zpIf){

}

void
zdo_serv(void *zpIf) {
	char zReqBuf[zCommonBufSiz];
	_i zSd = *((_i *)zpIf);
	zrecv_all(zSd, zReqBuf, zCommonBufSiz, NULL);

	switch (zReqBuf[0]) {
		case 'l':
			zlist_diff_files(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'd':
			zlist_diff_contents(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'D':
			zdeploy_new(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'L':
			zlist_log(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'R':
			zrevoke_from_log(zSd, (zDeployLogInfo *)(zReqBuf + 1));  // Need to disign a log struct
			break;
		default:
			zPrint_Err(0, NULL, "Undefined request");
	}
}

void
zstart_server(char *zpHost, char *zpPort, _i zServType) {
	struct epoll_event zEv, zEvents[zMaxEvents];
	_i zMajorSd, zConnSd, zNum, zEpollSd;

	zMajorSd = zgenerate_serv_SD(zpHost, zpPort, zServType);  // DONE: 'bind' and 'listen'

	zEpollSd = epoll_create1(0);
	zCheck_Negative_Exit(zEpollSd);

	zEv.events = EPOLLIN;
	zEv.data.fd = zMajorSd;
	zCheck_Negative_Exit(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv));

	for (;;) {
		zNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);
		zCheck_Negative_Exit(zNum);

		for (_i i = 0; i < zNum; i++) {
		   if (zEvents[i].data.fd == zMajorSd) {
			   zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
			   zCheck_Negative_Exit(zConnSd);

			   zEv.events = EPOLLIN | EPOLLET;
			   zEv.data.fd = zConnSd;
			   zCheck_Negative_Exit(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv));
			}
			else {
				zAdd_To_Thread_Pool(zdo_serv, &(zEvents[i].data.fd));
			}
		}
	}
}

void
zstart_client(void) {

}

/****************
 * UPDATE CACHE *
 ****************/
struct iovec *
zgenerate_cache(_i zRepoId) {
	_i zNewVersion = (_i)time(NULL);

	struct iovec *zpNewCacheVec[2] = {NULL};

	FILE *zpShellRetHandler[2] = {NULL};
	_i zDiffFilesNum = 0;

	char *zpRes[2] = {NULL};
	size_t zResLen[2] = {0};
	_i zBaseSiz = sizeof(zFileDiffInfo);
	char zShellBuf[zCommonBufSiz];

	zpShellRetHandler[0] = popen("git diff --name-only HEAD CURRENT | wc -l && git diff --name-only HEAD CURRENT | git log --format=%H -n 1 CURRENT", "r");
	zCheck_Null_Exit(zpShellRetHandler);

	if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) { return 0; }
	else {
		if (0 == (zDiffFilesNum = atoi(zpRes[0]))) { return  NULL; }
		zMem_Alloc(zpNewCacheVec[0], struct iovec, zDiffFilesNum);
		zpCacheVecSiz[zRepoId] = zDiffFilesNum;  // Global Var

		for (_i i = 0; i < zDiffFilesNum - 1; i++) {
			zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0]);

			zResLen[0] = strlen(zpRes[0]);
			zCheck_Null_Exit(
					zpNewCacheVec[0][i].iov_base = malloc(1 + zResLen[0] + zBaseSiz)
					);
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->CacheVersion = zNewVersion;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->RepoId= zRepoId;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->FileIndex = i;
			strcpy(((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->path, zpRes[0]);

			sprintf(zShellBuf, "git diff HEAD CURRENT -- %s | wc -l && git diff HEAD CURRENT -- %s", zpRes[0], zpRes[0]);
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

		char *zpBuf = zget_one_line_from_FILE(zpShellRetHandler[0]);
		zMem_Alloc(zppCurTagSig[zRepoId], char, 40);  // Not include '\0'
		strncpy(zppCurTagSig[zRepoId], zpBuf, 40);  // Global Var
		pclose(zpShellRetHandler[0]);
	}

	return zpNewCacheVec[0];
}

void
zupdate_cache(void *zpIf) {
	_i zRepoId = *((_i *)zpIf);

	struct iovec *zpOldCacheIf = zppCacheVec[zRepoId];
	if (NULL == (zppCacheVec[zRepoId] = zgenerate_cache(zRepoId))) {
		zppCacheVec[zRepoId] = zpOldCacheIf;  // Global Var
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
}

#undef UDP
#undef TCP
#undef zMaxEvents
