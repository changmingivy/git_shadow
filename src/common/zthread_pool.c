#include "zthread_pool.h"

static pthread_mutex_t zLock[2] = {PTHREAD_MUTEX_INITIALIZER};
static pthread_cond_t zCond = PTHREAD_COND_INITIALIZER;

static _i zPoolSiz;
static zThreadJobInfo *zpThreadPoll;

void * zThreadFunc(void *zpIndex);

zThreadJobInfo *
zthread_poll_init(_i zThreadNum, _i *zpJobQueue) {
	zPoolSiz = zThreadNum;
	zMem_Alloc(zpThreadPoll, zThreadJobInfo, zThreadNum);

	static _i i;
	for (i = 0; i < zThreadNum; i++) {
		pthread_create(&zpThreadPoll->Tid, NULL, zThreadFunc, &i);
		zpThreadPoll[i].JobQueue = zpJobQueue;
		zpThreadPoll[i].MarkStart= 0;
	}
	return zpThreadPoll;
}

void
zthread_poll_destroy(void) {
	for (_i i = 0; i < zPoolSiz; i++) {
		pthread_cancel(zpThreadPoll->Tid);
	}
	free(zpThreadPoll);
}

void *
zThreadFunc(void *zpIndex) {
	zCheck_Pthread_Func_Warning(pthread_detach(pthread_self()));
	_i *zpID = ((_i *)zpIndex);

zMark:;
	pthread_mutex_lock(&(zLock[0]));
	while (0 != *(zpThreadPoll[*zpID].JobQueue)) {
		pthread_cond_wait(&zCond, &(zLock[0]));
	}
	zpThreadPoll[*zpID].JobQueue = zpID;
	pthread_mutex_unlock(&(zLock[0]));

	pthread_mutex_lock(&(zLock[1]));
	while (1 != zpThreadPoll[*zpID].MarkStart) {
		pthread_cond_wait(&zCond, &(zLock[1]));
	}
	zpThreadPoll[*zpID].MarkStart = 0;
	pthread_mutex_unlock(&(zLock[1]));

	zpThreadPoll[*zpID].OpsFunc(zpThreadPoll[*zpID].p_param);
	
	goto zMark;
	return NULL;
}
