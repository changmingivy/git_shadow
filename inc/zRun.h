#ifndef ZRUN_H
#define ZRUN_H

#include "zCommon.h"
#include "zThreadPool.h"
#include "zDpOps.h"
#include "cJSON.h"

#define zServHashSiz 16


/* 服务端自身的 IP 地址与端口 */
typedef struct {
    char *p_ipAddr;
    char *p_port;
} zNetSrv__;

typedef struct __zSockAcceptParam__ {
    void *p_threadPoolMeta_;

    _i connSd;
} zSockAcceptParam__;


/*
 * 用于存放每个项目的专用元信息
 */
typedef struct __zRepo__ {
    /*
     * 所有需要传递给线程的数据结构
     * 均需在最前面预留一个指针的空间
     */
    zThreadTask__ *p_threadSource_;

    /* 项目 ID */
    _i repoId;

    /*
     * 版本号列表、差异文件列表等缓存的 ID
     * 实质就是每次刷新缓存时的 UNIX 时间戳
     */
    time_t  cacheId;

    /*
     * 项目在服务端上的绝对路径
     */
    char *p_repoPath;

    /*
     * 项目在服务端上的别名路径
     * 每次布署时由用户指定
     */
    char *p_repoAliasPath;

    /*
     * 服务端项目路径字符串长度
     * 会被频繁使用，因此全局留存以提升性能
     */
    _i repoPathLen;

    /*
     * 项目所在文件系统支持的最大路径长度
     * 相对于项目根目录的值，由底层文件系统决定
     * 用于度量 git 输出的差异文件相对路径长度
     */
    _i maxPathLen;

    /*
     * 用于标识仓库是否已经初始化完成：N 代表动作尚未完成，Y 代表已完成
     * 未初始化完成的项目，不接受任何动作请求
     */
    char initFinished;

    /*
     * 新布署请求通过改变此变量的值 (1 ===> 0) 打断旧的布署动作
     */
    _i dpingMark;

    /*
     * 每次布署的开始时间
     * 每台目标机的耗时均基于此计算
     */
    time_t  dpBaseTimeStamp;

    /*
     * 本项目全局 git handler
     */
    git_repository *p_gitRepoHandler;

    /*
     * 本项目全局 postgreSQL handler
     */
    zPgConnHd__ *p_pgConnHd_;

    /*
     * 用于控制并发流量的信号量
     * 防止并发超载
     */
    sem_t dpTraficControl;

    /*
     * 用于任务完成计数的原子性统计
     * 及通知调度线程任务已完成
     */
    pthread_mutex_t dpSyncLock;
    pthread_cond_t dpSyncCond;

    /*
     * 每个布署时指定的目标主机总数
     */
    _i totalHost;

    /*
     * 布署总任务数，其值总是 <= totalHost
     */
    _i dpTotalTask;

    /*
     * 任务完成数：此值与 dpTotalTask 相等时，即代表所有动作已完成
     * 但不代表全部成功，其中可能存在因发生错误而返回的结果
     */
    _i dpTaskFinCnt;

    /*
     * 用于标识本项目是否处于锁定状态
     * 即：可查询，但不允许布署
     */
    _c repoLock;

    /*
     * 代码库状态，若上一次布署失败，此项将置为 zRepoDamaged 状态
     * 用于提示用户看到的信息可能不准确
     */
    _c repoState;

    /*
     * 用于标识收集齐的结果是全部成功，还是其中有异常返回而增加的计数
     * bit[0] 置位表示目标机初始化过程中发生错误
     * bit[1] 置位表示布署过程中发生错误
     */
    _uc resType;

    /* 存放最近一次布署成功的版本号 */
    char lastDpSig[44];

    /* 正在布署过程中但尚未确定最终结果的版本号 */
    char dpingSig[44];

    /* 同一个项目的所有目标机登陆认证方式必须完全相同 */
    char sshUserName[256];
    char sshPort[6];

    /*
     * 存放所有目标机的并发布署参数
     * 目标机数量不超过 zForecastedHostNum 时，使用预置的空间，以提升效率
     */
    zDpCcur__ *p_dpCcur_;
    zDpCcur__ dpCcur_[zForecastedHostNum];

    /*
     * 每次布署时的所有目标机 IP(_ull[2]) 的链表及其散列
     * 用于增量对比差异 IP，并由此决定每台目标机是否需要初始化或布署
     */
    zDpRes__ *p_dpResList_;
    zDpRes__ *p_dpResHash_[zDpHashSiz];

    /*
     * 拿到此锁的线程才有权中止正在进行的布署动作
     * 用于确保同一时间不会有多个中断请求
     */
    pthread_mutex_t dpWaitLock;

    /*
     * 布署主锁：同一项目同一时间只允许一套布署流程在运行
     */
    pthread_mutex_t dpLock;

    /*
     * 布署成功之后，刷新项目缓存时需要此锁
     * 此锁将排斥所有查询类操作
     * 读写锁属性：写者优先
     */
    pthread_rwlock_t rwLock;
    //pthread_rwlockattr_t zRWLockAttr;

    /*
     * 并发布署屏障
     * 用于保证基础环境就绪之后，工作线程才开始真正的布署动作
     * 需要每次布署时根据目标机数量实时初始化，不能随项目启动执行初始化
     */
    //pthread_barrier_t dpBarrier;

    /* 存放新的版本号记录 */
    zVecWrap__ commitVecWrap_;
    struct iovec commitVec_[zCacheSiz];
    zRefData__ commitRefData_[zCacheSiz];

    /* 存放布署记录 */
    zVecWrap__ dpVecWrap_;
    struct iovec dpVec_[zCacheSiz];
    zRefData__ dpRefData_[zCacheSiz];

    /*
     * 线程内存池，预分配 8M 空间
     * 后续以 8M 为步进增长
     */
    void *p_memPool;

    /* 内存池锁，保证内存的原子性分配 */
    pthread_mutex_t memLock;

    /* 动态指示下一次内存分配的起始地址 */
    _ui memPoolOffSet;

    /*
     * 临时 SQL 表命名序号
     * 用以提升布署结果异步查询的性能
     */
    _ui tempTableNo;
} zRepo__;


struct zRun__ {
    void (* run) ();
    void * (* route) (void *);

    _i (* ops[zServHashSiz]) (cJSON *, _i);

    /* 供那些没有必要单独开辟独立锁的动作使用的通用条件变量与锁 */
    pthread_mutex_t commonLock;
    pthread_cond_t commonCond;

    /*
     * 系统全局内存负载值：0 - 100
     * 系统由高负载降至可用范围时，通知等待的线程继续其任务
     * 高于 80 拒绝布署，同时 git push 的过程中，若高于 80 则剩余任阻塞等待
     */
    _c memLoad;

    _s homePathLen;

    /* 既有项目 ID 中的最大值 */
    _s maxRepoId;

    /* 数组：指向每个项目的元信息 */
    zRepo__ *p_repoVec[1024];

    char *p_homePath;
    char *p_loginName;

    char *p_SSHPubKeyPath;
    char *p_SSHPrvKeyPath;

	/* 布署系统自身服务连接信息 */
	zNetSrv__ netSrv_;

    /* postgreSQL 全局认证信息 */
	zPgLogin__ pgLogin_;
    char pgConnInfo[2048];
};



#endif  // #ifndef ZRUN_H
