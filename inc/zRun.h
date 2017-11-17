#ifndef ZRUN_H
#define ZRUN_H

#include "zCommon.h"
#include "zThreadPool.h"
#include "zNetUtils.h"
#include "zDpOps.h"

#define zServHashSiz 16

typedef struct __zSockAcceptParam__ {
    void *p_threadPoolMeta_;  // 未使用，仅占位
    _i connSd;
} zSockAcceptParam__;

struct zRun__ {
    void (* run) (zNetSrv__ *, zPgLogin__ *);
    void * (* route) (void *);

    _i (* ops[zServHashSiz]) (char *, _i);
};

#endif  // #ifndef ZRUN_H
