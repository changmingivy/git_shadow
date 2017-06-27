#ifndef _Z
	#include "zmain.c"
#endif

pthread_mutex_t zCommonLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t zCommonCond = PTHREAD_COND_INITIALIZER;

void
zcallback_common_action(void *zpCurIf) {
//TEST: PASS
	zSubObjInfo *zpSubIf = (zSubObjInfo *)zpCurIf;

	pthread_mutex_lock(&zCommonLock);
	while (0 != zBitHash[zpSubIf->UpperWid]) {
		pthread_cond_wait(&zCommonCond, &zCommonLock);
	}
	zBitHash[zpSubIf->UpperWid] = 1;
	pthread_mutex_unlock(&zCommonLock);

	char zShellBuf[1 + strlen("zEvPath=;zEvType=;") + strlen(zpSubIf->path) + strlen(zpShellCommand)];
	sprintf(zShellBuf, "zEvPath=%s;zEvMark=%d;%s", zpSubIf->path, zpSubIf->EvType, zpShellCommand);

	system(zShellBuf);

	pthread_mutex_lock(&zCommonLock);
	zBitHash[zpSubIf->UpperWid] = 0;
	pthread_cond_signal(&zCommonCond);
	pthread_mutex_unlock(&zCommonLock);
}

/****************
 * UPDATE CACHE *
 ****************/
struct iovec *
zgenerate_cache(_i zRepoId) {
	_i zNewVersion = (_i)time(NULL);

	struct iovec *zpNewCacheVec[2] = {NULL};

	FILE *zpShellRetHandler[2] = {NULL};
	_i zDiffFilesNum = 0;

	char *zpRes[2] = {NULL};
	size_t zResLen[2] = {0};
	_i zBaseSiz = sizeof(zFileDiffInfo);
	char zShellBuf[zCommonBufSiz];

	zpShellRetHandler[0] = popen("git diff --name-only HEAD CURRENT | wc -l && git diff --name-only HEAD CURRENT | git log --format=%H -n 1 CURRENT", "r");
	zCheck_Null_Exit(zpShellRetHandler);

	if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) { return 0; }
	else {
		if (0 == (zDiffFilesNum = atoi(zpRes[0]))) { return  NULL; }
		zMem_Alloc(zpNewCacheVec[0], struct iovec, zDiffFilesNum);
		zpCacheVecSiz[zRepoId] = zDiffFilesNum;  // Global Var

		for (_i i = 0; i < zDiffFilesNum - 1; i++) {
			zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0]);

			zResLen[0] = strlen(zpRes[0]);
			zCheck_Null_Exit(
					zpNewCacheVec[0][i].iov_base = malloc(1 + zResLen[0] + zBaseSiz)
					);
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->CacheVersion = zNewVersion;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->RepoId= zRepoId;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->FileIndex = i;
			strcpy(((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->path, zpRes[0]);

			sprintf(zShellBuf, "git diff HEAD CURRENT -- %s | wc -l && git diff HEAD CURRENT -- %s", zpRes[0], zpRes[0]);
			zpShellRetHandler[1] = popen(zShellBuf, "r");
			zCheck_Null_Exit(zpShellRetHandler);

			zMem_Alloc(zpNewCacheVec[1], struct iovec, atoi(zpRes[1]));

			for (_i j = 0; NULL != (zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])); j++) {
				zResLen[1] = strlen(zpRes[1]);
				zMem_Alloc(zpNewCacheVec[j]->iov_base, char, 1 + zResLen[1]);
				strcpy(((char *)(zpNewCacheVec[1][j].iov_base)), zpRes[1]);
			}
			pclose(zpShellRetHandler[1]);
		}

		char *zpBuf = zget_one_line_from_FILE(zpShellRetHandler[0]);
		zMem_Alloc(zppCurTagSig[zRepoId], char, 40);  // Not include '\0'
		strncpy(zppCurTagSig[zRepoId], zpBuf, 40);  // Global Var
		pclose(zpShellRetHandler[0]);
	}

	pthread_mutex_lock(&zpDeployLock[zRepoId]);
	return zpNewCacheVec[0];
	pthread_mutex_unlock(&zpDeployLock[zRepoId]);
}

void
zupdate_cache(void *zpIf) {
	_i zRepoId = *((_i *)zpIf);
	struct iovec *zpOldCacheIf = zppCacheVec[zRepoId];
	if (NULL == (zppCacheVec[zRepoId] = zgenerate_cache(zRepoId))) {
		zppCacheVec[zRepoId] = zpOldCacheIf;  // Global Var
	}
	else {
		//struct iovec *zpOldCacheIf = (struct iovec *)zpIf;
		for (_ui i = 0; i < zpOldCacheIf->iov_len; i++) { 
			for (_ui j = 0; j < ((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->VecSiz; j++) {
				free((((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent[j]).iov_base);
			}
			free(((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent);
			free(zpOldCacheIf[i].iov_base); 
		}
		free(zpOldCacheIf);
	}
}
