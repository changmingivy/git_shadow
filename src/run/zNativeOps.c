#include "zNativeOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <time.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <libpq-fe.h>

extern struct zPosixReg__ zPosixReg_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zThreadPool__ zThreadPool_;
extern struct zLibGit__ zLibGit_;
extern struct zDpOps__ zDpOps_;
extern struct zPgSQL__ zPgSQL_;


static void *
zalloc_cache(_i zRepoId, _ui zSiz);

static void *
zget_diff_content(void *zpParam);

static void *
zget_file_list(void *zpParam);

static void *
zgenerate_cache(void *zpParam);

static _i
zinit_one_repo_env(zPgResTuple__ *zpRepoMeta);

static void *
zsys_load_monitor(void *zpParam);

static void *
zinit_env(zPgLogin__ *zpPgLogin_);

void *
zextend_pg_partition(void *zp);

struct zNativeOps__ zNativeOps_ = {
    .get_revs = zgenerate_cache,
    .get_diff_files = zget_file_list,
    .get_diff_contents = zget_diff_content,

    .proj_init = zinit_one_repo_env,
    .proj_init_all = zinit_env,

    .json_parser = { NULL },
    .alloc = zalloc_cache,
    .sysload_monitor = zsys_load_monitor,

    .extend_pg_partition = zextend_pg_partition
};


/*************
 * GLOB VARS *
 *************/
_i zGlobMaxRepoId;
zRepo__ *zpGlobRepo_[zGlobRepoIdLimit];

/* 系统 CPU 与 MEM 负载监控：以 0-100 表示 */
pthread_mutex_t zGlobCommonLock;
pthread_cond_t zGlobCommonCond;  // 系统由高负载降至可用范围时，通知等待的线程继续其任务(注：使用全局通用锁与之配套)
_ul zGlobMemLoad;  // 高于 80 拒绝布署，同时 git push 的过程中，若高于 80 则剩余任阻塞等待

char zGlobPgConnInfo[2048];  // postgreSQL 全局统一连接方式：所有布署相关数据存放于一个数据库中

/* 专用于缓存的内存调度分配函数，适用多线程环境，不需要free */
static void *
zalloc_cache(_i zRepoId, _ui zSiz) {
    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->memLock));

    if ((zSiz + zpGlobRepo_[zRepoId]->memPoolOffSet) > zMemPoolSiz) {
        void **zppPrev, *zpCur;
        /* 新增一块内存区域加入内存池，以上一块内存的头部预留指针位存储新内存的地址 */
        if (MAP_FAILED == (zpCur = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
            zPrint_Time();
            fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
            exit(1);
        }
        zppPrev = zpCur;
        zppPrev[0] = zpGlobRepo_[zRepoId]->p_memPool;  // 首部指针位指向上一块内存池map区
        zpGlobRepo_[zRepoId]->p_memPool = zpCur;  // 更新当前内存池指针
        zpGlobRepo_[zRepoId]->memPoolOffSet = sizeof(void *);  // 初始化新内存池区域的 offset
    }

    void *zpX = zpGlobRepo_[zRepoId]->p_memPool + zpGlobRepo_[zRepoId]->memPoolOffSet;
    zpGlobRepo_[zRepoId]->memPoolOffSet += zSiz;

    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->memLock));
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

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zpGlobRepo_[zpMeta_->repoId]->repoPathLen + 40 + 40 + zpGlobRepo_[zpMeta_->repoId]->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf, "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,
            zpGlobRepo_[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),
            zGet_OneFilePath(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zCommonBuf, "r") );

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    zCnter = 0;
    if (0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBytes(1448), zpShellRetHandler))) {
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
        zpTmpBaseData_[0]->dataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
            zpTmpBaseData_[0]->dataLen = zBaseDataLen;
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
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId) = NULL;
    } else {
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId) = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz = -7;  // 先赋为 -7
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_refData_ = NULL;
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_vec_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
            zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_vec_[i].iov_base = zpTmpBaseData_[2]->p_data;
            zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_vec_[i].iov_len = zpTmpBaseData_[2]->dataLen;
        }

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz = zCnter;
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
    zpNode_->pp_resHash[zpNode_->lineNum] = zpNode_;\
    ____zOffSet = 6 * zpNode_->offSet + 10;\
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
    for (_i i = 0; i < zpNode_->offSet; i++) {\
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
    zMeta__ **zppKeepPtr = zpNode_->pp_resHash;

    do {
        /* 分发直连的子节点 */
        if (NULL != zpNode_->p_firstChild) {
            zpNode_->p_firstChild->pp_resHash = zppKeepPtr;
            zdistribute_task(zpNode_->p_firstChild);  // 暂时以递归处理，线程模型会有收集不齐全部任务的问题
        }

        /* 自身及所有的左兄弟 */
        zGenerate_Graph(zpNode_);
        zpNode_ = zpNode_->p_left;
    } while ((NULL != zpNode_) && (zpNode_->pp_resHash = zppKeepPtr));

    return NULL;
}

#define zGenerate_Tree_Node() do {\
    zpTmpNode_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zMeta__));\
\
    zpTmpNode_[0]->lineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNode_[0]->offSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNode_[0]->p_firstChild = NULL;\
    zpTmpNode_[0]->p_left = NULL;\
    zpTmpNode_[0]->p_data = zalloc_cache(zpMeta_->repoId, 6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_->resLen[zNodeCnter]);\
    strcpy(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_->p_rets[zNodeCnter]);\
\
    zpTmpNode_[0]->opsId = 0;\
    zpTmpNode_[0]->repoId = zpMeta_->repoId;\
    zpTmpNode_[0]->commitId = zpMeta_->commitId;\
    zpTmpNode_[0]->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;\
    zpTmpNode_[0]->dataType = zpMeta_->dataType;\
\
    if (zNodeCnter == (zRegRes_->cnt - 1)) {\
        zpTmpNode_[0]->fileId = zpTmpNode_[0]->lineNum;\
        zpTmpNode_[0]->p_extraData = zalloc_cache(zpMeta_->repoId, zBaseDataLen);\
        memcpy(zpTmpNode_[0]->p_extraData, zCommonBuf, zBaseDataLen);\
    } else {\
        zpTmpNode_[0]->fileId = -1;\
        zpTmpNode_[0]->p_extraData = NULL;\
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
            zpTmpNode_[1]->p_firstChild = zpTmpNode_[0];\
        } else {\
            zpTmpNode_[2]->p_left = zpTmpNode_[0];\
        }\
    }\
\
    zNodeCnter++;\
    for (; zNodeCnter < zRegRes_->cnt; zNodeCnter++) {\
        zpTmpNode_[0]->p_firstChild = zalloc_cache(zpMeta_->repoId, sizeof(zMeta__));\
        zpTmpNode_[1] = zpTmpNode_[0];\
\
        zpTmpNode_[0] = zpTmpNode_[0]->p_firstChild;\
\
        zpTmpNode_[0]->p_father = zpTmpNode_[1];\
        zpTmpNode_[0]->p_firstChild = NULL;\
        zpTmpNode_[0]->p_left = NULL;\
\
        zpTmpNode_[0]->lineNum = zLineCnter;  /* 横向偏移 */\
        zLineCnter++;  /* 每个节点会占用一行显示输出 */\
        zpTmpNode_[0]->offSet = zNodeCnter;  /* 纵向偏移 */\
\
        zpTmpNode_[0]->p_data = zalloc_cache(zpMeta_->repoId, 6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_->resLen[zNodeCnter]);\
        strcpy(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_->p_rets[zNodeCnter]);\
\
        zpTmpNode_[0]->opsId = 0;\
        zpTmpNode_[0]->repoId = zpMeta_->repoId;\
        zpTmpNode_[0]->commitId = zpMeta_->commitId;\
        zpTmpNode_[0]->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;\
        zpTmpNode_[0]->dataType = zpMeta_->dataType;\
\
        zpTmpNode_[0]->fileId = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNode_[0]->p_extraData = NULL;\
    }\
    zpTmpNode_[0]->fileId = zpTmpNode_[0]->lineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNode_[0]->p_extraData = zalloc_cache(zpMeta_->repoId, zBaseDataLen);\
    memcpy(zpTmpNode_[0]->p_extraData, zCommonBuf, zBaseDataLen);\
} while(0)

/* 差异文件数量 >128 时，调用此函数，以防生成树图损耗太多性能；此时无需检查无差的性况 */
static void
zget_file_list_large(zMeta__ *zpMeta_, zVecWrap__ *zpTopVecWrap_, FILE *zpShellRetHandler, char *zpCommonBuf, _i zMaxBufLen) {
    zMeta__ zSubMeta_;
    zBaseData__ *zpTmpBaseData_[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    for (zCnter = 0; NULL != zNativeUtils_.read_line(zpCommonBuf, zMaxBufLen, zpShellRetHandler); zCnter++) {
        zBaseDataLen = strlen(zpCommonBuf);
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0]; }
        zpTmpBaseData_[0]->dataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zpCommonBuf, zBaseDataLen);
        zpTmpBaseData_[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
        zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[0] = zpTmpBaseData_[0]->p_next;
    }
    pclose(zpShellRetHandler);

    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId) = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz = zCnter;
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(zRefData__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(struct iovec));

    for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_[i].p_data = zpTmpBaseData_[2]->p_data;

        /* 用于转换成JsonStr */
        zSubMeta_.opsId = 0;
        zSubMeta_.repoId = zpMeta_->repoId;
        zSubMeta_.commitId = zpMeta_->commitId;
        zSubMeta_.fileId = i;
        zSubMeta_.cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;
        zSubMeta_.dataType = zpMeta_->dataType;
        zSubMeta_.p_data = zpTmpBaseData_[2]->p_data;
        zSubMeta_.p_extraData = NULL;

        /* 将zMeta__转换为JSON文本 */
        zDpOps_.struct_to_json(zpCommonBuf, &zSubMeta_);

        zVecDataLen = strlen(zpCommonBuf);
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[i].iov_len = zVecDataLen;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[i].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[i].iov_base, zpCommonBuf, zVecDataLen);

        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_[i].p_subVecWrap_ = NULL;
    }

    /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
    ((char *)(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[0].iov_base))[0] = '[';
}


static void *
zget_file_list(void *zpParam) {
    zMeta__ *zpMeta_ = (zMeta__ *)zpParam;
    zVecWrap__ *zpTopVecWrap_;
    FILE *zpShellRetHandler;

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpGlobRepo_[zpMeta_->repoId]->repoPathLen + 4 * 40 + zpGlobRepo_[zpMeta_->repoId]->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */

    sprintf(zCommonBuf, "cd \"%s\" && git diff --shortstat \"%s\" \"%s\" | grep -oP '\\d+(?=\\s*file)' && git diff --name-only \"%s\" \"%s\"",
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,
            zpGlobRepo_[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),
            zpGlobRepo_[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId));

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
    zRegRes__ zRegRes_[1] = {{.alloc_fn = zalloc_cache, .repoId = zpMeta_->repoId}};  // 使用项目内存池

    /* 在生成树节点之前分配空间，以使其不为 NULL，防止多个查询文件列的的请求导致重复生成同一缓存 */
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId) = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz = -7;  // 先赋为 -7，知会同类请求缓存正在生成过程中

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
                    if (0 == strcmp(zpTmpNode_[0]->p_data + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_->p_rets[zNodeCnter])) {
                        zpTmpNode_[1] = zpTmpNode_[0];
                        zpTmpNode_[0] = zpTmpNode_[0]->p_firstChild;
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
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_ = NULL;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_ = zalloc_cache(zpMeta_->repoId, sizeof(struct iovec));

        zSubMeta_.opsId = 0;
        zSubMeta_.repoId = zpMeta_->repoId;
        zSubMeta_.commitId = zpMeta_->commitId;
        zSubMeta_.fileId = -1;  // 置为 -1，不允许再查询下一级内容
        zSubMeta_.cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;
        zSubMeta_.dataType = zpMeta_->dataType;
        zSubMeta_.p_data = (0 == strcmp(zpGlobRepo_[zpMeta_->repoId]->lastDpSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId))) ? "===> 最新的已布署版本 <===" : "=> 无差异 <=";
        zSubMeta_.p_extraData = NULL;

        /* 将zMeta__转换为JSON文本 */
        zDpOps_.struct_to_json(zCommonBuf, &zSubMeta_);
        zCommonBuf[0] = '[';  // 逗号替换为 '['

        zVecDataLen = strlen(zCommonBuf);
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[0].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[0].iov_base, zCommonBuf, zVecDataLen);

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz = 1;
    } else {
        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNode_->pp_resHash = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(zMeta__ *));

        /* Tree 图 */
        zdistribute_task(zpRootNode_);

        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_
            = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(zRefData__));
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_
            = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(struct iovec));

        for (_ui zCnter = 0; zCnter < zLineCnter; zCnter++) {
            zDpOps_.struct_to_json(zCommonBuf, zpRootNode_->pp_resHash[zCnter]); /* 将 zMeta__ 转换为 json 文本 */

            zVecDataLen = strlen(zCommonBuf);
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[zCnter].iov_len = zVecDataLen;
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[zCnter].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[zCnter].iov_base, zCommonBuf, zVecDataLen);

            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_[zCnter].p_data = zpRootNode_->pp_resHash[zCnter]->p_extraData;
            zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_refData_[zCnter].p_subVecWrap_ = NULL;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_[0].iov_base))[0] = '[';

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz = zLineCnter;
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
    zDpLog__ zDpLog_[zCacheSiz] = {{ .p_revSig = NULL }};
    zVecWrap__ *zpTopVecWrap_ = NULL,
               *zpSortedTopVecWrap_ = NULL;
    zMeta__ *zpMeta_ = (zMeta__ *)zpParam,
            zSubMeta_;
    _i zCnter = 0,
       zVecDataLen = 0;
    time_t zTimeStamp = 0;

    /* 计算本函数需要用到的最大 BufSiz */
    char zCommonBuf[256 + zpGlobRepo_[zpMeta_->repoId]->repoPathLen + 12];

    if (zIsCommitDataType == zpMeta_->dataType) {
        zGitRevWalk__ *zpRevWalker = NULL;

        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->sortedCommitVecWrap_);

        sprintf(zCommonBuf, "refs/heads/server%d", zpMeta_->repoId);
        if (NULL == (zpRevWalker = zLibGit_.generate_revwalker(zpGlobRepo_[zpMeta_->repoId]->p_gitRepoHandler, zCommonBuf, 0))) {
            zPrint_Err(0, NULL, "\n!!! git repo ERROR !!!\n");
            exit(1);  // 出现严重错误，退出程序
        } else {
            for (zCnter = 0; zCnter < zCacheSiz && 0 != strcmp(zpGlobRepo_[zpMeta_->repoId]->lastDpSig, zDpLog_[zCnter].p_revSig); zCnter++) {
                zDpLog_[zCnter].p_revSig = zalloc_cache(zpMeta_->repoId, zBytes(64));
                zDpLog_[zCnter].p_timeStamp = zDpLog_[zCnter].p_revSig + 44;
                if (0 < (zTimeStamp = zLibGit_.get_one_commitsig_and_timestamp(zDpLog_[zCnter].p_revSig, zpGlobRepo_[zpMeta_->repoId]->p_gitRepoHandler, zpRevWalker))) {
                    snprintf(zDpLog_[zCnter].p_timeStamp, 20, "%ld", zTimeStamp);
                } else {
                    zDpLog_[zCnter].p_revSig = NULL;
                    break;
                }
            }

            zLibGit_.destroy_revwalker(zpRevWalker);
        }

        /* 存储的是实际的对象数量 */
        zpSortedTopVecWrap_->vecSiz = zpTopVecWrap_->vecSiz = zCnter;

    } else if (zIsDpDataType == zpMeta_->dataType) {
        zPgResHd__ *zpPgResHd_ = NULL;
        zPgRes__ *zpPgRes_ = NULL;

        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->sortedDpVecWrap_);

        /* 须使用 DISTINCT 关键字去重 */
        sprintf(zCommonBuf, "SELECT DISTINCT rev_sig, time_stamp FROM dp_log DESC ORDER BY time_stamp LIMIT %d WHERE proj_id == %d",
                zCacheSiz,
                zpMeta_->repoId);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zCommonBuf, true))) {
            zPgSQL_.conn_reset(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);

            if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zCommonBuf, true))) {
                zPgSQL_.res_clear(zpPgResHd_, NULL);
                zPgSQL_.conn_clear(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
                zPrint_Err(0, NULL, "!!! FATAL !!!");
                exit(1);
            }
        }

        /* 存储的是实际的对象数量 */
        zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_);
        zpTopVecWrap_->vecSiz = zpPgRes_->tupleCnt;
        zpSortedTopVecWrap_->vecSiz
            = zpTopVecWrap_->vecSiz
            = (zCacheSiz < zpTopVecWrap_->vecSiz) ? zCacheSiz : zpTopVecWrap_->vecSiz;

        for (zCnter = 0; zCnter < zpTopVecWrap_->vecSiz; zCnter++) {
            zDpLog_[zCnter].p_revSig = zalloc_cache(zpMeta_->repoId, zBytes(64));
            zDpLog_[zCnter].p_timeStamp = zDpLog_[zCnter].p_revSig + 44;

            strncpy(zDpLog_[zCnter].p_revSig, zpPgRes_->tupleRes_->pp_fields[0], 40);
            zDpLog_[zCnter].p_revSig[40] = '\0';

            strncpy(zDpLog_[zCnter].p_timeStamp, zpPgRes_->tupleRes_->pp_fields[1], 20);
            zDpLog_[zCnter].p_timeStamp[20] = '\0';  // 确保至少存在一个 '\0'
        }

        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    if (NULL != zDpLog_[0].p_revSig) {
        for (zCnter = 0; zCnter < zCacheSiz && NULL != zDpLog_[zCnter].p_revSig; zCnter++) {
            /* 用于转换成JsonStr */
            zSubMeta_.opsId = 0;
            zSubMeta_.repoId = zpMeta_->repoId;
            zSubMeta_.commitId = zCnter;
            zSubMeta_.fileId = -1;
            zSubMeta_.cacheId =  zpGlobRepo_[zpMeta_->repoId]->cacheId;
            zSubMeta_.dataType = zpMeta_->dataType;
            zSubMeta_.p_data = zDpLog_[zCnter].p_revSig;
            zSubMeta_.p_extraData = zDpLog_[zCnter].p_timeStamp;

            /* 将zMeta__转换为JSON文本 */
            zDpOps_.struct_to_json(zCommonBuf, &zSubMeta_);

            zVecDataLen = strlen(zCommonBuf);
            zpTopVecWrap_->p_vec_[zCnter].iov_len = zVecDataLen;
            zpTopVecWrap_->p_vec_[zCnter].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
            memcpy(zpTopVecWrap_->p_vec_[zCnter].iov_base, zCommonBuf, zVecDataLen);

            zpTopVecWrap_->p_refData_[zCnter].p_data = zDpLog_[zCnter].p_revSig;
            zpTopVecWrap_->p_refData_[zCnter].p_subVecWrap_ = NULL;
        }

        // !!! 布署记录已不需要额外排序：SQL 返回的结果的序列已经与提交列表相同
        // if (zIsDpDataType == zpMeta_->dataType) {
        //     /* 存储最近一次布署的 SHA1 sig，执行布署时首先对比布署目标与最近一次布署，若相同，则直接返回成功 */
        //     strcpy(zpGlobRepo_[zpMeta_->repoId]->lastDpSig, zpTopVecWrap_->p_refData_[zCnter - 1].p_data);
        //     /* 将布署记录按逆向时间排序（新记录显示在前面） */
        //     for (_i i = 0; i < zpTopVecWrap_->vecSiz; i++) {
        //         zCnter--;
        //         zpSortedTopVecWrap_->p_vec_[zCnter].iov_base = zpTopVecWrap_->p_vec_[i].iov_base;
        //         zpSortedTopVecWrap_->p_vec_[zCnter].iov_len = zpTopVecWrap_->p_vec_[i].iov_len;
        //     }
        // } else {
        //     /* 提交记录缓存本来就是有序的，不需要额外排序 */
        //     zpSortedTopVecWrap_->p_vec_ = zpTopVecWrap_->p_vec_;
        // }

        /* 提交记录与布署记录缓存均是有序的，不需要额外排序 */
        zpSortedTopVecWrap_->p_vec_ = zpTopVecWrap_->p_vec_;

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zpSortedTopVecWrap_->p_vec_[0].iov_base))[0] = '[';
    }

    /* 防止意外访问导致的程序崩溃 */
    memset(zpTopVecWrap_->p_refData_ + zpTopVecWrap_->vecSiz, 0, sizeof(zRefData__) * (zCacheSiz - zpTopVecWrap_->vecSiz));

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
    free(zpGlobRepo_[zRepoId]->p_repoPath);\
    free(zpGlobRepo_[zRepoId]);\
    zpGlobRepo_[zRepoId] = NULL;\
} while(0)

/*
 * pp_field: [0 repoId] [1 pathOnHost] [2 sourceUrl] [3 sourceBranch] [4 sourceVcsType] [5 needPull]
 */
static _i
zinit_one_repo_env(zPgResTuple__ *zpRepoMeta_) {
    zRegInit__ *zpRegInit_ = NULL;
    zRegRes__ *zpRegRes_ = NULL;
    _i zRepoId = 0,
       zErrNo = 0,
       zStrLen = 0;
    zPgResHd__ *zpPgResHd_ = NULL;

    /* 提取项目ID，调整 zGlobMaxRepoId */
    zRepoId = strtol(zpRepoMeta_->pp_fields[0], NULL, 10);

    if (zGlobRepoIdLimit <= zRepoId || 0 >= zRepoId) { return -32; }
    if (NULL != zpGlobRepo_[zRepoId]) { return -35; }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zpGlobRepo_[zRepoId], zRepo__, 1);
    zpGlobRepo_[zRepoId]->repoId = zRepoId;
    zpGlobRepo_[zRepoId]->selfPushMark = (NULL == zpRepoMeta_->pp_fields[5] || 'N' == zpRepoMeta_->pp_fields[5][0]) ? 0 : 1;

    /* 提取项目绝对路径，结果格式：/home/git/`dirname($Path_On_Host)`/.____DpSystem/`basename($Path_On_Host)` */
    zPosixReg_.compile(zpRegInit_, "[^/]+[/]*$");
    zPosixReg_.match(zpRegRes_, zpRegInit_, zpRepoMeta_->pp_fields[1]);
    zPosixReg_.free_meta(zpRegInit_);

    if (0 == zpRegRes_->cnt) { /* Handle ERROR ? */ }

    /* 去掉 basename 部分，之后拼接出最终的字符串 */
    zStrLen = strlen(zpRepoMeta_->pp_fields[1]);
    zpRepoMeta_->pp_fields[1][zStrLen - zpRegRes_->resLen[0]] = '\0';
    while ('/' == zpRepoMeta_->pp_fields[1][0]) {
        zpRepoMeta_->pp_fields[1]++;  /* 去除多余的 '/' */
    }
    zMem_Alloc(zpGlobRepo_[zRepoId]->p_repoPath, char, 32 + sizeof("/home/git/.____DpSystem/") + zStrLen);
    zpGlobRepo_[zRepoId]->repoPathLen = sprintf(zpGlobRepo_[zRepoId]->p_repoPath,
            "%s%s%s/%d/%s",
            "/home/git/",
            zpRepoMeta_->pp_fields[1],
            ".____DpSystem",
            zRepoId,
            zpRegRes_->p_rets[0]);
    zPosixReg_.free_res(zpRegRes_);

    /* 取出本项目所在路径的最大路径长度（用于度量 git 输出的差异文件相对路径长度） */
    zpGlobRepo_[zRepoId]->maxPathLen = pathconf(zpGlobRepo_[zRepoId]->p_repoPath, _PC_PATH_MAX);

    /* 调用外部 SHELL 执行检查和创建，便于维护 */
    char zCommonBuf[zGlobCommonBufSiz + zpGlobRepo_[zRepoId]->repoPathLen];
    sprintf(zCommonBuf, "sh ${zGitShadowPath}/serv_tools/zmaster_init_repo.sh \"%d\" \"%s\" \"%s\" \"%s\" \"%s\"",
            zpGlobRepo_[zRepoId]->repoId,
            zpGlobRepo_[zRepoId]->p_repoPath + 9,
            zpRepoMeta_->pp_fields[2],
            zpRepoMeta_->pp_fields[3],
            zpRepoMeta_->pp_fields[4]);

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

    /* 检测并生成项目代码定期更新命令 */
    if (0 == strcmp("git", zpRepoMeta_->pp_fields[4])) {
        zStrLen = sprintf(zCommonBuf, "cd %s && rm -f .git/index.lock; git pull --force \"%s\" \"%s\":server%d",
                zpGlobRepo_[zRepoId]->p_repoPath,
                zpRepoMeta_->pp_fields[2],
                zpRepoMeta_->pp_fields[3],
                zRepoId);
    } else if (0 == strcmp("svn", zpRepoMeta_->pp_fields[4])) {
        zStrLen = sprintf(zCommonBuf, "cd %s && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; rm -f .git/index.lock;"
                " svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server%d",
                zpGlobRepo_[zRepoId]->p_repoPath,
                zRepoId);
    } else {
        zFree_Source();
        return -37;
    }

    zMem_Alloc(zpGlobRepo_[zRepoId]->p_pullCmd, char, 1 + zStrLen);
    strcpy(zpGlobRepo_[zRepoId]->p_pullCmd, zCommonBuf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zpGlobRepo_[zRepoId]->p_memPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Time();
        fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
        exit(1);
    }
    void **zppPrev = zpGlobRepo_[zRepoId]->p_memPool;
    zppPrev[0] = NULL;
    zpGlobRepo_[zRepoId]->memPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->memLock), NULL) );

    /* 布署重试锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->dpRetryLock), NULL) );

    /* libssh2 并发锁 */
    zCheck_Pthread_Func_Exit( pthread_mutex_init(&(zpGlobRepo_[zRepoId]->dpSyncLock), NULL) );
    zCheck_Pthread_Func_Exit( pthread_cond_init(&(zpGlobRepo_[zRepoId]->dpSyncCond), NULL) );

    /* 为每个代码库生成一把读写锁 */
    zCheck_Pthread_Func_Exit( pthread_rwlock_init(&(zpGlobRepo_[zRepoId]->rwLock), NULL) );
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_init(&(zpGlobRepo_[zRepoId]->rwLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_setkind_np(&(zpGlobRepo_[zRepoId]->rwLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zpGlobRepo_[zRepoId]->rwLock), &(zpGlobRepo_[zRepoId]->rwLockAttr)));
    // zCheck_Pthread_Func_Exit(pthread_rwlockattr_destroy(&(zpGlobRepo_[zRepoId]->rwLockAttr)));

    /* 读写锁生成之后，立刻拿写锁 */
    pthread_rwlock_wrlock(&(zpGlobRepo_[zRepoId]->rwLock));

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepo_[zRepoId]->replyCntLock, NULL));

    /* 用于保证 "git pull" 原子性拉取的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zpGlobRepo_[zRepoId]->pullLock, NULL));

    /* 布署并发流量控制 */
    zCheck_Negative_Exit( sem_init(&(zpGlobRepo_[zRepoId]->dpTraficControl), 0, zDpTraficLimit) );

    /* 缓存版本初始化 */
    zpGlobRepo_[zRepoId]->cacheId = time(NULL);

    /* 全局 libgit2 Handler 初始化 */
    zCheck_Null_Exit( zpGlobRepo_[zRepoId]->p_gitRepoHandler = zLibGit_.env_init(zpGlobRepo_[zRepoId]->p_repoPath) );  // 目标库

    /* 本项目全局 pgSQL 连接 Handler */
    if (NULL == (zpGlobRepo_[zRepoId]->p_pgConnHd_ = zPgSQL_.conn(zGlobPgConnInfo))) {
        zPrint_Err(0, NULL, "Connect to pgSQL failed");
        exit(1);
    }

    /* 每次启动时尝试创建必要的表，按天分区（1天 == 86400秒） */
    _i zBaseId = time(NULL) / 86400 + 2;  /* +2 的意义: 防止恰好在临界时间添加记录导致异常*/
    sprintf(zCommonBuf, "CREATE TABLE IF NOT EXISTS dp_log_%d PARTITION OF dp_log FOR VALUES IN (%d) PARTITION BY RANGE (time_stamp);"
        /* 若第一条 SQL 执行失败(说明是服务器重启，而非是新建项目)，不会执行第二条，故此处可能发生的错误不会带入之后的错误检查逻辑中 */
        "CREATE TABLE IF NOT EXISTS dp_log_%d_%d PARTITION OF dp_log_%d FOR VALUES FROM (MINVALUE) TO (%d);",
        zRepoId, zRepoId,
        zRepoId, zBaseId, zRepoId, 86400 * zBaseId);

    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zRepoId]->p_pgConnHd_, zCommonBuf, false))) {
        zPgSQL_.res_clear(zpPgResHd_, NULL);
        zPrint_Err(0, NULL, "(errno: -91) pgSQL exec failed");
        exit(1);
    } else {
        zPgSQL_.res_clear(zpPgResHd_, NULL);
    }

    for (_i zId = 0; zId < 10; zId++) {
        sprintf(zCommonBuf, "CREATE TABLE IF NOT EXISTS dp_log_%d_%d PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                zRepoId, zBaseId + zId + 1, zRepoId, 86400 * (zBaseId + zId), 86400 * (zBaseId + zId + 1));

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zRepoId]->p_pgConnHd_, zCommonBuf, false))) {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zPrint_Err(0, NULL, "(errno: -91) pgSQL exec failed");
            exit(1);
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }
    }

    /* 获取最近一次布署的相关信息，只取一条，不需要使用 DISTINCT 关键字去重 */
    sprintf(zCommonBuf, "SELECT rev_sig, glob_res FROM dp_log ORDER BY time_stamp DESC LIMIT 1 WHERE proj_id == %d", zRepoId);
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zRepoId]->p_pgConnHd_, zCommonBuf, true))) {
        zPgSQL_.conn_clear(zpGlobRepo_[zRepoId]->p_pgConnHd_);
        zPrint_Err(0, NULL, "pgSQL exec failed");
        exit(1);
    } else {
        zPgRes__ *zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_);
        if (0 == zpPgRes_->tupleCnt) {  /* empty repo... */
            sprintf(zCommonBuf, "refs/heads/____base.XXXXXXXX");
            zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(zpGlobRepo_[zRepoId]->p_gitRepoHandler, zCommonBuf, 0);
            if (NULL != zpRevWalker && 0 < zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf, zpGlobRepo_[zRepoId]->p_gitRepoHandler, zpRevWalker)) {
                strncpy(zpGlobRepo_[zRepoId]->lastDpSig, zCommonBuf, 40);  /* 提取最近一次布署的SHA1 sig */
                zpGlobRepo_[zRepoId]->lastDpSig[40] = '\0';
                zpGlobRepo_[zRepoId]->repoState = zRepoGood;  /* 上一次布署结果 */

                zLibGit_.destroy_revwalker(zpRevWalker);
            } else {
                zPrint_Err(0, NULL, "read revSig from branch '____base.XXXXXXXX' failed");
                exit(1);
            }
        } else if (1 == zpPgRes_->tupleCnt && 2 == zpPgRes_->fieldCnt) {
            strncpy(zpGlobRepo_[zRepoId]->lastDpSig, zpPgRes_->tupleRes_[1].pp_fields[zpPgRes_->fieldCnt], 40);  /* 提取最近一次布署的SHA1 sig */
            zpGlobRepo_[zRepoId]->lastDpSig[40] = '\0';

            /* 上一次布署结果 */
            if ('F' == zpPgRes_->tupleRes_[1].pp_fields[1 + zpPgRes_->fieldCnt][0]) {
                zpGlobRepo_[zRepoId]->repoState = zRepoDamaged;
            } else {
                zpGlobRepo_[zRepoId]->repoState = zRepoGood;
            }

            zPgSQL_.res_clear(zpPgResHd_, NULL);
        } else {
            sprintf(zCommonBuf, "pgSQL data err, ProjId %d", zRepoId);
            zPrint_Err(0, NULL, zCommonBuf);
            exit(1);
        }
    }

    /* 指针指向自身的静态数据项 */
    zpGlobRepo_[zRepoId]->commitVecWrap_.p_vec_ = zpGlobRepo_[zRepoId]->commitVec_;
    zpGlobRepo_[zRepoId]->commitVecWrap_.p_refData_ = zpGlobRepo_[zRepoId]->commitRefData_;
    zpGlobRepo_[zRepoId]->sortedCommitVecWrap_.p_vec_ = zpGlobRepo_[zRepoId]->commitVec_;  // 提交记录总是有序的，不需要再分配静态空间

    zpGlobRepo_[zRepoId]->dpVecWrap_.p_vec_ = zpGlobRepo_[zRepoId]->dpVec_;
    zpGlobRepo_[zRepoId]->dpVecWrap_.p_refData_ = zpGlobRepo_[zRepoId]->dpRefData_;
    zpGlobRepo_[zRepoId]->sortedDpVecWrap_.p_vec_ = zpGlobRepo_[zRepoId]->sortedDpVec_;

    zpGlobRepo_[zRepoId]->p_dpCcur_ = zpGlobRepo_[zRepoId]->dpCcur_;

    /* 生成缓存 */
    zMeta__ zMeta_;
    zMeta_.repoId = zRepoId;

    zMeta_.dataType = zIsCommitDataType;
    zgenerate_cache(&zMeta_);

    zMeta_.dataType = zIsDpDataType;
    zgenerate_cache(&zMeta_);

    /* 释放锁 */
    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));

    /* 标记初始化动作已全部完成 */
    zpGlobRepo_[zRepoId]->initRepoFinMark = 1;

    /* 全局实际项目 ID 最大值调整，并通知上层调用者本项目初始化任务完成 */
    pthread_mutex_lock(&zGlobCommonLock);

    zGlobMaxRepoId = zRepoId > zGlobMaxRepoId ? zRepoId : zGlobMaxRepoId;

    if (NULL == zpRepoMeta_->p_taskCnt) {
        pthread_mutex_unlock(&zGlobCommonLock);
    } else {
        (* (zpRepoMeta_->p_taskCnt)) ++;
        pthread_mutex_unlock(&zGlobCommonLock);
        pthread_cond_signal(&zGlobCommonCond);
    }

    return 0;
}
#undef zFree_Source


/* 用于线程并发执行的外壳函数 */
static void *
zinit_one_repo_env_thread_wraper(void *zpParam) {
    _i zErrNo = 0;

    if (0 > (zErrNo = zinit_one_repo_env((zPgResTuple__ *) zpParam))) {
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
        if (70 > zGlobMemLoad) { pthread_cond_signal(&zGlobCommonCond); }

        zNativeUtils_.sleep(0.1);
    }
    return zpParam;  // 消除编译警告信息
}
#endif


/*
 * 定时扩展日志表分区
 * 每天尝试创建之后 10 天的分区表
 * 以 UNIX 时间戳 / 86400 秒的结果进行数据分区，表示从 1970-01-01 00:00:00 开始的整天数，每天 0 点整作为临界
 */
void *
zextend_pg_partition(void *zp __attribute__ ((__unused__))) {
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    char zCmdBuf[1024];

    while (1) {
        /* 每天（24 * 60 * 60 秒）尝试创建新日志表 */
        sleep(86400);

        /* 尝试连接到 pgSQL server */
        while (NULL == (zpPgConnHd_ = zPgSQL_.conn(zGlobPgConnInfo))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            zPrint_Err(0, NULL, "Connect to pgSQL failed");
            sleep(60);
        }

        /* 非紧要任务，串行执行即可 */
        _i zBaseId = time(NULL) / 86400,
           zId = 0;
        for (_i zRepoId = 0; zRepoId <= zGlobMaxRepoId; zRepoId++) {
            if (NULL == zpGlobRepo_[zRepoId] || 0 == zpGlobRepo_[zRepoId]->initRepoFinMark) { continue; }

            for (zId = 0; zId < 10; zId ++) {
                sprintf(zCmdBuf, "CREATE TABLE IF NOT EXISTS dp_log_%d_%d PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                        zRepoId, zBaseId + zId + 1, zRepoId, 86400 * (zBaseId + zId), 86400 * (zBaseId + zId + 1));

                if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zRepoId]->p_pgConnHd_, zCmdBuf, false))) {
                    zPgSQL_.res_clear(zpPgResHd_, NULL);
                    zPrint_Err(0, NULL, "(errno: -91) pgSQL exec failed");
                    continue;
                } else {
                    zPgSQL_.res_clear(zpPgResHd_, NULL);
                }
            }
        }

        zPgSQL_.conn_clear(zpPgConnHd_);
    }

    return NULL;
}


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
zinit_env(zPgLogin__ *zpPgLogin_) {
    char zDBPassFilePath[1024];
    char *zpHomePath = NULL;
    struct stat zStat_;

    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

    /* 确保 pgSQL 密钥文件存在并合法 */
    if (NULL == zpPgLogin_->p_passFilePath) {
        if (NULL == (zpHomePath = getenv("HOME"))) {
            zpHomePath = "/home/git";
        }

        snprintf(zDBPassFilePath, 1024, "%s/.pgpass", zpHomePath);
        zpPgLogin_->p_passFilePath = zDBPassFilePath;
    }

    zCheck_NotZero_Exit( stat(zpPgLogin_->p_passFilePath, &zStat_) );
    if (!S_ISREG(zStat_.st_mode)) {
        zPrint_Err(0, NULL, "postgreSQL: passfile is not a regular file!");
        exit(1);
    }
    zCheck_NotZero_Exit( chmod(zpPgLogin_->p_passFilePath, 00600) );

    /* 生成连接 pgSQL 的元信息 */
    snprintf(zGlobPgConnInfo, 2048,
            "%s%s"
            "%s%s"
            "%s%s"
            "%s%s"
            "%s%s"
            "%s%s"
            "sslmode=allow"
            "connect_timeout=6",
            NULL == zpPgLogin_->p_addr ? "host=" : "",
            NULL == zpPgLogin_->p_addr ? (NULL == zpPgLogin_->p_host ? "localhost" : zpPgLogin_->p_host) : "",
            NULL == zpPgLogin_->p_addr ? "" : "hostaddr=",
            NULL == zpPgLogin_->p_addr ? "" : zpPgLogin_->p_addr,
            "port=",
            NULL == zpPgLogin_->p_port ? "5432" : zpPgLogin_->p_port,
            "user=",
            NULL == zpPgLogin_->p_userName ? "git" : zpPgLogin_->p_userName,
            "passfile=",
            zpPgLogin_->p_passFilePath,
            "dbname=",
            NULL == zpPgLogin_->p_dbName ? "dpDB": zpPgLogin_->p_dbName
            );

    /* 尝试连接到 pgSQL server */
    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zGlobPgConnInfo))) {
        zPrint_Err(0, NULL, "Connect to pgSQL failed");
        exit(1);
    }

    /* 启动时尝试创建表，并查询已有项目信息 */
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_,
                    "CREATE TABLE IF NOT EXISTS proj_meta "
                    "("
                    "proj_id         int NOT NULL PRIMARY KEY,"
                    "path_on_host    string NOT NULL,"
                    "source_url      string NOT NULL,"
                    "source_branch   string NOT NULL,"
                    "source_vcs_type string NOT NULL,"
                    "need_pull       bool NOT NULL"
                    ");"

                    "CREATE TABLE IF NOT EXISTS dp_log "
                    "("
                    "proj_id         int NOT NULL,"
                    "rev_sig         string NOT NULL,"
                    "cache_id        bigint NOT NULL,"
                    "time_stamp      bigint NOT NULL,"
                    "time_limit      smallint NOT NULL DEFAULT 0,"
                    "res             smallint NOT NULL DEFAULT -1,"
                    "host_ip         string NOT NULL,"
                    "host_res        smallint NOT NULL DEFAULT -1,"
                    "host_timespent  smallint NOT NULL DEFAULT 0,"
                    "host_errno      int NOT NULL DEFAULT 0,"
                    "host_detail     string"
                    ") PARTITION BY LIST (proj_id);"

                    "SELECT proj_id, path_on_host, source_url, source_branch, source_vcs_type, need_pull FROM proj_meta",
                    true))) {
        zPgSQL_.conn_clear(zpPgConnHd_);
        zPrint_Err(0, NULL, "pgSQL exec failed");
        exit(1);
    } else {
        zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_);
        if (0 == zpPgRes_->tupleCnt) {
            zPrint_Err(0, NULL, "No valid repo found!");
            goto zMarkNotFound;
        }
    }

    zpPgRes_->taskCnt = 0;
    for (_i i = 0; i < zpPgRes_->tupleCnt; i++) {
        zpPgRes_->tupleRes_[i].p_taskCnt = &(zpPgRes_->taskCnt);
        zThreadPool_.add(zinit_one_repo_env_thread_wraper, zpPgRes_->tupleRes_ + i);
    }

    pthread_mutex_lock(&zGlobCommonLock);
    while (zpPgRes_->tupleCnt > zpPgRes_->taskCnt) {
        pthread_cond_wait(&zGlobCommonCond, &zGlobCommonLock);
    }
    pthread_mutex_unlock(&zGlobCommonLock);

    /* 清理资源占用，创建新项目时，需要重新建立连接 */
zMarkNotFound:
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    zPgSQL_.conn_clear(zpPgConnHd_);

#ifndef _Z_BSD
    zThreadPool_.add(zsys_load_monitor, NULL);
#endif

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
