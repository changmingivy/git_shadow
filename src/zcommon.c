#ifndef _Z
	#define _XOPEN_SOURCE 700
	#include <sys/socket.h>
	#include <netdb.h>
	#include <sys/types.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <sys/stat.h>
	#include <sys/signal.h>
	#include <dirent.h>
	#include <stdio.h>
	#include <string.h>
	#include <stdlib.h>
	#include <errno.h>
	#include "zutils.h"
#endif

/*
 * Functions for base64 coding [and decoding(TO DO)]
 */
static char zBase64Dict[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char *
zstr_to_base64(const char *zpOrig) {
	_i zOrigLen = strlen(zpOrig);
	_i zMax = (0 == zOrigLen % 3) ? (zOrigLen / 3 * 4) : (1 + zOrigLen / 3 * 4);
	_i zResLen = zMax + (4- (zMax % 4));

	char zRightOffset[zMax], zLeftOffset[zMax];

	char *zRes;
	zMem_Alloc(zRes, char, zResLen);

	_i i, j;

	for (i = j = 0; i < zMax; i++) {
		if (3 == (i % 4)) {
			zRightOffset[i] = 0;
			zLeftOffset[i] = 0;
		}
		else {	
			zRightOffset[i] = zpOrig[j]>>(2 * ((j % 3) + 1));
			zLeftOffset[i] = zpOrig[j]<<(2 * (2 - (j % 3)));
			j++;
		}
	}

	_c mask = 63;
	zRes[0] = zRightOffset[0] & mask;

	for (i = 1; i < zMax; i++) { zRes[i] = (zRightOffset[i] | zLeftOffset[i-1]) & mask; }
	zRes[zMax - 1] = zLeftOffset[zMax - 2] & mask;

	for (i = 0; i < zMax; i++) { zRes[i] = zBase64Dict[(_i)zRes[i]]; }
	for (i = zMax; i < zResLen; i++) { zRes[i] = '='; }

	return zRes;
}

/*
 * Functions for socket connection.
 */
static struct addrinfo *
zgenerate_hint(_i zFlags) {
	static struct addrinfo zHints;
	zHints.ai_flags = zFlags;
	zHints.ai_family = AF_INET;
	return &zHints;
}

// Generate a socket fd used by server to do 'accept'.
struct sockaddr *
zgenerate_serv_addr(char *zpHost, char *zpPort) {
	struct addrinfo *zpRes, *zpHint;
	zpHint = zgenerate_hint(AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV);

	_i zErr = getaddrinfo(zpHost, zpPort, zpHint, &zpRes);
	if (-1 == zErr){ zPrint_Err(errno, NULL, gai_strerror(zErr)); }

	return zpRes->ai_addr;
}

// Used by client.
static _i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zSockType, _i zProto) {
	if (zSockType == 0) { zSockType = SOCK_STREAM; }
	if (zProto == 0) { zProto = IPPROTO_TCP; }

	_i zSD = socket(AF_INET, zSockType, zProto);
	zCheck_Negative_Exit(zSD);
	for (_i i = 4; i > 0; --i) {
		if (0 == connect(zSD, zpAddr, zLen)) { return zSD; }
		close(zSD);
		sleep(i);
	}

	return -1;
}

// Used by client.
_i
zconnect(char *zpHost, char *zpPort, _i zFlags) {
	struct addrinfo *zpRes, *zpTmp, *zpHints;
	_i zSockD, zErr;

	zpHints = zgenerate_hint(zFlags);

	zErr = getaddrinfo(zpHost, zpPort, zpHints, &zpRes);
	if (-1 == zErr){ zPrint_Err(errno, NULL, gai_strerror(zErr)); }

	for (zpTmp = zpRes; NULL != zpTmp; zpTmp = zpTmp->ai_next) {
		if(0 < (zSockD  = ztry_connect(zpTmp->ai_addr, INET_ADDRSTRLEN, 0, 0))) {
			freeaddrinfo(zpRes);
			return zSockD;
		}
	}

	freeaddrinfo(zpRes);
	return -1;
}

// Send message from multi positions.
_i
zsendto(_i zSD, void *zpBuf, size_t zLen, struct sockaddr *zpAddr) {
	return sendto(zSD, zpBuf, zLen, 0, zpAddr, INET_ADDRSTRLEN);
}

_i
zsendmsg(_i zSD, struct iovec *zpIov, _i zIovSiz, _i zFlags, struct sockaddr *zpAddr) {
	struct msghdr zMsgIf = {
		.msg_name = zpAddr, 
		.msg_namelen = INET_ADDRSTRLEN, 
		.msg_iov = zpIov, 
		.msg_iovlen = zIovSiz, 
		.msg_control = NULL, 
		.msg_controllen = 0, 
		.msg_flags = 0
	};
	return sendmsg(zSD, &zMsgIf, zFlags);
}

_i
zrecvfrom(_i zSD, void *zpBuf, size_t zLen, struct sockaddr *zpAddr) {
	_i zFlags = MSG_WAITALL;
	socklen_t zAddrLen = 0;
	return recvfrom(zSD, zpBuf, zLen, zFlags, zpAddr, &zAddrLen);
}

/*
 * Daemonize a linux process to daemon.
 */
static void
zclose_fds(pid_t zPid) {
	struct dirent *zpDirIf;
	char zStrPid[8], zPath[64];

	sprintf(zStrPid, "%d", zPid);

	strcpy(zPath, "/proc/");
	strcat(zPath, zStrPid);
	strcat(zPath, "/fd");
	
	_i zFD;
	DIR *zpDir = opendir(zPath);
	while (NULL != (zpDirIf = readdir(zpDir))) {
		zFD = atoi(zpDirIf->d_name);
		if (2 != zFD) { close(zFD); }
	}
	closedir(zpDir);
}

void
zdaemonize(const char *zpWorkDir) {
	_i zFD;
	
	signal(SIGHUP, SIG_IGN);

	umask(0);
	zCheck_Negative_Exit(chdir(NULL == zpWorkDir? "/" : zpWorkDir));

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (zPid > 0) { exit(0); }

	setsid();
	zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (zPid > 0) { exit(0); }

	zclose_fds(getpid());	

	zFD = open("/dev/null", O_RDWR);
	dup2(zFD, 1);
//	dup2(zFD, 2);
}

/*
 * Fork a child process to exec an outer command.
 * The "zppArgv" must end with a "NULL"
 */
void
zfork_do_exec(const char *zpCommand, char **zppArgv) {
	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (0 == zPid) { execvp(zpCommand, zppArgv); }
	else { waitpid(zPid, NULL, 0); }
}

/*
 * DO NOT try to free memory.
 */
char *
zget_one_line_from_FILE(FILE *zpFile) {
	static char zBuf[zCommonBufSiz];
	char *zpRes = fgets(zBuf, zCommonBufSiz, zpFile);

	if (NULL == zpRes) {
		if(0 == feof(zpFile)) { zCheck_Null_Exit(zpRes); }
		return NULL;
	}
	return zBuf;
}
