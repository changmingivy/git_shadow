#define _XOPEN_SOURCE 700

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/wait.h>

#include <dirent.h>

#include "zutils.h"

#define zCommonBufSiz 4096 


_i
main(void) {
	char zBuf[zCommonBufSiz] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;
	pid_t zPid;

	_i zInotifyFD[2];
	zInotifyFD[0] = inotify_init();
	zInotifyFD[1] = inotify_init();

	zPid = fork();
	zCheck_Negative_Thread_Exit(zPid);

	if (0 == zPid) {
		_i zWD =  inotify_add_watch(zInotifyFD[0], "/tmp/xxx", IN_ALL_EVENTS);
		zCheck_Negative_Thread_Exit(zWD);
		printf("Child: %d\n", zWD);
	} else {
		_i zWD =  inotify_add_watch(zInotifyFD[1], "/tmp/yyy", IN_ALL_EVENTS);
		zCheck_Negative_Thread_Exit(zWD);
		printf("Father: %d\n", zWD);
		waitpid(zPid, NULL, 0);

		for(_i i = 0;;) {
			zLen = read(zInotifyFD[0], zBuf, sizeof(zBuf));
			zCheck_Negative_Thread_Exit(zLen);
			printf("OH...%d\n", ++i);
		}
	}
}
