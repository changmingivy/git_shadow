#ifndef _Z
    #include "../../zmain.c"
#endif

#define zThreadPollSiz 64

/* 优先使用线程池，若线程池满，则新建临时线程执行任务 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    pthread_mutex_lock(&(zThreadPollMutexLock[2]));\
    if (-1 == zJobQueue) {\
        zThreadJobInfo zTmpJosIf = {.OpsFunc = zFunc, .p_param = zParam};\
        zCheck_Pthread_Func_Exit(pthread_create(&(zTmpJosIf.Tid), NULL, ztmp_job_func, &zTmpJosIf));\
        pthread_mutex_unlock(&(zThreadPollMutexLock[2]));\
    } else {\
        zThreadPoll[zJobQueue].OpsFunc = zFunc;\
        zThreadPoll[zJobQueue].p_param = zParam;\
        zThreadPoll[zJobQueue].MarkStart = 1;\
\
        pthread_mutex_lock(&(zThreadPollMutexLock[0]));\
        zJobQueue = -1;\
        pthread_cond_signal(&(zThreadPoolCond[0]));\
        pthread_mutex_unlock(&(zThreadPollMutexLock[0]));\
\
        pthread_mutex_unlock(&(zThreadPollMutexLock[2]));\
\
        pthread_mutex_lock(&(zThreadPollMutexLock[1]));\
        pthread_mutex_unlock(&(zThreadPollMutexLock[1]));\
        pthread_cond_signal(&(zThreadPoolCond[1]));\
    }\
} while(0)

typedef struct zThreadJobInfo {
    pthread_t Tid;
    _i MarkStart;
    zThreadPoolOps OpsFunc;
    void *p_param;
} zThreadJobInfo;

zThreadJobInfo zThreadPoll[zThreadPollSiz];
_i zIndex[zThreadPollSiz];
_i zJobQueue = -1;

pthread_mutex_t zThreadPollMutexLock[3] = {PTHREAD_MUTEX_INITIALIZER};
pthread_cond_t zThreadPoolCond[2] = {PTHREAD_COND_INITIALIZER};

void *
zthread_func(void *zpIndex) {
    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    _i i = *((_i *) zpIndex);
zMark:;
    pthread_mutex_lock(&(zThreadPollMutexLock[0]));
    while (-1 != zJobQueue) {  // -1: no other thread is ahead of me.
        pthread_cond_wait(&(zThreadPoolCond[0]), &(zThreadPollMutexLock[0]));
    }

    pthread_mutex_lock(&(zThreadPollMutexLock[2]));
    zJobQueue = i;
    pthread_mutex_unlock(&(zThreadPollMutexLock[2]));

    pthread_mutex_unlock(&(zThreadPollMutexLock[0]));

    // 0: param is not ready, can not start; 1: param ready, can start.
    pthread_mutex_lock(&(zThreadPollMutexLock[1]));
    while (1 != zThreadPoll[i].MarkStart) {
        pthread_cond_wait(&zThreadPoolCond[1], &(zThreadPollMutexLock[1]));
    }

    zThreadPoll[i].MarkStart = 0;
    pthread_mutex_unlock(&(zThreadPollMutexLock[1]));

    zThreadPoll[i].OpsFunc(zThreadPoll[i].p_param);

    goto zMark;
    return NULL;
}

void *
ztmp_job_func(void *zpIf) {
    zThreadJobInfo *zpTmpJobIf = (zThreadJobInfo *) zpIf;
    zpTmpJobIf->OpsFunc(zpTmpJobIf->p_param);

    pthread_detach(pthread_self());  // 即使该步出错，也无处理错误，故不必检查返回值
    return NULL;
}

void
zthread_poll_init(void) {
    for (_i i = 0; i < zThreadPollSiz; i++) {
        zIndex[i] = i;
        zThreadPoll[i].MarkStart= 0;
        zCheck_Pthread_Func_Exit(pthread_create(&(zThreadPoll[i].Tid), NULL, zthread_func, &(zIndex[i])));
    }
}

#undef zThreadPollSiz
