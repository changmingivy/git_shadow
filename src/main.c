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

#include "zpcre2.h"

#define zCommonBufSiz 4096 
#define zHashSiz 8192


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


/*********************
 * Function define
 ********************/
zObjInfo *
zread_conf_file(const char *zpConfPath) {
//TEST: PASS
	zObjInfo *zpObjIf[3] = {NULL};

	zPCREInitInfo *zpInitIf[3] = {NULL};
	zPCRERetInfo *zpRetIf[3] = {NULL};

	_i zCnt = 0;
	char *zpRes = NULL;
	FILE *zpFile = fopen(zpConfPath, "r");

	zpInitIf[0] = zpcre_init("\\s*\\d\\s*/[/\\w]+");
	zpInitIf[1] = zpcre_init("\\d(?=\\s+)");
	zpInitIf[2] = zpcre_init("[/\\w]+(?=\\s*$)");

	while (NULL != (zpRes = zget_one_line_from_FILE(zpFile))) {
		zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
		if (0 == zpRetIf[0]->cnt) {
			zpcre_free_tmpsource(zpRetIf[0]);
			continue;
		} else {
			zMem_Alloc(zpObjIf[0], zObjInfo, 1);
			zpObjIf[0]->p_next = NULL;
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];

			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			zpcre_free_tmpsource(zpRetIf[1]);

			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);
			zpcre_free_tmpsource(zpRetIf[2]);

			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[0]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[2]);

	fclose(zpFile);
	return zpObjIf[2];
}

static void *
zftw_thread(void *zpPath) {
	pthread_detach(pthread_self());
	zinotify_add_watch_recursively((char *)zpPath);
	pthread_exit(NULL);
}

void
zinotify_add_watch_recursively(char *zpPath) {
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
}

void
zinotify_action(char *zpPath, _i zRecursiveMark) {
	pid_t zPid;

	if (0 == zRecursiveMark) {
		_i zWD = inotify_add_watch(
				zInotifyFD,
				zpPath,
				IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_MODIFY|IN_MOVED_TO|IN_MOVED_FROM|IN_EXCL_UNLINK
				);

		zCheck_Negative_Thread_Exit(zWD);

		zpPathHash[zWD] = zpPath;
	}
	else {
		zPid = fork();
		zCheck_Negative_Thread_Exit(zPid);

		if (0 == zPid) {
			zinotify_add_watch_recursively(zpPath);
			return;
		} else {
			waitpid(zPid, NULL, 0);
		}
	}

	char zBuf[zCommonBufSiz] __attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;
	char *zpNewPath;

	for (;;) {
		zLen = read(zInotifyFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Thread_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; 
				zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			zPid = fork();
			zCheck_Negative_Thread_Exit(zPid);

			if (0 == zPid) {
				zCheck_Negative_Thread_Exit(chdir(zpPathHash[zpEv->wd]));
				system(
						"git init . ;"
						"git config --global user.name $USER;"
						"git config --global user.email $PWD;"
						"git add --all :/;"
						"git commit -m \"Inotify auto commit: `date +\"%m-%d %H:%M:%S\"`\""
						);
			}
			else {
				if (zpEv->mask & IN_ISDIR && 
						(zpEv->mask & IN_CREATE || zpEv->mask & IN_MOVED_TO)) {

	  				// If a new subdir is created or moved in, add it to the watch list.
					zMem_Alloc(zpNewPath, char, 2 + strlen(zpPathHash[zpEv->wd]) + zpEv->len);

					strcpy(zpNewPath, zpPathHash[zpEv->wd]);
					strcat(zpNewPath, "/");
					strcat(zpNewPath, zpEv->name);

					zinotify_add_watch_recursively(zpNewPath);
				}
			}
		}
	}
}

// For each object, create one thread to monitor it.
static void *
zobj_thread(void *zpObjIf) {
	pthread_detach(pthread_self());

	zinotify_action(
			((zObjInfo *) zpObjIf)->path,
			((zObjInfo *) zpObjIf)->RecursiveMark
			);

	pthread_exit(NULL);
}

void
zconfig_file_monitor(const char *zpConfPath) {
	_i zConfFD = inotify_init();
	zCheck_Negative_Exit(
			inotify_add_watch(
				zConfFD,
				zpConfPath, 
				IN_CREATE|IN_MOVED_TO|IN_MODIFY|IN_DELETE|IN_MOVED_FROM
				)
			); 

	char zBuf[zCommonBufSiz] 
		__attribute__ ((aligned(__alignof__(struct inotify_event))));

	zCheck_Negative_Thread_Exit(
			read(zInotifyFD, zBuf, sizeof(zBuf))
			);
}

_i
main(_i zArgc, char **zppArgv) {
	if (3 == zArgc && 0 == strcmp("-f", zppArgv[1])) {
		struct stat zStat[1];
		zCheck_Negative_Exit(stat(zppArgv[2], zStat));
		if (!S_ISREG(zStat->st_mode)) {
			fprintf(stderr, "Need a regular file!\n"
					"Usage: file_monitor -f <Config File Path>\n");
			exit(1);
		}
	} else {
		fprintf(stderr, "Usage: file_monitor -f <Config File Path>\n");
		exit(1);
	}
	
	zObjInfo *zpObjIf[2] = {NULL};

	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

zReload: 
	for (zpObjIf[0] = zpObjIf[1] = zread_conf_file(zppArgv[2]); 
			NULL != zpObjIf[0]; zpObjIf[0] = zpObjIf[0]->p_next) {

		pthread_create(
				&(zpObjIf[0]->ControlTD), 
				NULL, 
				zobj_thread,
				zpObjIf[0]
				);
	}

	zconfig_file_monitor(zppArgv[2]);

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (0 == zPid) {
		for (_i i = 0; i < zHashSiz; i++) {
			if (NULL != zpPathHash[i]) { free(zpPathHash[i]); }
		}

		zFree_Memory_Common(zpObjIf[1], zpObjIf[0]);
		goto zReload;
	}
	else {
		exit(0);
	}
}
