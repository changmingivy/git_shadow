#ifndef _Z
	#include "zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
struct iovec *
zgenerate_cache(_i zRepoId) { // 生成缓存：差异文件列表、每个文件的差异内容、最近的布署日志信息
	_i zNewVersion = (_i)time(NULL);  // 以时间戳充当缓存版本号

	struct iovec *zpNewCacheVec[2] = {NULL};  // 维持2个iovec数据，一个用于缓存文件列表，另一个按行缓存每一个文件差异信息

	FILE *zpShellRetHandler[2] = {NULL};  // 执行SHELL命令，读取此句柄获取返回信息
	_i zDiffFilesNum = 0;  // 差异文件总数

	char *zpRes[2] = {NULL};  // 存储从命令行返回的原始文本信息
	char zShellBuf[zCommonBufSiz];  // 存储命令行字符串

	zpShellRetHandler[0] = popen("git diff --name-only HEAD CURRENT | wc -l "
			"&& git diff --name-only HEAD CURRENT "
			"&& git log --format=%H -n 1 CURRENT", "r");  // 第一行返回的是文件总数
	zCheck_Null_Exit(zpShellRetHandler);

	pthread_mutex_lock(&zpDeployLock[zRepoId]);  // 更新缓存前阻塞相同代码库的其它相关的写操作：布署、撤销等
	if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) { return NULL; }  // 命令没有返回结果，代表没有差异文件，理设上不存在此种情况
	else {
		if (0 == (zDiffFilesNum = atoi(zpRes[0]))) { return  NULL; }  // 同上，用于防止意外原因扰乱缓存数据
		zMem_Alloc(zpNewCacheVec[0], struct iovec, zDiffFilesNum);   // 为存储文件路径列表的iovec[0]分配空间
		zpCacheVecSiz[zRepoId] = zDiffFilesNum;  // 更新对应代码库的差异文件数量（更新到全局变量）

		for (_i i = 0; i < zDiffFilesNum - 1; i++) {
			zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0]);

			zCheck_Null_Exit(zpNewCacheVec[0][i].iov_base = malloc(1 + strlen(zpRes[0]) + sizeof(zFileDiffInfo)));

			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->CacheVersion = zNewVersion;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->RepoId= zRepoId;
			((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->FileIndex = i;
			strcpy(((zFileDiffInfo *)(zpNewCacheVec[0][i].iov_base))->path, zpRes[0]);

			sprintf(zShellBuf, "git diff HEAD CURRENT -- %s | wc -l && git diff HEAD CURRENT -- %s", zpRes[0], zpRes[0]);
			zpShellRetHandler[1] = popen(zShellBuf, "r");
			zCheck_Null_Exit(zpShellRetHandler);

			zMem_Alloc(zpNewCacheVec[1], struct iovec, atoi(zpRes[1]));  // 为每个文件的详细差异内容分配iovec[1]分配空间

			for (_i j = 0; NULL != (zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])); j++) {
				zMem_Alloc(zpNewCacheVec[j]->iov_base, char, 1 + strlen(zpRes[1]));
				strcpy(((char *)(zpNewCacheVec[1][j].iov_base)), zpRes[1]);
			}
			pclose(zpShellRetHandler[1]);
		}

		// 以下四行更新所属代码库的CURRENT tag SHA1 sig值
		char *zpBuf = zget_one_line_from_FILE(zpShellRetHandler[0]);  // 读取最后一行：CURRENT标签的SHA1 sig值 
		zMem_Alloc(zppCurTagSig[zRepoId], char, 40);  // 存入前40位，丢弃最后的'\0'
		strncpy(zppCurTagSig[zRepoId], zpBuf, 40);  // 更新对应代码库的最新CURRENT tag SHA1 sig
		pclose(zpShellRetHandler[0]);
	}

	// 以下部分更新日志缓存
	zDeployLogInfo *zpMetaLogIf, *zpTmpIf;
	struct stat zStatBufIf;

	zCheck_Negative_Exit(fstat(zpLogFd[0][zRepoId], &zStatBufIf));  // 获取当前日志文件属性
	zpPreLoadLogVecSiz[zRepoId] = (zStatBufIf.st_size / sizeof(zDeployLogInfo)) > zPreLoadLogSiz ? zPreLoadLogSiz : (zStatBufIf.st_size / sizeof(zDeployLogInfo));  // 计算需要缓存的实际日志数量
	zMem_Alloc(zppPreLoadLogVecIf[zRepoId], struct iovec, zpPreLoadLogVecSiz[zRepoId]);  // 根据计算出的数量分配相应的内存

	zpMetaLogIf = (zDeployLogInfo *)mmap(NULL, zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo), PROT_READ, MAP_PRIVATE, zpLogFd[0][zRepoId], zStatBufIf.st_size - zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo));  // 将meta日志mmap至内存
	zCheck_Null_Exit(zpMetaLogIf);

	zpTmpIf = zpMetaLogIf + zpPreLoadLogVecSiz[zRepoId] - 1;
	_ul zDataLogSiz = zpTmpIf->offset + zpTmpIf->len - zpMetaLogIf->offset;  // 根据meta日志属性确认data日志偏移量
	char *zpDataLog = mmap(NULL, zDataLogSiz, PROT_READ, MAP_PRIVATE, zpLogFd[1][zRepoId], zpMetaLogIf->offset);  // 将data日志mmap至内存
	zCheck_Null_Exit(zpDataLog);

	for (_ui i = 0; i < 2 * zpPreLoadLogVecSiz[zRepoId]; i++) {  // 拼装日志信息
		if (0 == i % 2) {
			zppPreLoadLogVecIf[zRepoId][i].iov_base =  zpMetaLogIf + i / 2;
			zppPreLoadLogVecIf[zRepoId][i].iov_len = sizeof(zDeployLogInfo);
		}
		else {
			zppPreLoadLogVecIf[zRepoId][i].iov_base = zpDataLog + (zpMetaLogIf + i / 2)->offset - zpMetaLogIf->offset;
			zppPreLoadLogVecIf[zRepoId][i].iov_len = (zpMetaLogIf + i / 2)->len;
		}
	}

	pthread_mutex_unlock(&zpDeployLock[zRepoId]);
	return zpNewCacheVec[0];
}
void
zupdate_cache(void *zpIf) {
	_i zRepoId = *((_i *)zpIf);
	struct iovec *zpOldCacheIf = zppCacheVecIf[zRepoId];
	if (NULL == (zppCacheVecIf[zRepoId] = zgenerate_cache(zRepoId))) {
		zppCacheVecIf[zRepoId] = zpOldCacheIf;  // 若新缓存返回状态不正常，回退到上一版缓存状态
	}
	else {
  		// 若新缓存正常返回，释放掉老缓存的内存空间
		_ui i;
		for (i = 0; i < zpOldCacheIf->iov_len; i++) {
			for (_ui j = 0; j < ((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->VecSiz; j++) {
				free((((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent[j]).iov_base);
			}
			free(((zFileDiffInfo *)(zpOldCacheIf[i].iov_base))->p_DiffContent);
			free(zpOldCacheIf[i].iov_base); 
		}
		free(zpOldCacheIf);
		// 如下部分用于销毁旧的布署日志缓存
		zDeployLogInfo *zpTmpIf = (zDeployLogInfo *)(zppPreLoadLogVecIf[zRepoId]->iov_base);
		munmap(zppPreLoadLogVecIf[zRepoId]->iov_base, zpPreLoadLogVecSiz[zRepoId] * sizeof(zDeployLogInfo));
		munmap(zppPreLoadLogVecIf[zRepoId + 1], (zpTmpIf + zpPreLoadLogVecSiz[zRepoId])->offset + (zpTmpIf + zpPreLoadLogVecSiz[zRepoId])->len - zpTmpIf->offset);
	}
}
