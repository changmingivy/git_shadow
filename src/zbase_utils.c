#ifndef _Z
    #include "zmain.c"
#endif

/*
 * Functions for base64 coding [and decoding(TO DO)]
 */
char zBase64Dict[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
char *
zstr_to_base64(const char *zpOrig) {
// TEST: PASS
    _i zOrigLen = strlen(zpOrig);
    _i zMax = (0 == zOrigLen % 3) ? (zOrigLen / 3 * 4) : (1 + zOrigLen / 3 * 4);
    _i zResLen = zMax + (4- (zMax % 4));

    char zRightOffset[zMax], zLeftOffset[zMax];

    char *zRes;
    zMem_Alloc(zRes, char, zResLen);

    _i i, j;

    for (i = j = 0; i < zMax; i++) {
        if (3 == (i % 4)) {
            zRightOffset[i] = 0;
            zLeftOffset[i] = 0;
        } else {
            zRightOffset[i] = zpOrig[j]>>(2 * ((j % 3) + 1));
            zLeftOffset[i] = zpOrig[j]<<(2 * (2 - (j % 3)));
            j++;
        }
    }

    _c mask = 63;
    zRes[0] = zRightOffset[0] & mask;

    for (i = 1; i < zMax; i++) { zRes[i] = (zRightOffset[i] | zLeftOffset[i-1]) & mask; }
    zRes[zMax - 1] = zLeftOffset[zMax - 2] & mask;

    for (i = 0; i < zMax; i++) { zRes[i] = zBase64Dict[(_i)zRes[i]]; }
    for (i = zMax; i < zResLen; i++) { zRes[i] = '='; }

    return zRes;
}

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
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEADDR, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址
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

// Send message from multi positions.
_i
zsendmsg(_i zSd, struct iovec *zpIov, _i zIovSiz, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    struct msghdr zMsgIf = {
        .msg_name = zpAddr,
        .msg_namelen = INET_ADDRSTRLEN,
        .msg_iov = zpIov,
        .msg_iovlen = zIovSiz,
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
 * DO NOT forget to free memory.
 * \n has been deleted!!!
 */
char *
zget_one_line_from_FILE(FILE *zpFile) {
// TEST: PASS
    char zBuf[zCommonBufSiz];
    char *zpRes = fgets(zBuf, zCommonBufSiz, zpFile);

    if (NULL == zpRes) {
        if(0 == feof(zpFile)) {
            zCheck_Null_Exit(zpRes);
        } else {
            return NULL;
        }
    }

    zpRes[strlen(zBuf) -1] = '\0';
    return zpRes;
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
