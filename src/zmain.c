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
#include <pthread.h>
#include <sys/mman.h>
#include <pwd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include <sys/inotify.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>

#include "../inc/zutils.h"

#define zCommonBufSiz 1024

#define zWatchHashSiz 8192  // 最多可监控的路径总数
#define zDeployHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数

#define zCacheSiz IOV_MAX  // 顶层缓存单元数量取 IOV_MAX
#define zPreLoadCacheSiz 3  // 版本批次及其下属的文件列表与内容缓存
#define zMemPoolSiz 8 * 1024 * 1024  // 内存池初始分配 8M 内存

#define zServHashSiz 14

#define zUnitSiz 8  // 分散聚离IO分段数量大于8时，效率会降低

#define UDP 0
#define TCP 1

#define zDeployUnLock 0
#define zDeployLocked 1

#define zRepoGood 0
#define zRepoDamaged 1

#define zIsCommitDataType 0
#define zIsDeployDataType 1

/****************
 * 数据结构定义 *
 ****************/
typedef void (* zThreadPoolOps) (void *);  // 线程池回调函数
///////////////////////////////////////////////////////////////////////////////////////////////////
struct zObjInfo {
    _s RepoId;  // 每个代码库对应的索引
    _s RecursiveMark;  // 是否递归标志
    _i UpperWid;  // 存储顶层路径的watch id，每个子路径的信息中均保留此项
    zThreadPoolOps CallBack;  // 发生事件中对应的回调函数
    char p_path[];  // 被监控对象的绝对路径名称
};
typedef struct zObjInfo zObjInfo;

struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
};
typedef struct zNetServInfo zNetServInfo;
///////////////////////////////////////////////////////////////////////////////////////////////////
/* 数据交互格式 */
struct zMetaInfo {
    _i OpsId;  // 网络交互时，代表操作指令（从0开始的连续排列的非负整数）；当用于生成缓存时，-1代表commit记录，-2代表deploy记录
    _i RepoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i CommitId;  // 版本号（对应于svn或git的单次提交标识）
    _i FileId;  // 单个文件在差异文件列表中index
    _ui HostId;  // 32位IPv4地址转换而成的无符号整型格式
    _i CacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i DataType;  // 缓存类型，zIsCommitDataType/zIsDeployDataType
    char *p_data;  // 数据正文，发数据时可以是版本代号、文件路径等(此时指向zRefDataInfo的p_data)等，收数据时可以是接IP地址列表(此时额外分配内存空间)等
    char *p_ExtraData;  // 附加数据，如：字符串形式的UNIX时间戳、IP总数量等

    pthread_cond_t *p_CondVar;  // 条件变量
    _i *p_FinMark;  // 值为 1 表示调用者已分发完所有的任务；值为 0 表示正在分发过程中
    _i *p_SelfCnter;  // 调用者任务发放计数
    _i *p_ThreadCnter;  // 各线程任务完成计数
    pthread_mutex_t *p_MutexLock[2];  // 2个互斥锁：其中[0]锁用作与条件变量配对使用，[1]锁用作线程计数
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

struct zDeployResInfo {
    _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _i DeployState;  // 布署状态：已返回确认信息的置为1，否则保持为0
    struct zDeployResInfo *p_next;
};
typedef struct zDeployResInfo zDeployResInfo;

/* 用于存放每个项目的元信息 */
struct zRepoInfo {
    _i RepoId;  // 项目代号
    char *p_RepoPath;  // 项目路径，如："/home/git/miaopai_TEST"
    _i LogFd;  // 每个代码库的布署日志日志文件g，用于存储 SHA1-sig+TimeStamp

    _i TotalHost;  // 每个项目的集群的主机数量

    _i ReplyCnt;  // 用于动态汇总单次布署或撤销动作的统计结果
    pthread_mutex_t ReplyCntLock;  // 用于保证 ReplyCnt 计数的正确性

    _i CacheId;  // 即：最新一次布署的时间戳(初始化为1000000000)

    pthread_rwlock_t RwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先

    void *p_MemPool;  // 线程内存池，预分配 16M 空间，后续以 8M 为步进增长
    pthread_mutex_t MemLock;  // 内存池锁
    _ui MemPoolOffSet;  // 动态指示下一次内存分配的起始地址

    char *p_PullCmd;  // 拉取代码时执行的Shell命令：svn与git有所不同

    /* 0：非锁定状态，允许布署或撤销、更新ip数据库等写操作 */
    /* 1：锁定状态，拒绝执行布署、撤销、更新ip数据库等写操作，仅提供查询功能 */
    _i DpLock;

    /* 代码库状态，若上一次布署／撤销失败，此项置为 zRepoDamaged 状态，用于提示用户看到的信息可能不准确 */
    _i RepoState;
    char zLastDeploySig[44];  // 存放最近一次布署的 40 位 SHA1 sig

    _ui MajorHostAddr;  // 以无符号整型格式存放的中转机(即实际执行分发的节点)IPv4地址
    char *p_HostAddrList;  // 以文本格式存储的 IPv4 地址列表，作为参数传给 zdeploy.sh 脚本
    struct zDeployResInfo *p_DpResListIf;  // 1、更新 IP 时对比差异；2、收集布署状态
    struct zDeployResInfo *p_DpResHashIf[zDeployHashSiz];  // 对上一个字段每个值做的散列

    _i CommitCacheQueueHeadId;  // 用于标识提交记录列表的队列头索引序号（index），意指：下一个操作需要写入的位置（不是最后一次已完成的写操作位置！）
    struct zVecWrapInfo CommitVecWrapIf;  // 存放 commit 记录的原始队列信息
    struct iovec CommitVecIf[zCacheSiz];
    struct zRefDataInfo CommitRefDataIf[zCacheSiz];

    struct zVecWrapInfo SortedCommitVecWrapIf;  // 存放经过排序的 commit 记录的缓存队列信息
    struct iovec SortedCommitVecIf[zCacheSiz];

    struct zVecWrapInfo DeployVecWrapIf;  // 存放 deploy 记录的原始队列信息
    struct iovec DeployVecIf[zCacheSiz];
    struct zRefDataInfo DeployRefDataIf[zCacheSiz];

    struct zVecWrapInfo SortedDeployVecWrapIf;  // 存放经过排序的 deploy 记录的缓存（从文件里直接取出的是旧的在前面，需要逆向排序）
    struct iovec SortedDeployVecIf[zCacheSiz];
};
typedef struct zRepoInfo zRepoInfo;

/************
 * 全局变量 *
 ************/
_i zGlobMaxRepoId = -1;  // 所有项目ID中的最大值
struct zRepoInfo **zppGlobRepoIf;

_i zInotifyFD;  // inotify 主描述符
struct zObjInfo *zpObjHash[zWatchHashSiz];  // 以watch id建立的HASH索引

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
#define zLogPath "_SHADOW/log/deploy/meta"  // 40位SHA1 sig字符串 + 时间戳
#define zInotifyObjRelativePath "/.git/logs/refs/heads/server"  // inotify 监控每个项目的server分支变动，用以动态追加提交记录缓存（最前面加一个 '/' ，省去每次使用时单独拼一个字符的麻烦）

/**********
 * 子模块 *
 **********/
#include "utils/pcre2/zpcre.c"
#include "utils/zbase_utils.c"
//#include "utils/md5_sig/zgenerate_sig_md5.c"  // 生成MD5 checksum检验和
#include "utils/thread_pool/zthread_pool.c"
#include "core/zinotify.c"  // 监控代码库文件变动
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

    zthread_poll_init();  // 初始化线程池
    zCheck_Negative_Exit( zInotifyFD = inotify_init() );  // 生成inotify master fd

    zinit_env(zpConfFilePath);  // 运行环境初始化

    zAdd_To_Thread_Pool( zinotify_wait, NULL );  // 等待事件发生
    zAdd_To_Thread_Pool( zauto_pull, NULL );  // 定时拉取远程代码

    zstart_server(&zNetServIf);  // 启动网络服务
}
