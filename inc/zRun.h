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
