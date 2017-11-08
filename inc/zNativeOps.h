#ifndef ZLOCALOPS_H
#define ZLOCALOPS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include "zCommon.h"
#include "zPosixReg.h"
#include "zNativeUtils.h"
#include "zThreadPool.h"
#include "zLibGit.h"
#include "zDpOps.h"

#define zMemPoolSiz 8 * 1024 * 1024  // 内存池初始分配 8M 内存

/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->memLock));\
    \
    void **zppPrev = zpGlobRepo_[zRepoId]->p_memPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        munmap(zpGlobRepo_[zRepoId]->p_memPool, zMemPoolSiz);\
        zpGlobRepo_[zRepoId]->p_memPool = zppPrev;\
    }\
    zpGlobRepo_[zRepoId]->memPoolOffSet = sizeof(void *);\
    /* memset(zpGlobRepo_[zRepoId]->p_memPool, 0, zMemPoolSiz); */\
    \
    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->memLock));\
} while(0)


/* 用于提取深层对象 */
#define zGet_OneCommitVecWrap_(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_)
#define zGet_OneFileVecWrap_(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_->p_refData_[zFileId].p_subVecWrap_)

#define zGet_OneCommitSig(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_->p_refData_[zFileId].p_data)


/* 用于提取原始数据 */
typedef struct __zBaseData__ {
    struct __zBaseData__ *p_next;
    _i dataLen;
    char p_data[];
} zBaseData__;


typedef struct __zPgLogin__ {
    char * p_host;
    char * p_addr;
    char * p_port;
    char * p_userName;
    char * p_passFilePath;
    char * p_dbName;
} zPgLogin__;


// ProjId  RevSig  TimeStamp  GlobRes(S Success | s FakeSuccess | F Fail)  GlobTimeSpent  TotalHostCnt  FailedHostCnt
// RevSig  CacheId    HostIp  HostRes(0 Success/-1 Unknown/-2 Fail)  HostTimeSpent  HostDetail
typedef struct __zDpLog__ {
    //void *_;  // used to kill pthread
    //_i *p_taskCnt;

    char *p_projId;
    char *p_revSig;
    char *p_timeStamp;  /* the time when dp finished */
    char *p_globRes;  /* s/success, S/(fake Success == 90% * success, etc.), f/fail*/
    char *p_globTimeSpent;
    char *p_totalHostCnt;
    char *p_failedHostCnt;

    char *p_cacheId;  /* use to mark multi dp for the same revSig*/
    char *p_hostIp;
    char *p_hostRes;  /* s/success, u/unknown, f/fail */
    char *p_hostTimeSpent;
    char *p_hostDetail;
} zDpLog__;


typedef struct __zRepoMeta__ {
    void *_;  // used to kill pthread
    _i *p_taskCnt;

    char *p_id;
    char *p_pathOnHost;
    char *p_sourceUrl;
    char *p_sourceBranch;
    char *p_sourceVcsType;
    char *p_needPull;
} zRepoMeta__;


typedef struct __zPgRes__ {
    _i tupleCnt;
    _i fieldCnt;
    _i taskCnt;

    zRepoMeta__ *p_repoMeta;
} zPgRes__;


struct zNativeOps__ {
    void * (* get_revs) (void *);
    void * (* get_diff_files) (void *);
    void * (* get_diff_contents) (void *);

    _i (* proj_init) (zRepoMeta__ *);
    void * (* proj_init_all) (zPgLogin__ *);

    /* 以 ANSI 字符集中的前 128 位成员作为索引 */
    void (* json_parser[128]) (void *, void *);
    void * (* alloc) (_i, _ui);
    void * (* sysload_monitor) (void *);
};


// extern struct zNativeOps__ zNativeOps_;

#endif  // #ifndef ZLOCALOPS_H
