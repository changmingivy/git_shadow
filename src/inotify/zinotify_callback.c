#ifndef _Z
    #include "../zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
#define zGet_SendIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_VecIf[zSelfId].iov_base)
#define zGet_SubVecWrapIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId]->p_SubVecWrapIf)
#define zGet_NativeDataPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].data)

#define zGet_OneCommitSendIfPtr(zpTopVecWrapIf, zCommitId) ((char *)zGet_SendIfPtr(zpTopVecWrapIf, zCommitId))
#define zGet_OneFileSendIfPtr(zpTopVecWrapIf, zCommitId, zFileId) zGet_SendIfPtr(zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCommitId), zFileId)
#define zGet_OneCommitSigPtr(zpTopVecWrapIf, zCommitId) zGet_NativeDataPtr(zpTopVecWrapIf, zCommitId)

#define zIsCommitCacheType 0
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
zgenerate_cache(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpCurVecWrapIf, *zpOldVecWrapIf;

	zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

	struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, zCnter;

	struct zCacheMetaInfo *zpSubCacheMetaIf;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128], zLogPathBuf[128];

	if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf;

    	sprintf(zShellBuf, "cd %s && git log --format=%%H%%ct", zpRepoGlobIf[zRepoId].RepoPath); // 必须在shell命令中切换到正确的工作路径
	} else {
		zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].DeployWrapVecIf;

		strcpy(zLogPathBuf, zpRepoGlobIf[zRepoId].RepoPath);
		strcat(zLogPathBuf, "/");
		strcat(zLogPathBuf, zSigLogPath);
		sprintf(zShellBuf, "cat %s", zLogPathBuf);
	}
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
			zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter) = NULL;
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

/*
 * 当监测到有新的代码提交时，为新版本代码生成缓存
 */
void
zupdate_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
	zCheck_Null_Exit(zpIf);
#endif
	struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf, *zpCurVecWrapIf, *zpOldVecWrapIf;

	struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, *zpHeadId;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128];

	zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;
	zpTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitWrapVecIf;
	zpSortedTopVecWrapIf = zpRepoGlobIf[zpCacheMetaIf->RepoId].SortedCommitVecIfCommitWrapVecIf;

	zpHeadId = &(zpRepoGlobIf[zpCacheMetaIf->RepoId].CommitCacheQueueNextId)

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log -1 --format=%%H%%ct", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    zpRes = zget_one_line_from_FILE(zpShellRetHandler);
    pclose(zpShellRetHandler);

    zSendDataLen = 2 * sizeof(zpRepoGlobIf[zRepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
	zVecDatalen = zSendDataLen + sizeof(zSendInfo);

    zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

    zpCommitSendIf->SelfId = *zpHeadId;
    zpCommitSendIf->DataLen = zSendDataLen;
    zpCommitSendIf->data = zpRepoGlobIf[zRepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
	zpCommitSendIf->data + sizeof(zpRepoGlobIf[zRepoId].CacheId) = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

	if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)) {
		zMem_Alloc(zpOldVecWrapIf, zVecWrapInfo, 1);

		zpOldVecWrapIf.p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_RefDataIf;
		zpOldVecWrapIf.p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_VecIf;
		zpOldVecWrapIf.VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->VecSiz;

		zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
		zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId) = NULL;
	}

	zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_base = zpCommitSendIf;
	zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_len = zVecDatalen;

	zpRes[zBytes(40)] = '\0';
    zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
    strcpy(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].data, zpRes);

	if (zCacheSiz > zpTopVecWrapIf.VecSiz) {
		zpTopVecWrapIf.VecSiz++;
		zpSortedTopVecWrapIf.VecSiz++;
	}

	// 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
	for (_i i = 0, j = *zpHeadId; i < zpTopVecWrapIf->VecSiz; i++) {
	    zpSortedTopVecWrapIf->p_VecIf[i].iov_base = zpTopVecWrapIf->p_VecIf[j].iov_base;
	    zpSortedTopVecWrapIf->p_VecIf[i].iov_len = zpTopVecWrapIf->p_VecIf[j].iov_len;

	    if ((zpTopVecWrapIf.VecSiz - 1) == j) {
	        j = 0;
	    } else {
	        j++;
	    }
	}

    /* 生成下一级缓存 */
	zMem_Alloc(zpSubCacheMetaIf, zCacheMetaInfo, 1);

	zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
	zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
	zpSubCacheMetaIf->zCommitId = *zpHeadId;
	zpSubCacheMetaIf->zFileId = -1;

    zget_file_list_and_diff_content(zpSubCacheMetaIf);

	/* 更新队列下一次将写入的位置的索引 */
	if (0 == *zpHeadId) {
		*zpHeadId = zCacheSiz -1;
	} else {
		(*zpHeadId)--;
	}
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
