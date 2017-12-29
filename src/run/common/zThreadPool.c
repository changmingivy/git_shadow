#include "zThreadPool.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static _i zthread_pool_init(_i zSiz, _i zGlobSiz);
static _i zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam, pthread_t *zpTidOUT);

/******************************
 * ====  对外公开的接口  ==== *
 ******************************/
struct zThreadPool__ zThreadPool_ = {
    .init = zthread_pool_init,
    .add = zadd_to_thread_pool
};

/* 线程池栈结构 */
static _i zThreadPollSiz;
static _i zOverflowMark;

static zThreadTask__ **zppPoolStack_;
static _i zStackHeader = -1;

static pthread_mutex_t zStackHeaderLock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t zThreadPoolTidTrash;


/*
 * 线程退出时，释放自身占用的资源
 */
static void
zthread_canceled_cleanup(void *zp_) {
    zThreadTask__ *zpSelfTask = (zThreadTask__ *) zp_;
    pthread_cond_destroy(&(zpSelfTask->condVar));
    free(zpSelfTask);

    sem_post(zThreadPool_.p_threadPoolSem);
}


static void *
zthread_pool_meta_func(void *zp_ __attribute__ ((__unused__))) {
    zThreadTask__ *zpSelfTask;
    zMEM_ALLOC(zpSelfTask, zThreadTask__, 1);

    zpSelfTask->selfTid = pthread_self();
    zpSelfTask->func = NULL;

    pthread_detach(zpSelfTask->selfTid);

    zCHECK_PTHREAD_FUNC_EXIT( pthread_cond_init(&(zpSelfTask->condVar), NULL) );

    /* 注册因收到 cancel 指令而退出时的资源清理动作 */
    pthread_cleanup_push(zthread_canceled_cleanup, zpSelfTask);

    /* 线程可被cancel，且 cancel 属性设置为立即退出 */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

zMark:
    pthread_mutex_lock(&zStackHeaderLock);

    if (zStackHeader < zOverflowMark) {
        zppPoolStack_[++zStackHeader] = zpSelfTask;
        while (NULL == zpSelfTask->func) {
            /* 等待任务到达 */
            pthread_cond_wait( &(zpSelfTask->condVar), &zStackHeaderLock );
        }
        pthread_mutex_unlock(&zStackHeaderLock);

        zpSelfTask->func(zpSelfTask->p_param);

        zpSelfTask->func = NULL;
        goto zMark;
    } else {
        pthread_mutex_unlock(&zStackHeaderLock);

        /*
         * 太多空闲线程时，自行退出
         * 会触发 push-pop 清理资源
         */
        pthread_exit((void *) -1);
    }

    pthread_cleanup_pop(0);
}


/*
 * @param: zGlobSiz 系统全局线程数量上限
 * @param: zSiz 当前线程池初始化成功后会启动的线程数量
 * @return: 成功返回 0，失败返回负数
 */
static _i
zthread_pool_init(_i zSiz, _i zGlobSiz) {
    pthread_mutexattr_t zMutexAttr;

    /*
     * 创建线程池时，先销毁旧锁，再重新初始化之，
     * 防止 fork 之后，在子进程中重建线程池时，形成死锁
     * 锁属性设置为 PTHREAD_MUTEX_NORMAL：不允许同一线程重复取锁
     */
    pthread_mutex_destroy(&zStackHeaderLock);

    pthread_mutexattr_init(&zMutexAttr);
    pthread_mutexattr_settype(&zMutexAttr, PTHREAD_MUTEX_NORMAL);

    pthread_mutex_init(&zStackHeaderLock, &zMutexAttr);

    pthread_mutexattr_destroy(&zMutexAttr);

    /*
     * 允许同时处于空闲状态的线程数量，
     * 即常备线程数量
     */
    zThreadPollSiz = zSiz;
    zOverflowMark = zThreadPollSiz - 1;

    /*
     * 线程池栈结构空间
     */
    zMEM_ALLOC(zppPoolStack_, void *, zThreadPollSiz);

    /*
     * 主进程程调用时，
     *     zGlobSiz 置为正整数，会尝试清除已存在的旧文件，并新建信号量
     * 项目进程中调用时，
     *     zGlobSiz 置为负数或 0，自动继承主进程的 handler
     */
    if (0 < zGlobSiz) {
        sem_unlink("git_shadow");
        if (SEM_FAILED ==
                (zThreadPool_.p_threadPoolSem = sem_open("git_shadow", O_CREAT|O_RDWR, 0700, zGlobSiz))) {
            return -1;
        }
    }

    for (_i i = 0; i < zThreadPollSiz; i++) {
        if (0 != sem_trywait(zThreadPool_.p_threadPoolSem)) {
            return -2;
        } else {
            zCHECK_PTHREAD_FUNC_RETURN(
                    pthread_create(&zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL),
                    -3);
        }
    }

    return 0;
}


/*
 * 线程池容量不足时，自动扩容
 * 空闲线程过多时，会自动缩容
 * @return 成功返回 0，失败返回 -1
 */
static _i
zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam, pthread_t *zpTidOUT) {
    pthread_mutex_lock(&zStackHeaderLock);

    while (0 > zStackHeader) {
        pthread_mutex_unlock(&zStackHeaderLock);

        /* git_shadow 在系统全局范围内启动的总线程数 */
        if (0 != sem_wait(zThreadPool_.p_threadPoolSem)) {
            return -1;
        }

        pthread_create(
                & zThreadPoolTidTrash,
                NULL,
                zthread_pool_meta_func, NULL);

        pthread_mutex_lock(&zStackHeaderLock);
    }


    zppPoolStack_[zStackHeader]->func = zFunc;
    zppPoolStack_[zStackHeader]->p_param = zpParam;

    if (NULL != zpTidOUT) {
        *zpTidOUT = zppPoolStack_[zStackHeader]->selfTid;
    }

    zStackHeader--;

    /* 防止解锁后，瞬间改变的情况 */
    _i zKeepStackHeader= zStackHeader;

    pthread_mutex_unlock(&zStackHeaderLock);
    pthread_cond_signal(&(zppPoolStack_[zKeepStackHeader]->condVar));

    return 0;
}
