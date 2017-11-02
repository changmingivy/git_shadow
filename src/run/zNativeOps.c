#include "zNativeOps.h"

#include <unistd.h>
#include <fcntl.h>

#include <time.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>


#define zDpSigLogPath "_SHADOW/log/deploy/meta"  // 40位SHA1 sig字符串 + 时间戳
#define zDpTimeSpentLogPath "_SHADOW/log/deploy/TimeSpent"  // 40位SHA1 sig字符串 + 时间戳

extern struct zPosixReg__ zPosixReg_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zThreadPool__ zThreadPool_;
extern struct zLibGit__ zLibGit_;
extern struct zDpOps__ zDpOps_;


static void *
zalloc_cache(_i zRepoId, _ui zSiz);

static void *
zget_diff_content(void *zpParam);

static void *
zget_file_list(void *zpParam);

static void *
zgenerate_cache(void *zpParam);

static _i
zinit_one_repo_env(char *zpRepoMetaData);

static void *
zsys_load_monitor(void *zpParam);

static void *
zinit_env(const char *zpConfPath);

struct zNativeOps__ zNativeOps_ = {
    .get_revs = zgenerate_cache,
    .get_diff_files = zget_file_list,
    .get_diff_contents = zget_diff_content,

    .proj_init = zinit_one_repo_env,
    .proj_init_all = zinit_env,

    .json_parser = { NULL },
    .alloc = zalloc_cache,
    .sysload_monitor = zsys_load_monitor
};


/*************
 * GLOB VARS *
 *************/
_i zGlobMaxRepoId;
zRepo__ *zpGlobRepo_[zGlobRepoIdLimit];

/* 系统 CPU 与 MEM 负载监控：以 0-100 表示 */
pthread_mutex_t zGlobCommonLock;
pthread_cond_t zSysLoadCond;  // 系统由高负载降至可用范围时，通知等待的线程继续其任务(注：使用全局通用锁与之配套)
_ul zGlobMemLoad;  // 高于 80 拒绝布署，同时 git push 的过程中，若高于 80 则剩余任阻塞等待


/* 专用于缓存的内存调度分配函数，适用多线程环境，不需要free */
static void *
zalloc_cache(_i zRepoId, _ui zSiz) {
    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->MemLock));

    if ((zSiz + zpGlobRepo_[zRepoId]->MemPoolOffSet) > zMemPoolSiz) {
        void **zppPrev, *zpCur;
        /* 新增一块内存区域加入内存池，以上一块内存的头部预留指针位存储新内存的地址 */
        if (MAP_FAILED == (zpCur = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
            zPrint_Time();
            fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
            exit(1);
        }
        zppPrev = zpCur;
        zppPrev[0] = zpGlobRepo_[zRepoId]->p_MemPool;  // 首部指针位指向上一块内存池map区
        zpGlobRepo_[zRepoId]->p_MemPool = zpCur;  // 更新当前内存池指针
        zpGlobRepo_[zRepoId]->MemPoolOffSet = sizeof(void *);  // 初始化新内存池区域的 offset
    }

    void *zpX = zpGlobRepo_[zRepoId]->p_MemPool + zpGlobRepo_[zRepoId]->MemPoolOffSet;
    zpGlobRepo_[zRepoId]->MemPoolOffSet += zSiz;

    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->MemLock));
    return zpX;
}


/*
 * 功能：生成单个文件的差异内容缓存
 */
static void *
zget_diff_content(void *zpParam) {
    zMeta__ *zpMeta_ = (zMeta__ *)zpParam;
    zVecWrap__ *zpTopVecWrap_;
    zBaseData__ *zpTmpBaseData_[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zpGlobRepo_[zpMeta_->RepoId]->RepoPathLen + 40 + 40 + zpGlobRepo_[zpMeta_->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf, "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath,
            zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId),
            zGet_OneFilePath(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    zCnter = 0;
    if (0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBytes(1448), zpShellRetHandler))) {
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zBaseData__) + zBaseDataLen);
        zpTmpBaseData_[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseData_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zBaseData__) + zBaseDataLen);
            zpTmpBaseData_[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

            zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
            zpTmpBaseData_[1] = zpTmpBaseData_[0];
        }

        pclose(zpShellRetHandler);
    } else {
        pclose(zpShellRetHandler);
        return (void *) -1;
    }

    if (0 == zCnter) {
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId) = NULL;
    } else {
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId) = zalloc_cache(zpMeta_->RepoId, sizeof(zVecWrap__));
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->VecSiz = -7;  // 先赋为 -7
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->p_RefData_ = NULL;
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->p_Vec_ = zalloc_cache(zpMeta_->RepoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
            zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->p_Vec_[i].iov_base = zpTmpBaseData_[2]->p_data;
            zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->p_Vec_[i].iov_len = zpTmpBaseData_[2]->DataLen;
        }

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->VecSiz = zCnter;
    }

    return NULL;
}


/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表
 */
#define zGenerate_Graph(zpNode_) do {\
    zMeta__ *____zpTmpNode_;\
    _i ____zOffSet;\
\
    zpNode_->pp_ResHash[zpNode_->LineNum] = zpNode_;\
    ____zOffSet = 6 * zpNode_->OffSet + 10;\
\
    zpNode_->p_data[--____zOffSet] = ' ';\
    zpNode_->p_data[--____zOffSet] = '\200';\
    zpNode_->p_data[--____zOffSet] = '\224';\
    zpNode_->p_data[--____zOffSet] = '\342';\
    zpNode_->p_data[--____zOffSet] = '\200';\
    zpNode_->p_data[--____zOffSet] = '\224';\
    zpNode_->p_data[--____zOffSet] = '\342';\
    zpNode_->p_data[--____zOffSet] = (NULL == zpNode_->p_left) ? '\224' : '\234';\
    zpNode_->p_data[--____zOffSet] = '\224';\
    zpNode_->p_data[--____zOffSet] = '\342';\
\
    ____zpTmpNode_ = zpNode_;\
    for (_i i = 0; i < zpNode_->OffSet; i++) {\
        zpNode_->p_data[--____zOffSet] = ' ';\
        zpNode_->p_data[--____zOffSet] = ' ';\
        zpNode_->p_data[--____zOffSet] = ' ';\
\
        ____zpTmpNode_ = ____zpTmpNode_->p_father;\
        if (NULL == ____zpTmpNode_->p_left) {\
            zpNode_->p_data[--____zOffSet] = ' ';\
        } else {\
            zpNode_->p_data[--____zOffSet] = '\202';\
            zpNode_->p_data[--____zOffSet] = '\224';\
            zpNode_->p_data[--____zOffSet] = '\342';\
        }\
    }\
\
    zpNode_->p_data = zpNode_->p_data + ____zOffSet;\
\
} while (0)

static void *
zdistribute_task(void *zpParam) {
    zMeta__ *zpNode_ = (zMeta__ *)zpParam;
    zMeta__ **zppKeepPtr = zpNode_->pp_ResHash;

    do {
        /* 分发直连的子节点 */
        if (NULL != zpNode_->p_FirstChild) {
            zpNode_->p_FirstChild->pp_ResHash = zppKeepPtr;
            zdistribute_task(zpNode_->p_FirstChild);  // 暂时以递归处理，线程模型会有收集不齐全部任务的问题
        }

        /* 自身及所有的左兄弟 */
        zGenerate_Graph(zpNode_);
        zpNode_ = zpNode_->p_left;
    } while ((NULL != zpNode_) && (zpNode_->pp_ResHash = zppKeepPtr));

    return NULL;
}

#define zGenerate_Tree_Node() do {\
    zpTmpNode_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zMeta__));\
\
    zpTmpNode_[0]->LineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNode_[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNode_[0]->p_FirstChild = NULL;\
    zpTmpNode_[0]->p_left = NULL;\
    zpTmpNode_[0]->p_data = zalloc_cache(zpMeta_->RepoId, 6 * zpTmpNode_[0]->OffSet + 10 + 1 + zRegRes_->ResLen[zNodeCnter]);\
    strcpy(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->OffSet + 10, zRegRes_->p_rets[zNodeCnter]);\
\
    zpTmpNode_[0]->OpsId = 0;\
    zpTmpNode_[0]->RepoId = zpMeta_->RepoId;\
    zpTmpNode_[0]->CommitId = zpMeta_->CommitId;\
    zpTmpNode_[0]->CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;\
    zpTmpNode_[0]->DataType = zpMeta_->DataType;\
\
    if (zNodeCnter == (zRegRes_->cnt - 1)) {\
        zpTmpNode_[0]->FileId = zpTmpNode_[0]->LineNum;\
        zpTmpNode_[0]->p_ExtraData = zalloc_cache(zpMeta_->RepoId, zBaseDataLen);\
        memcpy(zpTmpNode_[0]->p_ExtraData, zCommonBuf, zBaseDataLen);\
    } else {\
        zpTmpNode_[0]->FileId = -1;\
        zpTmpNode_[0]->p_ExtraData = NULL;\
    }\
\
    if (0 == zNodeCnter) {\
        zpTmpNode_[0]->p_father = NULL;\
        if (NULL == zpRootNode_) {\
            zpRootNode_ = zpTmpNode_[0];\
        } else {\
            for (zpTmpNode_[2] = zpRootNode_; NULL != zpTmpNode_[2]->p_left; zpTmpNode_[2] = zpTmpNode_[2]->p_left) {}\
            zpTmpNode_[2]->p_left = zpTmpNode_[0];\
        }\
    } else {\
        zpTmpNode_[0]->p_father = zpTmpNode_[1];\
        if (NULL == zpTmpNode_[2]) {\
            zpTmpNode_[1]->p_FirstChild = zpTmpNode_[0];\
        } else {\
            zpTmpNode_[2]->p_left = zpTmpNode_[0];\
        }\
    }\
\
    zNodeCnter++;\
    for (; zNodeCnter < zRegRes_->cnt; zNodeCnter++) {\
        zpTmpNode_[0]->p_FirstChild = zalloc_cache(zpMeta_->RepoId, sizeof(zMeta__));\
        zpTmpNode_[1] = zpTmpNode_[0];\
\
        zpTmpNode_[0] = zpTmpNode_[0]->p_FirstChild;\
\
        zpTmpNode_[0]->p_father = zpTmpNode_[1];\
        zpTmpNode_[0]->p_FirstChild = NULL;\
        zpTmpNode_[0]->p_left = NULL;\
\
        zpTmpNode_[0]->LineNum = zLineCnter;  /* 横向偏移 */\
        zLineCnter++;  /* 每个节点会占用一行显示输出 */\
        zpTmpNode_[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
        zpTmpNode_[0]->p_data = zalloc_cache(zpMeta_->RepoId, 6 * zpTmpNode_[0]->OffSet + 10 + 1 + zRegRes_->ResLen[zNodeCnter]);\
        strcpy(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->OffSet + 10, zRegRes_->p_rets[zNodeCnter]);\
\
        zpTmpNode_[0]->OpsId = 0;\
        zpTmpNode_[0]->RepoId = zpMeta_->RepoId;\
        zpTmpNode_[0]->CommitId = zpMeta_->CommitId;\
        zpTmpNode_[0]->CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;\
        zpTmpNode_[0]->DataType = zpMeta_->DataType;\
\
        zpTmpNode_[0]->FileId = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNode_[0]->p_ExtraData = NULL;\
    }\
    zpTmpNode_[0]->FileId = zpTmpNode_[0]->LineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNode_[0]->p_ExtraData = zalloc_cache(zpMeta_->RepoId, zBaseDataLen);\
    memcpy(zpTmpNode_[0]->p_ExtraData, zCommonBuf, zBaseDataLen);\
} while(0)

/* 差异文件数量 >128 时，调用此函数，以防生成树图损耗太多性能；此时无需检查无差的性况 */
static void
zget_file_list_large(zMeta__ *zpMeta_, zVecWrap__ *zpTopVecWrap_, FILE *zpShellRetHandler, char *zpCommonBuf, _i zMaxBufLen) {
    zMeta__ zSubMeta_;
    zBaseData__ *zpTmpBaseData_[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    for (zCnter = 0; NULL != zNativeUtils_.read_line(zpCommonBuf, zMaxBufLen, zpShellRetHandler); zCnter++) {
        zBaseDataLen = strlen(zpCommonBuf);
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zBaseData__) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0]; }
        zpTmpBaseData_[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zpCommonBuf, zBaseDataLen);
        zpTmpBaseData_[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
        zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[0] = zpTmpBaseData_[0]->p_next;
    }
    pclose(zpShellRetHandler);

    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId) = zalloc_cache(zpMeta_->RepoId, sizeof(zVecWrap__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz = zCnter;
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_ = zalloc_cache(zpMeta_->RepoId, zCnter * sizeof(zRefData__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_ = zalloc_cache(zpMeta_->RepoId, zCnter * sizeof(struct iovec));

    for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_[i].p_data = zpTmpBaseData_[2]->p_data;

        /* 用于转换成JsonStr */
        zSubMeta_.OpsId = 0;
        zSubMeta_.RepoId = zpMeta_->RepoId;
        zSubMeta_.CommitId = zpMeta_->CommitId;
        zSubMeta_.FileId = i;
        zSubMeta_.CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;
        zSubMeta_.DataType = zpMeta_->DataType;
        zSubMeta_.p_data = zpTmpBaseData_[2]->p_data;
        zSubMeta_.p_ExtraData = NULL;

        /* 将zMeta__转换为JSON文本 */
        zDpOps_.struct_to_json(zpCommonBuf, &zSubMeta_);

        zVecDataLen = strlen(zpCommonBuf);
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[i].iov_len = zVecDataLen;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[i].iov_base = zalloc_cache(zpMeta_->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[i].iov_base, zpCommonBuf, zVecDataLen);

        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_[i].p_SubVecWrap_ = NULL;
    }

    /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
    ((char *)(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[0].iov_base))[0] = '[';
}


static void *
zget_file_list(void *zpParam) {
    zMeta__ *zpMeta_ = (zMeta__ *)zpParam;
    zVecWrap__ *zpTopVecWrap_;
    FILE *zpShellRetHandler;

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpGlobRepo_[zpMeta_->RepoId]->RepoPathLen + 4 * 40 + zpGlobRepo_[zpMeta_->RepoId]->MaxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */

    sprintf(zCommonBuf, "cd \"%s\" && git diff --shortstat \"%s\" \"%s\" | grep -oP '\\d+(?=\\s*file)' && git diff --name-only \"%s\" \"%s\"",
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath,
            zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId),
            zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );

    /* 差异文件数量 >24 时使用 git 原生视图，避免占用太多资源，同时避免爆栈 */
    if (NULL == zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        pclose(zpShellRetHandler);
        return (void *) -1;
    } else {
        if (24 < strtol(zCommonBuf, NULL, 10)) {
            zget_file_list_large(zpMeta_, zpTopVecWrap_, zpShellRetHandler, zCommonBuf, zMaxBufLen);
            goto zMarkLarge;
        }
    }

    /* 差异文件数量 <=24 生成Tree图 */
    zMeta__ zSubMeta_;
    _ui zVecDataLen, zBaseDataLen, zNodeCnter, zLineCnter;
    zMeta__ *zpRootNode_, *zpTmpNode_[3];  // [0]：本体    [1]：记录父节点    [2]：记录兄长节点
    zRegInit__ zRegInit_[1];
    zRegRes__ zRegRes_[1] = {{.RepoId = zpMeta_->RepoId}};  // 使用项目内存池

    /* 在生成树节点之前分配空间，以使其不为 NULL，防止多个查询文件列的的请求导致重复生成同一缓存 */
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId) = zalloc_cache(zpMeta_->RepoId, sizeof(zVecWrap__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz = -7;  // 先赋为 -7，知会同类请求缓存正在生成过程中

    zpRootNode_ = NULL;
    zLineCnter = 0;
    zPosixReg_.compile(zRegInit_, "[^/]+");
    if (NULL != zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        zBaseDataLen = strlen(zCommonBuf);

        zCommonBuf[zBaseDataLen - 1] = '\0';  // 去掉换行符
        zPosixReg_.match(zRegRes_, zRegInit_, zCommonBuf);

        zNodeCnter = 0;
        zpTmpNode_[2] = zpTmpNode_[1] = zpTmpNode_[0] = NULL;
        zGenerate_Tree_Node(); /* 添加树节点 */

        while (NULL != zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
            zBaseDataLen = strlen(zCommonBuf);

            zCommonBuf[zBaseDataLen - 1] = '\0';  // 去掉换行符
            zPosixReg_.match(zRegRes_, zRegInit_, zCommonBuf);

            zpTmpNode_[0] = zpRootNode_;
            zpTmpNode_[2] = zpTmpNode_[1] = NULL;
            for (zNodeCnter = 0; zNodeCnter < zRegRes_->cnt;) {
                do {
                    if (0 == strcmp(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->OffSet + 10, zRegRes_->p_rets[zNodeCnter])) {
                        zpTmpNode_[1] = zpTmpNode_[0];
                        zpTmpNode_[0] = zpTmpNode_[0]->p_FirstChild;
                        zpTmpNode_[2] = NULL;
                        zNodeCnter++;
                        if (NULL == zpTmpNode_[0]) {
                            goto zMarkOuter;
                        } else {
                            goto zMarkInner;
                        }
                    }
                    zpTmpNode_[2] = zpTmpNode_[0];
                    zpTmpNode_[0] = zpTmpNode_[0]->p_left;
                } while (NULL != zpTmpNode_[0]);
                break;
zMarkInner:;
            }
zMarkOuter:;
            zGenerate_Tree_Node(); /* 添加树节点 */
        }
    }
    zPosixReg_.free_meta(zRegInit_);
    pclose(zpShellRetHandler);

    if (NULL == zpRootNode_) {
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_ = NULL;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_ = zalloc_cache(zpMeta_->RepoId, sizeof(struct iovec));

        zSubMeta_.OpsId = 0;
        zSubMeta_.RepoId = zpMeta_->RepoId;
        zSubMeta_.CommitId = zpMeta_->CommitId;
        zSubMeta_.FileId = -1;  // 置为 -1，不允许再查询下一级内容
        zSubMeta_.CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;
        zSubMeta_.DataType = zpMeta_->DataType;
        zSubMeta_.p_data = (0 == strcmp(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId))) ? "===> 最新的已布署版本 <===" : "=> 无差异 <=";
        zSubMeta_.p_ExtraData = NULL;

        /* 将zMeta__转换为JSON文本 */
        zDpOps_.struct_to_json(zCommonBuf, &zSubMeta_);
        zCommonBuf[0] = '[';  // 逗号替换为 '['

        zVecDataLen = strlen(zCommonBuf);
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[0].iov_base = zalloc_cache(zpMeta_->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[0].iov_base, zCommonBuf, zVecDataLen);

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz = 1;
    } else {
        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNode_->pp_ResHash = zalloc_cache(zpMeta_->RepoId, zLineCnter * sizeof(zMeta__ *));

        /* Tree 图 */
        zdistribute_task(zpRootNode_);

        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_
            = zalloc_cache(zpMeta_->RepoId, zLineCnter * sizeof(zRefData__));
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_
            = zalloc_cache(zpMeta_->RepoId, zLineCnter * sizeof(struct iovec));

        for (_ui zCnter = 0; zCnter < zLineCnter; zCnter++) {
            zDpOps_.struct_to_json(zCommonBuf, zpRootNode_->pp_ResHash[zCnter]); /* 将 zMeta__ 转换为 json 文本 */

            zVecDataLen = strlen(zCommonBuf);
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[zCnter].iov_len = zVecDataLen;
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[zCnter].iov_base = zalloc_cache(zpMeta_->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[zCnter].iov_base, zCommonBuf, zVecDataLen);

            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_[zCnter].p_data = zpRootNode_->pp_ResHash[zCnter]->p_ExtraData;
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_RefData_[zCnter].p_SubVecWrap_ = NULL;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_[0].iov_base))[0] = '[';

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz = zLineCnter;
    }

zMarkLarge:
    return NULL;
}


/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
static void *
zgenerate_cache(void *zpParam) {
    zMeta__ *zpMeta_, zSubMeta_;
    zVecWrap__ *zpTopVecWrap_, *zpSortedTopVecWrap_;
    zBaseData__ *zpTmpBaseData_[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    zpMeta_ = (zMeta__ *)zpParam;

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpGlobRepo_[zpMeta_->RepoId]->RepoPathLen + 12;
    char zCommonBuf[zMaxBufLen];

    FILE *zpShellRetHandler;
    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->SortedCommitVecWrap_);
        sprintf(zCommonBuf, "cd \"%s\" && git log server%d --format=\"%%H_%%ct\"", zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath, zpMeta_->RepoId); // 取 server 分支的提交记录
        zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->SortedDpVecWrap_);
        // 调用外部命令 tail，而不是用 fopen 打开，如此可用统一的 pclose 关闭
        sprintf(zCommonBuf, "tail -%d \"%s%s\"", zCacheSiz, zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath, zDpSigLogPath);
        zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    /* 第一行单独处理，避免后续每次判断是否是第一行 */
    zCnter = 0;
    if (NULL != zNativeUtils_.read_line(zCommonBuf, zGlobCommonBufSiz, zpShellRetHandler)) {
        /* 只提取比最近一次布署版本更新的提交记录 */
        if ((zIsCommitDataType == zpMeta_->DataType)
                && (0 == (strncmp(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
        zBaseDataLen = strlen(zCommonBuf);
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zBaseData__) + zBaseDataLen);
        zpTmpBaseData_[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zCommonBuf, zBaseDataLen);
        zpTmpBaseData_[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[1]->p_next = NULL;

        zCnter++;
        for (; (zCnter < zCacheSiz) && (NULL != zNativeUtils_.read_line(zCommonBuf, zGlobCommonBufSiz, zpShellRetHandler)); zCnter++) {
            /* 只提取比最近一次布署版本更新的提交记录 */
            if ((zIsCommitDataType == zpMeta_->DataType)
                    && (0 == (strncmp(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zCommonBuf, zBytes(40))))) { goto zMarkSkip; }
            zBaseDataLen = strlen(zCommonBuf);
            zpTmpBaseData_[0] = zalloc_cache(zpMeta_->RepoId, sizeof(zBaseData__) + zBaseDataLen);
            zpTmpBaseData_[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseData_[0]->p_data, zCommonBuf, zBaseDataLen);
            zpTmpBaseData_[0]->p_data[zBaseDataLen - 1] = '\0';

            zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
            zpTmpBaseData_[1] = zpTmpBaseData_[0];
        }
    }
zMarkSkip:
    pclose(zpShellRetHandler);

    /* 存储的是实际的对象数量 */
    zpSortedTopVecWrap_->VecSiz = zpTopVecWrap_->VecSiz = zCnter;

    if (0 != zCnter) {
        for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
            zpTmpBaseData_[2]->p_data[40] = '\0';

            /* 用于转换成JsonStr */
            zSubMeta_.OpsId = 0;
            zSubMeta_.RepoId = zpMeta_->RepoId;
            zSubMeta_.CommitId = i;
            zSubMeta_.FileId = -1;
            zSubMeta_.CacheId =  zpGlobRepo_[zpMeta_->RepoId]->CacheId;
            zSubMeta_.DataType = zpMeta_->DataType;
            zSubMeta_.p_data = zpTmpBaseData_[2]->p_data;
            zSubMeta_.p_ExtraData = &(zpTmpBaseData_[2]->p_data[41]);

            /* 将zMeta__转换为JSON文本 */
            zDpOps_.struct_to_json(zCommonBuf, &zSubMeta_);

            zVecDataLen = strlen(zCommonBuf);
            zpTopVecWrap_->p_Vec_[i].iov_len = zVecDataLen;
            zpTopVecWrap_->p_Vec_[i].iov_base = zalloc_cache(zpMeta_->RepoId, zVecDataLen);
            memcpy(zpTopVecWrap_->p_Vec_[i].iov_base, zCommonBuf, zVecDataLen);

            zpTopVecWrap_->p_RefData_[i].p_data = zpTmpBaseData_[2]->p_data;
            zpTopVecWrap_->p_RefData_[i].p_SubVecWrap_ = NULL;
        }

        if (zIsDpDataType == zpMeta_->DataType) {
            /* 存储最近一次布署的 SHA1 sig，执行布署时首先对比布署目标与最近一次布署，若相同，则直接返回成功 */
            strcpy(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zpTopVecWrap_->p_RefData_[zCnter - 1].p_data);
            /* 将布署记录按逆向时间排序（新记录显示在前面） */
            for (_i i = 0; i < zpTopVecWrap_->VecSiz; i++) {
                zCnter--;
                zpSortedTopVecWrap_->p_Vec_[zCnter].iov_base = zpTopVecWrap_->p_Vec_[i].iov_base;
                zpSortedTopVecWrap_->p_Vec_[zCnter].iov_len = zpTopVecWrap_->p_Vec_[i].iov_len;
            }
        } else {
            /* 提交记录缓存本来就是有序的，不需要额外排序 */
            zpSortedTopVecWrap_->p_Vec_ = zpTopVecWrap_->p_Vec_;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zpSortedTopVecWrap_->p_Vec_[0].iov_base))[0] = '[';
    }

    /* 防止意外访问导致的程序崩溃 */
    memset(zpTopVecWrap_->p_RefData_ + zpTopVecWrap_->VecSiz, 0, sizeof(zRefData__) * (zCacheSiz - zpTopVecWrap_->VecSiz));

    return NULL;
}


/************
 * INIT OPS *
 ************/
/*
 * 参数：
 *   新建项目基本信息五个字段
 *   初次启动标记(zInitMark: 1 表示为初始化时调用，0 表示动态更新时调用)
 * 返回值:
 *         -33：无法创建请求的项目路径
 *         -34：请求创建的新项目信息格式错误（合法字段数量不是五个）
 *         -35：
 *         -36：请求创建的项目路径已存在，且项目ID不同
 *         -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *         -38：拉取远程代码库失败（git clone 失败）
 *         -39：项目元数据创建失败，如：无法打开或创建布署日志文件meta等原因
 */
#define zFree_Source() do {\
    free(zpGlobRepo_[zRepoId]->p_RepoPath);\
    free(zpGlobRepo_[zRepoId]);\
    zpGlobRepo_[zRepoId] = NULL;\
    zPosixReg_.free_res(zRegRes_);\
    zPrint_Time();\
} while(0)


static _i
zinit_one_repo_env(char *zpRepoMetaData) {
    zRegInit__ zRegInit_[2];
    zRegRes__ zRegRes_[2] = {{.RepoId = -1}, {.RepoId = -1}};  // 使用系统 *alloc 函数分配内存

    _i zRepoId, zErrNo;

    /* 正则匹配项目基本信息（5个字段） */
    zPosixReg_.compile(zRegInit_, "(\\w|[[:punct:]])+");
    zPosixReg_.match(zRegRes_, zRegInit_, zpRepoMetaData);
    zPosixReg_.free_meta(zRegInit_);
    if (5 > zRegRes_->cnt) {
        zPosixReg_.free_res(zRegRes_);
        zPrint_Time();
        return -34;
    }

    /* 提取项目ID，调整 zGlobMaxRepoId */
    zRepoId = strtol(zRegRes_->p_rets[0], NULL, 10);
    if ((zGlobRepoIdLimit > zRepoId) && (0 < zRepoId)) {} else {
        zPosixReg_.free_res(zRegRes_);
        zPrint_Time();
        return -32;
    }

    if (NULL != zpGlobRepo_[zRepoId]) {
        zPosixReg_.free_res(zRegRes_);
        zPrint_Time();
        return -35;
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zpGlobRepo_[zRepoId], zRepo__, 1);
    zpGlobRepo_[zRepoId]->RepoId = zRepoId;
    zpGlobRepo_[zRepoId]->SelfPushMark = (6 == zRegRes_->cnt) ? 1 : 0;

    /* 提取项目绝对路径，结果格式：/home/git/`dirname($Path_On_Host)`/.____DpSystem/`basename($Path_On_Host)` */
    zPosixReg_.compile(zRegInit_ + 1, "[^/]+[/]*$");
    zPosixReg_.match(zRegRes_ + 1, zRegInit_ + 1, zRegRes_->p_rets[1]);
    zPosixReg_.free_meta(zRegInit_ + 1);
    /* 去掉 basename 部分 */
    zRegRes_->p_rets[1][zRegRes_->ResLen[1] - (zRegRes_ + 1)->ResLen[0]] = '\0';
    /* 拼接结果字符串 */
    while ('/' == zRegRes_->p_rets[1][0]) { zRegRes_->p_rets[1]++; }  // 去除多余的 '/'
    zMem_Alloc(zpGlobRepo_[zRepoId]->p_RepoPath, char, 32 + sizeof("/home/git/.____DpSystem/") + zRegRes_->ResLen[1]);
    zpGlobRepo_[zRepoId]->RepoPathLen = sprintf(zpGlobRepo_[zRepoId]->p_RepoPath, "%s%s%s/%d/%s", "/home/git/", zRegRes_->p_rets[1], ".____DpSystem", zRepoId, (zRegRes_ + 1)->p_rets[0]);
    zPosixReg_.free_res(zRegRes_ + 1);

    /* 取出本项目所在路径的最大路径长度（用于度量 git 输出的差异文件相对路径长度） */
    zpGlobRepo_[zRepoId]->MaxPathLen = pathconf(zpGlobRepo_[zRepoId]->p_RepoPath, _PC_PATH_MAX);

    /* 调用SHELL执行检查和创建 */
    char zCommonBuf[zGlobCommonBufSiz + zpGlobRepo_[zRepoId]->RepoPathLen];
    sprintf(zCommonBuf, "sh ${zGitShadowPath}/tools/zmaster_init_repo.sh \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", zRegRes_->p_rets[0], zpGlobRepo_[zRepoId]->p_RepoPath + 9, zRegRes_->p_rets[2], zRegRes_->p_rets[3], zRegRes_->p_rets[4]);

    /* system 返回的是与 waitpid 中的 status 一样的值，需要用宏 WEXITSTATUS 提取真正的错误码 */
    zErrNo = WEXITSTATUS( system(zCommonBuf) );
    if (255 == zErrNo) {
        zFree_Source();
        return -36;
    } else if (254 == zErrNo) {
        zFree_Source();
        return -33;
    } else if (253 == zErrNo) {
        zFree_Source();
        return -38;
    }

    /* 打开日志文件 */
    char zPathBuf[zGlobCommonBufSiz];
    sprintf(zPathBuf, "%s%s", zpGlobRepo_[zRepoId]->p_RepoPath, zDpSigLogPath);
    zpGlobRepo_[zRepoId]->DpSigLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zpGlobRepo_[zRepoId]->p_RepoPath, zDpTimeSpentLogPath);
    zpGlobRepo_[zRepoId]->DpTimeSpentLogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    if ((-1 == zpGlobRepo_[zRepoId]->DpSigLogFd) || (-1 == zpGlobRepo_[zRepoId]->DpTimeSpentLogFd)) {
        close(zpGlobRepo_[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -39;
    }

    /* 检测并生成项目代码定期更新命令 */
    char zPullCmdBuf[zGlobCommonBufSiz];
    if (0 == strcmp("git", zRegRes_->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && rm -f .git/index.lock; git pull --force \"%s\" \"%s\":server%d",
                zpGlobRepo_[zRepoId]->p_RepoPath,
                zRegRes_->p_rets[2],
                zRegRes_->p_rets[3],
                zRepoId);
    } else if (0 == strcmp("svn", zRegRes_->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd %s && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; rm -f .git/index.lock; svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server%d",
                zpGlobRepo_[zRepoId]->p_RepoPath,
                zRepoId);
    } else {
        close(zpGlobRepo_[zRepoId]->DpSigLogFd);
        zFree_Source();
        return -37;
    }

    zMem_Alloc(zpGlobRepo_[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zpGlobRepo_[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 清理资源占用 */
    zPosixReg_.free_res(zRegRes_);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zpGlobRepo_[zRepoId]->p_MemPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Time();
        fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
        exit(1);
    }
    void **zppPrev = zpGlobRepo_[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    zpGlobRepo_[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->MemLock), NULL) );

    /* 布署重试锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->DpRetryLock), NULL) );

    /* libssh2 并发锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->DpSyncLock), NULL) );
    zCheck_Pthread_Func_Exit( pthread_cond_init(&(zpGlobRepo_[zRepoId]->DpSyncCond), NULL) );

    /* 为每个代码库生成一把读写锁 */
    zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zpGlobRepo_[zRepoId]->RwLock), NULL) );
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_init(&(zpGlobRepo_[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_setkind_np(&(zpGlobRepo_[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zpGlobRepo_[zRepoId]->RwLock), &(zpGlobRepo_[zRepoId]->zRWLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_destroy(&(zpGlobRepo_[zRepoId]->zRWLockAttr)));

    /* 读写锁生成之后，立刻拿写锁 */
    pthread_rwlock_wrlock(&(zpGlobRepo_[zRepoId]->RwLock));

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepo_[zRepoId]->ReplyCntLock, NULL));
    /* 用于保证 "git pull" 原子性拉取的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepo_[zRepoId]->PullLock, NULL));

    /* 布署并发流量控制 */
    zCheck_Negative_Exit( sem_init(&(zpGlobRepo_[zRepoId]->DpTraficControl), 0, zDpTraficLimit) );

    /* 缓存版本初始化 */
    zpGlobRepo_[zRepoId]->CacheId = 1000000000;
    /* 上一次布署结果状态初始化 */
    zpGlobRepo_[zRepoId]->RepoState = zRepoGood;

    /* 提取最近一次布署的SHA1 sig，日志文件不会为空，初创时即会以空库的提交记录作为第一条布署记录 */
    sprintf(zCommonBuf, "cat %s%s | tail -1", zpGlobRepo_[zRepoId]->p_RepoPath, zDpSigLogPath);
    FILE *zpShellRetHandler;
    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );
    if (zBytes(40) != zNativeUtils_.read_hunk(zpGlobRepo_[zRepoId]->zLastDpSig, zBytes(40), zpShellRetHandler)) {
        zpGlobRepo_[zRepoId]->zLastDpSig[40] = '\0';
    }
    pclose(zpShellRetHandler);

    /* 指针指向自身的静态数据项 */
    zpGlobRepo_[zRepoId]->CommitVecWrap_.p_Vec_ = zpGlobRepo_[zRepoId]->CommitVec_;
    zpGlobRepo_[zRepoId]->CommitVecWrap_.p_RefData_ = zpGlobRepo_[zRepoId]->CommitRefData_;
    zpGlobRepo_[zRepoId]->SortedCommitVecWrap_.p_Vec_ = zpGlobRepo_[zRepoId]->CommitVec_;  // 提交记录总是有序的，不需要再分配静态空间

    zpGlobRepo_[zRepoId]->DpVecWrap_.p_Vec_ = zpGlobRepo_[zRepoId]->DpVec_;
    zpGlobRepo_[zRepoId]->DpVecWrap_.p_RefData_ = zpGlobRepo_[zRepoId]->DpRefData_;
    zpGlobRepo_[zRepoId]->SortedDpVecWrap_.p_Vec_ = zpGlobRepo_[zRepoId]->SortedDpVec_;

    zpGlobRepo_[zRepoId]->p_DpCcur_ = zpGlobRepo_[zRepoId]->DpCcur_;

    /* 生成缓存 */
    zMeta__ zMeta_;
    zMeta_.RepoId = zRepoId;

    zMeta_.DataType = zIsCommitDataType;
    zgenerate_cache(&zMeta_);

    zMeta_.DataType = zIsDpDataType;
    zgenerate_cache(&zMeta_);

    /* 全局 libgit2 Handler 初始化 */
    zCheck_Null_Exit( zpGlobRepo_[zRepoId]->p_GitRepoHandler = zLibGit_.env_init(zpGlobRepo_[zRepoId]->p_RepoPath) );  // 目标库

    /* 放锁 */
    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->RwLock));

    /* 标记初始化动作已全部完成 */
    zpGlobRepo_[zRepoId]->zInitRepoFinMark = 1;

    /* 全局实际项目 ID 最大值调整 */
    pthread_mutex_lock(&zGlobCommonLock);
    zGlobMaxRepoId = zRepoId > zGlobMaxRepoId ? zRepoId : zGlobMaxRepoId;
    pthread_mutex_unlock(&zGlobCommonLock);

    return 0;
}
#undef zFree_Source


/* 用于线程并发执行的外壳函数 */
static void *
zinit_one_repo_env_thread_wraper(void *zpParam) {
    char *zpOrigStr = ((char *) zpParam) + sizeof(void *);
    _i zErrNo;
    if (0 > (zErrNo = zinit_one_repo_env(zpOrigStr))) {
        fprintf(stderr, "[zinit_one_repo_env] ErrNo: %d\n", zErrNo);
    }

    return NULL;
}


#ifndef _Z_BSD
/* 定时获取系统全局负载信息 */
static void *
zsys_load_monitor(void *zpParam) {
    _ul zTotalMem, zAvalMem;
    FILE *zpHandler;

    zCheck_Null_Exit( zpHandler = fopen("/proc/meminfo", "r") );

    while(1) {
        fscanf(zpHandler, "%*s %ld %*s %*s %*ld %*s %*s %ld", &zTotalMem, &zAvalMem);
        zGlobMemLoad = 100 * (zTotalMem - zAvalMem) / zTotalMem;
        fseek(zpHandler, 0, SEEK_SET);

        /*
         * 此处不拿锁，直接通知，否则锁竞争太甚
         * 由于是无限循环监控任务，允许存在无效的通知
         * 工作线程等待在 80% 的水平线上，此处降到 70% 才通知
         */
        if (70 > zGlobMemLoad) { pthread_cond_signal(&zSysLoadCond); }

        zNativeUtils_.sleep(0.1);
    }
    return zpParam;  // 消除编译警告信息
}
#endif


/*
 * json 解析回调：数字与字符串
 */
static void
zparse_digit(void *zpIn, void *zpOut) {
    *((_i *)zpOut) = strtol(zpIn, NULL, 10);
}
static void
zparse_str(void *zpIn, void *zpOut) {
    strcpy(zpOut, zpIn);  // 正则匹配出的结果，不会为 NULL，因此不必检查 zpIn
}


/* 读取项目信息，初始化配套环境 */
static void *
zinit_env(const char *zpConfPath) {
    FILE *zpFile = NULL;
    static char zConfBuf[zGlobRepoNumLimit][zGlobCommonBufSiz];  // 预置 128 个静态缓存区
    _i zCnter = 0;

    /* json 解析时的回调函数索引 */
    zNativeOps_.json_parser['O']  // OpsId
        = zNativeOps_.json_parser['P']  // ProjId
        = zNativeOps_.json_parser['R']  // RevId
        = zNativeOps_.json_parser['F']  // FileId
        = zNativeOps_.json_parser['H']  // HostId
        = zNativeOps_.json_parser['C']  // CacheId
        = zNativeOps_.json_parser['D']  // DataType
        = zparse_digit;

    zNativeOps_.json_parser['d']  // data
        = zNativeOps_.json_parser['E']  // ExtraData
        = zparse_str;

    zCheck_Null_Exit( zpFile = fopen(zpConfPath, "r") );
    while (zGlobRepoNumLimit > zCnter) {
        if (NULL == zNativeUtils_.read_line(zConfBuf[zCnter] + sizeof(void *), zGlobCommonBufSiz, zpFile)) {
            goto zMarkFin;
        } else {
            zThreadPool_.add(zinit_one_repo_env_thread_wraper, zConfBuf[zCnter++]);
        }
    }

    /* 若代码为数量超过可以管理的上限，报错退出 */
    zPrint_Err(0, NULL, "代码库数量超出上限，布署系统已退出");
    exit(1);

zMarkFin:
    fclose(zpFile);

#ifndef _Z_BSD
//    char zCpuNumBuf[8];
//    zpFile = NULL;
//    zCheck_Null_Exit( zpFile = popen("cat /proc/cpuinfo | grep -c 'processor[[:blank:]]\\+:'", "r") );
//    zCheck_Null_Exit( zNativeUtils_.read_line(zCpuNumBuf, 8, zpFile) );
//    zSysCpuNum = strtol(zCpuNumBuf, NULL, 10);
//    fclose(zpFile);

    zThreadPool_.add(zsys_load_monitor, NULL);
#endif

    return NULL;
}


/* 去除json标识符:  ][}{\",:  */
// static void
// zclear_json_identifier(char *zpStr, _i zStrLen) {
//     char zDb[256] = {0};
//     zDb['['] = 1;
//     zDb[']'] = 1;
//     zDb['{'] = 1;
//     zDb['}'] = 1;
//     zDb[','] = 1;
//     zDb[':'] = 1;
//     zDb['\"'] = 1;
//
//     for (_i zCnter = 0; zCnter < zStrLen; zCnter++) {
//         if (1 == zDb[(_i)zpStr[zCnter]]) {
//             zpStr[zCnter] = '=';
//         }
//     }
// }
