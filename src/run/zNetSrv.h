#include "zCommon.h"
#include "zThreadPool.h"
#include "zNetUtils.h"
#include "zDpOps.h"

typedef struct zSockAcceptParam__ {
    void *p_ThreadPoolMeta_;  // 未使用，仅占位
    _i ConnSd;
} zSockAcceptParam__;


_i (* zNetOps[16]) (struct zMeta__ *, _i);
