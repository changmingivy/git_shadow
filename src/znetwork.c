#ifndef _Z
	#include "zmain.c"
#endif

#define zMaxEvents 64
#define UDP 0
#define TCP 1

/*
 * return contents from cache directly
 * use zsendmsg
 * msg form(sent from frontend, to git_shadow):
 * [OpsMark(l/d/D/R)][struct zFileDiffInfo]
 * Meaning:
 *		 -l:list modified file list, default behavior
 *		 -d:file content diff
 *		 -D:deploy, need shell script
 *		 -L:list deploy log
 *		 -R:revoke, need shell script
 *		 -r:reply msg from ECS
 */
void
zlist_diff_files(_i zSd, zFileDiffInfo *zpIf){
	zsendmsg(zSd, zppCacheVec[zpIf->RepoId], zpCacheVecSiz[zpIf->RepoId], 0, NULL);
}

void
zprint_diff_contents(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVec[zpIf->RepoId]->iov_base))->CacheVersion) {
		zsendmsg(zSd, zpIf->p_DiffContent, zpIf->VecSiz, 0, NULL);
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
zmilli_sleep(_i zMilliSec) {
	static struct timespec zNanoSecIf = { .tv_sec = 0, };
	zNanoSecIf.tv_nsec  = zMilliSec * 1000000;
	nanosleep(&zNanoSecIf, NULL);
}

void
zwrite_log(_i zRepoId, char *zpPathName, _i zPathLen) {
	// write to log.meta
	struct stat zStatBuf;
	zCheck_Negative_Exit(fstat(zpLogFd[0][zRepoId], &zStatBuf));

	zDeployLogInfo zDeployIf;
	pread(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo), zStatBuf.st_size - sizeof(zDeployLogInfo));

	zDeployIf.RepoId = zRepoId;
	zDeployIf.index += sizeof(zDeployLogInfo);
	zDeployIf.offset += zPathLen;
	zDeployIf.TimeStamp = time(NULL);
	zDeployIf.len = zPathLen;

	if (sizeof(zDeployLogInfo) != write(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo))) {
		zPrint_Err(0, NULL, "Can't write to log.meta!");
		exit(1);
	}
	// write to log.data
	if (zPathLen != write(zpLogFd[1][zRepoId], zpPathName, zPathLen)) {
		zPrint_Err(0, NULL, "Can't write to log.data!");
		exit(1);
	}
	// write to log.sig
	if ( 40 != write(zpLogFd[2][zRepoId], zppCurTagSig[zRepoId], 40)) {
		zPrint_Err(0, NULL, "Can't write to log.sig!");
		exit(1);
	}
}

void
zdeploy(_i zSd, zFileDiffInfo *zpDiffIf, _i zMarkAll) {
	if (zpDiffIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVec[zpDiffIf->RepoId]->iov_base))->CacheVersion) {
		char zShellBuf[4096];
		char *zpLogContents;
		_i zLogSiz;
		if (1 == zMarkAll) { 
			sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -D -P %s", zppRepoList[zpDiffIf->RepoId]); 
			zpLogContents = "ALL";
			zLogSiz = 4 * sizeof(char);
		} 
		else { 
			sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -d -P %s %s", zppRepoList[zpDiffIf->RepoId], zpDiffIf->path); 
			zpLogContents = zpDiffIf->path;
			zLogSiz = zpDiffIf->PathLen;
		}

		pthread_mutex_lock(&(zpDeployLock[zpDiffIf->RepoId]));
		system(zShellBuf);

		do {
			// TO DO: receive deploy results from ECS, 
			// and send it to frontend,
			// recv confirm information from frontend,
			zmilli_sleep(1000);
		} while (zpReplyCnt[zpDiffIf->RepoId] < zpTotalHost[zpDiffIf->RepoId]);

		zwrite_log(zpDiffIf->RepoId, zpLogContents, zLogSiz);
		pthread_mutex_unlock(&(zpDeployLock[zpDiffIf->RepoId]));

		zsendto(zSd, "Y", 2 * sizeof(char), NULL);
	} 
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);  // if cache version has changed, return a '!' to frontend
	}
}

void
zrevoke_from_log(_i zSd, zDeployLogInfo *zpLogIf){
	pthread_mutex_lock(&(zpDeployLock[zpLogIf->RepoId]));

	do {
		// TO DO: receive deploy results from ECS, 
		// and send it to frontend,
		// recv confirm information from frontend,
		zmilli_sleep(1000);
	} while (zpReplyCnt[zpLogIf->RepoId] < zpTotalHost[zpLogIf->RepoId]);

	char zBuf[zpLogIf->len];
	pread(zpLogFd[1][zpLogIf->RepoId], &zBuf, zpLogIf->len, zpLogIf->offset);
	zwrite_log(zpLogIf->RepoId, zBuf,zpLogIf->len);
	zsendto(zSd, "Y", 2 * sizeof(char), NULL);

	pthread_mutex_unlock(&(zpDeployLock[zpLogIf->RepoId]));
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
		case 'p':
			zprint_diff_contents(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'd':  // deploy one file
			zdeploy(zSd, (zFileDiffInfo *)(zReqBuf + 1), 0);
			break;
		case 'D':  // deploy all
			zdeploy(zSd, (zFileDiffInfo *)(zReqBuf + 1), 1);
			break;
		case 'L':
			zlist_log(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'R':
			zrevoke_from_log(zSd, (zDeployLogInfo *)(zReqBuf + 1));  // Need to disign a log struct
			break;
		case 'r':
			zpReplyCnt[((zDeployResInfo *)(zReqBuf + 1))->RepoId]++;
			break;
		default:
			zPrint_Err(0, NULL, "Undefined request");
	}
}

// exec on server
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

// exec on ECS
void
zclient_reply(char *zpHost, char *zpPort) {
	zDeployResInfo zDpResIf;

	_i zFd = open("/PATH/TO/MyIp", O_RDONLY);  // form like 10.10.10.10, Receive from cmdline?
	zCheck_Negative_Exit(zFd);

	char zAddrBuf[INET_ADDRSTRLEN] = {'\0'};
	zCheck_Negative_Exit(read(zFd, zAddrBuf, INET_ADDRSTRLEN));
	_i zStrResLen = (_i)(strlen(zAddrBuf) - 3);
	for (_i i =0, j = 0; j < zStrResLen; i++, j++) {
		while ('.' == zAddrBuf[i]) { i++; }
		zAddrBuf[j] = zAddrBuf[i];
	}
	zDpResIf.ClientAddr = strtol(zAddrBuf, NULL, 10);
	close(zFd);

	zFd = open("/PATH/TO/REPO/.git_shadwo/log.meta", O_RDONLY);  // Receive from cmdline?
	zCheck_Negative_Exit(zFd);

	zDeployLogInfo zDpLogIf;
	zCheck_Negative_Exit(read(zFd, &zDpLogIf, sizeof(zDeployLogInfo)));
	zDpResIf.RepoId = zDpLogIf.RepoId;
	close(zFd);

	_i zSd = ztcp_connect(zpHost, zpPort, AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV);
	if (-1 == zSd) {
		zPrint_Err(0, NULL, "Connect to server failed.");
		exit(1);
	}

	struct iovec zVec[2];
	char zMark = 'r';
	zVec[0].iov_base = &zDpResIf;
	zVec[0].iov_len = sizeof(zDeployResInfo);
	zVec[1].iov_base = &zMark;
	zVec[1].iov_len = sizeof(char);
	if ((sizeof(char) + sizeof(zDeployLogInfo)) != zsendmsg(zSd, zVec, 2, 0, NULL)) {
		zPrint_Err(0, NULL, "Reply to server failed.");
		exit(1);
	}
	shutdown(zSd, SHUT_RDWR);
}

#undef zMaxEvents
#undef UDP
#undef TCP
