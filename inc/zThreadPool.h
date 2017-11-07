#ifndef ZTHREADPOOL_H
#define ZTHREADPOOL_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include "zCommon.h"

struct zThreadPool__ {
    void (* init) (void);
    void (* add) (void * (*) (void *), void *);
};

// extern struct zThreadPool__ zThreadPool_;

#endif  // #ifndef ZTHREADPOOL_H
