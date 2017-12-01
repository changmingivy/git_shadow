#include "zThreadPool.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* 允许同时处于空闲状态的线程数量，即常备线程数量 */
#define zThreadPollSiz 385

#define zThreadPollSizMark (zThreadPollSiz - 1)

static void
zthread_poll_init(void);

static void
zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam);

/******************************
 * ====  对外公开的接口  ==== *
 ******************************/
struct zThreadPool__ zThreadPool_ = {
    .init = zthread_poll_init,
    .add = zadd_to_thread_pool
};

/* 线程池栈结构 */
static zThreadTask__ *zpPoolStack_[zThreadPollSiz];

static _i zStackHeader = -1;
static pthread_mutex_t zStackHeaderLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t zThreadPoolTidTrash;

static void
zthread_canceled_cleanup(void *zp_) {
    zThreadTask__ *zpSelfTask = (zThreadTask__ *) zp_;
    pthread_cond_destroy(&(zpSelfTask->condVar));
    free(zpSelfTask);
}

static void *
zthread_pool_meta_func(void *zp_ __attribute__ ((__unused__))) {
    zThreadTask__ *zpSelfTask;
    zMem_C_Alloc(zpSelfTask, zThreadTask__, 1);
    zpSelfTask->func = NULL;

    zCheck_Pthread_Func_Exit( pthread_cond_init(&(zpSelfTask->condVar), NULL) );

    pthread_detach( pthread_self() );

    /* 线程可被cancel，且cancel属性设置为立即退出 */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

zMark:
    pthread_mutex_lock(&zStackHeaderLock);

    if (zStackHeader < zThreadPollSizMark) {
        zpPoolStack_[++zStackHeader] = zpSelfTask;
        while (NULL == zpSelfTask->func) {
            /* 等待任务到达 */
            pthread_cond_wait( &(zpSelfTask->condVar), &zStackHeaderLock );
        }
        pthread_mutex_unlock(&zStackHeaderLock);

        /* 注册因收到 cancel 指令而退出时的资源清理动作 */
        pthread_cleanup_push(zthread_canceled_cleanup, zpSelfTask);

        zpSelfTask->func(zpSelfTask->p_param);

        pthread_cleanup_pop(0);

        zpSelfTask->func = NULL;
        goto zMark;
    } else {  /* 太多空闲线程时，回收资源 */
        pthread_mutex_unlock(&zStackHeaderLock);
        pthread_cond_destroy(&(zpSelfTask->condVar));

        free(zpSelfTask);
        return (void *) -1;
    }
}

static void
zthread_poll_init(void) {
    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zCheck_Pthread_Func_Exit( pthread_create(&zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL) );
    }
}

/*
 * !!!!  注：出于性能考虑，此模型尚没有考虑线程总量超过操作系统限制的情况  !!!!
 *
 * 可考虑加信号量控制系统全局总线程数
 *
 * 线程池容量不足时，自动扩容
 * 空闲线程过多时，会自动缩容
 */
static void
zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam) {
    pthread_mutex_lock(&zStackHeaderLock);
    while (0 > zStackHeader) {
        pthread_mutex_unlock(&zStackHeaderLock);
        pthread_create(&zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL);
        pthread_mutex_lock(&zStackHeaderLock);
    }
    _i zKeepStackHeader= zStackHeader;
    zpPoolStack_[zStackHeader]->func = zFunc;
    zpPoolStack_[zStackHeader]->p_param = zpParam;
    zStackHeader--;
    pthread_mutex_unlock(&zStackHeaderLock);
    pthread_cond_signal(&(zpPoolStack_[zKeepStackHeader]->condVar));
}
