#include <pthread.h>
#include <stdlib.h>
#include "zutils.h"

typedef void * (* zThreadOps) (void *);

typedef struct zThreadJobInfo {
	pthread_t Tid;
	_i MarkStart;
	_i *JobQueue;
	zThreadOps OpsFunc;
	void *p_param;
}zThreadJobInfo;
