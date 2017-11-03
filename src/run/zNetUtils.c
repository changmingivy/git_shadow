#include "zNetUtils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>

static _i
zgenerate_serv_SD(char *zpHost, char *zpPort, _i zServType);

static _i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags);

static _i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr);

static _i
zsendmsg(_i zSd, struct iovec *zpVec_, size_t zVecSiz, _i zFlags, struct sockaddr *zpAddr);

static _i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr);

static _ui
zconvert_ip_str_to_bin(const char *zpStrAddr);

static void
zconvert_ip_bin_to_str(_ui zIpBinAddr, char *zpBufOUT);

struct zNetUtils__ zNetUtils_ = {
    .gen_serv_sd = zgenerate_serv_SD,
    .tcp_conn = ztcp_connect,
    .sendto = zsendto,
    .sendmsg = zsendmsg,
    .recv_all = zrecv_all,
    .to_bin = zconvert_ip_str_to_bin,
    .to_str = zconvert_ip_bin_to_str
};

/*
 * Functions for socket connection.
 */
static struct addrinfo *
zgenerate_hint(_i zFlags) {
    static struct addrinfo zHints;
    zHints.ai_flags = zFlags;
    zHints.ai_family = AF_INET;
    return &zHints;
}

/* Generate a socket fd used by server to do 'accept' */
static struct sockaddr *
zgenerate_serv_addr(char *zpHost, char *zpPort) {
    struct addrinfo *zpRes, *zpHint;
    zpHint = zgenerate_hint(AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV);

    _i zErr = getaddrinfo(zpHost, zpPort, zpHint, &zpRes);
    if (-1 == zErr){
        zPrint_Err(errno, NULL, gai_strerror(zErr));
        _exit(1);
    }

    return zpRes->ai_addr;
}

/*
 * Start server: TCP or UDP,
 * Option zServType: 1 for TCP, 0 for UDP.
 */
static _i
zgenerate_serv_SD(char *zpHost, char *zpPort, _i zServType) {
    _i zSockType = (0 == zServType) ? SOCK_DGRAM : SOCK_STREAM;
    _i zSd = socket(AF_INET, zSockType, 0);
    zCheck_Negative_Return(zSd, -1);

    _i zReuseMark = 1;
#ifdef _Z_BSD
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEPORT, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址与端口
#else
    zCheck_Negative_Exit(setsockopt(zSd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &zReuseMark, sizeof(_i)));  // 不等待，直接重用地址与端口
#endif
    struct sockaddr *zpAddr_ = zgenerate_serv_addr(zpHost, zpPort);
    zCheck_Negative_Return(bind(zSd, zpAddr_, INET_ADDRSTRLEN), -1);

    zCheck_Negative_Return(listen(zSd, 5), -1);

    return zSd;
}

/*
 *  将指定的套接字属性设置为非阻塞
 */
static void
zset_nonblocking(_i zSd) {
    _i zOpts;
    zCheck_Negative_Exit( zOpts = fcntl(zSd, F_GETFL) );
    zOpts |= O_NONBLOCK;
    zCheck_Negative_Exit( fcntl(zSd, F_SETFL, zOpts) );
}

/* Used by client */
static _i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zSockType, _i zProto) {
    if (zSockType == 0) { zSockType = SOCK_STREAM; }
    if (zProto == 0) { zProto = IPPROTO_TCP; }

    _i zSd = socket(AF_INET, zSockType, zProto);
    zCheck_Negative_Return(zSd, -1);

    zset_nonblocking(zSd);

    if (0 == connect(zSd, zpAddr, zLen)) {
        return zSd;
    } else {
        struct pollfd zWd_ = {zSd, POLLIN | POLLOUT, -1};
        /*
         * poll 出错返回 -1，超时返回 0，
         * 在超时之前成功建立连接，则返回可用连接数量
         */
        if (0 < poll(&zWd_, 1, 10 * 1000)) { return zSd; }
        else { close(zSd); return -1; }  // 超时或出错
    }
}

/* Used by client */
static _i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
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

static _i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
    _i zSentSiz = sendto(zSd, zpBuf, zLen, MSG_NOSIGNAL | zFlags, zpAddr, INET_ADDRSTRLEN);
    return zSentSiz;
}

static _i
zsendmsg(_i zSd, struct iovec *zpVec_, size_t zVecSiz, _i zFlags, struct sockaddr *zpAddr) {
    if (NULL == zpVec_) { return -1; }

    struct msghdr zMsg_ = {
        .msg_name = zpAddr,
        .msg_namelen = (NULL == zpAddr) ? 0 : INET6_ADDRSTRLEN,
        .msg_iov = zpVec_,
        .msg_iovlen = zVecSiz,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    return sendmsg(zSd, &zMsg_, MSG_NOSIGNAL | zFlags);
}

static _i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

// static _i
// zrecv_nohang(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
//     socklen_t zAddrLen;
//     _i zRecvSiz;
//     if ((-1 == (zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr, &zAddrLen)))
//             && (EAGAIN == errno)) {
//         zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr, &zAddrLen);
//     }
//     return zRecvSiz;
// }

/*
 * 将文本格式的ip地址转换成二进制无符号整型(按网络字节序，即大端字节序)，以及反向转换
 */
static _ui
zconvert_ip_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpAddr;
    zCheck_Negative_Exit( inet_pton(AF_INET, zpStrAddr, &zIpAddr) );
    return zIpAddr.s_addr;
}

static void
zconvert_ip_bin_to_str(_ui zIpBinAddr, char *zpBufOUT) {
    struct in_addr zIpAddr;
    zIpAddr.s_addr = zIpBinAddr;
    inet_ntop(AF_INET, &zIpAddr, zpBufOUT, INET_ADDRSTRLEN);
}

// /*
//  * zget_one_line() 函数取出的行内容是包括 '\n' 的，此函数不会取到换行符
//  */
// static _ui
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
