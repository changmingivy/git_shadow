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

_i zStackHeader = -1;
pthread_mutex_t zStackHeaderLock = PTHREAD_MUTEX_INITIALIZER;
pthread_t ____zTidTrash____;

void *
zthread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    static zThreadPoolInfo zSelfTask = {.CondVar = PTHREAD_COND_INITIALIZER};

zMark:
    pthread_mutex_lock(&zStackHeaderLock);
    zpPoolStackIf[++zStackHeader] = &zSelfTask;
    while (NULL == zSelfTask.func) {
        pthread_cond_wait( &(zSelfTask.CondVar), &(zStackHeaderLock) );
    }
    pthread_mutex_unlock(&zStackHeaderLock);

    zSelfTask.func(zSelfTask.p_param);
    zSelfTask.func = NULL;

    goto zMark;
    return (void *) -1;
}

/* 必须使用此外壳函数，否则新线程无法detach自身，将造成资源泻漏 */
void *
ztmp_thread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    zThreadPoolInfo *zpTaskIf = (zThreadPoolInfo *) zpIf;

    zpTaskIf->func(zpTaskIf->p_param);  // 执行任务

    free(zpTaskIf);
    return NULL;
}

void
zthread_poll_init(void) {
    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zCheck_Pthread_Func_Exit( pthread_create(&____zTidTrash____, NULL, zthread_func, NULL) );
    }
}

/* 
 * 优先使用线程池，若线程池满，则新建临时线程执行任务
 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    pthread_mutex_lock(&zStackHeaderLock);\
    if (0 > zStackHeader) {\
        pthread_mutex_unlock(&zStackHeaderLock);\
        zThreadPoolInfo *zpTmpIf;\
        zMem_Alloc(zpTmpIf, zThreadPoolInfo, 1);\
        zpTmpIf->func = zFunc;\
        zpTmpIf->p_param = zParam;\
        pthread_create(&____zTidTrash____, NULL, ztmp_thread_func, zpTmpIf);\
    } else {\
        zpPoolStackIf[zStackHeader]->func = zFunc;\
        zpPoolStackIf[zStackHeader]->p_param = zParam;\
        zStackHeader--;\
        pthread_cond_signal(&(zpPoolStackIf[zStackHeader + 1]->CondVar));\
        pthread_mutex_unlock(&zStackHeaderLock);\
    }\
} while(0)
