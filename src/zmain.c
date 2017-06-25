#define _Z

#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <pthread.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>

#include <sys/inotify.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>

#include "zutils.h"

#define zCommonBufSiz 4096 

/***************
 * COMMON FUNC *
 ***************/
#include "zcommon.c"

/********
 * PCRE *
 ********/
#include "zpcre.c"

/***************
 * THREAD POOL *
 ***************/
#include "zthread_pool.c"

/***********
 * INOTIFY *
 ***********/
#include "zinotify.c"

/***********
 * NETWORK *
 ***********/
#include "znetwork.c"

/***************
 * CONFIG FILE *
 ***************/
static zObjInfo *
zread_conf_file(const char *zpConfPath) {
//TEST: PASS
	zObjInfo *zpObjIf[3] = {NULL};

	zPCREInitInfo *zpInitIf[4] = {NULL};
	zPCRERetInfo *zpRetIf[4] = {NULL};

	_i zCnt = 0;
	char *zpRes = NULL;
	FILE *zpFile = fopen(zpConfPath, "r");

	struct stat zStatBuf;

	zpInitIf[0] = zpcre_init("^\\s*\\d\\s*/[/\\w]+");
	zpInitIf[1] = zpcre_init("^\\d(?=\\s+)");
	zpInitIf[2] = zpcre_init("[/\\w]+(?=\\s*$)");
	zpInitIf[3] = zpcre_init("^\\s*($|#)");

	for (_i i = 1; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); i++) {
		zpRes[strlen(zpRes) - 1] = '\0';

		zpRetIf[3] = zpcre_match(zpInitIf[3], zpRes, 0);
		if (0 < zpRetIf[3]->cnt) {
			zpcre_free_tmpsource(zpRetIf[3]);
			continue;
		}
		zpcre_free_tmpsource(zpRetIf[3]);

		zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
		if (0 == zpRetIf[0]->cnt) {
			zpcre_free_tmpsource(zpRetIf[0]);
			zPrint_Time();
			fprintf(stderr, "\033[31m[Line %d] \"%s\": Invalid entry.\033[00m\n", i ,zpRes);
			continue;
		} else {
			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);
			if (-1 == lstat(zpRetIf[2]->p_rets[0], &zStatBuf) 
					|| !S_ISDIR(zStatBuf.st_mode)) {
				zpcre_free_tmpsource(zpRetIf[2]);
				zpcre_free_tmpsource(zpRetIf[1]);
				zpcre_free_tmpsource(zpRetIf[0]);
				zPrint_Time();
				fprintf(stderr, "\033[31m[Line %d] \"%s\": NO such directory or NOT a directory.\033[00m\n", i, zpRes);
				continue;
			}

			zpObjIf[0] = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];
			zpObjIf[0]->p_next = NULL;  // Must here!!!

			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);

			zpcre_free_tmpsource(zpRetIf[2]);
			zpcre_free_tmpsource(zpRetIf[1]);
			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[3]);
	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[0]);

	fclose(zpFile);
	return zpObjIf[2];
}

static void
zconfig_file_monitor(const char *zpConfPath) {
//TEST: PASS
	_i zConfFD = inotify_init();
	zCheck_Negative_Exit(
			inotify_add_watch(
				zConfFD,
				zpConfPath, 
				IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF
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

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;
			if (zpEv->mask & (IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) { return; }
		}
	}
}

/********
 * MAIN *
 ********/
_i
main(_i zArgc, char **zppArgv) {
//TEST: PASS
//	extern char *optarg;
//	extern int optind, opterr, optopt;
	struct stat zStat;

	opterr = 0;  // prevent getopt to print err info
	for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "CSf:x:h:p:"));) {
		switch (zOpt) {
		case 'f':
			if (-1 == stat(optarg, &zStat) || !S_ISREG(zStat.st_mode)) {
				zPrint_Time();
				fprintf(stderr, "\033[31;01mConfig file not exists or is not a regular file!\n"
						"Usage: %s -f <Config File Path>\033[00m\n", zppArgv[0]);
				exit(1);
			}
			break;
		case 'x':
			zpShellCommand = optarg;
			break;
		case 'h':  // host ip addr
			// TO DO
			break;
		case 'p':  // port
			// TO DO
			break;
		case 'C':  // Client
			// TO DO
			break;
		case 'S':  // Server
			// TO DO
			break;
		default: // zOpt == '?'
			zPrint_Time();
		 	fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
			exit(1);
		}
	}
//	for (; optind < zArgc; optind++) { printf("Argument = %s\n", zppArgv[optind]); }

	zdaemonize("/");

zReLoad:;
	zInotifyFD = inotify_init();
	zCheck_Negative_Exit(zInotifyFD);

	zthread_poll_init();

	zObjInfo *zpObjIf = NULL;
	if (NULL == (zpObjIf = zread_conf_file(zppArgv[2]))) {
		zPrint_Time();
		fprintf(stderr, "\033[31;01mNo valid entry found in config file!!!\n\033[00m\n");
	}
	else {
		do {
			zAdd_To_Thread_Pool(zinotify_add_top_watch, zpObjIf);
			zpObjIf = zpObjIf->p_next;
		} while (NULL != zpObjIf);
	}

	zAdd_To_Thread_Pool(zinotify_wait, NULL);
	zconfig_file_monitor(zppArgv[2]);  // Robustness

	close(zInotifyFD);

	pid_t zPid = fork();
	zCheck_Negative_Exit(zPid);
	if (0 == zPid) { goto zReLoad; }
	else { exit(0); }
}
