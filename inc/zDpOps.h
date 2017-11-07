#define ZDPOPS_H

#ifndef _Z_BSD

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#endif

#ifndef PTHREAD_H
#include <pthread.h>
#define PTHREAD_H
#endif

#include <semaphore.h>

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#ifndef ZNETUTILS_H
#include "zNetUtils.h"
#endif

#ifndef ZLIBSSH_H
#include "zLibSsh.h"
#endif

#ifndef ZLIBGIT_H
#include "zLibGit.h"
#endif

#ifndef ZLOCALOPS_H
#include "zNativeOps.h"
#endif

#ifndef ZLOCALUTILS_H
#include "zNativeUtils.h"
#endif

#ifndef ZPOSIXREG_H
#include "zPosixReg.h"
#endif

#ifndef ZTHREADPOOL_H
#include "zThreadPool.h"
#endif

#define zGlobRepoNumLimit 256  // 可以管理的代码库数量上限
#define zGlobRepoIdLimit 10 * 256  // 代码库 ID 上限
#define zCacheSiz 64  // 顶层缓存单元数量取值不能超过 IOV_MAX
#define zDpTraficLimit 256  // 同一项目可同时发出的 push 连接数量上限
#define zDpHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数
#define zSendUnitSiz 8  // sendmsg 单次发送的单元数量，在 Linux 平台上设定为 <=8 的值有助于提升性能
#define zForecastedHostNum 200  // 预测的目标主机数量上限
#define zSshSelfIpDeclareBufSiz zSizeOf("export ____zSelfIp='192.168.100.100';")  // 传递给目标机的 SSH 命令之前留出的空间，用于声明一个SHELL变量告诉目标机自身的通信IP

#define zGlobCommonBufSiz 1024

#define zDpUnLock 0
#define zDpLocked 1

#define zRepoGood 0
#define zRepoDamaged 1

#define zIsCommitDataType 0
#define zIsDpDataType 1

typedef struct {
    pthread_t SelfTid;
    pthread_cond_t CondVar;

    void * (* func) (void *);
    void *p_param;
} zThreadPool__;

typedef struct {
    zThreadPool__ *zpThreadSource_;  // 必须放置在首位
    _i RepoId;
    char *p_HostIpStrAddr;  // 单个目标机 Ip，如："10.0.0.1"
    char *p_HostServPort;  // 字符串形式的端口号，如："22"
    char *p_Cmd;  // 需要执行的指令集合

    _i zAuthType;
    const char *p_UserName;
    const char *p_PubKeyPath;  // 公钥所在路径，如："/home/git/.ssh/id_rsa.pub"
    const char *p_PrivateKeyPath;  // 私钥所在路径，如："/home/git/.ssh/id_rsa"
    const char *p_PassWd;  // 登陆密码或公钥加密密码

    char *p_RemoteOutPutBuf;  // 获取远程返回信息的缓冲区
    _ui RemoteOutPutBufSiz;

    pthread_cond_t *p_CcurCond;  // 线程同步条件变量
    pthread_mutex_t *p_CcurLock;  // 同步锁
    _ui *p_TaskCnt;  // SSH 任务完成计数
} zDpCcur__;

typedef struct __zDpRes__ {
    _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _i DpState;  // 布署状态：已返回确认信息的置为1，否则保持为 -1
    _i InitState;  // 远程主机初始化状态：已返回确认信息的置为1，否则保持为 -1
    char ErrMsg[256];  // 存放目标主机返回的错误信息
    struct __zDpRes__ *p_next;
} zDpRes__;

/* 在zSend__之外，添加了：本地执行操作时需要，但对前端来说不必要的数据段 */
typedef struct __zRefData__ {
    struct __zVecWrap__ *p_SubVecWrap_;  // 传递给 sendmsg 的下一级数据
    char *p_data;  // 实际存放数据正文的地方
} zRefData__;

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
typedef struct __zVecWrap__ {
    _i VecSiz;
    struct iovec *p_Vec_;  // 此数组中的每个成员的 iov_base 字段均指向 p_RefData_ 中对应的 p_data 字段
    struct __zRefData__ *p_RefData_;
} zVecWrap__;

/* 用于存放每个项目的元信息，同步锁不要紧挨着定义，在X86平台上可能会带来伪共享问题降低并发性能 */
typedef struct {
    _i RepoId;  // 项目代号
    time_t  CacheId;  // 即：最新一次布署的时间戳(初始化为1000000000)
    char *p_RepoPath;  // 项目路径，如："/home/git/miaopai_TEST"
    _i RepoPathLen;  // 项目路径长度，避免后续的使用者重复计算
    _i MaxPathLen;  // 项目最大路径长度：相对于项目根目录的值（由底层文件系统决定），用于度量git输出的差异文件相对路径长度

    _i SelfPushMark;  // 置为 1 表示该项目会主动推送代码到中控机，不需要拉取远程代码
    char *p_PullCmd;  // 拉取代码时执行的Shell命令：svn与git有所不同
    time_t LastPullTime;  // 最近一次拉取的时间，若与之的时间间隔较短，则不重复拉取
    pthread_mutex_t PullLock;  // 保证同一时间同一个项目只有一个git pull进程在运行

    /* FinMark 类标志：0 代表动作尚未完成，1 代表已完成 */
    char zInitRepoFinMark;

    /* 用于区分是布署动作占用写锁还是生成缓存占用写锁：1 表示布署占用，0 表示生成缓存占用 */
    _i zWhoGetWrLock;
    /* 远程主机初始化或布署动作开始时间，用于统计每台目标机器大概的布署耗时*/
    time_t  DpBaseTimeStamp;
    /* 布署超时上限 */
    time_t DpTimeWaitLimit;
    /* 目标机在重要动作执行前回发的keep alive消息 */
    time_t DpKeepAliveStamp;

    /* 本项目 git 库全局 Handler */
    git_repository *p_GitRepoHandler;

    /* 用于控制并发流量的信号量 */
    sem_t DpTraficControl;

    /* libssh2 与 libgit2 共用的并发同步锁与条件变量 */
    pthread_mutex_t DpSyncLock;
    pthread_cond_t DpSyncCond;
    _ui TotalHost;  // 每个项目的目标主机总数量，此值不能修改
    _ui DpTotalTask;  // 用于统计总任务数，可动态修改
    _ui DpTaskFinCnt;  // 用于统计任务完成数，仅代表执行函数返回
    _ui DpReplyCnt;  // 用于统计最终状态返回

    /* 0：非锁定状态，允许布署或撤销、更新ip数据库等写操作 */
    /* 1：锁定状态，拒绝执行布署、撤销、更新ip数据库等写操作，仅提供查询功能 */
    _c DpLock;
    /* 代码库状态，若上一次布署／撤销失败，此项置为 zRepoDamaged 状态，用于提示用户看到的信息可能不准确 */
    _c RepoState;
    _c ResType[2];  // 用于标识收集齐的结果是全部成功，还是其中有异常返回而增加的计数：[0] 远程主机初始化 [1] 布署

    char zLastDpSig[44];  // 存放最近一次布署的 40 位 SHA1 sig
    char zDpingSig[44];  // 正在布署过程中的版本号，用于布署耗时分析

    pthread_mutex_t ReplyCntLock;  // 用于保证 ReplyCnt 计数的正确性

    zDpCcur__ DpCcur_[zForecastedHostNum];
    zDpCcur__ *p_DpCcur_;
    zDpRes__ *p_DpResList_;  // 1、更新 IP 时对比差异；2、收集布署状态
    zDpRes__ *p_DpResHash_[zDpHashSiz];  // 对上一个字段每个值做的散列

    pthread_rwlock_t RwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    //pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先
    pthread_mutex_t DpRetryLock;  // 用于分离失败重试布署与生成缓存之间的锁竞争

    zVecWrap__ CommitVecWrap_;  // 存放 commit 记录的原始队列信息
    struct iovec CommitVec_[zCacheSiz];
    zRefData__ CommitRefData_[zCacheSiz];

    zVecWrap__ SortedCommitVecWrap_;  // 存放经过排序的 commit 记录的缓存队列信息，提交记录总是有序的，不需要再分配静态空间

    zVecWrap__ DpVecWrap_;  // 存放 deploy 记录的原始队列信息
    struct iovec DpVec_[zCacheSiz];
    zRefData__ DpRefData_[zCacheSiz];

    zVecWrap__ SortedDpVecWrap_;  // 存放经过排序的 deploy 记录的缓存（从文件里直接取出的是旧的在前面，需要逆向排序）
    struct iovec SortedDpVec_[zCacheSiz];

    void *p_MemPool;  // 线程内存池，预分配 16M 空间，后续以 8M 为步进增长
    pthread_mutex_t MemLock;  // 内存池锁
    _ui MemPoolOffSet;  // 动态指示下一次内存分配的起始地址
} zRepo__;


/* 既有的项目 ID 最大值 */
extern _i zGlobMaxRepoId;

/* 系统 CPU 与 MEM 负载监控：以 0-100 表示 */
extern pthread_mutex_t zGlobCommonLock;
extern pthread_cond_t zGlobCommonCond;  // 系统由高负载降至可用范围时，通知等待的线程继续其任务(注：使用全局通用锁与之配套)
extern _ul zGlobMemLoad;  // 高于 80 拒绝布署，同时 git push 的过程中，若高于 80 则剩余任阻塞等待

/* 指定服务端自身的Ip地址与端口 */
typedef struct {
    char *p_IpAddr;  // 字符串形式的ip点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
} zNetSrv__;

extern zNetSrv__ zNetSrv_;

/* 全局 META HASH */
extern zRepo__ *zpGlobRepo_[zGlobRepoIdLimit];

typedef struct __zMeta__ {
    _i OpsId;  // 网络交互时，代表操作指令（从0开始的连续排列的非负整数）；当用于生成缓存时，-1代表commit记录，-2代表deploy记录
    _i RepoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i CommitId;  // 版本号（对应于svn或git的单次提交标识）
    _i FileId;  // 单个文件在差异文件列表中index
    _ui HostId;  // 32位IPv4地址转换而成的无符号整型格式
    _i CacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i DataType;  // 缓存类型，zIsCommitDataType/zIsDpDataType
    char *p_data;  // 数据正文，发数据时可以是版本代号、文件路径等(此时指向zRefData__的p_data)等，收数据时可以是接IP地址列表(此时额外分配内存空间)等
    _i DataLen;  // 不能使和 _ui 类型，recv 返回 -1 时将会导致错误
    char *p_ExtraData;  // 附加数据，如：字符串形式的UNIX时间戳、IP总数量等
    _ui ExtraDataLen;

    /* 以下为 Tree 专属数据 */
    struct __zMeta__ *p_father;  // Tree 父节点
    struct __zMeta__ *p_left;  // Tree 左节点
    struct __zMeta__ *p_FirstChild;  // Tree 首子节点：父节点唯一直接相连的子节点
    struct __zMeta__ **pp_ResHash;  // Tree 按行号对应的散列
    _i LineNum;  // 行号
    _i OffSet;  // 纵向偏移
} zMeta__;

struct zDpOps__ {
    _i (* show_meta) (zMeta__ *, _i);
    _i (* show_meta_all) (zMeta__ * __attribute__ ((__unused__)), _i);

    _i (* print_revs) (zMeta__ *, _i);
    _i (* print_diff_files) (zMeta__ *, _i);
    _i (* print_diff_contents) (zMeta__ *, _i);

    _i (* creat) (zMeta__ *, _i);
    _i (* req_dp) (zMeta__ *, _i);
    _i (* dp) (zMeta__ *, _i);
    _i (* state_confirm) (zMeta__ *, _i);
    _i (* lock) (zMeta__ *, _i);
    _i (* req_file) (zMeta__ *, _i);

    void * (* route) (void *);
    _i (* json_to_struct) (char *, zMeta__ *);
    void (* struct_to_json) (char *, zMeta__ *);
};

// extern struct zDpOps__ zDpOps_;
