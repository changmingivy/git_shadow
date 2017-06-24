#define _XOPEN_SOURCE 700
#define _BSD_SOURCE
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <libgen.h>
#include "zpcre2.h"
extern char * zget_one_line_from_FILE(FILE *);
typedef struct zFileDiffInfo {
	_i CacheVersion;  // == time(NULL)
	_i FileIndex;  // file hash index
	_i PathLen;  // 1 + strlen()
	char path[];  // the path relative to code repo
} zFileDiffInfo;

typedef struct zDeployResInfo {
	_i CacheVersion;
	_i DeployState;
} zDeployResInfo;

typedef struct zCacheInfo {
	zFileDiffInfo **pp_cache;
	_i siz;
} zCacheInfo;

//_i HostIndex;  //ECS hash index
_i zCurCacheVersion;

_i
zupdate_cache(zFileDiffInfo **zppOldCache, _i zOldCacheSiz) {
	_i zNewVersion = (_i)time(NULL);

	zFileDiffInfo **zppDiffHash = NULL;
	_i zBaseSiz = sizeof(zFileDiffInfo);

	_i zTotalDiff = 0;
	char *zpRes = NULL;
	size_t zResLen = 0;

	FILE *zpShellRetHandler = popen("git diff --name-only HEAD CURRENT | wc -l && git diff --name-only HEAD CURRENT", "r");
	zCheck_Null_Exit(zpShellRetHandler);

	if (NULL == (zpRes = zget_one_line_from_FILE(zpShellRetHandler))) { return 0; }
	else {
		if (0 == (zTotalDiff = atoi(zpRes))) { return  -1; }
		zMem_Alloc(zppDiffHash, zFileDiffInfo *, zTotalDiff);

		for (_i i = 0; NULL != (zpRes =zget_one_line_from_FILE(zpShellRetHandler)); i++) {
			zResLen = strlen(zpRes);
			zCheck_Null_Exit(
					zppDiffHash[i] = malloc(1 + zResLen + zBaseSiz)
					);
			zppDiffHash[i]->CacheVersion = zNewVersion;
			zppDiffHash[i]->FileIndex = i;
			zppDiffHash[i]->PathLen = zResLen;
			strcpy(zppDiffHash[i]->path, zpRes);
		}
		for (_i i = 0; i < zOldCacheSiz; i++) { free(zppOldCache[i]); }
	}
	return zNewVersion;
}

static void *
zcallback_update_cache(void *zpIf) {
	zCacheInfo *zpOldCacheIf = (zCacheInfo *)zpIf;
	zCurCacheVersion = zupdate_cache(zpOldCacheIf->pp_cache, zpOldCacheIf->siz);
	return NULL;
}

//在 .git/logs 目录没有变动的情况下，对客户端的查询请求，直接返回cache中的数据
//有变动则更新cache

// use zsendmsg
// Sent msg as the form below to server
// [OpsMark(l/d/D/R)] [struct zFileDiffInfo]
// Meaning:
// 		-l:list modified file list
// 		-d:file content diff
// 		-D:deploy
// 		-R:revoke

