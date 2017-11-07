#define ZLOCALOPS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#ifndef ZPOSIXREG_H
#include "zPosixReg.h"
#endif

#ifndef ZLOCALUTILS_H
#include "zNativeUtils.h"
#endif

#ifndef ZTHREADPOOL_H
#include "zThreadPool.h"
#endif

#ifndef ZLIBGIT_H
#include "zLibGit.h"
#endif

#ifndef ZDPOPS_H
#include "zDpOps.h"
#endif

#define zMemPoolSiz 8 * 1024 * 1024  // 内存池初始分配 8M 内存

/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->MemLock));\
    \
    void **zppPrev = zpGlobRepo_[zRepoId]->p_MemPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        munmap(zpGlobRepo_[zRepoId]->p_MemPool, zMemPoolSiz);\
        zpGlobRepo_[zRepoId]->p_MemPool = zppPrev;\
    }\
    zpGlobRepo_[zRepoId]->MemPoolOffSet = sizeof(void *);\
    /* memset(zpGlobRepo_[zRepoId]->p_MemPool, 0, zMemPoolSiz); */\
    \
    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->MemLock));\
} while(0)


/* 用于提取深层对象 */
#define zGet_OneCommitVecWrap_(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_)
#define zGet_OneFileVecWrap_(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_->p_RefData_[zFileId].p_SubVecWrap_)

#define zGet_OneCommitSig(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_->p_RefData_[zFileId].p_data)


/* 用于提取原始数据 */
typedef struct __zBaseData__ {
    struct __zBaseData__ *p_next;
    _i DataLen;
    char p_data[];
} zBaseData__;


typedef struct __zPgLogin__ {
    char * p_host;
    char * p_addr;
    char * p_port;
    char * p_UserName;
    char * p_PassFilePath;
    char * p_DBName;
} zPgLogin__;


typedef struct __zRepoMetaStr__ {
    void *_;  // used to kill pthread
    _i *p_TaskCnt;

    char *p_id;
    char *p_PathOnHost;
    char *p_SourceUrl;
    char *p_SourceBranch;
    char *p_SourceVcsType;
    char *p_NeedPull;
} zRepoMetaStr__;


typedef struct __zPgRes__ {
    _i TupleCnt;
    _i FieldCnt;
    _i TaskCnt;

    zRepoMetaStr__ *p_RepoMetaStr;
} zPgRes__;


struct zNativeOps__ {
    void * (* get_revs) (void *);
    void * (* get_diff_files) (void *);
    void * (* get_diff_contents) (void *);

    _i (* proj_init) (zRepoMetaStr__ *);
    void * (* proj_init_all) (zPgLogin__ *);

    /* 以 ANSI 字符集中的前 128 位成员作为索引 */
    void (* json_parser[128]) (void *, void *);
    void * (* alloc) (_i, _ui);
    void * (* sysload_monitor) (void *);
};


// extern struct zNativeOps__ zNativeOps_;
