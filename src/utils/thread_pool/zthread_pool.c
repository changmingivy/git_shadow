#ifndef _Z
    #include "../../zmain.c"
#endif
 
#define zThreadPollSiz 128

typedef void * (* zThreadPoolOps) (void *);  // 线程池回调函数

struct zParamInfo {
    zThreadPoolOps func;
    void *p_param;
    struct zParamInfo *p_next;
};
typedef struct zParamInfo zParamInfo;

struct zThreadTaskInfo {
    pthread_mutex_t MutexLock;
    pthread_cond_t CondVar;

    _i Index;
    zParamInfo *p_ParamIf;
//  char _[48];  // 应对伪共享问题??? pthread 之类数据结构本身很复杂，无法有效估量其本身所占大，故此处效用难以判定，暂不启用
};
typedef struct zThreadTaskInfo zThreadTaskInfo;

zThreadTaskInfo zTaskQueueIf[zThreadPollSiz] = {{.MutexLock = PTHREAD_MUTEX_INITIALIZER, .CondVar = PTHREAD_COND_INITIALIZER}};  // 线程任务队列
zParamInfo zParamQueueIf[zThreadPollSiz];  // 线程入参队列

_ui zThreadPoolScheduler;
pthread_mutex_t zParamLock = PTHREAD_MUTEX_INITIALIZER;
pthread_t ____zTidTrash____;

void *
zthread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    zThreadTaskInfo *zpThreadTaskIf = (zThreadTaskInfo *) zpIf;

    zParamInfo zParamIf, *zpParamIf;
zMark:
    pthread_mutex_lock(&(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));
    while (NULL == zTaskQueueIf[zpThreadTaskIf->Index].p_ParamIf) {
        pthread_cond_wait(&(zTaskQueueIf[zpThreadTaskIf->Index].CondVar), &(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));
    }

    zpParamIf = zTaskQueueIf[zpThreadTaskIf->Index].p_ParamIf;
    zParamIf.func = zpParamIf->func;
    zParamIf.p_param = zpParamIf->p_param;
    zpParamIf->func = NULL;  // 标记为NULL，表示该入参已用完，可以复用

    zTaskQueueIf[zpThreadTaskIf->Index].p_ParamIf = zTaskQueueIf[zpThreadTaskIf->Index].p_ParamIf->p_next;

    zParamIf.func(zParamIf.p_param);
    pthread_mutex_unlock(&(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));  // 执行完任务之后再放锁，防止提前放锁导致“忙则越忙，闲则越闲的问题”

    goto zMark;
    return (void *) -1;
}

/* 必须使用此外壳函数，否则新线程无法detach自身，将造成资源泻漏 */
void *
ztmp_thread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    zParamInfo zParamIf, *zpParamIf;

    zpParamIf = (zParamInfo *) zpIf;
    zParamIf.func = zpParamIf->func;
    zParamIf.p_param = zpParamIf->p_param;
    zpParamIf->func = NULL;  // 标记为NULL，表示该入参已用完，可以复用

    zParamIf.func(zParamIf.p_param);

    return NULL;
}

void
zthread_poll_init(void) {
    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zTaskQueueIf[zCnter].Index = zCnter;
        zCheck_Pthread_Func_Exit( pthread_create(&____zTidTrash____, NULL, zthread_func, &(zTaskQueueIf[zCnter])) );
    }
}

/* 
 * 优先使用线程池，若线程池满，则新建临时线程执行任务
 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    _i ____zKeepId____;\
\
    pthread_mutex_lock(&zParamLock);\
    while (NULL != zParamQueueIf[zThreadPoolScheduler % zThreadPollSiz].func) {\
        zThreadPoolScheduler++;\
    }\
    ____zKeepId____ = zThreadPoolScheduler % zThreadPollSiz;\
    zParamQueueIf[____zKeepId____].func = zFunc;\
    pthread_mutex_unlock(&zParamLock);\
    zParamQueueIf[____zKeepId____].p_param = zParam;\
    zParamQueueIf[____zKeepId____].p_next = NULL;\
\
    if (0 > pthread_mutex_trylock(&(zTaskQueueIf[____zKeepId____].MutexLock))) {\
        pthread_create(&____zTidTrash____, NULL, ztmp_thread_func, &(zParamQueueIf[____zKeepId____]));\
    } else {\
        if (NULL == zTaskQueueIf[____zKeepId____].p_ParamIf) {\
            zTaskQueueIf[____zKeepId____].p_ParamIf = &(zParamQueueIf[____zKeepId____]);\
        } else {\
            zParamInfo *zpTmpIf = zTaskQueueIf[____zKeepId____].p_ParamIf;\
            while (NULL != zpTmpIf->p_next) { zpTmpIf = zpTmpIf->p_next; }\
            zpTmpIf->p_next = &(zParamQueueIf[____zKeepId____]);\
        }\
        pthread_mutex_unlock(&(zTaskQueueIf[____zKeepId____].MutexLock));\
        pthread_cond_signal(&(zTaskQueueIf[____zKeepId____].CondVar));\
    }\
} while(0)
