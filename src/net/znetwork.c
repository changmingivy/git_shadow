#ifndef _Z
    #include "../zmain.c"
#endif

#define zTypeConvert(zSrcObj, zTypeTo) ((zTypeTo)(zSrcObj))

/****************
 * 模块整体信息 *
 ****************/

/*
 * git_shadow充当TCP服务器角色，接收前端请求与后端主机信息反馈，尽可能使用缓存响应各类请求
 *
 * 对接规则：
 *         [OpsMark(l/d/D/R)000]+[meta]+[data]
 *
 * 代号含义:
 *         p:显示差异文件路径名称列表
 *         P:显示单个文件内容的详细差异信息
 *         d:布署某次commit的单个文件
 *         D:布署某次commit的所有文件
 *         l:打印最近十次布署日志
 *         L:打印所有历史布署日志
 *         r:撤销某次提交的单个文件的更改，只来自前端
 *         R:撤销某次提交的所有更改，只来自前端
 *         c:状态确认，后端主机返回
 *         C:状态确认，前端返回
 *         u:更新major ECS列表
 *         U:更新all ECS列表
 */

// 列出差异文件路径名称列表
void
zlist_diff_files(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zFileDiffInfo zIf = { .RepoId = -1, };

    if (zBytes(8) > zrecv_nohang(zSd, &zIf, sizeof(zFileDiffInfo), 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Recv data failed!");
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    pthread_rwlock_rdlock( &(zpRWLock[zIf.RepoId]) );
    if (NULL == zppCacheVecIf[zIf.RepoId]) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);
        return;
    }

    zsendmsg(zSd, zppCacheVecIf[zIf.RepoId], zpCacheVecSiz[zIf.RepoId], 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpRWLock[zIf.RepoId]) );
    shutdown(zSd, SHUT_RDWR);  // 若前端复用同一套接字则不需要关闭
}

// 打印当前版本缓存与CURRENT标签之间的指定文件的内容差异
void
zprint_diff_contents(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zFileDiffInfo zIf = { .RepoId = -1, };

    if (zBytes(20) > zrecv_nohang(zSd, &zIf, sizeof(zFileDiffInfo), 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Recv data failed!");
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    pthread_rwlock_rdlock(&(zpRWLock[zIf.RepoId]));
    if (NULL == zppCacheVecIf[zIf.RepoId]) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);
        return;
    }
    if (zIf.CacheVersion == ( (zFileDiffInfo *) (zppCacheVecIf[zIf.RepoId][0].iov_base))->CacheVersion ) {
        zsendmsg(zSd,
                zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->p_DiffContent,
                zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->VecSiz,
                0,
                NULL);  // 直接从缓存中提取
    } else {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若缓存版本不一致，要求前端重发报文
    }
    pthread_rwlock_unlock( &(zpRWLock[zIf.RepoId]) );
    shutdown(zSd, SHUT_RDWR);
}

// 列出最近zPreLoadLogSiz次或全部历史布署日志
void
zlist_log(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zDeployLogInfo zIf = { .RepoId = -1, };

    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);
    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Recv data failed!");
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    pthread_rwlock_rdlock( &(zpRWLock[zIf.RepoId]) );
    if (NULL == zppPreLoadLogVecIf[zIf.RepoId]) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);
        return;
    }
    _i zVecSiz;
    if ( 'l' == zIf.hints[0]){    // 默认直接直接回复预存的最近zPreLoadLogSiz次记录
        zsendmsg(zSd, zppPreLoadLogVecIf[zIf.RepoId], zpPreLoadLogVecSiz[zIf.RepoId], 0, NULL);
    } else {  // 若前端请求列出所有历史记录，从日志文件中读取
        struct stat zStatBufIf;
        zDeployLogInfo *zpMetaLogIf, *zpTmpIf;
        zCheck_Negative_Return(fstat(zpLogFd[0][zIf.RepoId], &(zStatBufIf)),);  // 获取日志属性

        zVecSiz = 2 * zStatBufIf.st_size / sizeof(zDeployLogInfo);  // 确定存储缓存区的大小
        struct iovec zVec[zVecSiz];

        zpMetaLogIf = (zDeployLogInfo *) mmap(NULL, zStatBufIf.st_size, PROT_READ, MAP_PRIVATE, zpLogFd[0][zIf.RepoId], 0);  // 将meta日志mmap至内存
        zCheck_Null_Return(zpMetaLogIf,);
        madvise(zpMetaLogIf, zStatBufIf.st_size, MADV_WILLNEED);  // 提示内核大量预读

        zpTmpIf = zpMetaLogIf + zStatBufIf.st_size / sizeof(zDeployLogInfo) - 1;
        _ul zDataLogSiz = zpTmpIf->offset + zpTmpIf->PathLen;  // 根据meta日志属性确认data日志偏移量
        char *zpDataLog = (char *) mmap(NULL, zDataLogSiz, PROT_READ, MAP_PRIVATE, zpLogFd[1][zIf.RepoId], 0);  // 将data日志mmap至内存
        zCheck_Null_Return(zpDataLog,);
        madvise(zpDataLog, zDataLogSiz, MADV_WILLNEED);  // 提示内核大量预读

        for (_i i = 0; i < zVecSiz; i++) {  // 拼装日志信息
            if (0 == i % 2) {
                zVec[i].iov_base = zpMetaLogIf + i / 2;
                zVec[i].iov_len = sizeof(zDeployLogInfo);
            } else {
                zVec[i].iov_base = zpDataLog + (zpMetaLogIf + i / 2)->offset;
                zVec[i].iov_len = (zpMetaLogIf + i / 2)->PathLen;
            }
        }
        zsendmsg(zSd, zVec, zVecSiz, 0, NULL);    // 发送结果
        munmap(zpMetaLogIf, zStatBufIf.st_size);  // 解除mmap
        munmap(zpDataLog, zDataLogSiz);
    }
    pthread_rwlock_unlock(&(zpRWLock[zIf.RepoId]));
    shutdown(zSd, SHUT_RDWR);
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId, char *zpLogContents, _i zLogSiz) {
    // write to .git_shadow/log/meta
    struct stat zStatBufIf;
    zCheck_Negative_Return( fstat(zpLogFd[0][zRepoId], &zStatBufIf), );  // 获取当前日志文件属性

    zDeployLogInfo zIf;
    if (0 == zStatBufIf.st_size) {
        zIf.index = 0;
    } else {
        zCheck_Negative_Return(
                pread(zpLogFd[0][zRepoId], &zIf, sizeof(zDeployLogInfo), zStatBufIf.st_size - sizeof(zDeployLogInfo)),
                );  // 读出前一个记录的信息
    }

    zIf.hints[0] = sizeof(zIf) - sizeof(zIf.PathLen);
    zIf.hints[1] = sizeof(zIf) - sizeof(zIf.PathLen) -sizeof(zIf.TimeStamp);
    zIf.hints[3] = zIf.hints[1];  // 需要前端回发的数据总长度

    zIf.RepoId = zRepoId;  // 代码库ID相同
    zIf.index += 1;  // 布署索引偏移量增加1(即：顺序记录布署批次ID)，用于从sig日志文件中快整定位对应的commit签名
    zIf.offset += zLogSiz;  // data日志中对应的文件路径名称位置偏移量
    zIf.TimeStamp = time(NULL);  // 日志时间戳(1900至今的秒数)
    zIf.PathLen= zLogSiz;  // 本次布署的文件路径名称长度

    // 其本信息写入.git_shadow/log/meta
    if (sizeof(zDeployLogInfo) != write(zpLogFd[0][zRepoId], &zIf, sizeof(zDeployLogInfo))) {
        zPrint_Err(0, NULL, "Can't write to log/meta!");
        exit(1);
    }
    // 将本次布署的文件路径名称写入.git_shadow/log/data尾部
    if (zLogSiz != write(zpLogFd[1][zRepoId], zpLogContents, zLogSiz)) {
        zPrint_Err(0, NULL, "Can't write to log.data!");
        exit(1);
    }
    // 将本次布署之前的CURRENT标签的40位sig字符串追加写入.git_shadow/log/sig
    if ( 40 != write(zpLogFd[2][zRepoId], zppCurTagSig[zRepoId], 40)) {
        zPrint_Err(0, NULL, "Can't write to log.sig!");
        exit(1);
    }
}

// 执行布署
void
zdeploy(void *zpIf) {
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char *zpLogContents;   // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
    _i zSd = *((_i *)zpIf);
    _i zLogSiz;
    zFileDiffInfo zIf = { .RepoId = -1, };

    _i zLen = zSizeOf(zFileDiffInfo) - zSizeOf(zIf.PathLen) - zSizeOf(zIf.p_DiffContent) - zSizeOf(zIf.VecSiz);
    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Recv data failed!");
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    if (zIf.CacheVersion == ((zFileDiffInfo *) (zppCacheVecIf[zIf.RepoId]->iov_base))->CacheVersion) {  // 确认缓存版本是否一致
        if ('D' == zIf.hints[0]) {
            sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -D", zppRepoPathList[zIf.RepoId]);
            zpLogContents = "ALL";
            zLogSiz = zBytes(4);
        } else {
            sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -d %s",
                    zppRepoPathList[zIf.RepoId],
                    zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->path);
            zpLogContents = zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->path;
            zLogSiz = zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->PathLen;
        }

        pthread_rwlock_wrlock( &(zpRWLock[zIf.RepoId]) );  // 加锁，布署没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
        _ui zSendBuf[zpTotalHost[zIf.RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
        _i zUnReplyCnt = 0;

        if (0 != system(zShellBuf)) { goto zMark; }

        do {
            zsleep(0.2);  // 每隔0.2秒向前端返回一次结果

            for (_i i = 0; i < zpTotalHost[zIf.RepoId]; i++) {  // 登记尚未确认状态的客户端ip列表
                if (0 == zppDpResList[zIf.RepoId][i].DeployState) {
                    zSendBuf[zUnReplyCnt] = zppDpResList[zIf.RepoId][i].ClientAddr;
                    zUnReplyCnt++;
                }
            }

            zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);
        } while (zpReplyCnt[zIf.RepoId] < zpTotalHost[zIf.RepoId]);  // 等待所有client端确认状态：前端人工标记＋后端自动返回
        zpReplyCnt[zIf.RepoId] = 0;

        zwrite_log(zIf.RepoId, zpLogContents, zLogSiz);  // 将本次布署信息写入日志

        for (_i i = 0; i < zpTotalHost[zIf.RepoId]; i++) {
            zppDpResList[zIf.RepoId][i].DeployState = 0;  // 重置client状态，以便下次布署使用
        }

        pthread_rwlock_unlock(&(zpRWLock[zIf.RepoId]));  // 释放锁
    } else {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  // 若缓存版本不一致，向前端发送“!”标识，要求重发报文
    }

zMark:
    shutdown(zSd, SHUT_RDWR);
}

// 依据布署日志，撤销指定文件或整次提交
void
zrevoke(void *zpIf){
    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char *zpLogContents;  // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
    _i zSd = *((_i *)zpIf);
    _i zLogSiz;
    zDeployLogInfo zIf = { .RepoId = -1, };

    _i zLen = zSizeOf(zIf) - zSizeOf(zIf.PathLen) -zSizeOf(zIf.TimeStamp);
    if (zLen > zrecv_nohang(zSd, &zIf, zLen, 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Recv data failed!");
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    if (zIf.index >= zPreLoadLogSiz) {
        zsendto(zSd, "-1", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    char zPathBuf[zTypeConvert(zppPreLoadLogVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo*)->PathLen];  // 存放待撤销的目标文件路径
    char zCommitSigBuf[41];  // 存放40位的git commit签名
    zCommitSigBuf[40] = '\0';

    zCheck_Negative_Return(
            pread(zpLogFd[1][zIf.RepoId], &zPathBuf, zTypeConvert(zppPreLoadLogVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo *)->PathLen, zTypeConvert(zppPreLoadLogVecIf[zIf.RepoId][zIf.index].iov_base, zDeployLogInfo *)->offset),
            );
    zCheck_Negative_Return(
            pread(zpLogFd[2][zIf.RepoId], &zCommitSigBuf, zBytes(40), zBytes(40) * zIf.index),
            );

    if ('R' == zIf.hints[0]) {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -R -i %s", zppRepoPathList[zIf.RepoId], zCommitSigBuf);
        zpLogContents = "ALL";
        zLogSiz = zBytes(4);
    } else {
        sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -r -i %s %s", zppRepoPathList[zIf.RepoId], zCommitSigBuf, zPathBuf);
        zpLogContents = zPathBuf;
        zLogSiz = zTypeConvert(zppCacheVecIf[zIf.RepoId]->iov_base, zDeployLogInfo*)->PathLen;
    }

    pthread_rwlock_wrlock( &(zpRWLock[zIf.RepoId]) );  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
    _ui zSendBuf[zpTotalHost[zIf.RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    _i zUnReplyCnt = 0;

    if (0 != system(zShellBuf)) { goto zMark; }

    do {
        zsleep(0.2);  // 每0.2秒统计一次结果，并发往前端

        for (_i i = 0; i < zpTotalHost[zIf.RepoId]; i++) {
            if (0 == zppDpResList[zIf.RepoId][i].DeployState) {
                zSendBuf[zUnReplyCnt] = zppDpResList[zIf.RepoId][i].ClientAddr;
                zUnReplyCnt++;
            }
        }

        zsendto(zSd, zSendBuf, zUnReplyCnt * sizeof(zSendBuf[0]), 0, NULL);  // 向前端发送当前未成功的列表
    } while (zpReplyCnt[zIf.RepoId] < zpTotalHost[zIf.RepoId]);  // 一直等待到所有client状态确认为止：前端人工确认＋后端自动确认
    zpReplyCnt[zIf.RepoId] = 0;

    zwrite_log(zIf.RepoId, zpLogContents, zLogSiz);  // 撤销完成，写入日志

    for (_i i = 0; i < zpTotalHost[zIf.RepoId]; i++) {
        zppDpResList[zIf.RepoId][i].DeployState = 0;  // 将本项目各主机状态重置为0
    }

    pthread_rwlock_unlock( &(zpRWLock[zIf.RepoId]) );

zMark:
    shutdown(zSd, SHUT_RDWR);
}

// 接收并更新对应的布署状态确认信息
void
zconfirm_deploy_state(void *zpIf) {
    _i zSd = *((_i *)zpIf);
    zDeployResInfo zIf = { .RepoId = -1 };

    if ((zSizeOf(zIf) - zSizeOf(zIf.DeployState) - zSizeOf(zIf.p_next)) > zrecv_nohang(zSd, &zIf, sizeof(zDeployResInfo), 0, NULL)) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Reply to frontend failed!");
        return;
    }

    if (0 > zIf.RepoId || zRepoNum <= zIf.RepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    zDeployResInfo *zpTmp = zpppDpResHash[zIf.RepoId][zIf.ClientAddr % zDeployHashSiz];  // HASH 定位
    while (zpTmp != NULL) {  // 遍历
        if (zpTmp->ClientAddr == zIf.ClientAddr) {
            zpTmp->DeployState = 1;
            zpReplyCnt[zIf.RepoId]++;
            return;
        }
        zpTmp = zpTmp->p_next;
    }
    zPrint_Err(0, NULL, "Recv unknown reply!!!");

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
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据长度不足，要求前端重发报文
        zPrint_Err(0, NULL, "Recv failed!");
        return;
    }

    zpWritePath = ('u' == zRecvBuf[0]) ? zMajorIpPathTxt : zAllIpPathTxt;
    zRepoId = (_i) zRecvBuf[zBytes(4)];

    if (0 > zRepoId || zRepoNum <= zRepoId) {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若收到的代码库ID异常，要求前端重发报文
        zPrint_Err(0, NULL, "Invalid Repo ID !");
        return;
    }

    strcpy(zPathBuf, zppRepoPathList[zRepoId]);
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
    goto zMarkSkip;  // MD5 sig 相同，无需写入，跳过写入环节

zMarkWrite:
    zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600);
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
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若MD5 sig 不符，要求前端重发报文
        zPrint_Err(0, NULL, "Warning: MD5 sig Diff!");
    }

zMarkSkip:
    shutdown(zSd, SHUT_RDWR);
}

// 启动git_shadow服务器
void
zstart_server(void *zpIf) {
#define zGetCmdId(zCmd) ((zCmd) - 64)
#define zMaxEvents 64
    // 如下部分定义服务接口
    zThreadPoolOps zNetServ[64] = {NULL};
    zNetServ[zGetCmdId('p')] = zlist_diff_files;
    zNetServ[zGetCmdId('P')] = zprint_diff_contents;
    zNetServ[zGetCmdId('l')] = zNetServ[zGetCmdId('L')] = zlist_log;
    zNetServ[zGetCmdId('d')] = zNetServ[zGetCmdId('D')] = zdeploy;
    zNetServ[zGetCmdId('r')] = zNetServ[zGetCmdId('R')] = zrevoke;
    zNetServ[zGetCmdId('c')] = zNetServ[zGetCmdId('C')] = zconfirm_deploy_state;
    zNetServ[zGetCmdId('u')] = zNetServ[zGetCmdId('U')] = zupdate_ipv4_db_txt;

    _i zCmd = -1;  // 用于存放前端发送的操作指令

    // 如下部分配置 epoll 环境
    zNetServInfo *zpNetServIf = (zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Return(
            epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv),
            );

    // 如下部分启动 epoll 监听服务
    for (;;) {
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return( zEvNum, );

        for (_i i = 0; i < zEvNum; i++) {
           if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
               zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
               zCheck_Negative_Return(zConnSd,);

               zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
               zEv.data.fd = zConnSd;
               zCheck_Negative_Return(
                       epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv),
                       );
            } else {
               zrecv_nohang(zEvents[i].data.fd, &zCmd, zBytes(1), MSG_PEEK, NULL);
               if (NULL != zNetServ[zGetCmdId(zCmd)]) {
                   zAdd_To_Thread_Pool(zNetServ[zGetCmdId(zCmd)], &(zEvents[i].data.fd));
               }
            }
        }
    }
#undef zMaxEvents
#undef zGetCmdId
}

// 客户端用于向中控机发送状态确认信息
void
zclient_reply(char *zpHost, char *zpPort) {
    zDeployResInfo zDpResIf = {.hints = {'c',}};  // confirm:标识这是一条状态确认信息
    _i zFd, zSd, zResLen;

    // 读取版本库ID
    zFd = open(zRepoIdPathTxt, O_RDONLY);
    zCheck_Negative_Return(zFd,);
    zCheck_Negative_Return(
            read(zFd, &(zDpResIf.RepoId), sizeof(_i)),
            );
    close(zFd);

    zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV);  // 以点分格式的ipv4地址连接服务端
    if (-1 == zSd) {
        zPrint_Err(0, NULL, "Connect to server failed.");
        exit(1);
    }

    zFd = open(zSelfIpPath, O_RDONLY);  // 读取本机的所有非回环ip地址，依次发送状态确认信息至服务端
    zCheck_Negative_Return(zFd,);

    _ui zIpv4Bin;
    while (0 != (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
        zCheck_Negative_Return(zResLen,);
        zDpResIf.ClientAddr = zIpv4Bin;  // 标识本机身份：ipv4地址
        if ((zBytes(4) + sizeof(zDeployLogInfo)) != zsendto(zSd, &zDpResIf, sizeof(zDeployResInfo), 0, NULL)) {
            zPrint_Err(0, NULL, "Reply to server failed.");
        }
    }

    close(zFd);
    shutdown(zSd, SHUT_RDWR);
}

#undef zTypeConvert
