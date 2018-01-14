#ifndef ZSUPERVISOR_H
#define ZSUPERVISOR_H

#include "zCommon.h"

#include "zRun.h"
#include "zNativeOps.h"

#include "zThreadPool.h"
#include "zPgSQL.h"

struct zSuperVisor__ {
    void * (* init) (void *);
    _i (* write_db) (void *, _i, struct sockaddr *, socklen_t);

    void * (* sys_monitor) (void *zp);
};

#endif  // #ifndef ZSUPERVISOR_H
