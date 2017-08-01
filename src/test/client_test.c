#define _Z
#define _zDEBUG
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

#include <pthread.h>
#include <sys/mman.h>

#include <sys/inotify.h>
#include <sys/epoll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>

#define zCommonBufSiz 1024
#include "../../inc/zutils.h"

#define zWatchHashSiz 8192  // 最多可监控的路径总数
#define zDeployHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数

#define zCacheSiz 1009
#define zPreLoadCacheSiz 10  // 版本批次及其下属的文件列表与内容缓存

/****************
 * 数据结构定义 *
 ****************/
typedef void (* zThreadPoolOps) (void *);  // 线程池回调函数
///////////////////////////////////////////////////////////////////////////////////////////////////
struct zObjInfo {
    _s RepoId;  // 每个代码库对应的索引
    _s RecursiveMark;  // 是否递归标志
    _i UpperWid;  // 存储顶层路径的watch id，每个子路径的信息中均保留此项
    char *zpRegexPattern;  // 符合此正则表达式的目录或文件将不被inotify监控
    zThreadPoolOps CallBack;  // 发生事件中对应的回调函数
    char path[];  // 被监控对象的绝对路径名称
};

struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
};
///////////////////////////////////////////////////////////////////////////////////////////////////
struct zCacheMetaInfo {  // 适用线程并发模型
    _i TopObjTypeMark;  // 0 表示 commit cache，1 表示  deploy cache
    _i RepoId;
    _i CommitId;
    _i FileId;
};

/* 用于接收前端传送的数据 */
struct zRecvInfo {
    _i OpsId;  // 操作指令（从0开始的连续排列的非负整数）
    _i RepoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i CacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i CommitId;  // 版本号（对应于svn或git的单次提交标识）
    _i FileId;  // 单个文件在差异文件列表中index
    _ui HostIp;  // 32位IPv4地址转换而成的无符号整型格式
    _i data[];  // 用于接收额外的数据，如：接收IP地址列表时
};

/* 用于向前端发送数据，struct iovec 中的 iov_base 字段指向此结构体 */
struct zSendInfo {
    _i SelfId;
    _i DataLen;
    _i data[];
};

/* 在zSendInfo之外，添加了：本地执行操作时需要，但对前端来说不必要的数据段 */
struct zRefDataInfo {
    struct zVecWrapInfo *p_SubVecWrapIf;  // 传递给 sendmsg 的下一级数据
    void *p_data;  // 当处于单个 Commit 记录级别时，用于存放 CommitSig 字符串格式，包括末尾的'\0'
};

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
struct zVecWrapInfo {
    _i VecSiz;
    struct iovec *p_VecIf;  // 此数组中的每个成员的 iov_base 字段均指向 p_RefDataIf 中对应的 p_SendIf 字段
    struct zRefDataInfo *p_RefDataIf;
};

struct zDeployResInfo {
    _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _i RepoId;  // 所属代码库
    _i DeployState;  // 布署状态：已返回确认信息的置为1，否则保持为0
    struct zDeployResInfo *p_next;
};

/* 用于存放每个项目的元信息 */
struct zRepoInfo {
    _i RepoId;  // 项目代号
    char RepoPath[64];  // 项目路径，如："/home/git/miaopai_TEST"
    _i LogFd;  // 每个代码库的布署日志日志文件：log/sig，用于存储 SHA1-sig

    _i TotalHost;  // 每个项目的集群的主机数量
    _ui *p_FailingList;  // 初始化时，分配 TotalHost 个 _ui 的内存空间，用于每次布署时收集尚未布署成功的主机列表

    pthread_rwlock_t RwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先

    void *p_MemPool;  // 线程内存池，预分配 16M 空间，后续以 8M 为步进增长
    size_t MemPoolSiz;  // 内存池初始大小：8M
    pthread_mutex_t MemLock;  // 内存池锁
    _ui MemPoolHeadId;  // 动态指示下一次内存分配的起始地址

    _i CacheId;  // 即：最新一次布署的时间戳(CURRENT 分支的时间戳，没有布署日志时初始化为0)

    /* 0：非锁定状态，允许布署或撤销、更新ip数据库等写操作 */
    /* 1：锁定状态，拒绝执行布署、撤销、更新ip数据库等写操作，仅提供查询功能 */
    _i DpLock;

    _i ReplyCnt;  // 用于动态汇总单次布署或撤销动作的统计结果
    pthread_mutex_t MutexLock;  // 用于保证 ReplyCnt 计数的正确性

    struct zDeployResInfo *p_DpResList;  // 布署状态收集
    struct zDeployResInfo *p_DpResHash[zDeployHashSiz];  // 对上一个字段每个值做的散列

    _i CommitCacheQueueHeadId;  // 用于标识提交记录列表的队列头索引序号（index）
    struct zVecWrapInfo CommitVecWrapIf;  // 存放 commit 记录的原始队列信息
    struct iovec CommitVecIf[zCacheSiz];
    struct zRefDataInfo CommitRefDataIf[zCacheSiz];

    struct zVecWrapInfo SortedCommitVecWrapIf;  // 存放经过排序的 commit 记录的缓存队列信息
    struct iovec SortedCommitVecIf[zCacheSiz];

    struct zVecWrapInfo DeployVecWrapIf;  // 存放 deploy 记录的原始队列信息
    struct iovec DeployVecIf[zCacheSiz];
    struct zRefDataInfo DeployRefDataIf[zCacheSiz];
};

struct zRepoInfo *zpGlobRepoIf;

/************
 * 全局变量 *
 ************/
_i zGlobRepoNum;  // 总共有多少个代码库

_i zInotifyFD;   // inotify 主描述符
struct zObjInfo *zpObjHash[zWatchHashSiz];  // 以watch id建立的HASH索引

#define UDP 0
#define TCP 1

/************
 * 配置文件 *
 ************/
// 以下路径均是相对于所属代码库的顶级路径
#define zAllIpPath ".git_shadow/info/host_ip_all.bin"  // 位于各自代码库路径下，以二进制形式存储后端所有主机的ipv4地址
#define zSelfIpPath ".git_shadow/info/host_ip_self.bin"  // 格式同上，存储客户端自身的ipv4地址
#define zAllIpTxtPath ".git_shadow/info/host_ip_all.txt"  // 存储点分格式的原始字符串ipv4地下信息，如：10.10.10.10
#define zMajorIpTxtPath ".git_shadow/info/host_ip_major.txt"  // 与布署中控机直接对接的master机的ipv4地址（点分格式），目前是zdeploy.sh使用，后续版本使用libgit2库之后，将转为内部直接使用
#define zRepoIdPath ".git_shadow/info/repo_id"
#define zLogPath ".git_shadow/log/deploy/sig"  // 40位SHA1 sig字符串，需要通过meta日志提供的索引访问

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
zrecv_all(_i zSd, void *zpBuf, size_t zLen, _i zFlags, struct sockaddr *zpAddr) {
// TEST: PASS
    socklen_t zAddrLen;
    _i zRecvSiz = recvfrom(zSd, zpBuf, zLen, MSG_WAITALL | zFlags, zpAddr, &zAddrLen);
    zCheck_Negative_Return(zRecvSiz, -1);
    return zRecvSiz;
}

void
zclient(char *zpX) {
        _i zSd = ztcp_connect("10.30.2.126", "20000", AI_NUMERICHOST | AI_NUMERICSERV);  // 以点分格式的ipv4地址连接服务端
           if (-1 == zSd) {
            fprintf(stderr, "Connect to server failed \n");
            exit(1);
        }
        char zStrBuf[] = "{\"OpsId\":1,\"RepoId\":88,\"CommitId\":1007,\"FileId\":-1,\"HostId\":0,\"CacheId\":1000000000,\"DataType\":0,\"Data\":\"88 /home/git/nnnrepo https://git.coding.net/kt10/zgit_shadow.git master git\"}";
        zsendto(zSd, zStrBuf, strlen(zStrBuf), 0, NULL);

        char zBuf[4096];
        recv(zSd, &zBuf, 4096, MSG_WAITALL);
        fprintf(stderr, "%s\n", zBuf);

        memset(zBuf, 0, 4096);
        recv(zSd, &zBuf, 4096, MSG_WAITALL);
        fprintf(stderr, "\n2:-------%s\n", zBuf);
//        for (_ui i = 0; i < 4096; i++) {
//            printf("%c", zBuf[i]);
//        }
        //fprintf(stderr, "%s", zBuf + 1 + strlen(zBuf));
        //fprintf(stderr, "%s", zBuf + 1 + strlen(zBuf + 1 + strlen(zBuf)));

        shutdown(zSd, SHUT_RDWR);
}

_i
main(_i zArgc, char **zppArgv) {
        zclient(zppArgv[1]);
}
