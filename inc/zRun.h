#ifndef ZRUN_H
#define ZRUN_H

#include "zCommon.h"
#include "zThreadPool.h"
#include "zNetUtils.h"
#include "zDpOps.h"

typedef struct __zSockAcceptParam__ {
    void *p_ThreadPoolMeta_;  // 未使用，仅占位
    _i ConnSd;
} zSockAcceptParam__;


struct zRun__ {
    void (* run) (zNetSrv__ *, zPgLogin__ *);
    void * (* route) (void *);

    _i (* ops[16]) (zMeta__*, _i);
};


// extern struct zRun__ zRun_;
#endif  // #ifndef ZRUN_H
