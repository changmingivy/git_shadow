#define ZRUN_H

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#ifndef ZTHREADPOOL_H
#include "zThreadPool.h"
#endif

#ifndef ZNETUTILS_H
#include "zNetUtils.h"
#endif

#ifndef ZDPOPS_H
#include "zDpOps.h"
#endif

typedef struct zSockAcceptParam__ {
    void *p_ThreadPoolMeta_;  // 未使用，仅占位
    _i ConnSd;
} zSockAcceptParam__;

struct zRun__ {
	void (* run) (void *);
	_i (* ops[16]) (zMeta__*, _i);
};


// extern struct zRun__ zRun_;
