#ifndef _Z
    #include "../../zmain.c"
#endif

#define zThreadPollSiz 1024

#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    pthread_mutex_lock(&(zThreadPollMutexLock_0));\
    while (-1 == zJobQueue) {\
        pthread_cond_wait(&(zThreadPoolCond_0), &(zThreadPollMutexLock_0));\
    }\
\
    zThreadPoll[zJobQueue].OpsFunc = zFunc;\
    zThreadPoll[zJobQueue].p_param = zParam;\
    zThreadPoll[zJobQueue].MarkStart = 1;\
\
    pthread_mutex_lock(&(zThreadPollMutexLock_1));\
    zJobQueue = -1;\
    pthread_cond_signal(&(zThreadPoolCond_1));\
    pthread_mutex_unlock(&(zThreadPollMutexLock_1));\
\
    pthread_mutex_unlock(&(zThreadPollMutexLock_0));\
\
    pthread_mutex_lock(&(zThreadPollMutexLock_2));\
    pthread_mutex_unlock(&(zThreadPollMutexLock_2));\
    pthread_cond_signal(&(zThreadPoolCond_2));\
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

pthread_mutex_t zThreadPollMutexLock_0 = PTHREAD_MUTEX_INITIALIZER;
char zTmp0[128];
pthread_mutex_t zThreadPollMutexLock_1 = PTHREAD_MUTEX_INITIALIZER;
char zTmp1[128];
pthread_mutex_t zThreadPollMutexLock_2 = PTHREAD_MUTEX_INITIALIZER;
char zTmp2[128];
pthread_cond_t zThreadPoolCond_0 = PTHREAD_COND_INITIALIZER;
char zTmp3[128];
pthread_cond_t zThreadPoolCond_1 = PTHREAD_COND_INITIALIZER;
char zTmp4[128];
pthread_cond_t zThreadPoolCond_2 = PTHREAD_COND_INITIALIZER;

void *
zthread_func(void *zpIndex) {
    zCheck_Pthread_Func_Return(pthread_detach(pthread_self()), NULL);

    _i i = *((_i *)zpIndex);
zMark:;
    pthread_mutex_lock(&(zThreadPollMutexLock_1));
    while (-1 != zJobQueue) {  // -1: no other thread is ahead of me.
        pthread_cond_wait(&(zThreadPoolCond_1), &(zThreadPollMutexLock_1));
    }

    pthread_mutex_lock(&(zThreadPollMutexLock_0));
    zJobQueue = i;
    pthread_cond_signal(&(zThreadPoolCond_0));
    pthread_mutex_unlock(&(zThreadPollMutexLock_0));

    pthread_mutex_unlock(&(zThreadPollMutexLock_1));

    // 0: param is not ready, can not start; 1: param ready, can start.
    pthread_mutex_lock(&(zThreadPollMutexLock_2));
    while (1 != zThreadPoll[i].MarkStart) {
        pthread_cond_wait(&zThreadPoolCond_2, &(zThreadPollMutexLock_2));
    }

    zThreadPoll[i].MarkStart = 0;
    pthread_mutex_unlock(&(zThreadPollMutexLock_2));

    zThreadPoll[i].OpsFunc(zThreadPoll[i].p_param);

    goto zMark;
    return NULL;
}

void
zthread_poll_init(void) {
    for (_i i = 0; i < zThreadPollSiz; i++) {
        zIndex[i] = i;
        zThreadPoll[i].MarkStart= 0;
        zCheck_Pthread_Func_Return(pthread_create(&(zThreadPoll[i].Tid), NULL, zthread_func, &(zIndex[i])),);
    }
}

#undef zThreadPollSiz
