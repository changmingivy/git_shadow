#define _Z
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <pwd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>

#define zCommonBufSiz 1024
#include "../inc/zutils.h"

/****************
 * 数据结构定义 *
 ****************/
struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
};

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
struct zVecWrapInfo {
    _i VecSiz;
    struct iovec *p_VecIf;  // 此数组中的每个成员的 iov_base 字段均指向 p_RefDataIf 中对应的 p_SendIf 字段
    struct zRefDataInfo *p_RefDataIf;
};

struct zRecvInfo {
    _i OpsId;  // 操作指令（从0开始的连续排列的非负整数）
    _i RepoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i CacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i CommitId;  // 版本号（对应于svn或git的单次提交标识）
    _i FileId;  // 单个文件在差异文件列表中index
    _ui HostIp;  // 32位IPv4地址转换而成的无符号整型格式
    _i data[];  // 用于接收额外的数据，如：接收IP地址列表时
};

#define UDP 0
#define TCP 1

/************
 * 配置文件 *
 ************/
// 以下路径均是相对于所属代码库的顶级路径
#define zSelfIpPath ".git_shadow/info/host_ip_self.bin"  // 格式同上，存储客户端自身的ipv4地址
#define zAllIpTxtPath ".git_shadow/info/host_ip_all.txt"  // 存储点分格式的原始字符串ipv4地下信息，如：10.10.10.10
#define zMajorIpTxtPath ".git_shadow/info/host_ip_major.txt"  // 与布署中控机直接对接的master机的ipv4地址（点分格式），目前是zdeploy.sh使用，后续版本使用libgit2库之后，将转为内部直接使用
#define zRepoIdPath ".git_shadow/info/repo_id"
#define zLogPath ".git_shadow/log/deploy/meta"

/**********
 * 子模块 *
 **********/
#include "utils/zbase_utils.c"

/*
 * 主机更新自身ipv4数据库文件
 */
void
zupdate_ipv4_db_self(_i zBaseFd) {
    FILE *zpFileHandler;
    char zBuf[zCommonBufSiz];
    _ui zIpv4Addr;
    _i zFd;

    zCheck_Negative_Exit(zFd = openat(zBaseFd, zSelfIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit( zpFileHandler = popen("ip addr | grep -oP '(\\d{1,3}\\.){3}\\d{1,3}' | grep -v 127", "r") );
    while (NULL != zget_one_line(zBuf, zCommonBufSiz, zpFileHandler)) {
        zIpv4Addr = zconvert_ipv4_str_to_bin(zBuf);
        if (zSizeOf(_ui) != write(zFd, &zIpv4Addr, zSizeOf(_ui))) {
            zPrint_Err(0, NULL, "自身IP地址更新失败!");
            exit(1);
        }
    }

    fclose(zpFileHandler);
    close(zFd);
}

/*
 * 用于集群中的主机向中控机发送状态确认信息
 */
void
zstate_reply(char *zpHost, char *zpPort) {
	struct zRecvInfo zDpResIf;
    _i zFd, zSd, zResLen;
    _ui zIpv4Bin;

    zCheck_Negative_Exit( zFd = open(zRepoIdPath, O_RDONLY) );
	/* 读取版本库ID */
    zCheck_Negative_Exit( read(zFd, &(zDpResIf.RepoId), sizeof(_i)) );
	/* 更新自身 ip 地址 */
	zupdate_ipv4_db_self(zFd);
    close(zFd);
	/* 填充动作代号 */
	zDpResIf.OpsId = 10;
	/* 以点分格式的ipv4地址连接服务端 */
    if (-1== (zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        zPrint_Err(0, NULL, "无法与中控机建立连接！");
        exit(1);
    }
	/* 读取本机的所有非回环ip地址，依次发送状态确认信息至服务端 */
    zCheck_Negative_Exit( zFd = open(zSelfIpPath, O_RDONLY) );

    while (0 < (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
        zDpResIf.HostIp = zIpv4Bin;
        if (sizeof(zDpResIf) != zsendto(zSd, &zDpResIf, sizeof(zDpResIf), 0, NULL)) {
            zPrint_Err(0, NULL, "布署状态信息回复失败！");
        }
    }

    shutdown(zSd, SHUT_RDWR);
    close(zFd);
}

_i
main(_i zArgc, char **zppArgv) {
// TEST: PASS
    struct zNetServInfo zNetServIf;  // 指定客户端要连接的目标服务器的Ipv4地址与端口
    zNetServIf.zServType = TCP;

    for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "Uh:p:"));) {
        switch (zOpt) {
            case 'h':
                zNetServIf.p_host= optarg; break;
            case 'p':
                zNetServIf.p_port = optarg; break;
            case 'U':
                zNetServIf.zServType = UDP;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPrint_Time();
                fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
                exit(1);
           }
    }

    zstate_reply(zNetServIf.p_host, zNetServIf.p_port);
    return 0;
}
