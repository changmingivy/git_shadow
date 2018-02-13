#ifndef ZSUPERVISOR_H
#define ZSUPERVISOR_H

#include "zcommon.h"

#include "zrun.h"
#include "znative_ops.h"
#include "zsv_cloud.h"

#include "zthread_pool.h"
#include "zpostgres.h"

struct zSuperVisor__ {
    void * (* init) (void *);
    _i (* write_db) (void *, _i, struct sockaddr *, socklen_t);

    void * (* sys_monitor) (void *zp);
};

#endif  // #ifndef ZSUPERVISOR_H
