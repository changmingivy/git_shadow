#include "zthread_pool.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static _i zthread_pool_init(_i zSiz, _i zGlobSiz);
static _i zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam);

/* 线程池栈结构 */
static _i zThreadPollSiz;
static _i zOverflowMark;

static zThreadTask__ **zppPoolStack_;

static _i zStackHeader;
static pthread_mutex_t zStackHeaderLock;

static pthread_t zThreadPoolTidTrash;

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

/******************************
 * ====  对外公开的接口  ==== *
 ******************************/
struct zThreadPool__ zThreadPool_ = {
    .init = zthread_pool_init,
    .add = zadd_to_thread_pool,
    .p_tid = & zThreadPoolTidTrash,
};


/*
 * 线程退出时，释放自身占用的资源
 */
static void
zthread_canceled_cleanup(void *zp_) {
    zThreadTask__ *zpSelfTask = (zThreadTask__ *) zp_;

    /* 释放占用的系统全局信号量 */
    sem_post(zThreadPool_.p_threadPoolSem);

    /* 清理内部分配的静态资源 */
    pthread_cond_destroy(&(zpSelfTask->condVar));
    free(zpSelfTask);
}


static void *
zthread_pool_meta_func(void *zp_ __attribute__ ((__unused__))) {
    /* detach 自身 */
    pthread_detach(pthread_self());

    /*
     * 线程池中的线程程默认不接受 cancel
     * 若有需求，在传入的工作函数中更改属性即可
     */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);

    /* 线程任务桩 */
    zThreadTask__ *zpSelfTask;
    zMEM_ALLOC(zpSelfTask, zThreadTask__, 1);

    zpSelfTask->func = NULL;

    zCHECK_PTHREAD_FUNC_EXIT(
            pthread_cond_init(&(zpSelfTask->condVar), NULL)
            );

    /* 线程退出时的资源清理动作 */
    pthread_cleanup_push(zthread_canceled_cleanup, zpSelfTask);

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
    }

    /*
     * 只有空闲线程数超过线程池栈深度时，
     * 才会运行至此
     */
    pthread_mutex_unlock(&zStackHeaderLock);

    /*
     * 参数置为非 0 值，
     * 则运行至此处时，
     * 清理函数一定会被执行
     * 不必再调用 pthread_exit();
     */
    pthread_cleanup_pop(1);

    return NULL;
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
     * 必须动态初始化为 -1
     * 否则子进程继承父进程的栈索引，将带来异常
     */
    zStackHeader = -1;

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
        sem_unlink("git_shadow_thread");
        if (SEM_FAILED ==
                (zThreadPool_.p_threadPoolSem = sem_open("git_shadow_thread", O_CREAT|O_RDWR, 0700, zGlobSiz))) {
            zPRINT_ERR_EASY_SYS();
            exit(1);
        }
    }

    for (_i i = 0; i < zThreadPollSiz; i++) {
        if (0 != sem_trywait(zThreadPool_.p_threadPoolSem)) {
            zPRINT_ERR_EASY_SYS();
            exit(1);
        } else {
            zCHECK_PTHREAD_FUNC_EXIT(
                    pthread_create(& zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL)
                    );
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
zadd_to_thread_pool(void * (* zFunc) (void *), void *zpParam) {
    pthread_mutex_lock(&zStackHeaderLock);

    while (0 > zStackHeader) {
        pthread_mutex_unlock(&zStackHeaderLock);

        /* 不能超过 git_shadow 在系统全局范围内启动的总线程数 */
        sem_wait(zThreadPool_.p_threadPoolSem);

        pthread_create(
                & zThreadPoolTidTrash,
                NULL,
                zthread_pool_meta_func, NULL);

        pthread_mutex_lock(&zStackHeaderLock);
    }

    zppPoolStack_[zStackHeader]->func = zFunc;
    zppPoolStack_[zStackHeader]->p_param = zpParam;

    /* 防止解锁后，瞬间改变的情况 */
    _i zKeepStackHeader= zStackHeader;

    zStackHeader--;

    pthread_mutex_unlock(&zStackHeaderLock);
    pthread_cond_signal(&(zppPoolStack_[zKeepStackHeader]->condVar));

    return 0;
}
