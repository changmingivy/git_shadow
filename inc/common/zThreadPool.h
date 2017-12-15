#ifndef ZTHREADPOOL_H
#define ZTHREADPOOL_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include <pthread.h>
#include "zCommon.h"

typedef struct zThreadTask__ {
    pthread_cond_t condVar;

    void * (* func) (void *);
    void *p_param;
} zThreadTask__ ;

struct zThreadPool__ {
    void (* init) (void);
    void (* add) (void * (*) (void *), void *);
};

#endif  // #ifndef ZTHREADPOOL_H
