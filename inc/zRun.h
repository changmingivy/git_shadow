#ifndef ZRUN_H
#define ZRUN_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include "zCommon.h"
#include "zNetUtils.h"
#include "zThreadPool.h"

#include "zPgSQL.h"
#include "zDpOps.h"
#include "cJSON.h"

#define zTCP_SERV_HASH_SIZ 16
#define zUDP_SERV_HASH_SIZ 4

#define zGLOB_REPO_NUM_LIMIT 1024  /* 最多可管理 1024 个项目，ID 范围：0 - 1023 */

#define zCACHE_SIZ 64  // 顶层缓存单元数量取值不能超过 IOV_MAX
#define zDP_HASH_SIZ 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数
#define zSEND_UNIT_SIZ 8  // sendmsg 单次发送的单元数量，在 Linux 平台上设定为 <=8 的值有助于提升性能
#define zFORECASTED_HOST_NUM 256  // 预测的目标主机数量上限

#define zGLOB_COMMON_BUF_SIZ 1024

#define zCACHE_GOOD 0
#define zCACHE_DAMAGED 1

#define zDATA_TYPE_COMMIT 0
#define zDATA_TYPE_DP 1


/* 服务端自身的 IP 地址与端口 */
typedef struct {
    char *p_ipAddr;
    char *p_port;

    char specStrForGit[INET6_ADDRSTRLEN];
} zNetSrv__;


typedef struct __zDpRes__ {
    /*
     * 目标机 IP
     */
    char *p_hostAddr;

    /*
     * unsigned long long int
     * IPv4 地址只使用第一个成员
     */
    _ull clientAddr[2];

    /*
     * << 布署状态 >>
     * bit[0]:目标端初始化(SSH)成功
     * bit[1]:服务端本地布署动作(git push)成功
     * bit[2]:目标端已收到推送内容(post-update)
     * bit[3]:目标端已确认内容无误(post-update)
     * bit[4]:目标端已确认布署后动作执行成功
     * bit[5]:
     * bit[6]:
     * bit[7]:
     */
    _uc resState;

    /*
     * << 错误类型 >>
     * err1 bit[0]: 服务端错误
     * err2 bit[1]: server ==> host 网络不通
     * err3 bit[2]: SSH 连接认证失败
     * err4 bit[3]: 目标端磁盘容量不足
     * err5 bit[4]: 目标端权限不足
     * err6 bit[5]: 目标端文件冲突
     * err7 bit[6]: 目标端路径不存在
     * err8 bit[7]: 目标端的 git 环境不可用
     * err9 bit[8]: 目标机 IP 格式错误/无法解析
     * err10 bit[9]: host ==> server 网络不通
     * err11 bit[10]: 目标端负载过高
     * err12 bit[11]: 目标机请求下载的文件路径在服务端找不到
     */
    _ui errState;

    /*
     * 存放本地产生的或目标机返回的错误信息
     */
    char errMsg[256];

    struct __zDpRes__ *p_next;
} zDpRes__;


typedef struct __zDpCcur__ {
    /*
     * 目标机 IP 在服务端 dpList 链中的节点索引
     * 通过此索引获取目标机 IP
     * 目标机端口从 zpRepo_ 中取
     */
    _i selfNodeIndex;

    /*
     * 工作线程将当次任务错误码写出到此值
     * 0 表示成功
     * 其余数字表示错误码
     * */
    _c errNo;

    /*
     * 工作线程标记布署动作已经开始运行
     * 初始化为 0，进入工作线程后，置为 1
     */
    _c startMark;

    /*
     * 工作线程写出的自身的 tid
     */
    pthread_t tid;
} zDpCcur__;


/* 用于存放本地操作时需要，但前端不需要的数据 */
typedef struct __zRefData__ {
    /* 传递给 sendmsg 的下一级数据 */
    struct __zVecWrap__ *p_subVecWrap_;

    /* 指向实际的数据存放空间 */
    char *p_data;
} zRefData__;


/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
typedef struct __zVecWrap__ {
    /* 此数组中每个成员的 iov_base 字段均指向 p_refData_ 中对应的 p_data 字段 */
    struct iovec *p_vec_;
    _i vecSiz;

    struct __zRefData__ *p_refData_;
} zVecWrap__;


typedef struct __zCacheMeta__ {
    _s opsId;  // 网络交互时，代表操作指令（从 1 开始的连续排列的非负整数）
    _s commitId;  // 版本号
    _s dataType;  // 缓存类型，zDATA_TYPE_COMMIT/zDATA_TYPE_DP
    _i fileId;  // 单个文件在差异文件列表中 index
    _l cacheId;  // 缓存版本代号（最新一次布署的时间戳）

    char *p_filePath;  // git diff --name-only 获得的文件路径
    char *p_treeData;  // 经过处理的 Tree 图显示内容

    /* 以下为 Tree 专属数据 */
    struct __zCacheMeta__ *p_father;  // Tree 父节点
    struct __zCacheMeta__ *p_left;  // Tree 左节点
    struct __zCacheMeta__ *p_firstChild;  // Tree 首子节点：父节点唯一直接相连的子节点
    struct __zCacheMeta__ **pp_resHash;  // Tree 按行号对应的散列

    _i lineNum;  // 行号
    _i offSet;  // 纵向偏移
} zCacheMeta__;


/*
 * 用于存放每个项目的专用元信息
 * 锁类型数据，空间间隔尽可能超过 128bytes，防止伪共享问题
 */
typedef struct __zRepo__ {
    /* 项目 ID */
    _i id;

    /*
     * 项目创建时间，
     * 格式：2016-12-01 09:29:00
     */
    char createdTime[24];

    /*
     * 项目在服务端上的绝对路径及其长度
     */
    char *p_path;
    _s pathLen;

    /*
     * 项目所在文件系统支持的最大路径长度
     * 相对于项目根目录的值，由底层文件系统决定
     * 用于度量 git 输出的差异文件相对路径长度
     */
    _s maxPathLen;

    /*
     * 项目在服务端上的别名路径
     * 每次布署时由用户指定
     */
    char *p_aliasPath;

    /*
     * 项目内存池，预分配 8M 空间
     * 后续以 8M 为步进增长
     * 能一次性分配的最长空间大小：8M - sizeof(void *)
     * 超过此大小的内存申请，需要使用系统 alloc 类函数
     */
    void *p_memPool;

    /* 动态指示下一次内存分配的起始地址 */
    _ui memPoolOffSet;

    /*
     * 本项目全局 postgreSQL handler
     */
    zPgConnHd__ *p_pgConnHd_;

    /*
     * 临时 SQL 表命名序号
     * 用以提升布署结果异步查询的性能
     */
    _ui tempTableNo;

    /*
     * 'Y'：允许强制清除有冲穾的文件或路径
     * 'N'：不允许
     */
    char forceDpMark;

    /*
     * 代码库状态，若上一次布署失败，此项将置为 zRepoDamaged 状态
     * 用于提示用户看到的信息可能不准确
     */
    char repoState;

    /*
     * 版本号列表、差异文件列表等缓存的 ID
     * 实质就是每次刷新缓存时的 UNIX 时间戳
     */
    time_t cacheId;

    /*
     * 本项目全局 git handler
     */
    git_repository *p_gitCommHandler;

    /*
     * 每次布署动作都要新开启一个 handler
     */
    git_repository *p_gitDpHandler;

    /*
     * 同步远程代码时对接的源库URL与分支名称
     * 之后用户可以改变
     */
    char *p_codeSyncURL;
    char *p_codeSyncBranch;

    /*
     * 同步远程代码时对接的源库URL 与 refs对
     * 服务端对应的分支名称在源库分支名称之后添加 8 个 X 为后缀
     * 格式：refs/heads/master:refs/heads/masterXXXXXXXX
     */
    char *p_codeSyncRefs;

    /*
     * 本地分支的完整名称：refs/heads/masterXXXXXXXX
     * 结果 == p_codeSyncRefs + (strlen(p_codeSyncRefs) - 8) / 2 + 1
     */
    char *p_localRef;

    /* 同一个项目的所有目标机登陆认证方式必须完全相同 */
    char sshUserName[256];
    char sshPort[6];

    /* SSH 认证类型：公钥或密码 */
    znet_auth_t authType;

    /*
     * 公钥认证时，用于提定公钥的密码
     * 密码认证时，用于指定用户登陆密码
     * 留空表示无密码
     */
    const char *p_passWd;

    /* 存放最近一次布署成功的版本号 */
    char lastDpSig[41];

    /* 正在布署过程中但尚未确定最终结果的版本号 */
    char dpingSig[41];

    /*
     * 每次布署时的所有目标机 IP(_ull[2]) 的链表及其散列
     * 用于增量对比差异 IP，并由此决定每台目标机是否需要初始化或布署
     */
    zDpRes__ *p_dpResList_;
    zDpRes__ *p_dpResHash_[zDP_HASH_SIZ];

    /* 存放新的版本号记录 */
    zVecWrap__ commitVecWrap_;
    struct iovec commitVec_[zCACHE_SIZ];
    zRefData__ commitRefData_[zCACHE_SIZ];

    /* 存放布署记录 */
    zVecWrap__ dpVecWrap_;
    struct iovec dpVec_[zCACHE_SIZ];
    zRefData__ dpRefData_[zCACHE_SIZ];

    /*
     * 项目目标机总数
     */
    _i totalHost;

    /*
     * 布署总任务数，其值总是 <= totalHost
     */
    _i dpTotalTask;

    /*
     * dpTaskFinCnt：
     *     此值与 dpTotalTask 相等时，即代表完成所有任务
     *     但不代表全部成功，其中可能存在因发生错误而返回的结果
     */
    _i dpTaskFinCnt;

    /*
     * 正在进行的布署会将期置为 0，
     * 后来到达的新布署通过将其置为 1，通知旧布署终止
     */
    _c dpWaitMark;

    /*
     * 用于标识收集齐的结果是全部成功，
     * 还是其中有异常返回而增加的计数
     * bit[0] 置位表示有异常
     */
    _uc resType;

    /*
     * 存放布署时每个工作线程需要的参数
     * 目标机数量不超过 zFORECASTED_HOST_NUM 时，使用预置的空间，以提升效率
     */
    zDpCcur__ *p_dpCcur_;
    zDpCcur__ dpCcur_[zFORECASTED_HOST_NUM];

    /*
     * 每次布署的开始时间
     * 每台目标机的耗时均基于此计算
     */
    time_t  dpBaseTimeStamp;

    /*
     * 单次布署动作的身份唯一性标识
     * 目前设置为 == dpBaseTimeStamp
     */
    _ui dpID;

    /*
     * 系统定义的布署前动作
     * 需要执行的 SSH 初始化命令
     */
    char *p_sysDpCmd;

    /*
     * 用户定义的布署后动作
     * 布署成功后需要执行的动作
     */
    char *p_userDpCmd;

    /*************
     * LOCK AREA *
     *************/

    /*
     * 用于任务完成计数的原子性统计
     * 及通知调度线程任务已完成
     */
    pthread_mutex_t dpSyncLock;
    char pad_0[128];
    pthread_cond_t dpSyncCond;
    char pad_1[128];

    /*
     * 用于确保同一时间不会有多个新布署请求阻塞排队，
     * 避免拥塞持续布署的情况
     */
    pthread_mutex_t dpWaitLock;
    char pad_2[128];

    /*
     * 布署主锁：同一项目同一时间只允许一套布署流程在运行
     */
    pthread_mutex_t dpLock;
    char pad_3[128];

    /*
     * 布署成功之后，刷新项目缓存时需要此锁
     * 此锁将排斥所有查询类操作
     * 读写锁属性：写者优先 ???
     */
    pthread_rwlock_t rwLock;
    char pad_4[128];
    //pthread_rwlockattr_t zRWLockAttr;

    /* 内存池锁，保证内存的原子性分配 */
    pthread_mutex_t memLock;
    char pad_5[128];

    /* 供那些没有必要单独开辟独立锁的动作使用的通用条件变量与锁 */
    pthread_mutex_t commLock;
    char pad_6[128];
    pthread_cond_t commCond;
    char pad_7[128];

    /*
     * 升级锁：系统本身升级时，
     * 需要排斥 IP 增量更新动作
     */
    pthread_rwlock_t sysUpdateLock;
} zRepo__;


typedef struct __zSysInfo__ {
    void * (* route_tcp) (void *);
    void * (* route_udp) (void *);

    _i (* ops_tcp[zTCP_SERV_HASH_SIZ]) (cJSON *, _i);
    _i (* ops_udp[zUDP_SERV_HASH_SIZ]) (void *, struct sockaddr *, socklen_t);

    /*
     * 系统全局负载值：0 - 100
     * 高于 80 时，不接受布署请求
     * 目前只用到 memload
     */
    _uc cpuLoad;
    _uc memLoad;
    _uc netLoad;
    _uc diskLoad;

    /*
     * 若置 0，表示项目不存在，或尚未初始化完成
     * 若置 1，表示项目存在，并且已经初始化完成
     */
    _c repoFinMark[zGLOB_REPO_NUM_LIMIT];

    /*
     * 主进程调用 connect 返回的套接字
     * 用于向项目进程传递业务 sd
     */
    _i repoUN[zGLOB_REPO_NUM_LIMIT];

    /* 常量，其值恒等于 zGLOB_REPO_NUM_LIMIT */
    _s globRepoNumLimit;

    /*
     * 服务端属主用户的家路径长度
     * 例如：strlen("/home/git")
     */
    _s homePathLen;

    /* 服务端网络信息 */
    zNetSrv__ netSrv_;

    /* 服务端主程序的启动根路径 */
    char *p_servPath;

    /* 服务端所属用户的登陆名称与家路径 */
    char *p_loginName;
    char *p_homePath;

    /* 服务端的公私钥存储位置 */
    char *p_sshPubKeyPath;
    char *p_sshPrvKeyPath;

    /* 错误码影射表 */
    char *p_errVec[128];

    /*
     * postgreSQL 全局认证信息
     */
    char pgConnInfo[2048];
    zPgLogin__ pgLogin_;
} zSysInfo__;


struct zRun__ {
    void (* run) ();

    zSysInfo__ *p_sysInfo_;
};


/*
 * udp server: use the same ipAddr and port as tcp server.
 */
typedef struct __zUdpInfo__ {
    _s opsId;

    char data[510];

    struct sockaddr peerAddr;
    socklen_t peerAddrLen;
} zUdpInfo__;


#endif  // #ifndef ZRUN_H
