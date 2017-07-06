#ifndef _Z
    #include "../zmain.c"
#endif

#define zMaxEvents 64

/****************
 * 模块整体信息 *
 ****************/

/*
 * git_shadow充当TCP服务器角色，接收前端请求与后端主机信息反馈，尽可能使用缓存响应各类请求
 *
 * 对接规则：
 *         执行动作代号(1byte)＋信息正文
 *         [OpsMark(l/d/D/R)]+[struct zFileDiffInfo]
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
 *         c:状态确认，前端或后端主机均可返回
 */

// 列出差异文件路径名称列表
void
zlist_diff_files(_i zSd){
    zFileDiffInfo zIf;
    if (zBytes(20) > zrecv_nohang(zSd, &zIf, sizeof(zFileDiffInfo), 0, NULL)) {
        zPrint_Err(0, NULL, "Recv data failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    pthread_rwlock_rdlock( &(zpRWLock[zIf.RepoId]) );
    zsendmsg(zSd, zppCacheVecIf[zIf.RepoId], zpCacheVecSiz[zIf.RepoId], 0, NULL);  // 直接从缓存中提取
    pthread_rwlock_unlock( &(zpRWLock[zIf.RepoId]) );
    shutdown(zSd, SHUT_RDWR);  // 若前端复用同一套接字则不需要关闭
}

// 打印当前版本缓存与CURRENT标签之间的指定文件的内容差异
void
zprint_diff_contents(_i zSd){
    zFileDiffInfo zIf;
    if (zBytes(20) > zrecv_nohang(zSd, &zIf, sizeof(zFileDiffInfo), 0, NULL)) {
        zPrint_Err(0, NULL, "Recv data failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    pthread_rwlock_rdlock(&(zpRWLock[zIf.RepoId]));
    if (zIf.CacheVersion == ( (zFileDiffInfo *) (zppCacheVecIf[zIf.RepoId][0].iov_base))->CacheVersion ) {
        zsendmsg(zSd,
                zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->p_DiffContent,
                zTypeConvert(zppCacheVecIf[zIf.RepoId][zIf.FileIndex].iov_base, zFileDiffInfo *)->VecSiz,
                0,
                NULL);  // 直接从缓存中提取
    }
    else {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若缓存版本不一致，要求前端重发报文
    }
    pthread_rwlock_unlock( &(zpRWLock[zIf.RepoId]) );
    shutdown(zSd, SHUT_RDWR);
}

// 列出最近zPreLoadLogSiz次或全部历史布署日志
void
zlist_log(_i zSd, _i zMark) {
    zDeployLogInfo zIf;
    if (zBytes(20) > zrecv_nohang(zSd, &zIf, sizeof(zDeployLogInfo), 0, NULL)) {
        zPrint_Err(0, NULL, "Recv data failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    pthread_rwlock_rdlock( &(zpRWLock[zIf.RepoId]) );
    _i zVecSiz;
    if ( 0 == zMark ){    // 默认直接直接回复预存的最近zPreLoadLogSiz次记录
        zsendmsg(zSd, zppPreLoadLogVecIf[zIf.RepoId], zpPreLoadLogVecSiz[zIf.RepoId], 0, NULL);
    }
    else {  // 若前端请求列出所有历史记录，从日志文件中读取
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
            }
            else {
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

void
zmilli_sleep(_i zMilliSec) {  // 毫秒级sleep
    static struct timespec zNanoSecIf = { .tv_sec = 0, };
    zNanoSecIf.tv_nsec  = zMilliSec * 1000000;
    nanosleep( &zNanoSecIf, NULL );
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId, char *zpPathName, _i zPathLen) {
    // write to .git_shadow/log/meta
    struct stat zStatBufIf;
    zCheck_Negative_Return( fstat(zpLogFd[0][zRepoId], &zStatBufIf), );  // 获取当前日志文件属性

    zDeployLogInfo zDeployIf;
    zCheck_Negative_Return(
            pread(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo), zStatBufIf.st_size - sizeof(zDeployLogInfo)),
            );  // 读出前一个记录的信息

    zDeployIf.RepoId = zRepoId;  // 代码库ID相同
    zDeployIf.index += 1;  // 布署索引偏移量增加1(即：顺序记录布署批次ID)，用于从sig日志文件中快整定位对应的commit签名
    zDeployIf.offset += zPathLen;  // data日志中对应的文件路径名称位置偏移量
    zDeployIf.TimeStamp = time(NULL);  // 日志时间戳(1900至今的秒数)
    zDeployIf.PathLen= zPathLen;  // 本次布署的文件路径名称长度

    // 其本信息写入.git_shadow/log/meta
    if (sizeof(zDeployLogInfo) != write(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo))) {
        zPrint_Err(0, NULL, "Can't write to log/meta!");
        exit(1);
    }
    // 将本次布署的文件路径名称写入.git_shadow/log/data尾部
    if (zPathLen != write(zpLogFd[1][zRepoId], zpPathName, zPathLen)) {
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
zdeploy(_i zSd,  _i zMark) {
    zFileDiffInfo zDiffIf;
    zDiffIf.zHint[0] = sizeof(zFileDiffInfo) - sizeof(_i);

    if (zBytes(20) > zrecv_nohang(zSd, &zDiffIf, sizeof(zDiffIf), 0, NULL)) {
        zPrint_Err(0, NULL, "Recv data failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    if (zDiffIf.CacheVersion == ((zFileDiffInfo *) (zppCacheVecIf[zDiffIf.RepoId]->iov_base))->CacheVersion) {  // 确认缓存版本是否一致
        char zShellBuf[4096];  // 存放SHELL命令字符串
        char *zpLogContents;   // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
        _i zLogSiz;
        if (1 == zMark) {
            sprintf(zShellBuf, "cd %s && ~git/.git_shadow/scripts/zdeploy.sh -D", zppRepoPathList[zDiffIf.RepoId]);
            zpLogContents = "ALL";
            zLogSiz = zBytes(4);
        }
        else {
            sprintf(zShellBuf, "cd %s && ~git/.git_shadow/scripts/zdeploy.sh -d %s",
                    zppRepoPathList[zDiffIf.RepoId],
                    zTypeConvert(zppCacheVecIf[zDiffIf.RepoId][zDiffIf.FileIndex].iov_base, zFileDiffInfo *)->path);
            zpLogContents = zTypeConvert(zppCacheVecIf[zDiffIf.RepoId][zDiffIf.FileIndex].iov_base, zFileDiffInfo *)->path;
            zLogSiz = zTypeConvert(zppCacheVecIf[zDiffIf.RepoId][zDiffIf.FileIndex].iov_base, zFileDiffInfo *)->PathLen;
        }

        pthread_rwlock_wrlock( &(zpRWLock[zDiffIf.RepoId]) );  // 加锁，布署没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
        system(zShellBuf);

        _ui zSendBuf[zpTotalHost[zDiffIf.RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
        _i i;
        do {
            zmilli_sleep(2000);  // 每隔0.2秒向前端返回一次结果

            for (i = 0; i < zpTotalHost[zDiffIf.RepoId]; i++) {  // 登记尚未确认状态的客户端ip列表
                if (0 == zppDpResList[zDiffIf.RepoId][i].DeployState) {
                    zSendBuf[i] = zppDpResList[zDiffIf.RepoId][i].ClientAddr;
                }
            }

            zsendto(zSd, zSendBuf, i * sizeof(_ui), 0, NULL);
        } while (zpReplyCnt[zDiffIf.RepoId] < zpTotalHost[zDiffIf.RepoId]);  // 等待所有client端确认状态：前端人工标记＋后端自动返回
        zpReplyCnt[zDiffIf.RepoId] = 0;

        zwrite_log(zDiffIf.RepoId, zpLogContents, zLogSiz);  // 将本次布署信息写入日志

        for (_i i = 0; i < zpTotalHost[zDiffIf.RepoId]; i++) {
            zppDpResList[zDiffIf.RepoId][i].DeployState = 0;  // 重置client状态，以便下次布署使用
        }

        pthread_rwlock_unlock(&(zpRWLock[zDiffIf.RepoId]));  // 释放锁
    }
    else {
        zsendto(zSd, "!", zBytes(2), 0, NULL);  // 若缓存版本不一致，向前端发送“!”标识，要求重发报文
    }
    shutdown(zSd, SHUT_RDWR);
}

// 依据布署日志，撤销指定文件或整次提交
void
zrevoke_from_log(_i zSd, _i zMark){
    zDeployLogInfo zLogIf;
    zLogIf.zHint[0] = sizeof(zDeployLogInfo) - sizeof(_i);
    zLogIf.zHint[1] = sizeof(zDeployLogInfo) - sizeof(_i) -sizeof(_l);

    if (zBytes(20) > zrecv_nohang(zSd, &zLogIf, sizeof(zLogIf), 0, NULL)) {
        zPrint_Err(0, NULL, "Recv data failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
    }

    if (zLogIf.index >= zPreLoadLogSiz) {
        zsendto(zSd, "-1", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    char zPathBuf[zTypeConvert(zppPreLoadLogVecIf[zLogIf.RepoId][zLogIf.index].iov_base, zDeployLogInfo*)->PathLen];  // 存放待撤销的目标文件路径
    char zCommitSigBuf[41];  // 存放40位的git commit签名
    zCommitSigBuf[40] = '\0';

    zCheck_Negative_Return(
            pread(zpLogFd[1][zLogIf.RepoId], &zPathBuf, zTypeConvert(zppPreLoadLogVecIf[zLogIf.RepoId][zLogIf.index].iov_base, zDeployLogInfo*)->PathLen, zTypeConvert(zppPreLoadLogVecIf[zLogIf.RepoId][zLogIf.index].iov_base, zDeployLogInfo*)->offset),
            );
    zCheck_Negative_Return(
            pread(zpLogFd[2][zLogIf.RepoId], &zCommitSigBuf, zBytes(40), zBytes(40) * zLogIf.index),
            );

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
    char *zpLogContents;  // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
    _i zLogSiz;
    if (1 == zMark) {
        sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -R -i %s -P %s", zCommitSigBuf, zppRepoPathList[zLogIf.RepoId]);
        zpLogContents = "ALL";
        zLogSiz = zBytes(4);
    }
    else {
        sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -r -i %s -P %s %s", zCommitSigBuf, zppRepoPathList[zLogIf.RepoId], zPathBuf);
        zpLogContents = zPathBuf;
        zLogSiz = zTypeConvert(zppCacheVecIf[zLogIf.RepoId]->iov_base, zDeployLogInfo*)->PathLen;
    }

    pthread_rwlock_wrlock( &(zpRWLock[zLogIf.RepoId]) );  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
    system(zShellBuf);

    _ui zSendBuf[zpTotalHost[zLogIf.RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
    _i i;
    do {
        zmilli_sleep(2000);  // 每0.2秒统计一次结果，并发往前端

        for (i = 0; i < zpTotalHost[zLogIf.RepoId]; i++) {
            if (0 == zppDpResList[zLogIf.RepoId][i].DeployState) {
                zSendBuf[i] = zppDpResList[zLogIf.RepoId][i].ClientAddr;
            }
        }

        zsendto(zSd, zSendBuf, i * sizeof(_ui), 0, NULL);  // 向前端发送当前未成功的列表
    } while (zpReplyCnt[zLogIf.RepoId] < zpTotalHost[zLogIf.RepoId]);  // 一直等待到所有client状态确认为止：前端人工确认＋后端自动确认
        zpReplyCnt[zLogIf.RepoId] = 0;

    zwrite_log(zLogIf.RepoId, zpLogContents, zLogSiz);  // 撤销完成，写入日志

    for (_i i = 0; i < zpTotalHost[zLogIf.RepoId]; i++) {
        zppDpResList[zLogIf.RepoId][i].DeployState = 0;  // 将本项目各主机状态重置为0
    }

    pthread_rwlock_unlock( &(zpRWLock[zLogIf.RepoId]) );
    shutdown(zSd, SHUT_RDWR);
}

// 接收并更新对应的布署状态确认信息
void
zconfirm_deploy_state(_i zSd, _i zMark) {
    zDeployResInfo zDpResIf;
    if (zBytes(20) > zrecv_nohang(zSd, &zDpResIf, sizeof(zDeployResInfo), 0, NULL)) {
        zPrint_Err(0, NULL, "Reply to frontend failed!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    zDeployResInfo *zpTmp = zpppDpResHash[zDpResIf.RepoId][zDpResIf.ClientAddr % zDeployHashSiz];  // HASH定位
    while (zpTmp != NULL) {  // 遍历
        if (zpTmp->ClientAddr == zDpResIf.ClientAddr) {
            zpTmp->DeployState = 1;
            zpReplyCnt[zDpResIf.RepoId]++;
            return;
        }
        zpTmp = zpTmp->p_next;
    }
    zPrint_Err(0, NULL, "Unknown client reply!!!");

    // 若是与ECS建立的连接，则关闭；若是与前端的连接，则保持
    if (0 == zMark) { shutdown(zSd, SHUT_RDWR); }
}

// 接收新的ipv4列表，写入txt文件，然后向发送者返回32位的MD5校验值
void
zupdate_ipv4_db_txt(_i zSd, _i zRepoId, _i zMark) {
    _i zFd, zRecvSiz;
    char *zpWrPath = (zMark == 0) ? zMajorIpPathTxt : zAllIpPathTxt;
    char zRecvBuf[zCommonBufSiz], zPathBuf[zCommonBufSiz] = {'\0'};

    strcpy(zPathBuf, zppRepoPathList[zRepoId]);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWrPath);
    zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    zCheck_Negative_Return(zFd,);

    // 接收网络数据并同步写入文件
    while (0 < (zRecvSiz = recv(zSd, zRecvBuf, zCommonBufSiz, 0))) {
        if (zRecvSiz != write(zFd, zRecvBuf, zRecvSiz)) {
            zPrint_Err(errno, NULL, "Write ipv4 list to txt file failed!");
            exit(1);
        }
    }
    zCheck_Negative_Return(zRecvSiz,);
    close(zFd);

    // 回复收到的ipv4列表文件的 MD5 checksum
    if (zBytes(32) != zsendto(zSd, zgenerate_file_sig_md5(zPathBuf), zBytes(32), 0, NULL)) {
        zPrint_Err(0, NULL, "Reply to frontend failed!");
    }
    shutdown(zSd, SHUT_RDWR);
}

// 路由函数
void
zdo_serv(void *zpSd) {
    _i zSd = *((_i *)zpSd);

    char zReqBuf[zBytes(4)];
    if (zBytes(4) > zrecv_nohang(zSd, zReqBuf, zBytes(4), MSG_PEEK, NULL)) { // 接收前端指令信息，读出指令但不真正取走数据
        zPrint_Err(0, NULL, "recv ERROR!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    switch (zReqBuf[0]) {
        case 'p':  // list:列出内容有变动的所有文件路径
            zlist_diff_files(zSd);
            break;
        case 'P':  // print:打印某个文件的详细变动内容
            zprint_diff_contents(zSd);
            break;
        case 'l':  // LIST:打印最近zPreLoadLogSiz次布署日志
            zlist_log(zSd, 0);
            break;
        case 'L':  // LIST:打印所有历史布署日志
            zlist_log(zSd, 1);
            break;
        case 'd':  // deploy:布署单个文件
            zdeploy(zSd, 0);
            break;
        case 'D':  // DEPLOY:布署当前提交所有文件
            zdeploy(zSd, 1);
            break;
        case 'r':  // revoke:撤销单个文件的更改
            zrevoke_from_log(zSd, 0);
            break;
        case 'R':  // REVOKE:撤销某次提交的全部更改
            zrevoke_from_log(zSd, 1);
            break;
        case 'c':  // confirm:客户端回复的布署成功确认信息
            zconfirm_deploy_state(zSd, 0);
            break;
        case 'C':  // CONFIRM:前端回复的人工确认信息
            zconfirm_deploy_state(zSd, 1);
            break;
        case 'u':  // update major clients ipv4 addr txt file: 与中控机直接通信的master客户端列表数据需要更新
            zupdate_ipv4_db_txt(zSd, zReqBuf[1], 0);  // zReqBuf[1] 存储代码库索引号／代号
            break;
        case 'U':  // Update all clients ipv4 addr txt file: 所有客户端ipv4地址列表数据需要更新
            zupdate_ipv4_db_txt(zSd, zReqBuf[1], 1);  // zReqBuf[1] 存储代码库索引号／代号
            break;
        default:
            zPrint_Err(0, NULL, "Undefined request");
    }
}

// 启动git_shadow服务器
void
zstart_server(void *zpIf) {
    zNetServInfo *zpNetServIf = (zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 已经做完bind和listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Return(
            epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv),
            );

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
            }
            else {
                zAdd_To_Thread_Pool(zdo_serv, &(zEvents[i].data.fd));
            }
        }
    }
}

// 用于向git_shadow返回布署成功的信息
void
zclient_reply(char *zpHost, char *zpPort) {
    zDeployResInfo zDpResIf = {.zHint = {'c',}};  // confirm:标识这是一条状态确认信息
    _i zFd, zSd, zResLen;

    zFd = open(zMetaLogPath, O_RDONLY);
    zCheck_Negative_Return(zFd,);

    zDeployLogInfo zDpLogIf;
    zCheck_Negative_Return(
            read(zFd, &zDpLogIf, sizeof(zDeployLogInfo)),
            );
    zDpResIf.RepoId = zDpLogIf.RepoId;  // 标识版本库ID
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

#undef zMaxEvents
