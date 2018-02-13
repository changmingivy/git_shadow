#ifndef ZLOCALOPS_H
#define ZLOCALOPS_H

#include "zcommon.h"

#define zMEM_POOL_SIZ 4 * 1024 * 1024  // 内存池初始分配 4M 内存

/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zMEM_POOL_REST(zRepoID) do {\
    pthread_mutex_lock(& zpRepo_->memLock);\
    \
    struct zMemPool__ *pTmp_ = NULL;\
    while(NULL != zpRepo_->p_memPool_->p_prev) {\
        pTmp_ = zpRepo_->p_memPool_;\
        zpRepo_->p_memPool_ = zpRepo_->p_memPool_->p_prev;\
        free(pTmp_);\
    }\
    zpRepo_->memPoolOffSet = 0;\
    /* memset(zpRepo_->p_memPool_, 0, zMEM_POOL_SIZ); */\
    \
    pthread_mutex_unlock(& zpRepo_->memLock);\
} while(0)


/* 用于提取深层对象 */
#define zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zCommitID) ((zpTopVecWrap_)->p_refData_[zCommitID].p_subVecWrap_)
#define zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zCommitID, zFileID) ((zpTopVecWrap_)->p_refData_[zCommitID].p_subVecWrap_->p_refData_[zFileID].p_subVecWrap_)

#define zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zCommitID) ((zpTopVecWrap_)->p_refData_[zCommitID].p_data)
#define zGET_ONE_FILE_PATH(zpTopVecWrap_, zCommitID, zFileID) ((zpTopVecWrap_)->p_refData_[zCommitID].p_subVecWrap_->p_refData_[zFileID].p_data)


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
