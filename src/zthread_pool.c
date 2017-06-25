#ifndef _Z
	#include <pthread.h>
	#include "zutils.h"
#endif

#define zThreadPollSiz 64

#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
		pthread_mutex_lock(&(zLock[0]));\
		while (-1 == zJobQueue) {\
			pthread_cond_wait(&(zCond[0]), &(zLock[0]));\
		}\
\
		zThreadPoll[zJobQueue].OpsFunc = zFunc;\
		zThreadPoll[zJobQueue].p_param = zParam;\
		zThreadPoll[zJobQueue].MarkStart = 1;\
\
		pthread_mutex_lock(&(zLock[1]));\
		zJobQueue = -1;\
		pthread_cond_signal(&(zCond[1]));\
		pthread_mutex_unlock(&(zLock[1]));\
\
		pthread_mutex_unlock(&(zLock[0]));\
\
		pthread_mutex_lock(&(zLock[2]));\
		pthread_mutex_unlock(&(zLock[2]));\
		pthread_cond_signal(&(zCond[2]));\
}while(0)

typedef void * (* zThreadOps) (void *);

typedef struct zThreadJobInfo {
	pthread_t Tid;
	_i MarkStart;
	zThreadOps OpsFunc;
	void *p_param;
}zThreadJobInfo;

static zThreadJobInfo zThreadPoll[zThreadPollSiz];
static _i zIndex[zThreadPollSiz];
static _i zJobQueue = -1;

static pthread_mutex_t zLock[3] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t zCond[3] = {PTHREAD_COND_INITIALIZER};

static void *
zthread_func(void *zpIndex) {
//TEST: PASS
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);
	_i i = *((_i *)zpIndex);

zMark:;
	pthread_mutex_lock(&(zLock[1]));
	while (-1 != zJobQueue) {  // -1: no other thread is ahead of me.
		pthread_cond_wait(&zCond[1], &(zLock[1]));
	}

	pthread_mutex_lock(&(zLock[0]));
	zJobQueue = i;
	pthread_cond_signal(&zCond[0]);
	pthread_mutex_unlock(&(zLock[0]));

	pthread_mutex_unlock(&(zLock[1]));

	// 0: param is not ready, can not start; 1: param ready, can start.
	pthread_mutex_lock(&(zLock[2]));
	while (1 != zThreadPoll[i].MarkStart) {
		pthread_cond_wait(&zCond[2], &(zLock[2]));
	}

	zThreadPoll[i].MarkStart = 0;
	pthread_mutex_unlock(&(zLock[2]));

	zThreadPoll[i].OpsFunc(zThreadPoll[i].p_param);
	
	goto zMark;
	return NULL;
}

static void
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

//static void
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
