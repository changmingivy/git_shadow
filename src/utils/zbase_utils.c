#ifndef _Z
    #include "../zmain.c"
#endif

// /*
//  * Functions for base64 coding [and decoding(TO DO)]
//  */
// char zBase64Dict[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// char *
// zstr_to_base64(const char *zpOrig) {
// // TEST: PASS
//     _i zOrigLen = strlen(zpOrig);
//     _i zMax = (0 == zOrigLen % 3) ? (zOrigLen / 3 * 4) : (1 + zOrigLen / 3 * 4);
//     _i zResLen = zMax + (4- (zMax % 4));
//
//     char zRightOffset[zMax], zLeftOffset[zMax];
//
//     char *zRes;
//     zMem_Alloc(zRes, char, zResLen);
//
//     _i i, j;
//
//     for (i = j = 0; i < zMax; i++) {
//         if (3 == (i % 4)) {
//             zRightOffset[i] = 0;
//             zLeftOffset[i] = 0;
//         } else {
//             zRightOffset[i] = zpOrig[j]>>(2 * ((j % 3) + 1));
//             zLeftOffset[i] = zpOrig[j]<<(2 * (2 - (j % 3)));
//             j++;
//         }
//     }
//
//     _c mask = 63;
//     zRes[0] = zRightOffset[0] & mask;
//
//     for (i = 1; i < zMax; i++) { zRes[i] = (zRightOffset[i] | zLeftOffset[i-1]) & mask; }
//     zRes[zMax - 1] = zLeftOffset[zMax - 2] & mask;
//
//     for (i = 0; i < zMax; i++) { zRes[i] = zBase64Dict[(_i)zRes[i]]; }
//     for (i = zMax; i < zResLen; i++) { zRes[i] = '='; }
//
//     return zRes;
// }

/*
 * Functions for socket connection.
 */
struct addrinfo *
zgenerate_hint(_i zFlags) {
// TEST: PASS
    static struct addrinfo zHints;
    zHints.ai_flags = zFlags;
    zHints.ai_family = AF_INET;
    return &zHints;
}

// Generate a socket fd used by server to do 'accept'.
struct sockaddr *
zgenerate_serv_addr(char *zpHost, char *zpPort) {
// TEST: PASS
    struct addrinfo *zpRes, *zpHint;
    zpHint = zgenerate_hint(AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV);

    _i zErr = getaddrinfo(zpHost, zpPort, zpHint, &zpRes);
    if (-1 == zErr){
        zPrint_Err(errno, NULL, gai_strerror(zErr));
        exit(1);
    }

    return zpRes->ai_addr;
}

// Start server: TCP or UDP,
// Option zServType: 1 for TCP, 0 for UDP.
_i
zgenerate_serv_SD(char *zpHost, char *zpPort, _i zServType) {
// TEST: PASS
    _i zSockType = (0 == zServType) ? SOCK_DGRAM : SOCK_STREAM;
    _i zSd = socket(AF_INET, zSockType, 0);
    zCheck_Negative_Return(zSd, -1);

    _i zReuseMark = 1;
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址
    struct sockaddr *zpAddrIf = zgenerate_serv_addr(zpHost, zpPort);
    zCheck_Negative_Return(bind(zSd, zpAddrIf, INET_ADDRSTRLEN), -1);

    zCheck_Negative_Return(listen(zSd, 5), -1);

    return zSd;
}

// Used by client.
_i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zSockType, _i zProto) {
// TEST: PASS
    if (zSockType == 0) { zSockType = SOCK_STREAM; }
    if (zProto == 0) { zProto = IPPROTO_TCP; }

    _i zSd = socket(AF_INET, zSockType, zProto);
    zCheck_Negative_Return(zSd, -1);
    for (_i i = 4; i > 0; --i) {
        if (0 == connect(zSd, zpAddr, zLen)) { return zSd; }
        close(zSd);
        sleep(i);
    }

    return -1;
}

// Used by client.
_i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
// TEST: PASS
    struct addrinfo *zpRes, *zpTmp, *zpHints;
    _i zSockD, zErr;

    zpHints = zgenerate_hint(zFlags);

    zErr = getaddrinfo(zpHost, zpPort, zpHints, &zpRes);
    if (-1 == zErr){ zPrint_Err(errno, NULL, gai_strerror(zErr)); }

    for (zpTmp = zpRes; NULL != zpTmp; zpTmp = zpTmp->ai_next) {
        if(0 < (zSockD  = ztry_connect(zpTmp->ai_addr, INET_ADDRSTRLEN, 0, 0))) {
            freeaddrinfo(zpRes);
            return zSockD;
        }
    }

    freeaddrinfo(zpRes);
    return -1;
}

_i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    _i zSentSiz = sendto(zSd, zpBuf, zLen, 0 | zFlags, zpAddr, INET_ADDRSTRLEN);
    zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

_i
zsendmsg(_i zSd, struct zVecWrapInfo *zpVecWrapIf, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    struct msghdr zMsgIf = {
        .msg_name = zpAddr,
        .msg_namelen = INET_ADDRSTRLEN,
        .msg_iov = zpVecWrapIf->p_VecIf,
        .msg_iovlen = zpVecWrapIf->VecSiz,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    _i zSentSiz = sendmsg(zSd, &zMsgIf, zFlags);
    zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

_i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

_i
zrecv_nohang(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

/*
 * Daemonize a linux process to daemon.
 */
void
zclose_fds(pid_t zPid) {
// TEST: PASS
    struct dirent *zpDirIf;
    char zStrPid[8], zPath[64];

    sprintf(zStrPid, "%d", zPid);

    strcpy(zPath, "/proc/");
    strcat(zPath, zStrPid);
    strcat(zPath, "/fd");

    _i zFD;
    DIR *zpDir = opendir(zPath);
    while (NULL != (zpDirIf = readdir(zpDir))) {
        zFD = strtol(zpDirIf->d_name, NULL, 10);
        if (2 != zFD) { close(zFD); }
    }
    closedir(zpDir);
}

// 这个版本的daemonize会保持标准错误输出描述符处于打开状态
void
zdaemonize(const char *zpWorkDir) {
// TEST: PASS
    zIgnoreAllSignal();

//  sigset_t zSigToBlock;
//  sigfillset(&zSigToBlock);
//  pthread_sigmask(SIG_BLOCK, &zSigToBlock, NULL);

    umask(0);
    zCheck_Negative_Return(chdir(NULL == zpWorkDir? "/" : zpWorkDir),);

    pid_t zPid = fork();
    zCheck_Negative_Return(zPid,);

    if (zPid > 0) { exit(0); }

    setsid();
    zPid = fork();
    zCheck_Negative_Return(zPid,);

    if (zPid > 0) { exit(0); }

    zclose_fds(getpid());

    _i zFD = open("/dev/null", O_RDWR);
    dup2(zFD, 1);
//  dup2(zFD, 2);
}

/*
 * Fork a child process to exec an outer command.
 * The "zppArgv" must end with a "NULL"
 */
void
zfork_do_exec(const char *zpCommand, char **zppArgv) {
// TEST: PASS
    pid_t zPid = fork();
    zCheck_Negative_Return(zPid,);

    if (0 == zPid) {
        execvp(zpCommand, zppArgv);
    } else {
        waitpid(zPid, NULL, 0);
    }
}

/*
 * 以返回是否是 NULL 为条件判断是否已读完所有数据
 * 可重入，可用于线程
 * 适合按行读取分别处理的场景
 */
void *
zget_one_line(char *zpBufOUT, _i zSiz, FILE *zpFile) {
    char *zpRes = fgets(zpBufOUT, zSiz, zpFile);
    if (NULL == zpRes && (0 == feof(zpFile))) {
        zPrint_Err(0, NULL, "<fgets> ERROR!");
        exit(1);
    }
    return zpRes;
}

/*
 * 以返回值小于 zSiz 为条件判断是否到达末尾（读完所有数据 )
 * 可重入，可用于线程
 * 适合一次性大量读取所有文本内容的场景
 */
_i
zget_str_content(char *zpBufOUT, size_t zSiz, FILE *zpFile) {
    size_t zCnt = fread(zpBufOUT, zBytes(1), zSiz, zpFile);
    if (zCnt < zSiz && (0 == feof(zpFile))) {
        zPrint_Err(0, NULL, "<fread> ERROR!");
        exit(1);
    }
    return zCnt;
}

/*
 * 纳秒级sleep，小数点形式赋值
 */
void
zsleep(_d zSecs) {
    struct timespec zNanoSecIf;
    zNanoSecIf.tv_sec = (_i) zSecs;
    zNanoSecIf.tv_nsec  = (zSecs - zNanoSecIf.tv_sec) * 1000000000;
    nanosleep( &zNanoSecIf, NULL );
}

/*
 * 用于在单独线程中执行外部命令
 */
void
zthread_system(void *zpCmd) {
    if (0 != system((char *) zpCmd)) {
    zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}

/*
 * 将文本格式的ipv4地址转换成二进制无符号整型(按网络字节序，即大端字节序)，以及反向转换
 */
_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit( inet_pton(AF_INET, zpStrAddr, &zIpv4Addr) );
    return zIpv4Addr.s_addr;
}

void
zconvert_ipv4_bin_to_str(_ui zIpv4BinAddr, char *zpBufOUT) {
    struct in_addr zIpv4Addr;
    zIpv4Addr.s_addr = zIpv4BinAddr;
    inet_ntop(AF_INET, &zIpv4Addr, zpBufOUT, INET_ADDRSTRLEN);
}

/*
 *  接收数据时使用
 *  将json文本转换为zMetaInfo结构体
 *  返回：用完data字段的内容后，需要释放资源的json对象指针
 */
cJSON *
zconvert_json_str_to_struct(char *zpJsonStr, struct zMetaInfo *zpMetaIf) {
    cJSON *zpRootObj;
    cJSON *zpValueObj;

    if (NULL == (zpRootObj = cJSON_Parse(zpJsonStr))) {
        return NULL;
    }

    /* 操作指令、代码库ID，所有操作都需要指定 */
    zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "O") );  // OpsId
    zpMetaIf->OpsId = zpValueObj->valueint;
    zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "R") );  // RepoId
    zpMetaIf->RepoId = zpValueObj->valueint;

    /* 8 - 9：确认主机布署状态时，需要IP */
    if (8 == zpMetaIf->OpsId || 9 == zpMetaIf->OpsId) {
        zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "H") );  // HostIp
        zpMetaIf->HostIp = zpValueObj->valueint;
    }

    /* 10 - 13：查文件列表、查差异内容、布署、撤销 */
    if (9 < zpMetaIf->OpsId) {
        zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "C") );  // CacheId
        zpMetaIf->CacheId = zpValueObj->valueint;

        zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "V") );  // VersionId(CommitId)
        zpMetaIf->CommitId = zpValueObj->valueint;

        /* 11 - 13：查差异内容、布署、撤销 */
        if (10 < zpMetaIf->OpsId) {
            zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "F") );   // FileId
            zpMetaIf->FileId = zpValueObj->valueint;

            /*12 - 13：布署、撤销，单主机布署时需要指定IP */
            if (11 < zpMetaIf->OpsId) {
                zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "H") );  // HostIp
                zpMetaIf->HostIp = zpValueObj->valueint;
            }
        }
    }

    /* 仅在更新集群IP地址数据库时，需要此项 */
    if (3 == zpMetaIf->OpsId || 4 == zpMetaIf->OpsId) {
        zCheck_Null_Exit( zpValueObj = cJSON_GetObjectItem(zpRootObj, "D") );  // Data
        zpMetaIf->p_data = zpValueObj->valuestring;
        return zpRootObj;  // 此时需要后续用完之后释放资源
    }

    cJSON_Delete(zpRootObj);
    return (void *)-1;  // 正常状态返回，区别于NULL，否则无法识别错误
}

/*
 * 若用到p_data字段，使用完json后释放顶层对象
 */
void
zjson_obj_free(cJSON *zpJsonRootObj) {
    cJSON_Delete(zpJsonRootObj);
}

/*
 * 生成缓存时使用
 * 将结构体数据转换成生成json文本
 */
void
zconvert_struct_to_json_str(char *zpJsonStrBuf, struct zMetaInfo *zpMetaIf) {
    sprintf(
            zpJsonStrBuf, "{\"O\":%d,\"R\":%d,\"V\":%d,\"F\":%d,\"H\":%d,\"C\":%d,\"T\":%s,\"D\":%s}",
            zpMetaIf->OpsId,
            zpMetaIf->RepoId,
            zpMetaIf->CommitId,
            zpMetaIf->FileId,
            zpMetaIf->HostIp,
            zpMetaIf->CacheId,
            zpMetaIf->p_TimeStamp,  // 不需要时会指向 “”
            zpMetaIf->p_data  // 不需要时会指向 “”
            );
}
