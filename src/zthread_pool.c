#ifndef _Z
	#include "zmain.c"
#endif

#define zThreadPollSiz 256

#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
		pthread_mutex_lock(&(zThreadPollMutexLock[0]));\
		while (-1 == zJobQueue) {\
			pthread_cond_wait(&(zThreadPoolCond[0]), &(zThreadPollMutexLock[0]));\
		}\
\
		zThreadPoll[zJobQueue].OpsFunc = zFunc;\
		zThreadPoll[zJobQueue].p_param = zParam;\
		zThreadPoll[zJobQueue].MarkStart = 1;\
\
		pthread_mutex_lock(&(zThreadPollMutexLock[1]));\
		zJobQueue = -1;\
		pthread_cond_signal(&(zThreadPoolCond[1]));\
		pthread_mutex_unlock(&(zThreadPollMutexLock[1]));\
\
		pthread_mutex_unlock(&(zThreadPollMutexLock[0]));\
\
		pthread_mutex_lock(&(zThreadPollMutexLock[2]));\
		pthread_mutex_unlock(&(zThreadPollMutexLock[2]));\
		pthread_cond_signal(&(zThreadPoolCond[2]));\
}while(0)

typedef struct zThreadJobInfo {
	pthread_t Tid;
	_i MarkStart;
	zThreadPoolOps OpsFunc;
	void *p_param;
}zThreadJobInfo;

zThreadJobInfo zThreadPoll[zThreadPollSiz];
_i zIndex[zThreadPollSiz];
_i zJobQueue = -1;

pthread_mutex_t zThreadPollMutexLock[3] = {PTHREAD_MUTEX_INITIALIZER};
pthread_cond_t zThreadPoolCond[3] = {PTHREAD_COND_INITIALIZER};

void *
zthread_func(void *zpIndex) {
//TEST: PASS
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);
	_i i = *((_i *)zpIndex);

zMark:;
	pthread_mutex_lock(&(zThreadPollMutexLock[1]));
	while (-1 != zJobQueue) {  // -1: no other thread is ahead of me.
		pthread_cond_wait(&zThreadPoolCond[1], &(zThreadPollMutexLock[1]));
	}

	pthread_mutex_lock(&(zThreadPollMutexLock[0]));
	zJobQueue = i;
	pthread_cond_signal(&zThreadPoolCond[0]);
	pthread_mutex_unlock(&(zThreadPollMutexLock[0]));

	pthread_mutex_unlock(&(zThreadPollMutexLock[1]));

	// 0: param is not ready, can not start; 1: param ready, can start.
	pthread_mutex_lock(&(zThreadPollMutexLock[2]));
	while (1 != zThreadPoll[i].MarkStart) {
		pthread_cond_wait(&zThreadPoolCond[2], &(zThreadPollMutexLock[2]));
	}

	zThreadPoll[i].MarkStart = 0;
	pthread_mutex_unlock(&(zThreadPollMutexLock[2]));

	zThreadPoll[i].OpsFunc(zThreadPoll[i].p_param);
	
	goto zMark;
	return NULL;
}

void
zthread_poll_init(void) {
//TEST: PASS
	for (_i i = 0; i < zThreadPollSiz; i++) {
		zIndex[i] = i;
		zThreadPoll[i].MarkStart= 0;
		zCheck_Pthread_Func_Exit(
				pthread_create(&(zThreadPoll[i].Tid), NULL, zthread_func, &(zIndex[i]))
				);
	}
}

//void
//zthread_poll_destroy(void) {
//// DO NOT USE!!!
//// Will kill new created threads!
//
//	for (_i i = 0; i < zThreadPollSiz; i++) {
//		pthread_cancel(zThreadPoll[i].Tid);
//	}
//}
//
#undef zThreadPollSiz
