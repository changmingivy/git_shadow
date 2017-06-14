#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

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

static char *zpPathHash[zHashSiz];
static _i zInotifyFD;

struct zObjInfo {
	char path[zCommonBufSiz];  // The directory to be monitored.
	pthread_t ControlTD;  // The thread which master the watching.

	_i RecursiveMark;  // Mark recursive monitor.
//	_i ValidState;  // Mark which node should be added or deleted, 0 for valid, -1 for invalid.

	struct zObjInfo *p_next;
};
typedef struct zObjInfo zObjInfo;

extern char * zget_one_line_from_FILE(FILE *);

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

_i
main(void) {
	zObjInfo *zpTest = zread_conf_file("/tmp/xx");
	while (NULL != zpTest) {
		printf("major: %s, %d\n", zpTest->path, zpTest->RecursiveMark);
		zpTest = zpTest->p_next;
	}
}
