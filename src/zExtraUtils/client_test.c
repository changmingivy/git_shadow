#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
//#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "../../inc/common/zCommon.h"

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

struct addrinfo *
zgenerate_hint(_i zFlags) {
    static struct addrinfo zHints;
    zHints.ai_flags = zFlags;
    zHints.ai_family = AF_INET;
    return &zHints;
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
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

#define zBufSiz 10240
void
zclient(char *zpParam) {
    _i zSd = ztcp_connect("192.168.1.254", "20000", AI_NUMERICHOST | AI_NUMERICSERV);
    if (-1 == zSd) {
        fprintf(stderr, "Connect to server failed \n");
        _exit(1);
    }

    // 列出所有项目元信息
    //char zStrBuf[] = "{\"OpsId\":5}";

    // 列出单个项目元信息
    //char zStrBuf[] = "{\"OpsId\":6,\"ProjId\":11}";

    // 创建新项目
    //char zStrBuf[] = "{\"OpsId\":1,\"ProjId\":\"111\",\"PathOnHost\":\"/home/git/111_Y\",\"SourceUrl\":\"https://git.coding.net/kt10/FreeBSD.git\",\"SourceBranch\":\"master\",\"SourceVcsType\":\"git\",\"NeedPull\":\"Y\"}";

    // 查询版本号列表
    //char zStrBuf[] = "{\"OpsId\":9,\"ProjId\":11,\"DataType\":0}";

    // 打印差异文件列表
    //char zStrBuf[] = "{\"OpsId\":10,\"ProjId\":11,\"RevId\":1,\"CacheId\":1000000000,\"DataType\":0}";

    // 打印差异文件内容
    //char zStrBuf[] = "{\"OpsId\":11,\"ProjId\":11,\"RevId\":0,\"FileId\":0,\"CacheId\":1000000000,\"DataType\":0}";

    // 布署与撤销
    char zStrBuf[8192]; sprintf(zStrBuf, "{\"OpsId\":12,\"ProjId\":11,\"RevId\":%s,\"CacheId\":1000000000,\"DataType\":0,\"data\":\"172.16.0.1|172.16.0.2|172.16.0.3|172.16.0.4|172.16.0.5|172.16.0.6|172.16.0.7|172.16.0.8|172.16.0.9|172.16.0.10|172.16.0.11|172.16.0.12|172.16.0.13|172.16.0.14|172.16.0.15|172.16.0.16|172.16.0.17|172.16.0.18|172.16.0.19|172.16.0.20|172.16.0.21|172.16.0.22|172.16.0.23|172.16.0.24\",\"ExtraData\":\"24\"}", zpParam);

    zsendto(zSd, zStrBuf, strlen(zStrBuf), 0, NULL);

    char zBuf[zBufSiz] = {'\0'};

    while (0 < recv(zSd, &zBuf, zBufSiz, 0)) {
        for (_i i = 0; i < zBufSiz; i++) {
            fprintf(stderr, "%c", zBuf[i]);
        }
        memset(zBuf, 0, zBufSiz);
    }

    close(zSd);
}

_i
main(_i zArgc __attribute__ ((__unused__)), char **zppArgv) {
    zclient(zppArgv[1]);
    return 0;
}
