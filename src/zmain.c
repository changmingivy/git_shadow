#define _Z
//#define _Z_BSD

#ifndef _Z_BSD
    #define _XOPEN_SOURCE 700
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifdef _Z_BSD
    #include <netinet/in.h>
    #include <signal.h>
#else
    #include <sys/signal.h>
#endif

#include <sys/mman.h>
#include <pthread.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <pwd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include <regex.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>

#include "../inc/zutils.h"

#define zGlobBufSiz 1024
#define zErrMsgBufSiz 256

#define zCacheSiz IOV_MAX  // 顶层缓存单元数量取 IOV_MAX
#define zSendUnitSiz 8  // sendmsg 单次发送的单元数量，在 Linux 平台上设定为 <=8 的值有助于提升性能
#define zMemPoolSiz 8 * 1024 * 1024  // 内存池初始分配 8M 内存

#define zDpHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数
#define zWatchHashSiz 1024  // 最多可监控的路径总数
#define zServHashSiz 16

#define zForecastedHostNum 200  // 预测的目标主机数量上限

#define UDP 0
#define TCP 1

#define zDpUnLock 0
#define zDpLocked 1

#define zRepoGood 0
#define zRepoDamaged 1

#define zIsCommitDataType 0
#define zIsDpDataType 1

/****************
 * 数据结构定义 *
 ****************/
struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
};
typedef struct zNetServInfo zNetServInfo;

/* 数据交互格式 */
struct zMetaInfo {
    _i OpsId;  // 网络交互时，代表操作指令（从0开始的连续排列的非负整数）；当用于生成缓存时，-1代表commit记录，-2代表deploy记录
    _i RepoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i CommitId;  // 版本号（对应于svn或git的单次提交标识）
    _i FileId;  // 单个文件在差异文件列表中index
    _ui HostId;  // 32位IPv4地址转换而成的无符号整型格式
    _i CacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i DataType;  // 缓存类型，zIsCommitDataType/zIsDpDataType
    char *p_data;  // 数据正文，发数据时可以是版本代号、文件路径等(此时指向zRefDataInfo的p_data)等，收数据时可以是接IP地址列表(此时额外分配内存空间)等
    _ui DataLen;
    char *p_ExtraData;  // 附加数据，如：字符串形式的UNIX时间戳、IP总数量等
    _ui ExtraDataLen;

    /* 以下为 Tree 专属数据 */
    struct zMetaInfo *p_father;  // Tree 父节点
    struct zMetaInfo *p_left;  // Tree 左节点
    struct zMetaInfo *p_FirstChild;  // Tree 首子节点：父节点唯一直接相连的子节点
    struct zMetaInfo **pp_ResHash;  // Tree 按行号对应的散列
    _i LineNum;  // 行号
    _i OffSet;  // 纵向偏移

    pthread_cond_t *p_CondVar;
    pthread_mutex_t *p_MutexLock;
    _i *p_TotalTask;  // 任务总数量
    _i *p_FinCnter;  // 已完成的任务计数
};
typedef struct zMetaInfo zMetaInfo;

/* 用于提取原始数据 */
struct zBaseDataInfo {
    struct zBaseDataInfo *p_next;
    _i DataLen;
    char p_data[];
};
typedef struct zBaseDataInfo zBaseDataInfo;

/* 在zSendInfo之外，添加了：本地执行操作时需要，但对前端来说不必要的数据段 */
struct zRefDataInfo {
    struct zVecWrapInfo *p_SubVecWrapIf;  // 传递给 sendmsg 的下一级数据
    char *p_data;  // 实际存放数据正文的地方
};
typedef struct zRefDataInfo zRefDataInfo;

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
struct zVecWrapInfo {
    _i VecSiz;
    struct iovec *p_VecIf;  // 此数组中的每个成员的 iov_base 字段均指向 p_RefDataIf 中对应的 p_data 字段
    struct zRefDataInfo *p_RefDataIf;
};
typedef struct zVecWrapInfo zVecWrapInfo;

struct zDpResInfo {
    _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _i DpState;  // 布署状态：已返回确认信息的置为1，否则保持为 -1
    _i InitState;  // 远程主机初始化状态：已返回确认信息的置为1，否则保持为 -1
    char ErrMsg[zErrMsgBufSiz];  // 存放目标主机返回的错误信息
    struct zDpResInfo *p_next;
};
typedef struct zDpResInfo zDpResInfo;

/* 用于存放每个项目的元信息，同步锁不要紧挨着定义，在X86平台上可能会带来伪共享问题降低并发性能 */
struct zRepoInfo {
    _i RepoId;  // 项目代号
    time_t  CacheId;  // 即：最新一次布署的时间戳(初始化为1000000000)
    _ui TotalHost;  // 每个项目的集群的主机数量
    char *p_RepoPath;  // 项目路径，如："/home/git/miaopai_TEST"
    _i RepoPathLen;  // 项目路径长度，避免后续的使用者重复计算
    _i MaxPathLen;  // 项目最大路径长度：相对于项目根目录的值（由底层文件系统决定），用于度量git输出的差异文件相对路径长度

    _i SelfPushMark;  // 置为 1 表示该项目会主动推送代码到中控机，不需要拉取远程代码
    char *p_PullCmd;  // 拉取代码时执行的Shell命令：svn与git有所不同
    time_t LastPullTime;  // 最近一次拉取的时间，若与之的时间间隔较短，则不重复拉取
    pthread_mutex_t PullLock;  // 保证同一时间同一个项目只有一个git pull进程在运行

    _i DpSigLogFd;  // 每个代码库的布署日志日志文件，用于存储 SHA1-sig+TimeStamp
    _i DpTimeSpentLogFd;  // 布署耗时日志

    /* FinMark 类标志：0 代表动作尚未完成，1 代表已完成 */
    char zInitRepoFinMark;

    /* 0：非锁定状态，允许布署或撤销、更新ip数据库等写操作 */
    /* 1：锁定状态，拒绝执行布署、撤销、更新ip数据库等写操作，仅提供查询功能 */
    _i DpLock;

    /* 用于区分是布署动作占用写锁还是生成缓存占用写锁：1 表示布署占用，0 表示生成缓存占用 */
    _i zWhoGetWrLock;
    /* 远程主机初始化或布署动作开始时间，用于统计每台目标机器大概的布署耗时*/
    time_t  DpBaseTimeStamp;
    /* 布署超时上限 */
    time_t DpTimeWaitLimit;
    /* 目标机在重要动作执行前回发的keep alive消息 */
    time_t DpKeepAliveStamp;

    /* Tree 图专用 */
    pthread_cond_t TreeCondVar;  // 条件变量
    pthread_mutex_t TreeMutexLock;
    _i TreeTotalTask;  // 总任务数（节点数）
    _i TreeFinCnter;  // 线程已完成的任务计数

    /* 代码库状态，若上一次布署／撤销失败，此项置为 zRepoDamaged 状态，用于提示用户看到的信息可能不准确 */
    _i RepoState;
    char zLastDpSig[44];  // 存放最近一次布署的 40 位 SHA1 sig
    char zDpingSig[44];  // 正在布署过程中的版本号，用于布署耗时分析

    char ProxyHostStrAddr[16];  // 代理机 IPv4 地址，最长格式16字节，如：123.123.123.123\0
    char HostStrAddrList[2][4 + 16 * zForecastedHostNum];  // 若 IPv4 地址数量不超过 zForecastedHostNum 个，则使用该内存，若超过，则另行静态分配
    char *p_HostStrAddrList[2];  // [0]：以文本格式存储的 IPv4 地址列表，作为参数传给 zdeploy.sh 脚本；[1] 用于收集新增的IP，增量初始化远程主机
    struct zDpResInfo *p_DpResListIf;  // 1、更新 IP 时对比差异；2、收集布署状态
    struct zDpResInfo *p_DpResHashIf[zDpHashSiz];  // 对上一个字段每个值做的散列

    pthread_rwlock_t RwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    pthread_mutex_t DpRetryLock;  // 用于分离失败重试布署与生成缓存之间的锁竞争
    //pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先

    struct zVecWrapInfo CommitVecWrapIf;  // 存放 commit 记录的原始队列信息
    struct iovec CommitVecIf[zCacheSiz];
    struct zRefDataInfo CommitRefDataIf[zCacheSiz];

    struct zVecWrapInfo SortedCommitVecWrapIf;  // 存放经过排序的 commit 记录的缓存队列信息，提交记录总是有序的，不需要再分配静态空间

    _ui ReplyCnt[2];  // [0] 远程主机初始化成功计数；[1] 布署成功计数
    pthread_mutex_t ReplyCntLock;  // 用于保证 ReplyCnt 计数的正确性

    struct zVecWrapInfo DpVecWrapIf;  // 存放 deploy 记录的原始队列信息
    struct iovec DpVecIf[zCacheSiz];
    struct zRefDataInfo DpRefDataIf[zCacheSiz];

    struct zVecWrapInfo SortedDpVecWrapIf;  // 存放经过排序的 deploy 记录的缓存（从文件里直接取出的是旧的在前面，需要逆向排序）
    struct iovec SortedDpVecIf[zCacheSiz];

    void *p_MemPool;  // 线程内存池，预分配 16M 空间，后续以 8M 为步进增长
    pthread_mutex_t MemLock;  // 内存池锁
    _ui MemPoolOffSet;  // 动态指示下一次内存分配的起始地址
};
typedef struct zRepoInfo zRepoInfo;

/************
 * 全局变量 *
 ************/
_i zGlobMaxRepoId = -1;  // 所有项目ID中的最大值
struct zRepoInfo **zppGlobRepoIf;

/* 服务接口 */
typedef _i (* zNetOpsFunc) (struct zMetaInfo *, _i);  // 网络服务回调函数
zNetOpsFunc zNetServ[zServHashSiz];

/* 以 ANSI 字符集中的前 128 位成员作为索引 */
typedef void (* zJsonParseFunc) (void *, void *);
zJsonParseFunc zJsonParseOps[128];

/************
 * 配置文件 *
 ************/
#define zRepoIdPath "_SHADOW/info/repo_id"
#define zDpSigLogPath "_SHADOW/log/deploy/meta"  // 40位SHA1 sig字符串 + 时间戳
#define zDpTimeSpentLogPath "_SHADOW/log/deploy/TimeSpent"  // 40位SHA1 sig字符串 + 时间戳

/**********
 * 子模块 *
 **********/
/* 专用于缓存的内存调度分配函数，适用多线程环境，不需要free */
void *
zalloc_cache(_i zRepoId, size_t zSiz) {
    pthread_mutex_lock(&(zppGlobRepoIf[zRepoId]->MemLock));

    if ((zSiz + zppGlobRepoIf[zRepoId]->MemPoolOffSet) > zMemPoolSiz) {
        void **zppPrev, *zpCur;
        /* 新增一块内存区域加入内存池，以上一块内存的头部预留指针位存储新内存的地址 */
        if (MAP_FAILED == (zpCur = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
            zPrint_Time();
            fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
            exit(1);
        }
        zppPrev = zpCur;
        zppPrev[0] = zppGlobRepoIf[zRepoId]->p_MemPool;  // 首部指针位指向上一块内存池map区
        zppGlobRepoIf[zRepoId]->p_MemPool = zpCur;  // 更新当前内存池指针
        zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);  // 初始化新内存池区域的 offset
    }

    void *zpX = zppGlobRepoIf[zRepoId]->p_MemPool + zppGlobRepoIf[zRepoId]->MemPoolOffSet;
    zppGlobRepoIf[zRepoId]->MemPoolOffSet += zSiz;

    pthread_mutex_unlock(&(zppGlobRepoIf[zRepoId]->MemLock));
    return zpX;
}

#include "utils/posix_regex/zregex.c"
#include "utils/zbase_utils.c"
//#include "utils/md5_sig/zgenerate_sig_md5.c"  // 生成MD5 checksum检验和
#include "utils/thread_pool/zthread_pool.c"
#include "utils/zserv_utils.c"
#include "core/zserv.c"  // 对外提供网络服务

/***************************
 * +++___ main 函数 ___+++ *
 ***************************/
_i
main(_i zArgc, char **zppArgv) {
    char *zpConfFilePath = NULL;
    struct stat zStatIf;
    struct zNetServInfo zNetServIf;  // 指定服务端自身的Ipv4地址与端口
    zNetServIf.zServType = TCP;

    for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "Uh:p:f:"));) {
        switch (zOpt) {
            case 'h':
                zNetServIf.p_host= optarg; break;
            case 'p':
                zNetServIf.p_port = optarg; break;
            case 'U':
                zNetServIf.zServType = UDP;
            case 'f':
                if (-1 == stat(optarg, &zStatIf) || !S_ISREG(zStatIf.st_mode)) {
                        zPrint_Time();
                        fprintf(stderr, "\033[31;01m配置文件异常!\n用法: %s -f <PATH>\033[00m\n", zppArgv[0]);
                        exit(1);
                }
                zpConfFilePath = optarg;
                break;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPrint_Time();
                fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
                exit(1);
           }
    }

    zdaemonize("/");  // 转换自身为守护进程，解除与终端的关联关系
    zthread_poll_init();  // 初始化线程池：旧的线程池设计，在大压力下应用有阻死风险，暂不用之
    zinit_env(zpConfFilePath);  // 运行环境初始化
    zstart_server(&zNetServIf);  // 启动网络服务
}
