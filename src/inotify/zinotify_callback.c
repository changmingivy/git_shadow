#ifndef _Z
    #include "../zmain.c"
#endif

/****************
 * UPDATE CACHE *
 ****************/
// 此部分的多个函数用于生成缓存：差异文件列表、每个文件的差异内容、最近的布署日志信息

void
zupdate_sig_cache(void *zpRepoId) {
// TEST:PASS
    _i zRepoId = *((_i *)zpRepoId);
    char zShellBuf[zCommonBufSiz], *zpRes;
    FILE *zpShellRetHandler;

    /* 以下部分更新所属代码库的CURRENT SHA1 sig值 */
    sprintf(zShellBuf, "cd %s && git log --format=%%H -n 1 CURRENT", zppRepoPathList[zRepoId]);
    zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));
    zCheck_Null_Exit(zpRes = zget_one_line_from_FILE(zpShellRetHandler));  // 读取CURRENT分支的SHA1 sig值

    if (zBytes(40) > strlen(zpRes)) {
        zPrint_Err(0, NULL, "Invalid CURRENT sig!!!");
        exit(1);
    }

    if (NULL == zppCURRENTsig[zRepoId]) {
        zMem_Alloc(zppCURRENTsig[zRepoId], char, zBytes(41));  // 含 '\0'
    }

    strncpy(zppCURRENTsig[zRepoId], zpRes, zBytes(41));  // 更新对应代码库的最新CURRENT 分支SHA1 sig
    pclose(zpShellRetHandler);
}

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
    zCheck_Negative_Exit(zFd[0] = open(zppRepoPathList[zpLogIf->RepoId], O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(pread(zFd[1], zCommitSig, zBytes(41), zBytes(41) * zpLogIf->index));
    close(zFd[0]);
    close(zFd[1]);

    sprintf(zShellBuf, "cd %s && git log %s --name-only --format=", zppRepoPathList[zpLogIf->RepoId], zCommitSig);
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
    for (_i i = 0, j = zpLogCacheQueueHeadIndex[zpLogIf->RepoId]; i < zpCacheVecSiz[zpLogIf->RepoId]; i++) {
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_base = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_base;
        zppSortedLogCacheVecIf[zpLogIf->RepoId][i].iov_len = zppLogCacheVecIf[zpLogIf->RepoId][j].iov_len;

        if (0 == j) {
            j = zpCacheVecSiz[zpLogIf->RepoId] - 1;
        } else {
            j--;
        }
    }
}

//#define zVersionHashSiz 256
//typedef struct {
//	struct iovec *p_vec;
//	_i VecSiz;
//} zVecInfo;
//
//typedef struct {
//	zVecInfo *p_DiffContentVecIf;  // 指向具体的文件差异内容，按行存储
//	_i FileId;
//    _i len;  // 文件路径长度，提供给前端使用
//    char data[];  // 相对于代码库的路径
//} zFileInfo;
//
//typedef struct {
//	zVecInfo *p_FileListVecIf;
//	_i CommitId;
//    _i len;
//	char CommitSig[];
//} zCodeVersionInfo;
//
//zVecInfo **zppRepoIf;  // 代码库信息数组，每个成员包含一个大小为 zVersionHashSiz 的 commit HASH，用于缓存代码版本信息

zVecInfo *
zget_diff_content(_i zRepoId, _i zCommitId, _i zFileId) {
    struct iovec *zpLineContentVecIf;
	_i zVecSiz = 0, zAllocSiz = 128, zDataLen = 0;

    FILE *zpShellRetHandler;
    char *zpRes, *zpLineContent, zShellBuf[zCommonBufSiz];

	zMem_Alloc(zpLineContentVecIf, struct iovec, zAllocSiz);

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git diff %s CURRENT -- %s", zppRepoPathList[zRepoId],
			((zCodeVersionInfo *)(((zppRepoIf[zRepoId])->p_vec)[zCommitId].iov_base))->data,
			((zFileInfo *)((zCodeVersionInfo *)(((zppRepoIf[zRepoId])->p_vec)[zCommitId].iov_base))->p_FileListVecIf->p_vec)[zFileId].data);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (_i i = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); i++, zVecSiz++) {
		if (i > zAllocSiz) {
			zAllocSiz *= 2;
			zMem_Re_Alloc(zpLineContentVecIf, struct iovec, zAllocSiz);
		}

		zDataLen = 1 + strlen(zpRes);
        zMem_Alloc(zpLineContentVecIf[i].iov_base, char, zDataLen);

		zpLineContentVecIf[i].iov_len = zDataLen;
        strcpy(zpLineContentVecIf[i].iov_base, zpRes);
	}

	pclose(zpShellRetHandler);
	zMem_Re_Alloc(zpLineContentVecIf, struct iovec, zVecSiz);

	static zVecInfo zVecIf;
	zVecIf.p_vec = zpLineContentVecIf;
	zVecIf.VecSiz = zVecSiz;
	return &zVecIf;
}

/*
 * 返回差异文件列表，以 iovec 数组形式存储
 */
zVecInfo *
zget_file_list(_i zRepoId, _i zCommitId) {
    struct iovec *zpFileListVecIf;
	zFileInfo *zpFileIf;
	_i zVecSiz = 0, zAllocSiz = 128, zDataLen = 0;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[zCommonBufSiz];

	zMem_Alloc(zpFileListVecIf, struct iovec, zAllocSiz);

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s git diff --name-only %s CURRENT ", zppRepoPathList[zRepoId],
			((zCodeVersionInfo *)(((zppRepoIf[zRepoId])->p_vec)[zCommitId].iov_base))->data);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (_i i = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); i++, zVecSiz++) {
		if (i > zAllocSiz) {
			zAllocSiz *= 2;
			zMem_Re_Alloc(zpFileListVecIf, struct iovec, zAllocSiz);
		}

            zDataLen = 1 + strlen(zpRes) + sizeof(zFileInfo);
            zCheck_Null_Exit( zpFileIf = malloc(zDataLen) );

            zpFileIf->FileId = i;
			zpFileIf->len = zDataLen;
			zpFileIf->p_DiffContentVecIf = zget_diff_content(zRepoId, zCommitId, zpFileIf->FileId);
            strcpy(zpFileIf->data, zpRes);
			
            zpFileListVecIf[i].iov_base = zpFileIf;
	}

	pclose(zpShellRetHandler);
	zMem_Re_Alloc(zpFileListVecIf, struct iovec, zVecSiz);

	static zVecInfo zVecIf;
	zVecIf.p_vec = zpFileListVecIf;
	zVecIf.VecSiz = zVecSiz;
	return &zVecIf;
}

//void
//zfree_version_source(zVecInfo *zpIf) {
//	_i i, j;
//	zFileInfo *zpToFree[2];
//	for (i = 0; i < zpIf->p_FileListVecIf->VecSiz; i++) {
//		zpToFree[0] = (zFileInfo *) (zpIf->p_FileListVecIf->p_vec[i].iov_base);
//		for (j = 0; j < zpToFree[0]->p_DiffContentVecIf->VecSiz; j++) {
//
//		}
//		free(zpToFree[0]);
//	}
//	free(zpIf->p_FileListVecIf);
//	zpIf->p_FileListVecIf = NULL;
//}

zVecInfo *
zupdate_version_list(_i zRepoId, _i zInitMark) {
    static struct iovec zVersionVecIf[zVersionHashSiz];
	zCodeVersionInfo *zpVersionIf;
	_i zDataLen = 0;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[zCommonBufSiz], *zpInitOption;

	zpInitOption = (0 == zInitMark) ? "" : "-1";

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s git log %s --format=%%H\\0%%ct", zppRepoPathList[zRepoId], zpInitOption);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (_i i = zpRepoHashHeadId[zRepoId] + 1;  i < zVersionHashSiz; i++) {
		if (NULL == (zpRes = zget_one_line_from_FILE(zpShellRetHandler))) {
			break;
		}
		
		if (zVersionHashSiz == i) {
			i = 0;
		}

//		if (NULL != zppRepoIf[zRepoId][i].p_vec) {
//			zfree_version_source(&zppRepoIf[zRepoId][i]);
//		}

        zDataLen = 1 + strlen(zpRes) + sizeof(zCodeVersionInfo);
        zCheck_Null_Exit( zpVersionIf= malloc(zDataLen) );

        zpVersionIf->CommitId = i;
		zpVersionIf->len = zDataLen;
		zpVersionIf->p_FileListVecIf= zget_file_list(zRepoId, zpVersionIf->CommitId);
        strcpy(zpVersionIf->data, zpRes);
		
        zVersionVecIf[i].iov_base = zpVersionIf;
	}

	pclose(zpShellRetHandler);

	static zVecInfo zVecIf;
	zVecIf.p_vec = zVersionVecIf;
	zVecIf.VecSiz = zVersionHashSiz;
	return &zVecIf;
}

void
zupdate_diff_cache(_i zRepoId) {
// TEST:PASS
    pthread_rwlock_wrlock( &(zpRWLock[zRepoId]) );  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等

    // 首先释放掉老缓存的内存空间
    if (NULL != zppCacheVecIf[zRepoId]) {
        for (_i i = 0; i < zpCacheVecSiz[zRepoId]; i++) {
            for (_i j = 0; j < ((zFileDiffInfo *)(zppCacheVecIf[zRepoId][i].iov_base))->VecSiz; j++) {
                free((((zFileDiffInfo *)(zppCacheVecIf[zRepoId][i].iov_base))->p_DiffContent[j]).iov_base);
            }
            free(((zFileDiffInfo *)(zppCacheVecIf[zRepoId][i].iov_base))->p_DiffContent);
            free(zppCacheVecIf[zRepoId][i].iov_base);
        }
        free(zppCacheVecIf[zRepoId]);
    }

    // 如下开始创建新的缓存
    _i zNewVersion = (_i)time(NULL);  // 以时间戳充当缓存版本号
    struct iovec *zpNewCacheVec[2];  // 维持2个iovec数据，一个用于缓存文件列表，另一个按行缓存每一个文件差异信息

    FILE *zpShellRetHandler[2];  // 执行SHELL命令，读取此句柄获取返回信息
    _i zDiffFileNum, zDiffLineNum, zLen;  // 差异文件总数

    char *zpRes[2];  // 存储从命令行返回的原始文本信息
    char zShellBuf[2][zCommonBufSiz];  // 存储命令行字符串

    zFileDiffInfo *zpIf;

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf[0], "cd %s && git diff --name-only HEAD CURRENT | wc -l && git diff --name-only HEAD CURRENT ", zppRepoPathList[zRepoId]);
    zCheck_Null_Exit(zpShellRetHandler[0] = popen(zShellBuf[0], "r"));

    /* 以下部分更新差异文件列表及详情缓存 */
    if (NULL == (zpRes[0] = zget_one_line_from_FILE(zpShellRetHandler[0]))) {  // 第一行返回的是文件总数
        pclose(zpShellRetHandler[0]);
        return;
    } else {
        if (0 == (zDiffFileNum = strtol(zpRes[0], NULL, 10))) {
            pclose(zpShellRetHandler[0]);
            return;
        }

        zMem_Alloc(zpNewCacheVec[0], struct iovec, zDiffFileNum);   // 为存储文件路径列表的iovec[0]分配空间
        zpCacheVecSiz[zRepoId] = zDiffFileNum;  // 更新对应代码库的差异文件数量（更新到全局变量）

        for (_i i = 0; i < zDiffFileNum; i++) {
            zpRes[0] =zget_one_line_from_FILE(zpShellRetHandler[0]);
            zLen = 1 + strlen(zpRes[0]) + sizeof(zFileDiffInfo);

            zCheck_Null_Exit(zpIf = malloc(zLen));
            zpIf->hints[0] = sizeof(zFileDiffInfo) - sizeof(zpIf->PathLen);
            zpIf->hints[3] = sizeof(zFileDiffInfo) - sizeof(zpIf->PathLen) - sizeof(zpIf->p_DiffContent) - sizeof(zpIf->VecSiz);
            zpIf->CacheVersion = zNewVersion;
            zpIf->RepoId= zRepoId;
            zpIf->FileIndex = i;
            strcpy(zpIf->path, zpRes[0]);

            zpNewCacheVec[0][i].iov_base = zpIf;
            zpNewCacheVec[0][i].iov_len = zLen;

            // 必须在shell命令中切换到正确的工作路径
            sprintf(zShellBuf[1], "cd %s && git diff HEAD CURRENT -- %s | wc -l && git diff HEAD CURRENT -- %s", zppRepoPathList[zRepoId], zpRes[0], zpRes[0]);
            zCheck_Null_Exit(zpShellRetHandler[1] = popen(zShellBuf[1], "r"));

            zCheck_Null_Exit(zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1]));  // 读出差异行总数
            zDiffLineNum = strtol(zpRes[1], NULL, 10);
            zpIf->VecSiz = zDiffLineNum;  // 填充文件内容差别行数

            zMem_Alloc(zpNewCacheVec[1], struct iovec, zDiffLineNum);  // 为每个文件的详细差异内容分配iovec[1]分配空间
            zpIf->p_DiffContent = zpNewCacheVec[1];  // 填充文件内容差别详情

            for (_i j = 0; NULL != (zpRes[1] =zget_one_line_from_FILE(zpShellRetHandler[1])); j++) {
                zLen = 1 + strlen(zpRes[1]);
                zMem_Alloc(zpNewCacheVec[1][j].iov_base, char, zLen);
                strcpy(zpNewCacheVec[1][j].iov_base, zpRes[1]);

                zpNewCacheVec[1][j].iov_len = zLen;
            }
            pclose(zpShellRetHandler[1]);
        }
        pclose(zpShellRetHandler[0]);
    }

    zppCacheVecIf[zRepoId] = zpNewCacheVec[0]; // 更新全局变量

    pthread_rwlock_unlock( &(zpRWLock[zRepoId]) );
}

void
zthread_update_diff_cache(void *zpIf) {
// TEST:PASS
    _i zRepoId = ((zObjInfo *)zpIf)->RepoId;
    zupdate_diff_cache(zRepoId);
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
    zCheck_Negative_Exit(zFd[0] = open(zppRepoPathList[zRepoId], O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zpTotalHost[zRepoId] = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zppDpResList[zRepoId], zDeployResInfo, zpTotalHost[zRepoId]);  // 分配数组空间，用于顺序读取
    zMem_C_Alloc(zpppDpResHash[zRepoId], zDeployResInfo *, zDeployHashSiz);  // 对应的 HASH 索引,用于快速定位写入
    for (_i j = 0; j < zpTotalHost[zRepoId]; j++) {
        (zppDpResList[zRepoId][j].hints)[3] = sizeof(zDeployResInfo) - sizeof(zppDpResList[zRepoId][j].DeployState) - sizeof(zppDpResList[zRepoId][j].p_next);  // hints[3] 用于提示前端回发多少字节的数据
        zppDpResList[zRepoId][j].RepoId = zRepoId;  // 写入代码库索引值
        zppDpResList[zRepoId][j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zppDpResList[zRepoId][j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zppDpResList[zRepoId][j].ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpppDpResHash[zRepoId][(zppDpResList[zRepoId][j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpppDpResHash[zRepoId][(zppDpResList[zRepoId][j].ClientAddr) % zDeployHashSiz] = &(zppDpResList[zRepoId][j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zppDpResList[zRepoId][j]);
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

    pthread_rwlock_wrlock(&(zpRWLock[zRepoId]));

    zCheck_Negative_Exit(zFd[0] = open(zppRepoPathList[zRepoId], O_RDONLY));

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

    pthread_rwlock_unlock(&(zpRWLock[zRepoId]));
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
            zppRepoPathList[zpObjIf->RepoId],
            zpObjIf->RepoId,
            zppRepoPathList[zpObjIf->RepoId],
            zpObjHash[zpObjIf->UpperWid]->path);

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}
