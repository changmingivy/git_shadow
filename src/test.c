#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include <pthread.h>
#include <semaphore.h>

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>

#include <libgen.h>

#include "zpcre2.h"

#define zCommonBufSiz 4096 
#define zHashSiz 8192
#define zMaxThreadsNum 64

#define zWatchBit (IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF | IN_EXCL_UNLINK)

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
			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);

			zpObjIf[0] = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
			zpObjIf[0]->p_next = NULL;
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];

			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);

			zpcre_free_tmpsource(zpRetIf[2]);
			zpcre_free_tmpsource(zpRetIf[1]);
			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[0]);

	fclose(zpFile);
	return zpObjIf[2];
}

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
				zWatchBit
				);
	zCheck_Negative_Exit(zWid);

	zpPathHash[zWid] = zpPath;

	struct dirent *zpEntry;
	char *zpNewPath;

	DIR *zpDir = opendir(zpPath);
	zCheck_Null_Exit(zpDir);

	while (NULL != (zpEntry = readdir(zpDir))) {
		if (DT_DIR == zpEntry->d_type 
				&& strcmp(".", zpEntry->d_name) 
				&& strcmp("..", zpEntry->d_name) 
				&& strcmp(".git", zpEntry->d_name)) {
			zMem_Alloc(zpNewPath, char, 2 + strlen(zpPath) + strlen(zpEntry->d_name));

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
zthread_git(void *zpIf) {
//TEST: PASS
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);

	char *zpPath = (char *) zpIf;

	struct stat zStat;
	while (-1 == stat(zpPath, &zStat)) {
		zpPath = dirname(zpPath);
	}

	zCheck_Negative_Thread_Exit(chdir(zpPath));
//	system(
//			"(git config --global user.name $USER;"
//			"git config --global user.email $PWD;"
//			"git add .;"
//			"git commit -m \"Inotify auto commit: `date +\"%m-%d %H:%M:%S\"`\")>/dev/null"
//			);
	system("git config --list");  //Just for test.
	return NULL;
}

void
zinotify_add_watch(char *zpPath, _i zRecursiveMark) {
//TEST: PASS
	if (0 == zRecursiveMark) {
		_i zWid = inotify_add_watch(
				zInotifyFD,
				zpPath,
				zWatchBit
				);
		zCheck_Negative_Exit(zWid);

		zpPathHash[zWid] = zpPath;
	}
	else {
			zinotify_add_watch_recursively(zpPath);
	}
}

void *
zthread_wait_event(void *zpIf) {
//TEST: PASS
	char zBuf[zCommonBufSiz]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset, *zpNewPath;

	for (;;) {
		zLen = read(zInotifyFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; 
				zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			if (zpEv->mask & IN_ISDIR) {
				if(zpEv->mask & (IN_CREATE | IN_MOVED_TO)) {
		  		// If a new subdir is created or moved in, add it to the watch list.
					fprintf(stderr, "\033[31;01m add! \033[00m\n");
					zMem_Alloc(zpNewPath, char, 2 + strlen(zpPathHash[zpEv->wd]) + zpEv->len);
		
					strcpy(zpNewPath, zpPathHash[zpEv->wd]);
					strcat(zpNewPath, "/");
					strcat(zpNewPath, zpEv->name);
		
					zinotify_add_watch_recursively(zpNewPath);
				}
				else if (zpEv->mask & (IN_MODIFY | IN_DELETE | IN_MOVED_FROM)) { 
					fprintf(stderr, "\033[31;01m del sub! \033[00m\n");
				}
				else if (zpEv->mask & IN_IGNORED) {
					fprintf(stderr, "\033[31;01m del self! \033[00m\n");
					free(zpPathHash[zpEv->wd]);  // BUG: can't get this event.
					continue;
				}
				else {
					fprintf(stderr, "\033[31;01mWARNING: Unknown inotify event occur!!!\033[00m\n");
				}
	
				// Git add and commit.
				zCheck_Pthread_Func_Exit(
						pthread_create(&zTid, NULL, zthread_git, zpPathHash[zpEv->wd])
						);
			}

		}
	}
}

// For each object, create one thread to monitor it.
static void *
zthread_add_top_watch(void *zpObjIf) {
//TEST: PASS
	zCheck_Pthread_Func_Warning(
			pthread_detach(pthread_self())
			);

	zinotify_add_watch(
			((zObjInfo *) zpObjIf)->path,
			((zObjInfo *) zpObjIf)->RecursiveMark
			);
	return NULL;
}

void
zconfig_file_monitor(const char *zpConfPath) {
	_i zConfFD = inotify_init();
	zCheck_Negative_Exit(
			inotify_add_watch(
				zConfFD,
				zpConfPath, 
				IN_CLOSE_WRITE | IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF
				)
			); 

	char zBuf[zCommonBufSiz] 
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;

	for (;;) {
		zLen = read(zConfFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; 
				zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;

			if (zpEv->mask & (IN_CLOSE_WRITE | IN_MODIFY)) { return; }
			else if (zpEv->mask & (IN_MOVE_SELF | IN_DELETE_SELF)) {
				fprintf(stderr, "\033[31;01mConfig file disappeared!\nMonitor daemon exited.\n");
				exit(1);
			}
		}
	}
}

/***********************************************************************************/
_i
main(_i zArgc, char **zppArgv) {
	//zdaemonize(NULL);
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

zReLoad:;
	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zCheck_Negative_Exit(sem_init(&zSem, 0, zMaxThreadsNum));

	zObjInfo *zpObjIf[2] = {NULL};
	for (zpObjIf[0] = zpObjIf[1] = zread_conf_file(zppArgv[2]); 
			NULL != zpObjIf[0]; zpObjIf[0] = zpObjIf[0]->p_next) {

		zCheck_Pthread_Func_Exit(
				pthread_create(&zTid, NULL, zthread_add_top_watch, zpObjIf[0])
				);
	}

	pthread_create(&zTid, NULL, zthread_wait_event, NULL);

	zconfig_file_monitor(zppArgv[2]);

	close(zInotifyFD);
	zCheck_Negative_Exit(sem_destroy(&zSem));

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);

	if (0 == zPid) { goto zReLoad; }
	else { exit(0); }
}
/***********************************************************************************/
