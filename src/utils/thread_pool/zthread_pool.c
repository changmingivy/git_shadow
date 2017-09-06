 #ifndef _Z
     #include "../../zmain.c"
 #endif
 
#define zThreadPollSiz 128

typedef void * (* zThreadPoolOps) (void *);  // 线程池回调函数

struct zOpsObjInfo {
    zThreadPoolOps func;
    void *p_param;
    struct zOpsObjInfo *p_next;
};
typedef struct zOpsObjInfo zOpsObjInfo;

struct zThreadTaskInfo {
    _i Index;
    pthread_mutex_t MutexLock;
    pthread_cond_t CondVar;
    zOpsObjInfo *p_OpsObjIf;
};
typedef struct zThreadTaskInfo zThreadTaskInfo;

zThreadTaskInfo zTaskQueueIf[zThreadPollSiz];
_ui zThreadPoolScheduler;
pthread_t ____zTidTrash____;

void *
zthread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    zThreadTaskInfo *zpThreadTaskIf = (zThreadTaskInfo *) zpIf;

    zOpsObjInfo *zpOpsObjIf;
zMark:
    pthread_mutex_lock(&(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));
    while (NULL == zTaskQueueIf[zpThreadTaskIf->Index].p_OpsObjIf) {
        pthread_cond_wait(&(zTaskQueueIf[zpThreadTaskIf->Index].CondVar), &(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));
    }
    zpOpsObjIf = zTaskQueueIf[zpThreadTaskIf->Index].p_OpsObjIf;
    zTaskQueueIf[zpThreadTaskIf->Index].p_OpsObjIf = zTaskQueueIf[zpThreadTaskIf->Index].p_OpsObjIf->p_next;
    pthread_mutex_unlock(&(zTaskQueueIf[zpThreadTaskIf->Index].MutexLock));

    zpOpsObjIf->func(zpOpsObjIf->p_param);

    free(zpOpsObjIf);  // 任务完成，释放静态参数内存
    goto zMark;
    return (void *) -1;
}

/* 必须使用此外壳函数，否则新线程无法detach自身，将造成资源泻漏 */
void *
ztmp_thread_func(void *zpIf) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    zOpsObjInfo *zpOpsObjIf = (zOpsObjInfo *) zpIf;

    zpOpsObjIf->func(zpOpsObjIf->p_param);

    free(zpOpsObjIf);  // 任务完成，释放静态参数内存
    return NULL;
}

void
zthread_poll_init(void) {
    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zTaskQueueIf[zCnter].Index = zCnter;
        pthread_mutex_init(&(zTaskQueueIf[zCnter].MutexLock), NULL);
        pthread_cond_init(&(zTaskQueueIf[zCnter].CondVar), NULL);
        zCheck_Pthread_Func_Exit( pthread_create(&____zTidTrash____, NULL, zthread_func, &(zTaskQueueIf[zCnter])) );
    }
}

/* 
 * 优先使用线程池，若线程池满，则新建临时线程执行任务
 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    _i ____zKeepQueueId____, ____zKeepId____;\
    ____zKeepQueueId____ = zThreadPoolScheduler++;\
    ____zKeepId____ = ____zKeepQueueId____ % zThreadPollSiz;\
\
    if (0 > pthread_mutex_trylock(&(zTaskQueueIf[____zKeepId____].MutexLock))) {\
        zOpsObjInfo *zpOpsObjIf;\
        zMem_Alloc(zpOpsObjIf, zOpsObjInfo, 1);\
        zpOpsObjIf->func = zFunc;\
        zpOpsObjIf->p_param = zParam;\
        pthread_create(&____zTidTrash____, NULL, ztmp_thread_func, zpOpsObjIf);\
    } else {\
        if (NULL == zTaskQueueIf[____zKeepId____].p_OpsObjIf) {\
            zMem_Alloc(zTaskQueueIf[____zKeepId____].p_OpsObjIf, zOpsObjInfo, 1);\
            zTaskQueueIf[____zKeepId____].p_OpsObjIf->func = zFunc;\
            zTaskQueueIf[____zKeepId____].p_OpsObjIf->p_param = zParam;\
            zTaskQueueIf[____zKeepId____].p_OpsObjIf->p_next = NULL;\
        } else {\
            zOpsObjInfo *zpTmpIf;\
            zpTmpIf = zTaskQueueIf[____zKeepId____].p_OpsObjIf;\
            while (NULL != zpTmpIf->p_next) { zpTmpIf = zpTmpIf->p_next; }\
            zMem_Alloc(zpTmpIf->p_next, zOpsObjInfo, 1);\
            zpTmpIf->p_next->func = zFunc;\
            zpTmpIf->p_next->p_param = zParam;\
            zpTmpIf->p_next->p_next = NULL;\
        }\
        pthread_mutex_unlock(&(zTaskQueueIf[____zKeepId____].MutexLock));\
        pthread_cond_signal(&(zTaskQueueIf[____zKeepId____].CondVar));\
    }\
} while(0)
