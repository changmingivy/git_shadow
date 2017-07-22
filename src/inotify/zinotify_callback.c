#ifndef _Z
    #include "../zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
// 此部分的多个函数用于生成缓存：差异文件列表、每个文件的差异内容、最近的布署日志信息

//void
//zupdate_sig_cache(void *zpRepoId) {
//// TEST:PASS
//    _i zRepoId = *((_i *)zpRepoId);
//    char zShellBuf[zCommonBufSiz], *zpRes;
//    FILE *zpShellRetHandler;
//
//    /* 以下部分更新所属代码库的CURRENT SHA1 sig值 */
//    sprintf(zShellBuf, "cd %s && git log --format=%%H -n 1 CURRENT", zpRepoGlobIf[zRepoId].RepoPath);
//    zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));
//    zCheck_Null_Exit(zpRes = zget_one_line_from_FILE(zpShellRetHandler));  // 读取CURRENT分支的SHA1 sig值
//
//    if (zBytes(40) > strlen(zpRes)) {
//        zPrint_Err(0, NULL, "Invalid CURRENT sig!!!");
//        exit(1);
//    }
//
//    if (NULL == zppCURRENTsig[zRepoId]) {
//        zMem_Alloc(zppCURRENTsig[zRepoId], char, zBytes(41));  // 含 '\0'
//    }
//
//    strncpy(zppCURRENTsig[zRepoId], zpRes, zBytes(41));  // 更新对应代码库的最新CURRENT 分支SHA1 sig
//    pclose(zpShellRetHandler);
//}

void
zupdate_log_cache(void *zpDeployLogIf) {
// TEST:PASS
    if (NULL == zpDeployLogIf) { return; }  // robustness

    zDeployLogInfo *zpLogIf = (zDeployLogInfo *)zpDeployLogIf;
    size_t zRealLen = sizeof(zDeployLogInfo) + zBytes(zpLogIf->PathLen);
    zDeployLogInfo *zpLogCacheIf = malloc(zRealLen);

    char zCommitSig[zBytes(41)], zShellBuf[zCommonBufSiz], *zpLineContent;
    FILE *zpFile;
    _i zLen;

    _i zFd[2];
    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zpLogIf->RepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(pread(zFd[1], zCommitSig, zBytes(41), zBytes(41) * zpLogIf->index));
    close(zFd[0]);
    close(zFd[1]);

    sprintf(zShellBuf, "cd %s && git log %s --name-only --format=", zpRepoGlobIf[zpLogIf->RepoId].RepoPath, zCommitSig);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    for (size_t zWrOffSet = 0; NULL != (zpLineContent = zget_one_line_from_FILE(zpFile));) {
        zLen = strlen(zpLineContent);
        zpLineContent[zLen] = '\0';
        zpLineContent[zLen - 1] = '\n';
        strcpy(zpLogCacheIf->path + zWrOffSet, zpLineContent);
        zWrOffSet += 1 + zLen;
    }

    if (zpLogCacheQueueHeadIndex[zpLogIf->RepoId] == zpLogCacheVecSiz[zpLogIf->RepoId] - 1) {
        zpLogCacheQueueHeadIndex[zpLogIf->RepoId] = 0;
    } else {
        zpLogCacheQueueHeadIndex[zpLogIf->RepoId]++;
    }

    struct iovec *zpVecQueueHead = &zppLogCacheVecIf[zpLogIf->RepoId][ zpLogCacheQueueHeadIndex[zpLogIf->RepoId] ];
    if (NULL != zpVecQueueHead->iov_base) {
        free(zpVecQueueHead->iov_base);
    };
    zpVecQueueHead->iov_base = zpLogCacheIf;
    zpVecQueueHead->iov_len = zRealLen;

    // 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
    for (_i i = 0, j = zpLogCacheQueueHeadIndex[zpLogIf->RepoId]; i < zpLogCacheVecSiz[zpLogIf->RepoId]; i++) {
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_base = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_base;
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_len = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_len;

        if (0 == j) {
            j = zpLogCacheVecSiz[zpLogIf->RepoId] - 1;
        } else {
            j--;
        }
    }
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表及差异内容缓存
 */
void
zget_file_list_and_diff_content(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpUpperVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], *zpRes;

    struct zSendInfo *zpSendIf;  // 此项是 iovec 的 io_base 字段
    _i zVecId;
	_i zSendDataLen, zVecDatalen;
    _i zAllocSiz = 128;

	zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;
	if (0 ==zpCacheMetaIf->TopObjTypeMark) {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf;
	} else {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].DeployWrapVecIf;
	}

	if (-1 == zFileId) {
 		zpUpperVecWrapIf = zpTopVecWrapIf;
	} else {
 		zpUpperVecWrapIf = zGet_SubVecIf(zpTopVecWrapIf, zpCacheMetaIf->zCommitId);
	}

    zMem_Alloc(zpCurVecWrapIf, zVecInfo, 1);
    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s %s CURRENT -- %s", zpRepoGlobIf[zpCacheMetaIf->zRepoId].RepoPath,
			(-1 == zFileId) ? "--name-only" : "",
			zGet_OneCommitSigPtr(zpTopVecWrapIf, zpCacheMetaIf->zCommitId),
			(-1 == zFileId) ? "" : ( (zSendInfo *) zGet_SendIfPtr(zpUpperVecWrapIf, zpCacheMetaIf->zFileId) )->data);

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); zVecId++) {
        if (zVecId >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf);
        }

		zSendDataLen = 1 + strlen(zpRes);
		zVecDatalen = zSendDataLen + sizeof(zSendInfo);
        zCheck_Null_Exit( zpSendIf = malloc(zVecDataLen) );

        zpSendIf->SelfId = zVecId;
        zpSendIf->DataLen = zSendDataLen;
        strcpy(zpSendIf->data, zpRes);

        zpCurVecWrapIf->p_VecIf[zVecId].iov_base = zpSendIf;
        zpCurVecWrapIf->p_VecIf[zVecId].iov_len = zVecDatalen;
    }
    pclose(zpShellRetHandler);

	zpCurVecWrapIf->VecSiz = zVecId;
    if (0 == zpCurVecWrapIf->VecSiz) {
		/* 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空 */
        free(zpCurVecWrapIf->p_VecIf[zVecId]);
        zpCurVecWrapIf->p_VecIf[zVecId] = NULL;
		return;
    } else {
		/* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf[zVecId], struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf[zVecId]);
	}

	/* 将自身关联到上一级数据结构 */
	zpUpperVecWrapIf->p_RefDataIf->p_SubVecWrapIf = zpCurVecWrapIf;

    if (-1 == zFileId) {
		/* 如果是文件路径级别，且差异文件数量不为0，则需要分配RefData空间，指向文件对比差异内容VecInfo */
    	zMem_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zpCurVecWrapIf->VecSiz);

		/* 如果是文件级别，则进入下一层获取对应的差异内容 */
        for (_i zId = 0; zId < zpCurVecWrapIf->VecSiz; zId++) {
			zMem_Alloc(zpSubCacheMetaIf, zCacheMetaInfo, 1);
			zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
			zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
			zpSubCacheMetaIf->zCommitId = zpCacheMetaIf->zCommitId;
			zpSubCacheMetaIf->zFileId = zId;

            zAdd_To_Thread_Pool(zget_file_diff_info, zpSubCacheMetaIf);
        }
	} else {
		/* 如果是文件差异内容级别，因为没有下一级数据，所以置为NULL */
		zpCurVecWrapIf->p_RefDataIf = NULL;
	}

	free(zpIf);  // 上一级传入的结构体空间是在堆上分配的
}

/*
 *  传入的是一个包含单次 commit 信息的额外malloc出来的 zVerWrapInfo 结构体指针，需要释放其下的文件列表结构及其内部的文件内容结构
 */
void
zfree_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zVecWrapInfo *zpVecWrapIf = (struct zVecWrapInfo *) zpIf;
    for (_i zFileId = 0; zFileId < zpVecWrapIf->VecSiz; zFileId++) {
        for (_i zLineId = 0; zLineId < zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->VecSiz; zLineId++) {
			free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf[zLineId].iov_base);
			free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf[zLineId]);
        }

		free(zpVecWrapIf->p_VecIf[zFileId].iov_base);
		free(zpVecWrapIf->p_VecIf[zFileId]);

		free(zpVecWrapIf->p_RefDataIf[zFileId].p_SubVecWrapIf);
		free(zpVecWrapIf->p_RefDataIf[zFileId]);
    }

	free(zpVecWrapIf->p_VecIf.iov_base);
	free(zpVecWrapIf->p_VecIf);

	free(zpVecWrapIf->p_RefDataIf.p_SubVecWrapIf);
	free(zpVecWrapIf->p_RefDataIf);

	free(zpIf);  // 传入的对象是为线程任务新开辟的内存空间，需要释放掉
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zinit_cache(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpCurVecWrapIf, *zpOldVecWrapIf;

	zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

	if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf;
	} else {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].DeployWrapVecIf;
	}

	struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, zCnter;

	struct zCacheMetaInfo *zpSubCacheMetaIf;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128];

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log --format=%%H%%ct", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
	
    for (zCnter = 0; (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))) && (zCnter < zCacheSiz); zCnter++) {
        zSendDataLen = 2 * sizeof(zpRepoGlobIf[zRepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
		zVecDatalen = zSendDataLen + sizeof(zSendInfo);

        zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

        zpCommitSendIf->SelfId = zCnter;
        zpCommitSendIf->DataLen = zSendDataLen;
        zpCommitSendIf->data = zpRepoGlobIf[zRepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
		zpCommitSendIf->data + sizeof(zpRepoGlobIf[zRepoId].CacheId) = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

		if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf) {
			zMem_Alloc(zpOldVecWrapIf, zVecWrapInfo, 1);

			zpOldVecWrapIf.p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_RefDataIf;
			zpOldVecWrapIf.p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf;
			zpOldVecWrapIf.VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->VecSiz;

			zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +

			zpTopVecWrapIf->p_RefDataIf[zCnter].p_SubWrapVecIf = NULL;
		}

		zpTopVecWrapIf->p_VecIf[zCnter].iov_base = zpCommitSendIf;
		zpTopVecWrapIf->p_VecIf[zCnter].iov_len = zVecDatalen;

		zpRes[zBytes(40)] = '\0';
        zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
        strcpy(zpTopVecWrapIf->p_RefDataIf[zCnter].data, zpRes);

	    zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.p_VecIf[zCnter].iov_base
			= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.p_VecIf[zCnter].iov_base;
	    zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.p_VecIf[zCnter].iov_len
			= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.p_VecIf[zCnter].iov_len;
	}
    pclose(zpShellRetHandler);

	zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.VecSiz 
		= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.VecSiz 
		= zCnter;  // 存储的是实际的对象数量

	zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId = zCacheSiz;  // 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置

    // 生成下一级缓存
	_i zCacheUpdateLimit = (zPreLoadCacheSiz < zCnter) ? zPreLoadCacheSiz : zCnter;
    for (zCnter = 0; zCnter < zCacheUpdateLimit; zCnter++) {
		zMem_Alloc(zpSubCacheMetaIf, zCacheMetaInfo, 1);

		zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
		zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
		zpSubCacheMetaIf->zCommitId = zCnter;
		zpSubCacheMetaIf->zFileId = -1;

    	zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubCacheMetaIf); // +
    }
}

void
zupdate_cache(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpCurVecWrapIf, *zpOldVecWrapIf;

	zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;
	if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf;
	} else {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].DeployWrapVecIf;
	}

	struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, zCnter;

	struct zCacheMetaInfo *zpSubCacheMetaIf;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128];

	_i zCacheUpdateLimit;

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log %s --format=%%H%%ct",
			zpRepoGlobIf[zRepoId].RepoPath,
			(1 == zpCacheMetaIf->InitMark) ? "" : "-1");

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
	
	if (1 == zpCacheMetaIf->InitMark) {
		zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId = 0;
	}

    for (zCnter = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId; (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))) && (zCnter < zCacheSiz); zCnter++) {
        zSendDataLen = 2 * sizeof(zpRepoGlobIf[zRepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
		zVecDatalen = zSendDataLen + sizeof(zSendInfo);

        zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

        zpCommitSendIf->SelfId = zCnter;
        zpCommitSendIf->DataLen = zSendDataLen;
        zpCommitSendIf->data = zpRepoGlobIf[zRepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
		zpCommitSendIf->data + sizeof(zpRepoGlobIf[zRepoId].CacheId) = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

		if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf) {
			zMem_Alloc(zpOldVecWrapIf, zVecWrapInfo, 1);

			zpOldVecWrapIf.p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_RefDataIf;
			zpOldVecWrapIf.p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf;
			zpOldVecWrapIf.VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->VecSiz;

			zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
		}

		zpTopVecWrapIf->p_VecIf[zCnter].iov_base = zpCommitSendIf;
		zpTopVecWrapIf->p_VecIf[zCnter].iov_len = zVecDatalen;

		zpRes[zBytes(40)] = '\0';
        zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
        strcpy(zpTopVecWrapIf->p_RefDataIf[zCnter].data, zpRes);
	}
    pclose(zpShellRetHandler);

	zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.VecSiz 
		= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.VecSiz 
		= zCnter;  // 存储的是实际的对象数量

	if (1 == zpCacheMetaIf->InitMark) {
		zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId = zCacheSiz;
	} else {
		if (0 == zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId) {
			zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId = zCacheSiz -1;
		} else {
			zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId--;  // 更新队列下一次将写入的位置的索引
		}

		if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
    		// 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
			_i i, j, k;
			if (zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.VecSiz
					== (zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId + 1)) {
				k = 0;
			} else {
				k = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId + 1;
			}

    		for (_i i = 0, j = k; i < zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.VecSiz; i++) {
    		    zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.p_VecIf[i].iov_base
					= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.p_VecIf[j].iov_base;
    		    zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitWrapVecIf.p_VecIf[i].iov_len;
					= zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.p_VecIf[j].iov_len;

    		    if ((zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf.VecSiz - 1) == j) {
    		        j = 0;
    		    } else {
    		        j++;
    		    }
    		}
		}
	}

	zCacheUpdateLimit = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId + (zPreLoadCacheSiz < zCnter) ? zPreLoadCacheSiz : zCnter;
	
    // 生成下一级缓存
    for (_i i = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId; i < zCacheUpdateLimit; i++) {
		zMem_Alloc(zpSubCacheMetaIf, zCacheMetaInfo, 1);

		zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
		zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
		zpSubCacheMetaIf->zCommitId = i - zCnter;
		zpSubCacheMetaIf->zFileId = -1;

    	zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubCacheMetaIf); // +
    }
}
void
zthread_update_cache(void *zpIf) {
// TEST:PASS
    _i zRepoId = *((_i *)(zpIf));

    pthread_rwlock_wrlock( &(zpRepoGlobIf[zRepoId].RwLock) );
    zupdate_commit_cache(zRepoId);
    pthread_rwlock_unlock( &(zpRepoGlobIf[zRepoId].RwLock) );
}

/*
 * 将文本格式的ipv4地址转换成二进制无符号整型(按网络字节序，即大端字节序)
 */
_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
// TEST:PASS
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit(inet_pton(AF_INET, zpStrAddr, &zIpv4Addr));
    return zIpv4Addr.s_addr;
}

/*
 * 客户端更新自身ipv4数据库文件
 */
void
zupdate_ipv4_db_self(_i zBaseFd) {
// TEST:PASS
    _ui zIpv4Addr;
    char *zpBuf;
    FILE *zpFileHandler;

    _i zFd;
    zCheck_Negative_Exit(zFd = openat(zBaseFd, zSelfIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = popen("ip addr | grep -oP '(\\d{1,3}\\.){3}\\d{1,3}' | grep -v 127", "r"));
    while (NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler))) {
        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (zSizeOf(_ui) != write(zFd, &zIpv4Addr, zSizeOf(_ui))) {
            zPrint_Err(0, NULL, "Write to $zSelfIpPath failed!");
            exit(1);
        }
    }

    fclose(zpFileHandler);
    close(zFd);
}

/*
 * 更新ipv4 地址缓存
 */
void
zupdate_ipv4_db_hash(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    zDeployResInfo *zpTmpIf;

    _i zFd[2] = {-9};
    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zpRepoGlobIf[zRepoId].TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zpRepoGlobIf[zRepoId].p_DpResList, zDeployResInfo, zpRepoGlobIf[zRepoId].TotalHost);  // 分配数组空间，用于顺序读取

    for (_i j = 0; j < zpRepoGlobIf[zRepoId].TotalHost; j++) {
        (zpRepoGlobIf[zRepoId].p_DpResList->hints)[3] = sizeof(zDeployResInfo) - sizeof(zpRepoGlobIf[zRepoId].p_DpResList->DeployState) - sizeof(zpRepoGlobIf[zRepoId].p_DpResList->p_next);  // hints[3] 用于提示前端回发多少字节的数据
        zpRepoGlobIf[zRepoId].p_DpResList[j].RepoId = zRepoId;  // 写入代码库索引值
        zpRepoGlobIf[zRepoId].p_DpResList[j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zpRepoGlobIf[zRepoId].p_DpResList[j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zpRepoGlobIf[zRepoId].p_DpResList->ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpRepoGlobIf[zRepoId].p_DpResHash[(zpRepoGlobIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpRepoGlobIf[zRepoId].p_DpResHash[(zpRepoGlobIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz] = &(zpRepoGlobIf[zRepoId].p_DpResList[j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zpRepoGlobIf[zRepoId].p_DpResList[j]);
        }
    }

    close(zFd[1]);
}

/*
 * 监控到ip数据文本文件变动，触发此函数执行二进制ip数据库更新，更新全员ip数据库
 */
void
zupdate_ipv4_db_all(_i zRepoId) {
// TEST:PASS
    FILE *zpFileHandler = NULL;
    char *zpBuf = NULL;
    _ui zIpv4Addr = 0;
    _i zFd[3] = {0};

    pthread_rwlock_wrlock(&(zpRepoGlobIf[zRepoId].RwLock));

    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zRepoId].RepoPath, O_RDONLY));

    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPathTxt, O_RDONLY));
    zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zAllIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = fdopen(zFd[1], "r"));
    zPCREInitInfo *zpPCREInitIf = zpcre_init("^(\\d{1,3}\\.){3}\\d{1,3}$");
    zPCRERetInfo *zpPCREResIf;
    for (_i i = 1; NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler)); i++) {
        zpPCREResIf = zpcre_match(zpPCREInitIf, zpBuf, 0);
        if (0 == zpPCREResIf->cnt) {
            zpcre_free_tmpsource(zpPCREResIf);
            zPrint_Time();
            fprintf(stderr, "\033[31;01m[%s]-[Line %d]: Invalid entry!\033[00m\n", zAllIpPath, i);
            exit(1);
        }
        zpcre_free_tmpsource(zpPCREResIf);

        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (sizeof(_ui) != write(zFd[2], &zIpv4Addr, sizeof(_ui))) {
            zPrint_Err(0, NULL, "Write to $zAllIpPath failed!");
            exit(1);
        }
    }
    zpcre_free_metasource(zpPCREInitIf);
    fclose(zpFileHandler);
    close(zFd[2]);
    close(zFd[1]);
    close(zFd[0]);

    // ipv4 数据文件更新后，立即更新对应的缓存中的列表与HASH
    zupdate_ipv4_db_hash(zRepoId);

    pthread_rwlock_unlock(&(zpRepoGlobIf[zRepoId].RwLock));
}

void
zthread_update_ipv4_db_all(void *zpIf) {
// TEST:PASS
    _i zRepoId = *((_i *)zpIf);
    zupdate_ipv4_db_all(zRepoId);
}
/*
 * 通用函数，调用外部程序或脚本文件执行相应的动作
 * 传入参数：
 * $1：代码库ID
 * $2：代码库绝对路径
 */
void
zthread_common_func(void *zpIf) {
    zObjInfo *zpObjIf = (zObjInfo *) zpIf;
    char zShellBuf[zCommonBufSiz];

    sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify %d %s %s",
        zpRepoGlobIf[zpObjIf->RepoId].RepoPath,
        zpObjIf->RepoId,
        zpRepoGlobIf[zpObjIf->RepoId].RepoPath,
        zpObjHash[zpObjIf->UpperWid]->path);

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}
