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
#include "zNetUtils.h"
#include "zThreadPool.h"
#include "zLibGit.h"
#include "zPgSQL.h"
#include "zDpOps.h"
#include "zRun.h"
//#include "zMd5Sum.h"

#define zMEM_POOL_SIZ 8 * 1024 * 1024  // 内存池初始分配 8M 内存

/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zMEM_POOL_REST(zRepoId) do {\
    pthread_mutex_lock(& zpRepo_->memLock);\
    \
    void **zppPrev = zpRepo_->p_memPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        free(zpRepo_->p_memPool);\
        zpRepo_->p_memPool = zppPrev;\
    }\
    zpRepo_->memPoolOffSet = sizeof(void *);\
    /* memset(zpRepo_->p_memPool, 0, zMEM_POOL_SIZ); */\
    \
    pthread_mutex_unlock(& zpRepo_->memLock);\
} while(0)


/* 用于提取深层对象 */
#define zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_)
#define zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_->p_refData_[zFileId].p_subVecWrap_)

#define zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_data)
#define zGET_ONE_FILE_PATH(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_refData_[zCommitId].p_subVecWrap_->p_refData_[zFileId].p_data)


/* 用于提取原始数据 */
typedef struct __zBaseData__ {
    struct __zBaseData__ *p_next;
    _i dataLen;
    char p_data[];
} zBaseData__;

struct zNativeOps__ {
    void (* get_revs) (void *);
    void * (* get_diff_files) (void *);
    void * (* get_diff_contents) (void *);

    void (* repo_init) (char **, _i);
    void (* repo_init_all) ();

    void * (* alloc) (size_t);

    void * (* cron_ops) (void *);
};

#endif  // #ifndef ZLOCALOPS_H
