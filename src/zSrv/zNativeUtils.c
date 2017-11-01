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

// /*
//  *  将指定的套接字属性设置为非阻塞
//  */
// void
// zset_nonblocking(_i zSd) {
//     _i zOpts;
//     zCheck_Negative_Exit( zOpts = fcntl(zSd, F_GETFL) );
//     zOpts |= O_NONBLOCK;
//     zCheck_Negative_Exit( fcntl(zSd, F_SETFL, zOpts) );
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
#ifdef _Z_BSD
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEPORT, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址与端口
#else
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址与端口
#endif
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

//    for (_i i = 4; i > 0; --i) {
        if (0 == connect(zSd, zpAddr, zLen)) { return zSd; }
        close(zSd);
//        sleep(i);
//    }

    return -1;
}

// Used by client.
_i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
// TEST: PASS
    struct addrinfo *zpRes = NULL, *zpTmp = NULL, *zpHints = NULL;
    _i zSd, zErr;

    zpHints = zgenerate_hint(zFlags);

    zErr = getaddrinfo(zpHost, zpPort, zpHints, &zpRes);
    if (-1 == zErr){ zPrint_Err(errno, NULL, gai_strerror(zErr)); }

    for (zpTmp = zpRes; NULL != zpTmp; zpTmp = zpTmp->ai_next) {
        if(0 < (zSd = ztry_connect(zpTmp->ai_addr, INET_ADDRSTRLEN, 0, 0))) {
            freeaddrinfo(zpRes);
            return zSd;
        }
    }

    freeaddrinfo(zpRes);
    return -1;
}

_i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    _i zSentSiz = sendto(zSd, zpBuf, zLen, MSG_NOSIGNAL | zFlags, zpAddr, INET_ADDRSTRLEN);
    //zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

_i
zsendmsg(_i zSd, struct zVecWrapInfo *zpVecWrapIf, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    if (NULL == zpVecWrapIf) { return -1; }

    struct msghdr zMsgIf = {
        .msg_name = zpAddr,
        .msg_namelen = (NULL == zpAddr) ? 0 : INET6_ADDRSTRLEN,
        .msg_iov = zpVecWrapIf->p_VecIf,
        .msg_iovlen = zpVecWrapIf->VecSiz,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    return sendmsg(zSd, &zMsgIf, MSG_NOSIGNAL | zFlags);
}

_i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

//_i
//zrecv_nohang(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
//// TEST: PASS
//    socklen_t zAddrLen;
//    _i zRecvSiz;
//    if ((-1 == (zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr, &zAddrLen)))
//            && (EAGAIN == errno)) {
//        zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr, &zAddrLen);
//    }
//    return zRecvSiz;
//}

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
    zCheck_Negative_Exit(zPid);

    if (0 == zPid) {
        execve(zpCommand, zppArgv, NULL);
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
    size_t zCnt;
    zCheck_Negative_Exit( zCnt = read(fileno(zpFile), zpBufOUT, zSiz) );
    return zCnt;
}

// // 注意：fread 版的实现会将行末的换行符处理掉
// _i
// zget_str_content_1(char *zpBufOUT, size_t zSiz, FILE *zpFile) {
//     size_t zCnt = fread(zpBufOUT, zBytes(1), zSiz, zpFile);
//     if (zCnt < zSiz && (0 == feof(zpFile))) {
//         zPrint_Err(0, NULL, "<fread> ERROR!");
//         exit(1);
//     }
//     return zCnt;
// }

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
 * 纳秒时间，用于两个时间之间精确差值[ 计数有问题，且 CentOS-6 上不可用 ]
 */
// _d
// zreal_time() {
//     struct timespec zNanoSecIf;
//     if (0 > clock_gettime(CLOCK_REALTIME, &zNanoSecIf)) {
//         return -1.0;
//     } else {
//         return (zNanoSecIf.tv_sec + (((_d) zNanoSecIf.tv_nsec) / 1000000000));
//     }
// }

/*
 * 用于在单独线程中执行外部命令，如：定时拉取远程代码时，可以避免一个拉取动作卡住，导致后续的所有拉取都被阻塞
 */
void *
zthread_system(void *zpCmd) {
    if (NULL != zpCmd) { system((char *) zpCmd); }
    return NULL;
}

// /*
//  * 用途：
//  *   从字符串取按指定分割符逐一取出每个字段
//  * 返回值:
//  *   下一个字段的第一个字符在源字符串中的下标（index）
//  * 参数：
//  *   zpOffSet：定义一个整型变量赋值为0，之后循环传入此同一个变量即可
//  *   zpBufOUT：每一次循环后，存放的是取出的字段（子字符串，将原分割符替换为了'\0'）
//  *   zStrLen：是使用 strlen() 函数获得的源字符串的长度（不含 '\0'）
//  * 取值完毕判断条件：
//  *   以返回值大于 (zStrLen + 1) 为条件终止循环取字段
//  */
// _i
// zget_str_field(char *zpBufOUT, char *zpStr, _i zStrLen, char zDelimiter, _i *zpOffSet) {
//     _i i = 0;
//     for (; (*zpOffSet < zStrLen) && (zpStr[*zpOffSet] != zDelimiter); (*zpOffSet)++) {
//         zpBufOUT[i++] = zpStr[*zpOffSet];
//     }
//     zpBufOUT[i] = '\0';
//     (*zpOffSet)++;
//     return *zpOffSet;
// }

/*
 * 将文本格式的ip地址转换成二进制无符号整型(按网络字节序，即大端字节序)，以及反向转换
 */
_ui
zconvert_ip_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpAddr;
    zCheck_Negative_Exit( inet_pton(AF_INET, zpStrAddr, &zIpAddr) );
    return zIpAddr.s_addr;
}

void
zconvert_ip_bin_to_str(_ui zIpBinAddr, char *zpBufOUT) {
    struct in_addr zIpAddr;
    zIpAddr.s_addr = zIpBinAddr;
    inet_ntop(AF_INET, &zIpAddr, zpBufOUT, INET_ADDRSTRLEN);
}

// /*
//  * zget_one_line() 函数取出的行内容是包括 '\n' 的，此函数不会取到换行符
//  */
// _ui
// zconvert_ip_str_to_bin_1(char *zpStrAddr) {
//     char zBuf[INET_ADDRSTRLEN];
//     _uc zRes[4];
//     _i zOffSet = 0, zLen;
// 
//     if ((zLen = strlen(zpStrAddr)) > INET_ADDRSTRLEN) { return -1; }
// 
//     for (_i i = 0; i < 4 && ((1 + zLen) >= zget_str_field(zBuf, zpStrAddr, zLen, '.', &zOffSet)); i++) {
//         zRes[i] = (char)strtol(zBuf, NULL, 10);
//     }
// 
//     return *((_ui *)zRes);
// }

/*
 * json 解析回调：数字与字符串
 */
void
zparse_digit(void *zpIn, void *zpOut) {
    *((_i *)zpOut) = strtol(zpIn, NULL, 10);
}

void
zparse_str(void *zpIn, void *zpOut) {
    strcpy(zpOut, zpIn);  // 正则匹配出的结果，不会为 NULL，因此不必检查 zpIn
}

/*
 *  接收数据时使用
 *  将json文本转换为zMetaInfo结构体
 *  返回：出错返回-1，正常返回0
 */
_i
zconvert_json_str_to_struct(char *zpJsonStr, struct zMetaInfo *zpMetaIf) {
    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1] = {{.RepoId = -1}};  // 此时尚没取得 zpMetaIf->RepoIf 之值，不可使用项目内存池

    zreg_compile(zRegInitIf, "[^][}{\",:][^][}{\",]*");  // posix 的扩展正则语法中，中括号中匹配'[' 或 ']' 时需要将后一半括号放在第一个位置，而且不能转义
    zreg_match(zRegResIf, zRegInitIf, zpJsonStr);
    zReg_Free_Metasource(zRegInitIf);

    zRegResIf->cnt -= zRegResIf->cnt % 2;  // 若末端有换行、空白之类字符，忽略之

    void *zpBuf[128];
    zpBuf['O'] = &(zpMetaIf->OpsId);
    zpBuf['P'] = &(zpMetaIf->RepoId);
    zpBuf['R'] = &(zpMetaIf->CommitId);
    zpBuf['F'] = &(zpMetaIf->FileId);
    zpBuf['H'] = &(zpMetaIf->HostId);
    zpBuf['C'] = &(zpMetaIf->CacheId);
    zpBuf['D'] = &(zpMetaIf->DataType);
    zpBuf['d'] = zpMetaIf->p_data;
    zpBuf['E'] = zpMetaIf->p_ExtraData;

    for (_ui zCnter = 0; zCnter < zRegResIf->cnt; zCnter += 2) {
        if (NULL == zJsonParseOps[(_i)(zRegResIf->p_rets[zCnter][0])]) {
            strcpy(zpMetaIf->p_data, zpJsonStr);  // 必须复制，不能调整指针，zpJsonStr 缓存区会被上层调用者复用
            zReg_Free_Tmpsource(zRegResIf);
            return -7;
        }
        zJsonParseOps[(_i)(zRegResIf->p_rets[zCnter][0])](zRegResIf->p_rets[zCnter + 1], zpBuf[(_i)(zRegResIf->p_rets[zCnter][0])]);
    }

    zReg_Free_Tmpsource(zRegResIf);
    return 0;
}

/*
 * 生成缓存时使用
 * 将结构体数据转换成生成json文本
 */
void
zconvert_struct_to_json_str(char *zpJsonStrBuf, struct zMetaInfo *zpMetaIf) {
    sprintf(
            zpJsonStrBuf, ",{\"OpsId\":%d,\"CacheId\":%d,\"ProjId\":%d,\"RevId\":%d,\"FileId\":%d,\"DataType\":%d,\"data\":\"%s\",\"ExtraData\":\"%s\"}",
            zpMetaIf->OpsId,
            zpMetaIf->CacheId,
            zpMetaIf->RepoId,
            zpMetaIf->CommitId,
            zpMetaIf->FileId,
            zpMetaIf->DataType,
            (NULL == zpMetaIf->p_data) ? "_" : zpMetaIf->p_data,
            (NULL == zpMetaIf->p_ExtraData) ? "_" : zpMetaIf->p_ExtraData
            );
}

// /*
//  *  检查一个目录是否已存在
//  *  返回：1表示已存在，0表示不存在，-1表示出错
//  */
// _i
// zCheck_Dir_Existence(char *zpDirPath) {
//     _i zFd;
//     if (-1 == (zFd = open(zpDirPath, O_RDONLY | O_DIRECTORY))) {
//         if (EEXIST == errno) {
//             return 1;
//         } else {
//             return -1;
//         }
//     }
//     close(zFd);
//     return 0;
// }

/* 
 * 去除用字符串末尾的一个或多个换行符LB (Line Break)
 * 返回新的字符串长度，不含最后的 '\0'
 */
_i
zdel_LB(char *zpStr) {
    char *zpStrPtr = zpStr;
    _ui zStrLen = strlen(zpStr);

    while ('\n' == zpStrPtr[zStrLen - 1]) { zStrLen--; }
    zpStrPtr[zStrLen] = '\0';

    return zStrLen;
}
