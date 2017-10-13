#ifndef _Z
    #include "../../zmain.c"
#endif
 
#define zThreadPollSiz 128

typedef void * (* zThreadPoolOps) (void *);  // 线程池回调函数

struct zThreadPoolInfo {
    pthread_cond_t CondVar;

    zThreadPoolOps func;
    void *p_param;
};
typedef struct zThreadPoolInfo zThreadPoolInfo;

/* 线程池栈结构 */
zThreadPoolInfo *zpPoolStackIf[zThreadPollSiz];

_i ____zStackHeader = -1;
pthread_mutex_t zStackHeaderLock = PTHREAD_MUTEX_INITIALIZER;
pthread_t ____zThreadPoolTidTrash;

void *
zthread_func(void *zpIf) {
    pthread_detach( pthread_self() );  // 即使该步出错，也无处理错误，故不必检查返回值

    zThreadPoolInfo *zpSelfTask;
    zMem_C_Alloc(zpSelfTask, zThreadPoolInfo, 1);  // 分配已清零的空间
    pthread_cond_init(&(zpSelfTask->CondVar), NULL);

zMark:
    pthread_mutex_lock(&zStackHeaderLock);
    zpPoolStackIf[++____zStackHeader] = zpSelfTask;
    while (NULL == zpSelfTask->func) {
        pthread_cond_wait( &(zpSelfTask->CondVar), &zStackHeaderLock );
    }
    pthread_mutex_unlock(&zStackHeaderLock);

    zpSelfTask->func(zpSelfTask->p_param);
    sem_post(&zGlobSemaphore);  // 任务完成，释放信号量

    zpSelfTask->func = NULL;
    goto zMark;

    return (void *) -1;
}

/* 必须使用此外壳函数，否则新线程无法detach自身，将造成资源泻漏 */
void *
ztmp_thread_func(void *zpIf) {
    pthread_detach( pthread_self() );  // 即使该步出错，也无处理错误，故不必检查返回值

    zThreadPoolInfo *zpTaskIf = (zThreadPoolInfo *) zpIf;

    zpTaskIf->func(zpTaskIf->p_param);  // 执行任务
    sem_post(&zGlobSemaphore);  // 任务完成，释放信号量

    free(zpTaskIf);
    return NULL;
}

void
zthread_poll_init(void) {
    /* 全局并发线程总数限制 */
    sem_init(&zGlobSemaphore, 0, zGlobThreadLimit);

    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zCheck_Pthread_Func_Exit( pthread_create(&____zThreadPoolTidTrash, NULL, zthread_func, NULL) );
    }
}

/* 
 * 优先使用线程池，若线程池满，则新建临时线程执行任务
 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    sem_wait(&zGlobSemaphore);  /* 防止操作系统负载过高 */\
    pthread_mutex_lock(&zStackHeaderLock);\
    if (0 > ____zStackHeader) {\
        pthread_mutex_unlock(&zStackHeaderLock);\
        zThreadPoolInfo *____zpTmpJobIf;\
        zMem_Alloc(____zpTmpJobIf, zThreadPoolInfo, 1);\
        ____zpTmpJobIf->func = zFunc;\
        ____zpTmpJobIf->p_param = zParam;\
        pthread_create(&____zThreadPoolTidTrash, NULL, ztmp_thread_func, ____zpTmpJobIf);\
    } else {\
        _i ____zKeepStackHeader= ____zStackHeader;\
        zpPoolStackIf[____zStackHeader]->func = zFunc;\
        zpPoolStackIf[____zStackHeader]->p_param = zParam;\
        ____zStackHeader--;\
        pthread_mutex_unlock(&zStackHeaderLock);\
        pthread_cond_signal(&(zpPoolStackIf[____zKeepStackHeader]->CondVar));\
    }\
} while(0)
