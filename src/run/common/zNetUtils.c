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
zgenerate_serv_SD(char *zpHost, char *zpPort, zProtoType__ zProtoType);

static _i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags);

static _i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr_, zIpType__ zIpType);

static _i
zsendmsg(_i zSd, struct iovec *zpVec_, size_t zVecSiz, _i zFlags, struct sockaddr *zpAddr_, zIpType__ zIpType);

static _i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr_);

static _i
zconvert_ip_str_to_bin(const char *zpStrAddr, zIpType__ zIpType, _ui *zpResOUT/* _ui[4] */);

static _i
zconvert_ip_bin_to_str(_ui *zpIpNumeric/* _ui[4] */, zIpType__ zIpType, char *zpResOUT/* char[INET6_ADDRSTRLEN] */);

struct zNetUtils__ zNetUtils_ = {
    .gen_serv_sd = zgenerate_serv_SD,
    .tcp_conn = ztcp_connect,
    .sendto = zsendto,
    .sendmsg = zsendmsg,
    .recv_all = zrecv_all,
    .to_numaddr = zconvert_ip_str_to_bin,
    .to_straddr = zconvert_ip_bin_to_str
};

/*
 * Start server: TCP or UDP,
 * Option zServType: 1 for TCP, 0 for UDP.
 */
static _i
zgenerate_serv_SD(char *zpHost, char *zpPort, zProtoType__ zProtoType) {
    _i zSd = -1,
       zErrNo = -1;
    struct addrinfo *zpRes_ = NULL,
                    *zpAddrInfo_ = NULL;
    struct addrinfo zHints_  = { .ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV };

    zHints_.ai_socktype = (zProtoUdp == zProtoType) ? SOCK_DGRAM : SOCK_STREAM;
    zHints_.ai_protocol = (zProtoUdp == zProtoType) ? IPPROTO_UDP:IPPROTO_TCP;

    if (0 != (zErrNo = getaddrinfo(zpHost, zpPort, &zHints_, &zpRes_))) {
        zPrint_Err(errno, NULL, gai_strerror(zErrNo));
        _exit(1);
    }

    for (zpAddrInfo_ = zpRes_; NULL != zpAddrInfo_; zpAddrInfo_ = zpAddrInfo_->ai_next) {
        if(0 < (zSd = socket( zpAddrInfo_->ai_family, zHints_.ai_socktype, zHints_.ai_protocol))) {
            break;
        }
    }
    zCheck_Negative_Exit(zSd);

    /* 不等待，直接重用地址与端口 */
#ifdef _Z_BSD
    zCheck_Negative_Exit( setsockopt(zSd, SOL_SOCKET, SO_REUSEPORT, &zSd, sizeof(_i)) );
#else
    zCheck_Negative_Exit( setsockopt(zSd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &zSd, sizeof(_i)) );
#endif

    zCheck_Negative_Exit( bind(zSd, zpRes_->ai_addr, (AF_INET6 == zpAddrInfo_->ai_family) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)) );
    zCheck_Negative_Exit( listen(zSd, 6) );

    freeaddrinfo(zpRes_);
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
ztry_connect(struct sockaddr *zpAddr_, _i zIpFamily, _i zSockType, _i zProto) {
    if (zSockType == 0) { zSockType = SOCK_STREAM; }
    if (zProto == 0) { zProto = IPPROTO_TCP; }

    _i zSd = socket(zIpFamily, zSockType, zProto);
    zCheck_Negative_Return(zSd, -1);

    zset_nonblocking(zSd);

    if (0 == connect(zSd, zpAddr_, (AF_INET6 == zIpFamily) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))) {
        return zSd;
    } else {  /* 多线程环境检查 errno == EINPROGRESS 也无意义 */
        struct pollfd zWd_ = {zSd, POLLIN | POLLOUT, -1};
        /*
         * poll 出错返回 -1，超时返回 0，
         * 在超时之前成功建立连接，则返回可用连接数量
         * connect 8 秒超时
         */
        if (0 < poll(&zWd_, 1, 8 * 1000)) { return zSd; }
    }

    /* 已超时或出错 */
    close(zSd);
    return -1;
}

/* Used by client */
static _i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
    _i zSd = -1,
       zErrNo = -1;
    struct addrinfo *zpRes_ = NULL,
                    *zpAddrInfo_ = NULL,
                    zHints_ = { .ai_flags = (0 == zFlags) ? AI_NUMERICHOST | AI_NUMERICSERV : zFlags };

    if (0 != (zErrNo = getaddrinfo(zpHost, zpPort, &zHints_, &zpRes_))){
        zPrint_Err(errno, NULL, gai_strerror(zErrNo));
        return -1;
    }

    for (zpAddrInfo_ = zpRes_; NULL != zpAddrInfo_; zpAddrInfo_ = zpAddrInfo_->ai_next) {
        if(0 < (zSd = ztry_connect( zpAddrInfo_->ai_addr, zpAddrInfo_->ai_family, SOCK_STREAM, IPPROTO_TCP))) {
            freeaddrinfo(zpRes_);
            return zSd;
        }
    }

    freeaddrinfo(zpRes_);
    return -1;
}

static _i
zsendto(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr_, zIpType__ zIpType) {
    _i zSentSiz = sendto(zSd, zpBuf, zLen, MSG_NOSIGNAL | zFlags,
            zpAddr_,
            (NULL == zpAddr_) ? 0: ((zIpTypeV6 == zIpType) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in))
            );
    return zSentSiz;
}

static _i
zsendmsg(_i zSd, struct iovec *zpVec_, size_t zVecSiz, _i zFlags, struct sockaddr *zpAddr_, zIpType__ zIpType) {
    if (NULL == zpVec_) { return -1; }

    struct msghdr zMsg_ = {
        .msg_name = zpAddr_,
        .msg_namelen = (NULL == zpAddr_) ? 0: ((zIpTypeV6 == zIpType) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)),
        .msg_iov = zpVec_,
        .msg_iovlen = zVecSiz,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = 0
    };

    return sendmsg(zSd, &zMsg_, MSG_NOSIGNAL | zFlags);
}

static _i
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr_) {
    socklen_t zAddrLen = 0;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr_, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

// static _i
// zrecv_nohang(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr_) {
//     socklen_t zAddrLen = 0;
//     _i zRecvSiz = 0;
//     if ((-1 == (zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr_, &zAddrLen)))
//             && (EAGAIN == errno)) {
//         zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_DONTWAIT | zFlags, zpAddr_, &zAddrLen);
//     }
//     return zRecvSiz;
// }

/*
 * 将文本格式的ip地址转换成二进制无符号整型数组(按网络字节序，即大端字节序)，以及反向转换
 * inet_pton: 返回 1 表示成功，返回 0 表示指定的地址无效，返回 -1 表示指定的ip类型错误
 */
static _i
zconvert_ip_str_to_bin(const char *zpStrAddr, zIpType__ zIpType, _ui *zpResOUT/* _ui[4] */) {
    _i zErrNo = -1;

    if (zIpTypeV6 == zIpType) {
        struct in6_addr zIp6Addr_ = {{{0}}};
        _ui *zp = (_ui *) (zIp6Addr_.__in6_u.__u6_addr8);

        if (1 == inet_pton(AF_INET6, zpStrAddr, &zIp6Addr_)) {
            zpResOUT[0] = * (zp + 3);
            zpResOUT[1] = * (zp + 2);
            zpResOUT[2] = * (zp + 1);
            zpResOUT[3] = * (zp + 0);

            zErrNo = 0;
        }
    } else {
        struct in_addr zIpAddr_ = {0};

        if (1 == inet_pton(AF_INET, zpStrAddr, &zIpAddr_)) {
            zpResOUT[0] = zIpAddr_.s_addr;
            zpResOUT[1] = 0;
            zpResOUT[2] = 0;
            zpResOUT[3] = 0;

            zErrNo = 0;
        }
    }

    return zErrNo;
}

static _i
zconvert_ip_bin_to_str(_ui *zpIpNumeric/* _ui[4] */, zIpType__ zIpType, char *zpResOUT/* char[INET6_ADDRSTRLEN] */) {
    _i zErrNo = -1;

    if (zIpType == zIpTypeV6) {
        struct in6_addr zIpAddr_ = {{{0}}};
        _uc *zp = (_uc *) zpIpNumeric;

        zIpAddr_.__in6_u.__u6_addr8[0] = * (zp + 12);
        zIpAddr_.__in6_u.__u6_addr8[1] = * (zp + 13);
        zIpAddr_.__in6_u.__u6_addr8[2] = * (zp + 14);
        zIpAddr_.__in6_u.__u6_addr8[3] = * (zp + 15);

        zIpAddr_.__in6_u.__u6_addr8[4] = * (zp + 8);
        zIpAddr_.__in6_u.__u6_addr8[5] = * (zp + 9);
        zIpAddr_.__in6_u.__u6_addr8[6] = * (zp + 10);
        zIpAddr_.__in6_u.__u6_addr8[7] = * (zp + 11);

        zIpAddr_.__in6_u.__u6_addr8[8] = * (zp + 4);
        zIpAddr_.__in6_u.__u6_addr8[9] = * (zp + 5);
        zIpAddr_.__in6_u.__u6_addr8[10] = * (zp + 6);
        zIpAddr_.__in6_u.__u6_addr8[11] = * (zp + 7);

        zIpAddr_.__in6_u.__u6_addr8[12] = * (zp + 0);
        zIpAddr_.__in6_u.__u6_addr8[13] = * (zp + 1);
        zIpAddr_.__in6_u.__u6_addr8[14] = * (zp + 2);
        zIpAddr_.__in6_u.__u6_addr8[15] = * (zp + 3);

        if (NULL != inet_ntop(AF_INET, &zIpAddr_, zpResOUT, INET6_ADDRSTRLEN)) { zErrNo = 0; }
    } else {
        struct in_addr zIpAddr_ = {0};
        zIpAddr_.s_addr = zpIpNumeric[0];
        if (NULL != inet_ntop(AF_INET, &zIpAddr_, zpResOUT, INET_ADDRSTRLEN)) { zErrNo = 0; }
    }

    return zErrNo;
}
