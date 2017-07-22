#ifndef _Z
    #include "../zmain.c"
#endif

#define zTypeConvert(zSrcObj, zTypeTo) ((zTypeTo)(zSrcObj))

/****************
 * 模块整体信息 *
 ****************/

// 列出提交或布署的版本代号列表
void
zlist_commit(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp) - sizeof(zIf.FileId) -sizeof(zIf.CommitId);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    pthread_rwlock_rdlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );
    zsendmsg(zSd, ((zCommitInfo *)(zpRepoGlobIf[zIf.RepoId].p_CommitVecIf[0]->p_vec[zIf.CommitId].iov_base))->p_SubObjVecIf, 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );

    shutdown(zSd, SHUT_RDWR);  // 若前端复用同一套接字则不需要关闭
}

// 列出差异文件路径名称列表
void
zlist_diff_files(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp) - sizeof(zIf.FileId);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (zCommitPreCacheSiz <= zIf.CommitId) {
        pthread_rwlock_wrlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );
        zfree_commit_source((zpRepoGlobIf[zIf.RepoId].p_VecIf[0]->p_vec)[zIf.CommitId].iov_base);  // 内部已检查 NULL
        zget_file_diff_info(zIf.RepoId, zIf.CommitId, -1);
        pthread_rwlock_unlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );
    }

    pthread_rwlock_rdlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );
    zsendmsg(zSd, ((zCodeInfo *)(zpRepoGlobIf[zIf.RepoId].p_VecIf[0]->p_vec[zIf.CommitId].iov_base))->p_SubObjVecIf, 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );

    shutdown(zSd, SHUT_RDWR);  // 若前端复用同一套接字则不需要关闭
}

// 打印当前版本缓存与CURRENT标签之间的指定文件的内容差异
void
zprint_diff_contents(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zCodeInfo *zpOneCommitIf, *zpOneFileIf;
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    pthread_rwlock_rdlock(&(zpRepoGlobIf[zIf.RepoId].RwLock));
    zpOneCommitIf = (zCodeInfo *) (zpRepoGlobIf[zIf.RepoId].p_VecIf[0]->p_vec[zIf.CommitId].iov_base);
    zpOneFileIf= (zCodeInfo *)(zpOneCommitIf->p_SubObjVecIf->p_vec[zIf.FileId].iov_base);
    zsendmsg(zSd, zpOneFileIf->p_SubObjVecIf, 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );

    shutdown(zSd, SHUT_RDWR);
}

// 列出最近zLogCacheSiz次或全部历史布署日志
void
zlist_log(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    _i zVecSiz;
    zVecInfo zVecIf;

    zDeployLogInfo zIf = { .RepoId = -1, };
    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (NULL == zppLogCacheVecIf[zIf.RepoId]) {
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    pthread_rwlock_rdlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );

    if ( 'l' == zIf.hints[0]){    // 默认直接直接回复预存的最近zLogCacheSiz次记录
        zVecIf.p_vec = zppSortedLogCacheVecIf[zIf.RepoId];
        zVecIf.VecSiz = zpLogCacheVecSiz[zIf.RepoId];
        zsendmsg(zSd, &zVecIf, 0, NULL);  // 按时间戳降序排列的缓存结果
    } else {  // 若前端请求列出所有历史记录，从日志文件中读取
        zDeployLogInfo *zpMetaLogIf;
        char *zpDpSig, *zpPathBuf, zShellBuf[zCommonBufSiz], *zpLineContent;
        struct stat zStatIf[2];
        FILE *zpFile;

        _i zFd[3];
        zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zIf.RepoId].RepoPath, O_RDONLY));
        zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zMetaLogPath, O_RDONLY));
        zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zSigLogPath, O_RDONLY));
        zCheck_Negative_Exit(fstat(zFd[1], &(zStatIf[0])));  // 获取日志属性
        zCheck_Negative_Exit(fstat(zFd[2], &(zStatIf[1])));  // 获取日志属性
        close(zFd[0]);

        zVecSiz = 2 * zStatIf[0].st_size / sizeof(zDeployLogInfo);  // 确定存储缓存区的大小
        struct iovec zVec[zVecSiz];

        zCheck_Null_Exit(zpMetaLogIf = (zDeployLogInfo *) mmap(NULL, zStatIf[0].st_size, PROT_READ, MAP_PRIVATE, zFd[1], 0));  // 将meta日志mmap至内存
        madvise(zpMetaLogIf, zStatIf[0].st_size, MADV_WILLNEED);  // 提示内核大量预读

        zCheck_Null_Exit(zpDpSig = (char *) mmap(NULL, zStatIf[1].st_size, PROT_READ, MAP_PRIVATE, zFd[2], 0));  // 将sig日志mmap至内存
        madvise(zpMetaLogIf, zStatIf[1].st_size, MADV_WILLNEED);  // 提示内核大量预读

        for (_i i = 0; i < zVecSiz; i++) {  // 拼装日志信息
            if (0 == i % 2) {
                zVec[i].iov_base = zpMetaLogIf + i / 2;
                zVec[i].iov_len = sizeof(zDeployLogInfo);
            } else {
                zMem_Alloc(zpPathBuf, char, (zpMetaLogIf + i / 2)->PathLen);
                sprintf(zShellBuf, "cd %s && git log %s -1 --name-only --format=", zpRepoGlobIf[zIf.RepoId].RepoPath, zpDpSig + (i / 2) * zBytes(41));
                zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));

                for (size_t zWrOffSet = 0; NULL != (zpLineContent = zget_one_line_from_FILE(zpFile));) {
                    zLen = strlen(zpLineContent);
                    zpLineContent[zLen] = '\0';
                    zpLineContent[zLen - 1] = '\n';
                    strcpy(zpPathBuf + zWrOffSet, zpLineContent);
                    zWrOffSet += 1 + zLen;
                }

                zVec[i].iov_base = zpPathBuf;
                zVec[i].iov_len = (zpMetaLogIf + i / 2)->PathLen;
            }
        }

        zVecIf.p_vec = zVec;
        zVecIf.VecSiz = zVecSiz;
        zsendmsg(zSd, &zVecIf, 0, NULL);    // 发送结果

        munmap(zpMetaLogIf, zStatIf[0].st_size);
        munmap(zpMetaLogIf, zStatIf[1].st_size);
        close(zFd[1]);
        close(zFd[2]);
    }

    pthread_rwlock_unlock(&(zpRepoGlobIf[zIf.RepoId].RwLock));
    shutdown(zSd, SHUT_RDWR);
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    char zShellBuf[128], *zpRes;
    FILE *zpFile;
    _i zFd[2];

    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));  // 获取当前sig日志文件属性
    close(zFd[0]);
    close(zFd[1]);

    // 将CURRENT标签的40位sig字符串追加写入.git_shadow/log/sig
    sprintf(zShellBuf, "cd %s && git log -1 CURRENT --format=%%H", zpRepoGlobIf[zRepoId].RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zpRes = zget_one_line_from_FILE(zpFile);

    if ( zBytes(41) != write(zpRepoGlobIf[zRepoId].LogFd, zppCURRENTsig[zRepoId], zBytes(41))) {
        zCheck_Negative_Exit(ftruncate(zpRepoGlobIf[zRepoId].LogFd, zStatIf.st_size));
        zPrint_Err(0, NULL, "Can't write to <.git_shadow/log/deploy/sig> !");
        exit(1);
    }
}

// 执行布署，目前仅支持单文件布署与全部布署两种模式（文件多选布署待实现）
void
zdeploy(void *zpIf) {
// TEST:PASS
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    _i zSd = *((_i *)zpIf);
    zRecvInfo zIf = { .RepoId = -1, };
    _i zLen = sizeof(zIf) - sizeof(zIf.HostIp);
    zCodeInfo *zpOneCommitIf, *zpOneFileIf;

    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if ('D' == zIf.hints[0]) {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -D", zpRepoGlobIf[zIf.RepoId].RepoPath);
    } else {
        zpOneCommitIf = (zCodeInfo *) (zpRepoGlobIf[zIf.RepoId].p_VecIf[0]->p_vec[zIf.CommitId].iov_base);
        zpOneFileIf= (zCodeInfo *)(zpOneCommitIf->p_SubObjVecIf->p_vec[zIf.FileId].iov_base);
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -d %s", zpRepoGlobIf[zIf.RepoId].RepoPath, zpOneFileIf->data);
    }

    pthread_rwlock_wrlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );  // 加锁，布署没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "shell 布署命令出错!");
    }

    _ui zSendBuf[zpRepoGlobIf[zIf.RepoId].TotalHost];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    do {
        zsleep(0.2);  // 每隔0.2秒向前端返回一次结果

        _i zUnReplyCnt = 0;  // 必须放在 do...while 循环内部
        for (_i i = 0; i < zpRepoGlobIf[zIf.RepoId].TotalHost; i++) {  // 登记尚未确认状态的客户端ip列表
            if (0 == zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->DeployState) {
                zSendBuf[zUnReplyCnt] = zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->ClientAddr;
                zUnReplyCnt++;
            }
        }

        zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);
    } while (zpRepoGlobIf[zIf.RepoId].ReplyCnt < zpRepoGlobIf[zIf.RepoId].TotalHost);  // 等待所有client端确认状态：前端人工标记＋后端自动返回
    zpRepoGlobIf[zIf.RepoId].ReplyCnt = 0;

    zwrite_log_and_update_cache(zIf.RepoId);  // 将本次布署信息写入日志

    for (_i i = 0; i < zpRepoGlobIf[zIf.RepoId].TotalHost; i++) {
        zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->DeployState = 0;  // 重置client状态，以便下次布署使用
    }

    pthread_rwlock_unlock(&(zpRepoGlobIf[zIf.RepoId].RwLock));  // 释放锁
    shutdown(zSd, SHUT_RDWR);
}

// 依据布署日志，撤销指定文件或整次提交，目前仅支持单文件撤销与全部整批次撤销两种模式（文件多选撤销待实现）
void
zrevoke(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zDeployLogInfo zIf = { .RepoId = -1, };
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char zCommitSigBuf[41];  // 存放40位的git commit sig 及 一个 '\0'

    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);
    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (zIf.index >= zLogCacheSiz) {
        zPrint_Err(0, NULL, "请求撤销的文件索引超出缓存范围!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    _i zFd[2];
    zCheck_Negative_Exit(zFd[0] = open(zpRepoGlobIf[zIf.RepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Return(pread(zFd[1], &zCommitSigBuf, zBytes(40), zBytes(40) * zIf.index),);
    zCommitSigBuf[40] = '\0';
    close(zFd[0]);
    close(zFd[1]);

    if ('R' == zIf.hints[0]) {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -R -i %s", zpRepoGlobIf[zIf.RepoId].RepoPath, zCommitSigBuf);
    } else {
        _i zPathIdInCache;  // 用于接收某个文件路径名称在路径列表中的行号（从1开始）

        if (zSizeOf(_i) > zrecv_nohang(zSd, &zPathIdInCache, sizeof(_i), 0, NULL)) {
            zPrint_Err(0, NULL, "接收到的数据不完整!");
            shutdown(zSd, SHUT_RDWR);
            return;
        }

        _i zOffSet = 0;
        for (_i i = 0; i < zPathIdInCache; i++) {
            zOffSet += 1 + strlen(zTypeConvert(zppLogCacheVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo*)->path + zOffSet);
        }

        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -r -i %s %s", zpRepoGlobIf[zIf.RepoId].RepoPath, zCommitSigBuf, zTypeConvert(zppLogCacheVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo*)->path + zOffSet);
    }

    pthread_rwlock_wrlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等

    if (0 != system(zShellBuf)) {  // 执行外部 shell 命令
    }

    _ui zSendBuf[zpRepoGlobIf[zIf.RepoId].TotalHost];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    _i zUnReplyCnt = 0;  // 尚未回复的目标主机计数
    do {
        zsleep(0.2);  // 每0.2秒统计一次结果，并发往前端

        for (_i i = 0; i < zpRepoGlobIf[zIf.RepoId].TotalHost; i++) {
            if (0 == zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->DeployState) {
            zSendBuf[zUnReplyCnt] = zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->ClientAddr;
            zUnReplyCnt++;
            }
        }

        zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);  // 向前端发送当前未成功的列表
    } while (zpRepoGlobIf[zIf.RepoId].ReplyCnt < zpRepoGlobIf[zIf.RepoId].TotalHost);  // 一直等待到所有client状态确认为止：前端人工确认＋后端自动确认
    zpRepoGlobIf[zIf.RepoId].ReplyCnt = 0;

    zwrite_log_and_update_cache(zIf.RepoId);  // 将本次布署信息写入日志

    for (_i i = 0; i < zpRepoGlobIf[zIf.RepoId].TotalHost; i++) {
        zpRepoGlobIf[zIf.RepoId].p_DpResHash[i]->DeployState = 0;  // 将本项目各主机状态重置为0
    }

    pthread_rwlock_unlock( &(zpRepoGlobIf[zIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

// 接收并更新对应的布署状态确认信息
void
zconfirm_deploy_state(void *zpIf) {
// TEST:PASS
    _i zSd = *((_i *)zpIf);
    zDeployResInfo zIf = { .RepoId = -1 };

    if ((zSizeOf(zIf) - zSizeOf(zIf.p_next)) > zrecv_nohang(zSd, &zIf, (zSizeOf(zIf) - zSizeOf(zIf.p_next)), 0, NULL)) {
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    // HASH 索引
    for (zDeployResInfo *zpTmp = zpRepoGlobIf[zIf.RepoId].p_DpResHash[zIf.ClientAddr % zDeployHashSiz]; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (zpTmp->ClientAddr == zIf.ClientAddr) {
            zpTmp->DeployState = 1;
            zpRepoGlobIf[zIf.RepoId].ReplyCnt++;
            shutdown(zSd, SHUT_RDWR);
            return;
        }
    }

    zPrint_Err(0, NULL, "不明来源的确认信息!");
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 从前端接收新的ipv4列表及其 md5 sig
 * 若接收的md5 sig 与本地文件不相等，则写入txt文件
 * 若相等，则不需要写入操作
 */
void
zupdate_ipv4_db_txt(void *zpIf) {
    char zRecvBuf[zCommonBufSiz], zPathBuf[zCommonBufSiz], *zpSigMD5, *zpWritePath;
    _i zRepoId, zFd, zRecvSiz, zSd = * ((_i *) zpIf);

    if ((zBytes(36) + zSizeOf(_i)) > recv(zSd, zRecvBuf, (zBytes(36) + zSizeOf(_i)), 0)) { // 接收4字节提示信息＋4字节代码库ID＋32节字MD5 sig
        zPrint_Err(0, NULL, "接收到的数据不完整!");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    zpWritePath = ('u' == zRecvBuf[0]) ? zMajorIpPathTxt : zAllIpPathTxt;
    zRepoId = (_i) zRecvBuf[zBytes(4)];

    if (0 > zRepoId || zRepoNum <= zRepoId) {
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        shutdown(zSd, SHUT_RDWR);
        return;
    }

    strcpy(zPathBuf, zpRepoGlobIf[zRepoId].RepoPath);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWritePath);
    if (NULL == (zpSigMD5 = zgenerate_file_sig_md5(zPathBuf))) {
        goto zMarkWrite;  // 文件不存在，跳转到写入环节
    }

    for (_i i = zBytes(4) + zSizeOf(_i); i < (zBytes(36) + zSizeOf(_i)); i++) {
        if (zRecvBuf[i] != zpSigMD5[i]) {
            goto zMarkWrite;  // MD5 sig 不同，跳转到写入环节
        }
    }
    shutdown(zSd, SHUT_RDWR);  // 若 MD5 sig 相同，无需写入
    return;

zMarkWrite:
    zCheck_Negative_Exit(zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600));
    zCheck_Negative_Return(zFd,);

    // 接收网络数据并同步写入文件
    while (0 < (zRecvSiz = recv(zSd, zRecvBuf, zCommonBufSiz, 0))) {
        if (zRecvSiz != write(zFd, zRecvBuf, zRecvSiz)) {
            zPrint_Err(errno, NULL, "Write ipv4 list to txt file failed!");
            exit(1);
        }
    }
    close(zFd);
    zCheck_Negative_Return(zRecvSiz,);

    // 检验文件新的 MD5 checksum
    if (0 != strcmp(zpSigMD5, zgenerate_file_sig_md5(zPathBuf))) {
        zsendto(zSd, "!", zBytes(1), 0, NULL);  //  若MD5 sig 不符，要求前端重发报文
        zPrint_Err(0, NULL, "ECS主机ip列表文件接收异常：MD5 检验失败!");
    }

    shutdown(zSd, SHUT_RDWR);
}

// 启动git_shadow服务器
void
zstart_server(void *zpIf) {
#define zMaxEvents 64
#define zServHashSiz 12
    // 如下部分定义服务接口
    zThreadPoolOps zNetServ[zServHashSiz] = {NULL};
    zNetServ[0] = ;
    zNetServ[1] = zlist_commit;
    zNetServ[2] = ;
    zNetServ[3] = ;
    zNetServ[4] = ;
    zNetServ[5] = ;
    zNetServ[6] = ;
    zNetServ[7] = ;
    zNetServ[8] = ;
    zNetServ[9] = ;
    zNetServ[10] = ;
    zNetServ[11] = ;

    // 如下部分配置 epoll 环境
    zNetServInfo *zpNetServIf = (zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv),);

    // 如下部分启动 epoll 监听服务
    for (_i zCmd = 0;;) {  // zCmd: 用于存放前端发送的操作指令
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return(zEvNum,);

        for (_i i = 0; i < zEvNum; i++, zCmd = 0) {
           if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
               zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
               zCheck_Negative_Return(zConnSd,);

               zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
               zEv.data.fd = zConnSd;
               zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv),);
            } else {
               if ((sizeof(_i) == zrecv_nohang(zEvents[i].data.fd, &zCmd, sizeof(_i), MSG_PEEK, NULL))
                   && (zServHashSiz > zGetCmdId(zCmd)) && (0 <= zCmd) && (NULL != zNetServ[zCmd])) {
                   zAdd_To_Thread_Pool(zNetServ[zCmd], &(zEvents[i].data.fd));
               }
            }
        }
    }
#undef zMaxEvents
#undef zServHashSiz
}

// 客户端用于向中控机发送状态确认信息
void
zclient_reply(char *zpHost, char *zpPort) {
    zDeployResInfo zDpResIf = {.hints = {'c',}};  // confirm:标识这是一条状态确认信息
    _i zFd, zSd, zResLen;

    // 读取版本库ID
    zCheck_Negative_Exit(zFd = open(zRepoIdPath, O_RDONLY));
    zCheck_Negative_Exit(zFd);
    zCheck_Negative_Exit(read(zFd, &(zDpResIf.RepoId), sizeof(_i)));
    close(zFd);

    if (-1== (zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV))) {  // 以点分格式的ipv4地址连接服务端
        zPrint_Err(0, NULL, "Connect to server failed!");
        exit(1);
    }

    zCheck_Negative_Exit(zFd = open(zSelfIpPath, O_RDONLY));  // 读取本机的所有非回环ip地址，依次发送状态确认信息至服务端
    zCheck_Negative_Return(zFd,);

    _ui zIpv4Bin;
    while (0 != (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
        zCheck_Negative_Return(zResLen,);
        zDpResIf.ClientAddr = zIpv4Bin;  // 标识本机身份：ipv4地址
        if ((sizeof(zDeployResInfo) - sizeof(zDpResIf.p_next)) != zsendto(zSd, &zDpResIf, sizeof(zDeployResInfo) - sizeof(zDpResIf.p_next), 0, NULL)) {
            zPrint_Err(0, NULL, "布署状态确认信息发送失败!");
        }
    }

    close(zFd);
    shutdown(zSd, SHUT_RDWR);
}

#undef zTypeConvert
