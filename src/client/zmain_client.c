#define _XOPEN_SOURCE 700

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "../../inc/zutils.h"

#define zCommonBufSiz 1024
#define UDP 0
#define TCP 1
#define zRelativeRepoIdPath "info/repo_id"

struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
};

void *
zget_one_line(char *zpBufOUT, _i zSiz, FILE *zpFile) {
    char *zpRes = fgets(zpBufOUT, zSiz, zpFile);
    if (NULL == zpRes && (0 == feof(zpFile))) {
        zPrint_Err(0, NULL, "<fgets> ERROR!");
        exit(1);
    }
    return zpRes;
}

_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit( inet_pton(AF_INET, zpStrAddr, &zIpv4Addr) );
    return zIpv4Addr.s_addr;
}

_i
ztry_connect(struct sockaddr *zpAddr, socklen_t zLen, _i zSockType, _i zProto) {
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

struct addrinfo *
zgenerate_hint(_i zFlags) {
    static struct addrinfo zHints;
    zHints.ai_flags = zFlags;
    zHints.ai_family = AF_INET;
    return &zHints;
}

_i
ztcp_connect(char *zpHost, char *zpPort, _i zFlags) {
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
    _i zSentSiz = sendto(zSd, zpBuf, zLen, 0 | zFlags, zpAddr, INET_ADDRSTRLEN);
    zCheck_Negative_Return(zSentSiz, -1);
    return zSentSiz;
}

/*
 * 用于向中控机发送状态确认信息
 */
void
zstate_reply(char *zpHost, char *zpPort) {
    FILE *zpFileHandler;
    char zBuf[INET_ADDRSTRLEN] = {'\0'};
    char zJsonBuf[zCommonBufSiz] = {'\0'};
    _i zRepoId, zFd, zSd;
    _ui zIpv4Bin;

    zCheck_Negative_Exit( zFd = open(zRelativeRepoIdPath, O_RDONLY) );
    zCheck_Negative_Exit( read(zFd, &zRepoId, sizeof(_i)) );
    close(zFd);

    /* 读取本机的所有常规IPv4地址，依次发送状态确认信息至服务端 */
    zCheck_Null_Exit( zpFileHandler = popen("ip addr | grep -oP '(\\d+\\.){3}\\d+' | grep -vE '^(169|127|0|255)\\.$'", "r") );
    while (NULL != zget_one_line(zBuf, INET_ADDRSTRLEN, zpFileHandler)) {
        zBuf[strlen(zBuf) - 1] = '\0';  // 清除 '\n'，否则转换结果将错乱
        zIpv4Bin = zconvert_ipv4_str_to_bin(zBuf);

        if (-1== (zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
            zPrint_Err(0, NULL, "无法与中控机建立连接！");
            exit(1);
        }

        sprintf(zJsonBuf, "{\"OpsId\":8,\"ProjId\":%d,\"HostId\":%u}", zRepoId, zIpv4Bin);
        if ((_i)strlen(zJsonBuf) != zsendto(zSd, zJsonBuf, strlen(zJsonBuf), 0, NULL)) {
            zPrint_Err(0, NULL, "布署状态回复失败！");
        }

        shutdown(zSd, SHUT_RDWR);
    }

    fclose(zpFileHandler);
}

_i
main(_i zArgc, char **zppArgv) {
   struct zNetServInfo zNetServIf;
   zNetServIf.zServType = TCP;

   for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "Uh:p:"));) {
       switch (zOpt) {
           case 'h':
               zNetServIf.p_host= optarg; break;
           case 'p':
               zNetServIf.p_port = optarg; break;
           case 'U':
               zNetServIf.zServType = UDP;
           default:
               zPrint_Time();
               fprintf(stderr, "\033[31;01m无效选项!\033[00m\n");
               exit(1);
          }
   }

   zstate_reply(zNetServIf.p_host, zNetServIf.p_port);
   return 0;
}
