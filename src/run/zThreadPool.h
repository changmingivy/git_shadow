#define ZTHREADPOOL_H

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

struct zThreadPool__ {
    void (* init) (void);
    void (* add) (void * (*) (void *), void *);
};

// extern struct zThreadPool__ zThreadPool_;
