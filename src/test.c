#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>

#include <semaphore.h>  //fix

#include "zpcre2.h"

#define zCommonBufSiz 4096 
#define zHashSiz 8192
#define zMaxThreads 256


/*******************************
 * DATA STRUCT DEFINE
 ******************************/
typedef struct zObjInfo {
	char path[zCommonBufSiz];  // The directory to be monitored.
	pthread_t ControlTD;  // The thread which master the watching.

	_i RecursiveMark;  // Mark recursive monitor.
//	_i ValidState;  // Mark which node should be added or deleted, 0 for valid, -1 for invalid.

	struct zObjInfo *p_next;
}zObjInfo;


/***********************
 * FUNCTION DECLARE
 **********************/
extern char * zget_one_line_from_FILE(FILE *);
void zinotify_action(char *, _i);
void zinotify_add_watch_recursively(char *);


/***************
 * Global var
 **************/
static char *zpPathHash[zHashSiz];
static _i zInotifyFD;
static sem_t zSem;  //fix

static void *
zftw_thread(void *zpPath) {
	pthread_detach(pthread_self());
	zinotify_add_watch_recursively((char *)zpPath);
	pthread_exit(NULL);
}

void
zinotify_add_watch_recursively(char *zpPath) {
	zCheck_Negative_Thread_Exit(sem_wait(&zSem)); //fix
	_i zWD = inotify_add_watch(
				zInotifyFD, 
				zpPath,
				IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_MODIFY|IN_MOVED_TO|IN_MOVED_FROM|IN_EXCL_UNLINK
				);
	zCheck_Negative_Thread_Exit(zWD);

	zpPathHash[zWD] = zpPath;

	struct dirent *zpEntry;
	char *zpNewPath;
	pthread_t zTD;

	DIR *zpDir = opendir(zpPath);
	zCheck_Null_Thread_Exit(zpDir);

	while (NULL != (zpEntry = readdir(zpDir))) {
		if (DT_DIR == zpEntry->d_type) {
			zMem_Alloc(zpNewPath, char, 2 + strlen(zpPath) + strlen(zpEntry->d_name));

			strcpy(zpNewPath, zpPath);
			strcat(zpNewPath, "/");
			strcat(zpNewPath, zpEntry->d_name);

			pthread_create(&zTD, NULL, zftw_thread, zpNewPath);
		}
	}

	closedir(zpDir);
	zCheck_Negative_Thread_Exit(sem_post(&zSem));  //fix
}

_i
main(void) {
	char zBuf[4096];
	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zCheck_Negative_Exit(sem_init(&zSem, 0, zMaxThreads)); //fix

	pid_t zPid = fork();
	if (0== zPid) {
		zinotify_add_watch_recursively("/tmp/etc");
	} else {
		waitpid(zPid, NULL, 0);
		zCheck_Negative_Exit(sem_destroy(&zSem)); //fix
		for(;;) {
			read(zInotifyFD, zBuf, 4096);
			printf("Occur!!!  \n");
		}
	}
	exit(0);
}
