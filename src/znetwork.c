#ifndef _Z
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
	#include "zutils.h"
	#define zCommonBufSiz 4096 
#endif

typedef struct zDeployResInfo {
	_us CacheVersion;
	_us DeployState;
} zDeployResInfo;

//_i HostIndex;  //ECS hash index

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
