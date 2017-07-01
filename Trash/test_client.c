#ifndef _Z
    //#include "../zmain.c"
#define _XOPEN_SOURCE 700
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netdb.h>
	#include "zutils.h"
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <dirent.h>
#endif

/*
 * Functions for socket connection.
 */
struct addrinfo *
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
    if (-1 == zErr){
        zPrint_Err(errno, NULL, gai_strerror(zErr));
        exit(1);
    }

    return zpRes->ai_addr;
}

// Start server: TCP or UDP,
// Option zServType: 1 for TCP, 0 for UDP.
_i
zgenerate_serv_SD(char *zpHost, char *zpPort, _i zServType) {
    _i zSockType = (0 == zServType) ? SOCK_DGRAM : SOCK_STREAM;
    _i zSd = socket(AF_INET, zSockType, 0);
    zCheck_Negative_Return(zSd, -1);

    struct sockaddr *zpAddrIf = zgenerate_serv_addr(zpHost, zpPort);
    zCheck_Negative_Return(bind(zSd, zpAddrIf, INET_ADDRSTRLEN), -1);

    zCheck_Negative_Return(listen(zSd, 5), -1);

    return zSd;
}

// Used by client.
_i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zSockType, _i zProto) {
    if (zSockType == 0) { zSockType = SOCK_STREAM; }
    if (zProto == 0) { zProto = IPPROTO_TCP; }

    _i zSd = socket(AF_INET, zSockType, zProto);
    zCheck_Negative_Return(zSd, -1);
    for (_i i = 4; i > 0; --i) {
        if (0 == connect(zSd, zpAddr, zLen)) { return zSd; }
        close(zSd);
        sleep(i);
    }

    return -1;
}

// Used by client.
_i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
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
zsendto(_i zSd, void *zpBuf, size_t zLen, struct sockaddr *zpAddr) {
    _i zSentSiz = sendto(zSd, zpBuf, zLen, 0, zpAddr, INET_ADDRSTRLEN);
    zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

_i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, struct sockaddr *zpAddr) {
    _i zFlags = MSG_WAITALL;
    socklen_t zAddrLen = 0;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

_i
main(void) {
	char zBuf[128];
	char *zpX = zBuf;
	_i zSd = ztcp_connect("127.0.0.1", "20000", 0);
	zsendto(zSd, "Hi", 3, NULL);
	_i zLen = 0;
	//while (0 < (zLen = zrecv_all(zSd, zpX, 128, NULL))) { 
//		printf("%d\n", zLen);	
//		printf("%s\n", zBuf);
//		zpX += zLen;
//	}

	//zLen = zrecv_all(zSd, zBuf, 128, NULL);
	zLen = recv(zSd, zBuf, 128, 0);

	printf("%s %s %s\n", zBuf, zpX + 4, zpX + 8);

//	shutdown(zSd, SHUT_RDWR);
}
