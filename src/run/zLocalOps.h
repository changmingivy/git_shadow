#define ZLOCALOPS_H

#ifndef ZCOMMON_H
#include "zCommon.h"
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
    \
    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->MemLock));\
} while(0)


/* 用于提取深层对象 */
#define zGet_OneCommitVecWrap_(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_)
#define zGet_OneFileVecWrap_(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_->p_RefData_[zFileId].p_SubVecWrap_)

#define zGet_OneCommitSig(zpTopVecWrap_, zCommitId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrap_, zCommitId, zFileId) ((zpTopVecWrap_)->p_RefData_[zCommitId].p_SubVecWrap_->p_RefData_[zFileId].p_data)


/* 用于提取原始数据 */
typedef struct {
    struct zBaseData__ *p_next;
    _i DataLen;
    char p_data[];
} zBaseData__;


struct zLocalOps__ {
    void * (* get_revs) (void *);
    void * (* get_diff_files) (void *);
    void * (* get_diff_contents) (void *);

    _i (* proj_init) (char *zpRepoMetaData);
    void * (* proj_init_all) (const char *);

    /* 以 ANSI 字符集中的前 128 位成员作为索引 */
    void (* json_parser[128]) (void *, void *);
    void * (* alloc) (_i, _ui);
    void * (* sysload_monitor) (void *);
};


extern struct zLocalOps__ zLocalOps_;
