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
	#include "zutils.h"
	#include "zcommon.c"
	#include "zmain.c"
	#include "zthread_pool.c"
	#define zCommonBufSiz 4096 
	static char CurTagSig[40];  // git SHA1 sig
	static struct  iovec *zpCacheVec;  // Global cache for git diff content
	static _i zCacheVecSiz;
#endif

#include <sys/epoll.h>

#define UDP 0
#define TCP 1

#define zMaxEvents 64

typedef struct zDeployResInfo {
	_us CacheVersion;
	_us DeployState;
} zDeployResInfo;

//_i HostIndex;  //ECS hash index

/*
 * return contents from cache directly
 * use zsendmsg
 * msg form(sent from frontend, to git_shadow):
 * [OpsMark(l/d/D/R)][struct zFileDiffInfo]
 * Meaning:
 * 		-l:list modified file list
 * 		-d:file content diff
 * 		-D:deploy
 * 		-R:revoke
 */
void
zlist_diff_files(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zpCacheVec->iov_base))->CacheVersion) {
		zsendmsg(zSd, zpCacheVec, zCacheVecSiz, 0, NULL);
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);
	}
}

void
zlist_diff_contents(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zpCacheVec->iov_base))->CacheVersion) {
		zsendmsg(zSd, zpIf->p_DiffContent, zpIf->VecSiz, 0, NULL);
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);
	}
}

void
zdeploy_new(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zpCacheVec->iov_base))->CacheVersion) {
		char zShellBuf[4096];
		sprintf(zShellBuf, "");
		system(zShellBuf);
		// TO DO: send deploy results to frontend.
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);
	}
}

void
zlist_log(_i zSd) {

}

void
zrevoke_from_log(_i zSd, void *zpIf){

}

static void
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
			zlist_log(zSd);
			break;
		case 'R':
			zrevoke_from_log(zSd, (void *)(zReqBuf + 1));  // Need to disign a log struct
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
