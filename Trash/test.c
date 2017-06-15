#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/mman.h>  //fix

#include <pthread.h>
#include <semaphore.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>

#include "zpcre2.h"

#define zCommonBufSiz 4096 
#define zHashSiz 8192
#define zMaxThreadsNum 64


/*******************************
 * DATA STRUCT DEFINE
 ******************************/
typedef struct zObjInfo {
	struct zObjInfo *p_next;
	_i RecursiveMark;  // Mark recursive monitor.
//	_i ValidState;  // Mark which node should be added or deleted, 0 for valid, -1 for invalid.
//	pthread_t ControlTD;  // The thread which master the watching.
	char path[];  // The directory to be monitored.
}zObjInfo;

/***********************
 * FUNCTION DECLARE
 **********************/
extern char * zget_one_line_from_FILE(FILE *);
void zinotify_add_watch_recursively(char *);


/***************
 * Global var
 **************/
static sem_t zSem;
static _i zInotifyFD;
static char *zpPathHash[zHashSiz];
pthread_t zTid;

static void *
zthread_add_sub_watch(void *zpPath) {
//TEST: PASS
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);
	zinotify_add_watch_recursively((char *)zpPath);
	return NULL;
}

void
zinotify_add_watch_recursively(char *zpPath) {
//TEST: PASS
	zCheck_Negative_Exit(sem_wait(&zSem));
	_i zWid = inotify_add_watch(
				zInotifyFD, 
				zpPath,
				IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_MOVE_SELF|IN_MODIFY|IN_MOVED_TO|IN_MOVED_FROM|IN_EXCL_UNLINK  //fix
				);
	zCheck_Negative_Exit(zWid);

	zpPathHash[zWid] = zpPath;

	struct dirent *zpEntry;
	char *zpNewPath;

	DIR *zpDir = opendir(zpPath);
	zCheck_Null_Exit(zpDir);

	size_t zLen = strlen(zpPath);

	while (NULL != (zpEntry = readdir(zpDir))) {
		if (DT_DIR == zpEntry->d_type 
				&& strcmp(".", zpEntry->d_name) 
				&& strcmp("..", zpEntry->d_name) 
				&& strcmp(".git", zpEntry->d_name)) {
			zMem_Alloc(zpNewPath, char, 2 + zLen + strlen(zpEntry->d_name));

			strcpy(zpNewPath, zpPath);
			strcat(zpNewPath, "/");
			strcat(zpNewPath, zpEntry->d_name);

			zCheck_Pthread_Func_Exit(
					pthread_create(&zTid, NULL, zthread_add_sub_watch, zpNewPath)
					);
		}
	}

	closedir(zpDir);
	zCheck_Negative_Exit(sem_post(&zSem));
}

void *
zthread_wait_event(void *zpIf) {
	char zBuf[zCommonBufSiz]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;
	char *zpNewPath;

	for (;;) {
		zLen = read(zInotifyFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; 
				zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

	  		// If a new subdir is created or moved in, add it to the watch list.
			if (zpEv->mask & IN_ISDIR) {
				if(zpEv->mask & (IN_CREATE|IN_MOVED_TO)) {
					zMem_Alloc(zpNewPath, char, 2 + strlen(zpPathHash[zpEv->wd]) + zpEv->len);
	
					strcpy(zpNewPath, zpPathHash[zpEv->wd]);
					strcat(zpNewPath, "/");
					strcat(zpNewPath, zpEv->name);
	
					zinotify_add_watch_recursively(zpNewPath);
				}
				else if (zpEv->mask & (IN_MODIFY|IN_DELETE|IN_MOVED_FROM)) { }
				else if (zpEv->mask & (IN_IGNORED|IN_DELETE_SELF|IN_MOVE_SELF)) {
					free(zpPathHash[zpEv->wd]);  // BUG: can't get this event.
					continue;
				}
				else {
					fprintf(stderr, "\033[31;01mWARNING: Unknown inotify event occur!!!\033[00m\n");
				}
				//Do git
			}
		}
	}
}

_i
main(void) {
	//char zBuf[4096];

	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zCheck_Negative_Exit(sem_init(&zSem, 0, zMaxThreadsNum));

	zinotify_add_watch_recursively("/tmp/etc/etc");

	sleep(1);
	for (_i i = 0; i < zHashSiz; i++) {
		if (NULL == zpPathHash[i]) {
			continue;
		}
		printf("%d: %s\n", i, zpPathHash[i]);
	}
	pthread_create(&zTid, NULL, zthread_wait_event, NULL);
	sleep(1200);
}
