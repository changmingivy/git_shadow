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
#include "zPgSQL.h"
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

struct zNativeOps__ {
    void * (* get_revs) (void *);
    void * (* get_diff_files) (void *);
    void * (* get_diff_contents) (void *);

    _i (* proj_init) (zPgResTuple__ *);
    void * (* proj_init_all) (zPgLogin__ *);

    void * (* alloc) (_i, _ui);
    void * (* sysload_monitor) (void *);

    void * (* extend_pg_partition) (void *zp);
};

#endif  // #ifndef ZLOCALOPS_H
