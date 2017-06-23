#define _POSIX_C_SOURCE 201112L
#include <sys/socket.h>
#include <netdb.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/syslog.h>

#include <dirent.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "zutils.h"

#define zCommonBufSiz 4096 // The defination will be undefine in the end of the file.

void *
zregister_malloc(const size_t zSiz) {
	register void *zpRes = malloc(zSiz);
	zCheck_Null_Exit(zpRes);
	return zpRes;
}

void *
zregister_realloc(void *zpPrev, const size_t zSiz) {
	register void *zpRes = realloc(zpPrev, zSiz);
	zCheck_Null_Exit(zpRes);
	return zpRes;
}

void *
zregister_calloc(const int zCnt, const size_t zSiz) {
	register void *zpRes = calloc(zCnt, zSiz);
	zCheck_Null_Exit(zpRes);
	return zpRes;
}

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

	for (i = 1; i < zMax; i++) {
		zRes[i] = (zRightOffset[i] | zLeftOffset[i-1]) & mask;
	}
	zRes[zMax - 1] = zLeftOffset[zMax - 2] & mask;

	for (i = 0; i < zMax; i++) {
		zRes[i] = zBase64Dict[(_i)zRes[i]];
	}
	
	for (i = zMax; i < zResLen; i++) {
		zRes[i] = '=';
	}

	return zRes;
}

/*
 * Functions for socket connection.
 * Static resource, so DO NOT to attemp to free the result.
 */
static struct addrinfo *
zinit_hints(_i zAF, _i zFlags) {
	static struct addrinfo zHints;
	zHints.ai_flags = zFlags;
	zHints.ai_family = (0 == zAF) ? AF_INET : zAF;

	return &zHints;
}

#define zMaxTry 4
static _i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zAF, _i zType, _i zProto) {
	if (zAF == 0) {
		zAF = AF_INET;
		zType = SOCK_STREAM;
		zProto = IPPROTO_TCP;
	}

	_i zSockD = socket(zAF, zType, zProto);
	zCheck_Negative_Exit(zSockD);

	for (_i i = zMaxTry; i > 0; i--) {
		if (0 == connect(zSockD, zpAddr, zLen)) {
			return zSockD;
		}

		close(zSockD);
		sleep(2 * (zMaxTry - i));
	}

	return -1;
}
#undef zMaxTry

_i
zsock_connect(char *zpHost, char *zpPort, _i zFlags) {
	struct addrinfo *zpRes, *zpTmp, *zpHints;
	_i zSockD, zErr;

	zpHints = zinit_hints(0, zFlags);

	zErr = getaddrinfo(zpHost, zpPort, zpHints, &zpRes);
	if (-1 == zErr){
		zPrint_Err(errno, NULL, gai_strerror(zErr));
	}

	zpTmp = zpRes;
	for (; NULL != zpTmp; zpTmp = zpTmp->ai_next) {
		if((zSockD  = ztry_connect(zpTmp->ai_addr, INET_ADDRSTRLEN, 0, 0, 0)) > 0) {
			freeaddrinfo(zpRes);
			return zSockD;
		}
	}

	freeaddrinfo(zpRes);
	return -1;
}

char *
zget_reponse(char *zpReq, char *zpHost, char *zpPort, _i zFlags) {
	_i zSockD;
	static char zBuf[zCommonBufSiz];

	zCheck_Negative_Exit((zSockD = zsock_connect(zpHost, zpPort, zFlags)));
	zCheck_Negative_Exit(send(zSockD, zpReq, strlen(zpReq), 0));
	zCheck_Negative_Exit(recv(zSockD, zBuf, zCommonBufSiz, MSG_WAITALL));

	shutdown(zSockD, SHUT_RDWR);

	return zBuf;
}

void
zbind_dynamic_domain (_i zFlags) {
	char *zHost = "members.3322.net";
	char *zPort = "80";
	char *zReq[3];

	zReq[0] = "GET /dyndns/update?hostname=fanhui.f3322.net&myip=ipaddress&wildcard=OFF&mx=mail.exchanger.ext&backmx=NO&offline=NO HTTP/1.1\nHost: members.3322.net\nConnection: Close\nAuthorization: Basic ";
	zReq[1] = zstr_to_base64("aibbigql");
	zReq[2] = "\n\n";

	char request[] = {'\0'};
	for (_i i = 0; i < 3; i++) {
		strcat(request, zReq[i]);
	}

	free(zReq[1]);

	zget_reponse(request, zHost, zPort, zFlags);
}

/*
 * Daemonize a process to daemon.
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
	if (zPid > 0) {
		exit(0);
	}

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

	if (0 == zPid) {
		execvp(zpCommand, zppArgv);
	}
	else {
		waitpid(zPid, NULL, 0);
	}
}


/*
 * DO NOT try to free memory.
 */
char *
zget_one_line_from_FILE(FILE *zpFile) {
	static char zBuf[zCommonBufSiz];
	char *zpRes = fgets(zBuf, zCommonBufSiz, zpFile);

	if (NULL == zpRes) {
		if(0 == feof(zpFile)) {
			zCheck_Null_Exit(zpRes);
		}
		return NULL;
	}
	return zBuf;
}

#undef zCommonBufSiz
