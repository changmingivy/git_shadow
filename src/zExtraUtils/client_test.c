#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "../../inc/common/zCommon.h"
#include "../run/common/zNetUtils.c"

#define zBufSiz 10240
void
zclient(char *zpParam) {
    _i zSd = ztcp_connect("::1", "20000", AI_NUMERICHOST | AI_NUMERICSERV);
    if (-1 == zSd) {
        fprintf(stderr, "Connect to server failed \n");
        _exit(1);
    }

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

    // 布署/撤销
    char zStrBuf[8192]; sprintf(zStrBuf, "{\"OpsId\":12,\"ProjId\":11,\"RevId\":%s,\"CacheId\":1000000000,\"DataType\":0,\"data\":\"::1 0::1\":\"2\"}", zpParam);

    zsend_nosignal(zSd, zStrBuf, strlen(zStrBuf));

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
