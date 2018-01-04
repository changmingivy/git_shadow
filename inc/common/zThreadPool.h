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
#include <semaphore.h>
#include "zCommon.h"

typedef struct zThreadTask__ {
    pthread_cond_t condVar;

    void * (* func) (void *);
    void *p_param;
} zThreadTask__ ;

struct zThreadPool__ {
    _i (* init) (_i, _i);
    _i (* add) (void * (*) (void *), void *);

    /*
     * 这是一个使用 sem_open 创建的，
     * 系统范围内所有进程共享的命名信号量，名称："git_shadow"，文件位置：/dev/shm/sem.git_shadow
     * 用于限制主进程及所有项目进程的总线程数
     */
    sem_t *p_threadPoolSem;

    /*
     * 存放无用的 tid
     */
    pthread_t *p_tid;
};

#endif  // #ifndef ZTHREADPOOL_H
