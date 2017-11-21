#define _Z
//#define _Z_BSD

#ifndef _Z_BSD
    #define _XOPEN_SOURCE 700
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
#endif

#ifdef _Z_BSD
    #include <netinet/in.h>
    #include <signal.h>
#else
    #include <sys/signal.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "zCommon.h"

struct addrinfo *
zgenerate_hint(_i zFlags) {
    static struct addrinfo zHints;
    zHints.ai_flags = zFlags;
    zHints.ai_family = AF_INET;
    return &zHints;
}

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

_i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
    _i zSentSiz = sendto(zSd, zpBuf, zLen, 0 | zFlags, zpAddr, INET_ADDRSTRLEN);
    zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

_i
main(_i zArgc, char **zppArgv) {
    _i zSd, zLen;
    char zSendBuf[4096];

    if (9 != zArgc) { _exit(1); }
    zLen = sprintf(zSendBuf, "{\"OpsId\":%s,\"ProjId\":%s,\"HostId\":%s,\"RevSig\":\"%s\",\"TimeStamp\":%s,\"ReplyType\":\"%s\"}",
            zppArgv[3],
            zppArgv[4],
            zppArgv[5],
            zppArgv[6],
            zppArgv[7],
            zppArgv[8]
            );

    if (0 < (zSd = ztcp_connect(zppArgv[1], zppArgv[2], 0))) {
        zsendto(zSd, zSendBuf, zLen, 0, NULL);
        close(zSd);  // 只有连接成功才需要关闭
    }

    return 0;
}
