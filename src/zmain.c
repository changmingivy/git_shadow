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

#define zCommonBufSiz 4096
#define zMaxRepoNum 64
#define zWatchHashSiz 8192  // 最多可监控的路径总数

#define zDeployHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数

#define zCacheSiz 1009
#define zPreLoadCacheSiz 10  // 版本批次及其下属的文件列表与内容缓存

#include "../inc/zutils.h"
#include "utils/zbase_utils.c"
#include "utils/pcre2/zpcre.c"

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
    _i HostIp;  // 32位IPv4地址转换而成的无符号整型格式
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

    pthread_rwlock_t RwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先

    _i CacheId;  // 即：最新一次布署的时间戳(CURRENT 分支的时间戳，没有布署日志时初始化为0)

    _i ReplyCnt;  // 用于动态汇总单次布署或撤销动作的统计结果
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

struct zRepoInfo *zpRepoGlobIf;

/************
 * 全局变量 *
 ************/
_i zRepoNum;  // 总共有多少个代码库

_i zInotifyFD;   // inotify 主描述符
struct zObjInfo *zpObjHash[zWatchHashSiz];  // 以watch id建立的HASH索引

zThreadPoolOps zCallBackList[16];  // 索引每个回调函数指针，对应于zObjInfo中的CallBackId

#define UDP 0
#define TCP 1

/************
 * 配置文件 *
 ************/
// 以下路径均是相对于所属代码库的顶级路径
#define zAllIpPath ".git_shadow/info/client_ip_all.bin"  // 位于各自代码库路径下，以二进制形式存储后端所有主机的ipv4地址
#define zSelfIpPath ".git_shadow/info/client_ip_self.bin"  // 格式同上，存储客户端自身的ipv4地址
#define zAllIpPathTxt ".git_shadow/info/client_ip_all.txt"  // 存储点分格式的原始字符串ipv4地下信息，如：10.10.10.10
#define zMajorIpPathTxt ".git_shadow/info/client_ip_major.txt"  // 与布署中控机直接对接的master机的ipv4地址（点分格式），目前是zdeploy.sh使用，后续版本使用libgit2库之后，将转为内部直接使用
#define zRepoIdPath ".git_shadow/info/repo_id"

#define zMetaLogPath ".git_shadow/log/deploy/meta"  // 元数据日志，以zDeployLogInfo格式存储，主要包含data、sig两个日志文件中数据的索引
//#define zDataLogPath ".git_shadow/log/deploy/data"  // 文件路径日志，需要通过meta日志提供的索引访问
#define zSigLogPath ".git_shadow/log/deploy/sig"  // 40位SHA1 sig字符串，需要通过meta日志提供的索引访问

/**********
 * 子模块 *
 **********/
#include "utils/md5_sig/zgenerate_sig_md5.c"  // 生成MD5 checksum检验和
#include "utils/thread_pool/zthread_pool.c"
//#include "test/zprint_test.c"
#include "core/zinotify.c"  // 监控代码库文件变动
#include "core/zserv.c"  // 对外提供网络服务
#include "zinit.c"  // 读取主配置文件

/***************************
 * +++___ main 函数 ___+++ *
 ***************************/
_i
main(_i zArgc, char **zppArgv) {
// TEST: PASS
    char *zpConfFilePath = NULL;
    struct stat zStatIf;
    _i zActionType = 0;
    zNetServInfo zNetServIf;  // 指定服务端自身的Ipv4地址与端口，或者客户端要连接的目标服务器的Ipv4地址与端口
    zNetServIf.zServType = TCP;

    for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "CUh:p:f:"));) {
        switch (zOpt) {
            case 'C':  // 启动客户端功能
                zActionType = 1; break;
            case 'h':
                zNetServIf.p_host= optarg; break;
            case 'p':
                zNetServIf.p_port = optarg; break;
            case 'U':
                zNetServIf.zServType = UDP;
            case 'f':
                if (-1 == stat(optarg, &zStatIf) || !S_ISREG(zStatIf.st_mode)) {  // 若指定的主配置文件不存在或不是普通文件，则报错退出
                        zPrint_Time();
                        fprintf(stderr, "\033[31;01mConfig file not exists or is not a regular file!\n"
                            "Usage: %s -f <Config File Path>\033[00m\n", zppArgv[0]);
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

        if (1 == zActionType) {  // 客户端功能，用于在ECS上由git hook自动执行，向服务端发送状态确认信息
        zupdate_ipv4_db_self(AT_FDCWD);  // 回应之前客户端将更新自身的ipv4地址库
        zclient_reply(zNetServIf.p_host, zNetServIf.p_port);
        return 0;
    }

    zdaemonize("/");  // 转换自身为守护进程，解除与终端的关联关系

zReLoad:;
    // +++___+++ 需要手动维护每个回调函数的索引 +++___+++
    zCallBackList[0] = zthread_common_func;
    zCallBackList[1] = zthread_update_commit_cache;
    zCallBackList[2] = zthread_update_ipv4_db_all;

    zthread_poll_init();  // 初始化线程池
    zInotifyFD = inotify_init();  // 生成inotify master fd
    zCheck_Negative_Exit(zInotifyFD);

    zparse_conf_and_init_env(zpConfFilePath); // 解析主配置文件，并将有效条目添加到监控队列

    zAdd_To_Thread_Pool(zstart_server, &zNetServIf);  // 读取配置文件之前启动网络服务
    zAdd_To_Thread_Pool(zinotify_wait, NULL);  // 等待事件发生

//  ztest_print();

    zconfig_file_monitor(zpConfFilePath);  // 主线程监控自身主配置文件的内容变动
    close(zInotifyFD);  // 主配置文件有变动后，关闭inotify master fd

    pid_t zPid = fork(); // 之后父进程退出，子进程按新的主配置文件内容重新初始化
    zCheck_Negative_Exit(zPid);

    if (0 < zPid) {
        exit(0);
    } else {
        goto zReLoad;
    }
}
