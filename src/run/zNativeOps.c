#include "zNativeOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <time.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/mman.h>
#include <sys/wait.h>
#include <dirent.h>

#include <libpq-fe.h>

extern struct zPosixReg__ zPosixReg_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zNetUtils__ zNetUtils_;
extern struct zThreadPool__ zThreadPool_;
extern struct zLibGit__ zLibGit_;
extern struct zDpOps__ zDpOps_;
extern struct zRun__ zRun_;
extern struct zPgSQL__ zPgSQL_;

static void * zalloc_cache(_i zRepoId, _ui zSiz);
static void * zget_diff_content(void *zp);
static void * zget_file_list(void *zp);
static void zgenerate_cache(void *zp);
static _i zinit_one_repo_env(zPgResTuple__ *zpRepoMeta, _i zSdToClose);
static void * zsys_load_monitor(void *zp);
static void * zinit_env(zPgLogin__ *zpPgLogin_);
static void * zextend_pg_partition(void *zp);

static _i zcode_fetch_mgmt(_i zRepoId);
static void * zcache_sync(void *zp __attribute__ ((__unused__)));

struct zNativeOps__ zNativeOps_ = {
    .get_revs = zgenerate_cache,
    .get_diff_files = zget_file_list,
    .get_diff_contents = zget_diff_content,

    .proj_init = zinit_one_repo_env,
    .proj_init_all = zinit_env,

    .alloc = zalloc_cache,
    .sysload_monitor = zsys_load_monitor,

    .extend_pg_partition = zextend_pg_partition,
    .fetch_mgmt = zcode_fetch_mgmt,
};


/*
 * 专用于项目缓存的 alloc 函数
 * 适用多线程环境，与布署动作同步开拓与释放资源
 */
static void *
zalloc_cache(_i zRepoId, _ui zSiz) {
    pthread_mutex_lock(& zRun_.p_repoVec[zRepoId]->memLock);

    /*
     * 检测当前内存池片区剩余空间是否充裕
     */
    if ((zSiz + zRun_.p_repoVec[zRepoId]->memPoolOffSet) > zMemPoolSiz) {
        /*
         * 新增一片内存，加入内存池
         */
        void *zpCur = NULL;
        if (MAP_FAILED == (zpCur = mmap(NULL, zMemPoolSiz, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0))) {
            zPRINT_ERR_EASY_SYS();
            exit(1);
        }

        /*
         * 首部指针位，指向内存池中的前一片区
         */
        void **zppPrev = zpCur;
        zppPrev[0] = zRun_.p_repoVec[zRepoId]->p_memPool;

        /*
         * 内存池指针更新
         */
        zRun_.p_repoVec[zRepoId]->p_memPool = zpCur;

        /*
         * 新内存片区开头的一个指针大小的空间已经被占用
         * 不能再分配，需要跳过
         */
        zRun_.p_repoVec[zRepoId]->memPoolOffSet = sizeof(void *);
    }

    /*
     * 分配内存
     */
    void *zpX = zRun_.p_repoVec[zRepoId]->p_memPool + zRun_.p_repoVec[zRepoId]->memPoolOffSet;
    zRun_.p_repoVec[zRepoId]->memPoolOffSet += zSiz;

    pthread_mutex_unlock(& zRun_.p_repoVec[zRepoId]->memLock);

    return zpX;
}


/*
 * 功能：生成单个文件的差异内容缓存
 */
static void *
zget_diff_content(void *zp) {
    zCacheMeta__ *zpMeta_ = (zCacheMeta__ *)zp;

    zVecWrap__ *zpTopVecWrap_ = NULL,
               *zpVecWrap = NULL;

    zBaseData__ *zpTmpBaseData_[3] = { NULL };
    _i zBaseDataLen = 0,
       zCnter = 0;

    /* MTU 上限，每个分片最多可以发送1448 Bytes */
    char zRes[zBYTES(1448)];

    if (zDATA_TYPE_COMMIT == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zRun_.p_repoVec[zpMeta_->repoId]->repoPathLen + 40 + 40 + zRun_.p_repoVec[zpMeta_->repoId]->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf,
            "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zRun_.p_repoVec[zpMeta_->repoId]->p_repoPath,
            zRun_.p_repoVec[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),
            zGet_OneFilePath(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId));

    FILE *zpShellRetHandler = NULL;
    zCHECK_NULL_EXIT( zpShellRetHandler = popen(zCommonBuf, "r") );

    /*
     * 读取差异内容
     * 没有下一级数据，大片读取，不再分行
     */
    zCnter = 0;
    if (0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBYTES(1448), zpShellRetHandler))) {
        zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
        zpTmpBaseData_[0]->dataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBYTES(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
            zpTmpBaseData_[0]->dataLen = zBaseDataLen;
            memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

            zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
            zpTmpBaseData_[1] = zpTmpBaseData_[0];
        }

        pclose(zpShellRetHandler);
    } else {
        pclose(zpShellRetHandler);
        zPRINT_ERR_EASY("");
        return (void *) -1;
    }

    if (0 == zCnter) {
        zpVecWrap = (void *) -1;
    } else {
        zpVecWrap = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
        zpVecWrap->vecSiz = zCnter;
        zpVecWrap->p_refData_ = NULL;
        zpVecWrap->p_vec_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
            zpVecWrap->p_vec_[i].iov_base = zpTmpBaseData_[2]->p_data;
            zpVecWrap->p_vec_[i].iov_len = zpTmpBaseData_[2]->dataLen;
        }
    }

    /* 数据完全生成之后，再插入到缓存结构中，保障可用性 */
    pthread_mutex_lock(& zRun_.p_repoVec[zpMeta_->repoId]->commLock);
    zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId) = zpVecWrap;
    pthread_mutex_unlock(& zRun_.p_repoVec[zpMeta_->repoId]->commLock);

    return NULL;
}


/*
 * 功能：生成单个 commitSig 与线上已布署版本之间的文件差异列表
 */
#define zGENERATE_GRAPH(zpNode_) {\
    zCacheMeta__ *zpTmpNode_;\
    _i zOffSet;\
\
    zpNode_->pp_resHash[zpNode_->lineNum] = zpNode_;\
    zOffSet = 6 * zpNode_->offSet + 10;\
\
    zpNode_->p_treeData[--zOffSet] = ' ';\
    zpNode_->p_treeData[--zOffSet] = '\200';\
    zpNode_->p_treeData[--zOffSet] = '\224';\
    zpNode_->p_treeData[--zOffSet] = '\342';\
    zpNode_->p_treeData[--zOffSet] = '\200';\
    zpNode_->p_treeData[--zOffSet] = '\224';\
    zpNode_->p_treeData[--zOffSet] = '\342';\
    zpNode_->p_treeData[--zOffSet] = (NULL == zpNode_->p_left) ? '\224' : '\234';\
    zpNode_->p_treeData[--zOffSet] = '\224';\
    zpNode_->p_treeData[--zOffSet] = '\342';\
\
    zpTmpNode_ = zpNode_;\
    for (_i i = 0; i < zpNode_->offSet; i++) {\
        zpNode_->p_treeData[--zOffSet] = ' ';\
        zpNode_->p_treeData[--zOffSet] = ' ';\
        zpNode_->p_treeData[--zOffSet] = ' ';\
\
        zpTmpNode_ = zpTmpNode_->p_father;\
        if (NULL == zpTmpNode_->p_left) {\
            zpNode_->p_treeData[--zOffSet] = ' ';\
        } else {\
            zpNode_->p_treeData[--zOffSet] = '\202';\
            zpNode_->p_treeData[--zOffSet] = '\224';\
            zpNode_->p_treeData[--zOffSet] = '\342';\
        }\
    }\
\
    zpNode_->p_treeData = zpNode_->p_treeData + zOffSet;\
\
}

static void *
zdistribute_task(void *zp) {
    zCacheMeta__ *zpNode_ = (zCacheMeta__ *)zp;
    zCacheMeta__ **zppKeepPtr = zpNode_->pp_resHash;

    do {
        /* 分发直连的子节点 */
        if (NULL != zpNode_->p_firstChild) {
            zpNode_->p_firstChild->pp_resHash = zppKeepPtr;
            zdistribute_task(zpNode_->p_firstChild);  // 暂时以递归处理，线程模型会有收集不齐全部任务的问题
        }

        /* 自身及所有的左兄弟 */
        zGENERATE_GRAPH(zpNode_);
        zpNode_ = zpNode_->p_left;
    } while ((NULL != zpNode_) && (zpNode_->pp_resHash = zppKeepPtr));

    return NULL;
}

#define zGENERATE_TREE_NODE() do {\
    zpTmpNode_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zCacheMeta__));\
\
    zpTmpNode_[0]->lineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNode_[0]->offSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNode_[0]->p_firstChild = NULL;\
    zpTmpNode_[0]->p_left = NULL;\
    zpTmpNode_[0]->p_treeData = zalloc_cache(zpMeta_->repoId, 6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_.p_resLen[zNodeCnter]);\
    strcpy(zpTmpNode_[0]->p_treeData + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_.pp_rets[zNodeCnter]);\
\
    if (zNodeCnter == (zRegRes_.cnt - 1)) {\
        zpTmpNode_[0]->fileId = zpTmpNode_[0]->lineNum;\
        zpTmpNode_[0]->p_filePath = zalloc_cache(zpMeta_->repoId, zBaseDataLen);\
        memcpy(zpTmpNode_[0]->p_filePath, zCommonBuf, zBaseDataLen);\
    } else {\
        zpTmpNode_[0]->fileId = -1;\
        zpTmpNode_[0]->p_filePath = NULL;\
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
    for (; zNodeCnter < zRegRes_.cnt; zNodeCnter++) {\
        zpTmpNode_[0]->p_firstChild = zalloc_cache(zpMeta_->repoId, sizeof(zCacheMeta__));\
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
        zpTmpNode_[0]->p_treeData = zalloc_cache(zpMeta_->repoId, 6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_.p_resLen[zNodeCnter]);\
        strcpy(zpTmpNode_[0]->p_treeData + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_.pp_rets[zNodeCnter]);\
\
        zpTmpNode_[0]->fileId = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNode_[0]->p_filePath = NULL;\
    }\
    zpTmpNode_[0]->fileId = zpTmpNode_[0]->lineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNode_[0]->p_filePath = zalloc_cache(zpMeta_->repoId, zBaseDataLen);\
    memcpy(zpTmpNode_[0]->p_filePath, zCommonBuf, zBaseDataLen);\
} while(0)

/*
 * 差异文件数量 >24 时使用 git 原生视图，
 * 避免占用太多资源，同时避免爆栈
 */
static void *
zget_file_list(void *zp) {
    zCacheMeta__ *zpMeta_ = (zCacheMeta__ *) zp;
    zVecWrap__ *zpTopVecWrap_ = NULL,
               *zpVecWrap = NULL;
    _i zVecDataLen = 0,
       zBaseDataLen = 0;

    if (zDATA_TYPE_COMMIT == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zRun_.p_repoVec[zpMeta_->repoId]->repoPathLen + 4 * 40 + zRun_.p_repoVec[zpMeta_->repoId]->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须首先在 shell 命令中切换到正确的工作路径 */
    sprintf(zCommonBuf,
            "cd \"%s\" "
            "&& git diff --shortstat \"%s\" \"%s\"|grep -oP '\\d+(?=\\s*file)' "
            "&& git diff --name-only \"%s\" \"%s\"",
            zRun_.p_repoVec[zpMeta_->repoId]->p_repoPath,
            zRun_.p_repoVec[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),
            zRun_.p_repoVec[zpMeta_->repoId]->lastDpSig,
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId));

    FILE *zpShellRetHandler = NULL;
    zCHECK_NULL_EXIT( zpShellRetHandler = popen(zCommonBuf, "r") );

    if (NULL == zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        pclose(zpShellRetHandler);
        zPRINT_ERR_EASY("");
        return (void *) -1;
    } else {
        if (24 < strtol(zCommonBuf, NULL, 10)) {
            zBaseData__ *zpTmpBaseData_[3] = { NULL };

            /* 采集原始数据 */
            _i zCnter = 0;
            for (; NULL != zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler);
                    zCnter++) {
                zBaseDataLen = strlen(zCommonBuf);
                zpTmpBaseData_[0] = zalloc_cache(zpMeta_->repoId, sizeof(zBaseData__) + zBaseDataLen);
                if (0 == zCnter) {
                    zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
                }
                zpTmpBaseData_[0]->dataLen = zBaseDataLen;
                memcpy(zpTmpBaseData_[0]->p_data, zCommonBuf, zBaseDataLen);
                zpTmpBaseData_[0]->p_data[zBaseDataLen - 1] = '\0';

                zpTmpBaseData_[1]->p_next = zpTmpBaseData_[0];
                zpTmpBaseData_[1] = zpTmpBaseData_[0];
                zpTmpBaseData_[0] = zpTmpBaseData_[0]->p_next;
            }
            pclose(zpShellRetHandler);

            /* 加工数据 */
            zpVecWrap = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
            zpVecWrap->vecSiz = zCnter;
            zpVecWrap->p_refData_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(zRefData__));
            zpVecWrap->p_vec_ = zalloc_cache(zpMeta_->repoId, zCnter * sizeof(struct iovec));

            for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
                zpVecWrap->p_refData_[i].p_data = zpTmpBaseData_[2]->p_data;

                /* 转换为 JSON 字符串 */
                zVecDataLen = sprintf(zCommonBuf,
                        ",{\"fileId\":%d,\"filePath\":\"%s\"}",
                        i,
                        zpTmpBaseData_[2]->p_data);

                zpVecWrap->p_vec_[i].iov_len = zVecDataLen;
                zpVecWrap->p_vec_[i].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
                memcpy(zpVecWrap->p_vec_[i].iov_base, zCommonBuf, zVecDataLen);

                zpVecWrap->p_refData_[i].p_subVecWrap_ = NULL;
            }

            /* 跳过 Tree 图部分 */
            goto zMarkLarge;
        }
    }

    /* 差异文件数量 <=24 生成Tree图 */
    _i zNodeCnter = 0,
       zLineCnter = 0;

    /*
     * root:根节点
     * [0]:本体;[1]:记录父节点;[2]:记录兄长节点
     */
    zCacheMeta__ *zpRootNode_ = NULL,
                 *zpTmpNode_[3] = { NULL };
    /*
     * 使用字符串分割函数
     * 不使用正则
     * 使用项目内存池
     */
    zRegRes__ zRegRes_ = {
        .alloc_fn = zalloc_cache,
        .repoId = zpMeta_->repoId
    };

    /*
     * 采集原始数据，并分解生成树结构
     */
    if (NULL != zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
        zBaseDataLen = strlen(zCommonBuf);

        zCommonBuf[zBaseDataLen - 1] = '\0';  /* 去除换行符 */
        zPosixReg_.str_split_fast(&zRegRes_, zCommonBuf, "/");

        zNodeCnter = 0;
        zpTmpNode_[2] = zpTmpNode_[1] = zpTmpNode_[0] = NULL;

        /* 添加树节点 */
        zGENERATE_TREE_NODE();

        while (NULL != zNativeUtils_.read_line(zCommonBuf, zMaxBufLen, zpShellRetHandler)) {
            zBaseDataLen = strlen(zCommonBuf);

            zCommonBuf[zBaseDataLen - 1] = '\0';  /* 去除换行符 */
            zPosixReg_.str_split_fast(&zRegRes_, zCommonBuf, "/");

            zpTmpNode_[0] = zpRootNode_;
            zpTmpNode_[2] = zpTmpNode_[1] = NULL;
            for (zNodeCnter = 0; zNodeCnter < zRegRes_.cnt;) {
                do {
                    if (0 == strcmp(zpTmpNode_[0]->p_treeData + 6 * zpTmpNode_[0]->offSet + 10,
                                zRegRes_.pp_rets[zNodeCnter])) {
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
            /* 添加树节点 */
            zGENERATE_TREE_NODE();
        }
    }

    pclose(zpShellRetHandler);


    /* 加工数据 */
    zpVecWrap = zalloc_cache(zpMeta_->repoId, sizeof(zVecWrap__));
    if (NULL == zpRootNode_) {
        zpVecWrap->vecSiz = 1;

        zpVecWrap->p_refData_ = NULL;
        zpVecWrap->p_vec_ = zalloc_cache(zpMeta_->repoId, sizeof(struct iovec));

        /* 转换为 JSON 文本 */
        zVecDataLen = sprintf(zCommonBuf,
                "[{\"fileId\":-1,\"filePath\":\"%s\"}",
                (0 == strcmp(zRun_.p_repoVec[zpMeta_->repoId]->lastDpSig,
                             zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId))) ?
                "===> 最新的已布署版本 <===" : "=> 无差异 <=");

        zpVecWrap->p_vec_[0].iov_len = zVecDataLen;
        zpVecWrap->p_vec_[0].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
        memcpy(zpVecWrap->p_vec_[0].iov_base, zCommonBuf, zVecDataLen);
    } else {
        zpVecWrap->vecSiz = zLineCnter;

        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNode_->pp_resHash = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(zCacheMeta__ *));

        /* 生成最终的 Tree 图 */
        zdistribute_task(zpRootNode_);

        zpVecWrap->p_refData_
            = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(zRefData__));
        zpVecWrap->p_vec_
            = zalloc_cache(zpMeta_->repoId, zLineCnter * sizeof(struct iovec));

        for (_i i = 0; i < zLineCnter; i++) {
            /* 转换为 json 文本 */
            zVecDataLen = sprintf(zCommonBuf, ",{\"fileId\":%d,\"filePath\":\"%s\"}",
                    zpRootNode_->pp_resHash[i]->fileId,
                    zpRootNode_->pp_resHash[i]->p_treeData);

            zpVecWrap->p_vec_[i].iov_len = zVecDataLen;
            zpVecWrap->p_vec_[i].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
            memcpy(zpVecWrap->p_vec_[i].iov_base, zCommonBuf, zVecDataLen);

            zpVecWrap->p_refData_[i].p_data = zpRootNode_->pp_resHash[i]->p_filePath;
            zpVecWrap->p_refData_[i].p_subVecWrap_ = NULL;
        }
    }

zMarkLarge:
    /* 修饰第一项，形成 json 结构；后半个 ']' 会在网络服务中通过单独一个 send 发出 */
    ((char *) (zpVecWrap->p_vec_[0].iov_base))[0] = '[';

    /* 数据完全生成之后，再插入到缓存结构中，保障可用性 */
    pthread_mutex_lock(& zRun_.p_repoVec[zpMeta_->repoId]->commLock);
    zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId) = zpVecWrap;
    pthread_mutex_unlock(& zRun_.p_repoVec[zpMeta_->repoId]->commLock);

    return NULL;
}


/*
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效
 * 因此每次都需要重新执行此函数以刷新缓存
 */
static void
zgenerate_cache(void *zp) {
    char *zpRevSig[zCACHE_SIZ] = { NULL };
    char zTimeStampVec[16 * zCACHE_SIZ];

    zVecWrap__ *zpTopVecWrap_ = NULL;
    zCacheMeta__ *zpMeta_ = (zCacheMeta__ *) zp;

    time_t zTimeStamp = 0;
    _i zVecDataLen = 0,
       i = 0;

    /* 计算本函数需要用到的最大 BufSiz */
    char zCommonBuf[256 + zRun_.p_repoVec[zpMeta_->repoId]->repoPathLen + 12];

    if (zDATA_TYPE_COMMIT == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->commitVecWrap_;

        /* use: refs/remotes/origin/%sXXXXXXXX ??? */
        zGitRevWalk__ *zpRevWalker = NULL;
        sprintf(zCommonBuf, "refs/heads/%sXXXXXXXX",
                zRun_.p_repoVec[zpMeta_->repoId]->p_codeSyncBranch);
        if (NULL == (zpRevWalker = zLibGit_.generate_revwalker(
                        zRun_.p_repoVec[zpMeta_->repoId]->p_gitCommHandler,
                        zCommonBuf,
                        0))) {
            zPRINT_ERR_EASY("");
            exit(1);
        } else {
            for (i = 0; i < zCACHE_SIZ; i++) {
                zpRevSig[i] = zalloc_cache(zpMeta_->repoId, zBYTES(44));
                if (0 < (zTimeStamp = zLibGit_.get_one_commitsig_and_timestamp(
                                zpRevSig[i],
                                zRun_.p_repoVec[zpMeta_->repoId]->p_gitCommHandler,
                                zpRevWalker))
                        &&
                        0 != strcmp(zRun_.p_repoVec[zpMeta_->repoId]->lastDpSig, zpRevSig[i])) {
                    sprintf(zTimeStampVec + 16 * i, "%ld", zTimeStamp);
                } else {
                    zpRevSig[i] = NULL;
                    break;
                }
            }

            zLibGit_.destroy_revwalker(zpRevWalker);
        }

        /* 存储的是实际的对象数量 */
        zpTopVecWrap_->vecSiz = zpTopVecWrap_->vecSiz = i;

    } else if (zDATA_TYPE_DP == zpMeta_->dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zpMeta_->repoId]->dpVecWrap_;
        zPgRes__ *zpPgRes_ = NULL;

        /* 须使用 DISTINCT 关键字去重 */
        sprintf(zCommonBuf,
                "SELECT DISTINCT rev_sig, time_stamp FROM dp_log "
                "WHERE proj_id = %d ORDER BY time_stamp DESC LIMIT %d",
                zpMeta_->repoId,
                zCACHE_SIZ);

        if (0 != zPgSQL_.exec_once(zRun_.pgConnInfo, zCommonBuf, & zpPgRes_)) {
            zPRINT_ERR_EASY("");
            exit(1);
        }

        /* 存储的是实际的对象数量 */
        if (NULL == zpPgRes_) {
            zpTopVecWrap_->vecSiz = 0;
        } else {
            zpTopVecWrap_->vecSiz = (zCACHE_SIZ < zpPgRes_->tupleCnt) ? zCACHE_SIZ : zpPgRes_->tupleCnt;
        }
        zpTopVecWrap_->vecSiz = zpTopVecWrap_->vecSiz;

        for (i = 0; i < zpTopVecWrap_->vecSiz; i++) {
            zpRevSig[i] = zalloc_cache(zpMeta_->repoId, zBYTES(41));
            /*
             * 存储 RevSig 的 SQL 数据类型是 char(40)，只会存数据正文，不存 '\0'
             * 使用 libpq 取出来的值是 41 位，最后又会追加一个 '\0'
             */
            strcpy(zpRevSig[i], zpPgRes_->tupleRes_[i].pp_fields[0]);
            strcpy(zTimeStampVec + 16 * i, zpPgRes_->tupleRes_[i].pp_fields[1]);
        }

        zPgSQL_.res_clear(NULL, zpPgRes_);
    } else {
        /* BUG! */
        zPRINT_ERR_EASY("");
        exit(1);
    }

    if (NULL != zpRevSig[0]) {
        for (i = 0; i < zCACHE_SIZ && NULL != zpRevSig[i]; i++) {
            /* 转换为JSON 文本 */
            zVecDataLen = sprintf(zCommonBuf,
                    ",{\"revId\":%d,\"revSig\":\"%s\",\"revTimeStamp\":\"%s\"}",
                    i,
                    zpRevSig[i],
                    zTimeStampVec + 16 * i);

            zpTopVecWrap_->p_vec_[i].iov_len = zVecDataLen;
            zpTopVecWrap_->p_vec_[i].iov_base = zalloc_cache(zpMeta_->repoId, zVecDataLen);
            memcpy(zpTopVecWrap_->p_vec_[i].iov_base, zCommonBuf, zVecDataLen);

            zpTopVecWrap_->p_refData_[i].p_data = zpRevSig[i];
            zpTopVecWrap_->p_refData_[i].p_subVecWrap_ = NULL;
        }

        /*
         * 修饰第一项，形成二维json
         * 最后一个 ']' 会在网络服务中通过单独一个 send 发过去
         */
        ((char *) (zpTopVecWrap_->p_vec_[0].iov_base))[0] = '[';
    }

    /*
     * 程序安全：
     * 防止意外或恶意访问导致程序崩溃
     */
    memset(zpTopVecWrap_->p_refData_ + zpTopVecWrap_->vecSiz,
            0,
            sizeof(zRefData__) * (zCACHE_SIZ - zpTopVecWrap_->vecSiz));
}


/************
 * INIT OPS *
 ************/
#define zFREE_SOURCE() do {\
    free(zRun_.p_repoVec[zRepoId]->p_repoPath);\
    free(zRun_.p_repoVec[zRepoId]);\
    zRun_.p_repoVec[zRepoId] = NULL;\
} while(0)

#define zERR_RETURN_OR_EXIT(zResNo) do {\
    zPRINT_ERR_EASY("");\
    if (0 <= zSdToClose) {  /* 新建项目失败返回，则删除路径 */\
        zNativeUtils_.path_del(zRun_.p_repoVec[zRepoId]->p_repoPath);\
    }\
    if (0 < zResNo) {\
        exit(1);\
    } else {\
        return zResNo;\
    }\
} while(0)

/*
 * 参数：项目基本信息
 * @zSdToClose 作用一：子进程凭此决定是否需要关闭此套接字；作用二：用以识别是项目载入还是项目新建
 * zpRepoMeta_->pp_field[i]: [0 repoId] [1 pathOnHost] [2 sourceURL] [3 sourceBranch] [4 sourceVcsType] [5 needPull]
 */
static _i
zinit_one_repo_env(zPgResTuple__ *zpRepoMeta_, _i zSdToClose) {
    zRegInit__ zRegInit_;
    zRegRes__ zRegRes_ = { .alloc_fn = NULL };

    _s zRepoId = 0,
       zStrLen = 0;
    _c zNeedPull = -1;

    char *zpOrigPath = NULL,
         zKeepValue = 0;

    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

    char zCommonBuf[zGLOB_COMMON_BUF_SIZ];

    _us zSourceUrlLen = strlen(zpRepoMeta_->pp_fields[2]),
        zSourceBranchLen = strlen(zpRepoMeta_->pp_fields[3]),
        zSyncRefsLen = sizeof("+refs/heads/:refs/heads/XXXXXXXX") -1 + 2 * zSourceBranchLen;

    /* 提取项目ID */
    zRepoId = strtol(zpRepoMeta_->pp_fields[0], NULL, 10);

    /* 检查项目 ID 是否超限 */
    if (zGLOB_REPO_ID_LIMIT <= zRepoId || 0 >= zRepoId) {
        zPRINT_ERR_EASY("");
        return -32;
    }

    /* 检查项目 ID 是否冲突 */
    if (NULL != zRun_.p_repoVec[zRepoId]) {
        zPRINT_ERR_EASY("");
        return -35;
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMEM_C_ALLOC(zRun_.p_repoVec[zRepoId], zRepo__, 1);
    zRun_.p_repoVec[zRepoId]->repoId = zRepoId;

    /* needPull */
    zNeedPull = toupper(zpRepoMeta_->pp_fields[5][0]);

    /*
     * 提取项目绝对路径
     * 去掉 basename 部分，之后拼接出最终的服务端路径字符串
     * 服务端项目路径格式：${HOME}/`dirname $zPathOnHost`/.____DpSystem/${zProjNo}/`basename $zPathOnHost`
     */
    zPosixReg_.init(&zRegInit_, "[^/]+[/]*$");
    zPosixReg_.match(&zRegRes_, &zRegInit_, zpRepoMeta_->pp_fields[1]);
    zPosixReg_.free_meta(&zRegInit_);

    if (0 == zRegRes_.cnt) {
        zPosixReg_.free_res(&zRegRes_);
        free(zRun_.p_repoVec[zRepoId]);
        zRun_.p_repoVec[zRepoId] = NULL;
        zPRINT_ERR_EASY("");
        return -29;
    }

    zpOrigPath = zpRepoMeta_->pp_fields[1];
    zStrLen = strlen(zpOrigPath);
    zKeepValue = zpOrigPath[zStrLen - zRegRes_.p_resLen[0] - 1];
    zpOrigPath[zStrLen - zRegRes_.p_resLen[0] - 1] = '\0';

    while ('/' == zpOrigPath[0]) {  /* 去除多余的 '/' */
        zpOrigPath++;
    }

    zMEM_ALLOC(zRun_.p_repoVec[zRepoId]->p_repoPath, char,
            sizeof("//.____DpSystem//") + zRun_.homePathLen + zStrLen + 16 + zRegRes_.p_resLen[0]);

    zRun_.p_repoVec[zRepoId]->repoPathLen =
        sprintf(zRun_.p_repoVec[zRepoId]->p_repoPath,
            "%s/%s/.____DpSystem/%d/%s",
            zRun_.p_homePath,
            zpOrigPath,
            zRepoId,
            zRegRes_.pp_rets[0]);

    zPosixReg_.free_res(&zRegRes_);

    zpRepoMeta_->pp_fields[1][zStrLen - zRegRes_.p_resLen[0] - 1]
        = zKeepValue;  /* 恢复原始字符串，上层调用者需要使用 */

    /*
     * 取出本项目所在文件系统支持的最大路径长度
     * 用于度量 git 输出的差异文件相对路径最大可用空间
     */
    zRun_.p_repoVec[zRepoId]->maxPathLen =
        pathconf(zRun_.p_repoVec[zRepoId]->p_repoPath, _PC_PATH_MAX);

    /*
     * 项目别名路径，由用户每次布署时指定
     * 长度限制为 maxPathLen
     */
    zMEM_ALLOC(zRun_.p_repoVec[zRepoId]->p_repoAliasPath, char, zRun_.p_repoVec[zRepoId]->maxPathLen);
    zRun_.p_repoVec[zRepoId]->p_repoAliasPath[0] = '\0';

    {////
    /*
     * ================
     *  服务端创建项目
     * ================
     */
    struct stat zS_;
    DIR *zpDIR = NULL;
    struct dirent *zpItem = NULL;
    char zPathBuf[zRun_.p_repoVec[zRepoId]->maxPathLen];
    if (0 == stat(zRun_.p_repoVec[zRepoId]->p_repoPath, &zS_)) {
        /**
         * 若是项目新建，则不允许存在同名路径
         * 既有项目初始化会将 zSdToClose 置为 -1
         */
        if (0 <= zSdToClose) {
            zFREE_SOURCE();
            zPRINT_ERR_EASY("");
            return -36;
        } else {
            if (! S_ISDIR(zS_.st_mode)) {
                zFREE_SOURCE();
                zPRINT_ERR_EASY("");
                return -30;
            } else {  /* TO DEL: 兼容旧版本 */
                /* 全局 libgit2 Handler 初始化 */
                if (NULL == (zRun_.p_repoVec[zRepoId]->p_gitCommHandler
                            = zLibGit_.env_init(zRun_.p_repoVec[zRepoId]->p_repoPath))) {
                    zFREE_SOURCE();
                    zPRINT_ERR_EASY("");
                    return -46;
                }

                /* 不必关心执行结果 */
                sprintf(zCommonBuf, "%sXXXXXXXX", zpRepoMeta_->pp_fields[3]);
                zLibGit_.branch_add(zRun_.p_repoVec[zRepoId]->p_gitCommHandler,
                        zCommonBuf, "HEAD", zFalse);

                /*
                 * 删除所有除 .git 之外的文件与目录
                 * readdir 的结果为 NULL 时，需要依 errno 判断是否出错
                 */
                zCHECK_NULL_EXIT( zpDIR = opendir(zRun_.p_repoVec[zRepoId]->p_repoPath) );

                errno = 0;
                while (NULL != (zpItem = readdir(zpDIR))) {
                    if (DT_DIR == zpItem->d_type) {
                        if (0 != strcmp(".git", zpItem->d_name)
                                && 0 != strcmp(".", zpItem->d_name)
                                && 0 != strcmp("..", zpItem->d_name)) {

                            snprintf(zPathBuf, zRun_.p_repoVec[zRepoId]->maxPathLen, "%s/%s",
                                    zRun_.p_repoVec[zRepoId]->p_repoPath, zpItem->d_name);
                            if (0 != zNativeUtils_.path_del(zPathBuf)) {
                                closedir(zpDIR);
                                zFREE_SOURCE();
                                zPRINT_ERR_EASY("");
                                return -40;
                            }
                        }
                    } else {
                        snprintf(zPathBuf, zRun_.p_repoVec[zRepoId]->maxPathLen, "%s/%s",
                                zRun_.p_repoVec[zRepoId]->p_repoPath, zpItem->d_name);
                        if (0 != unlink(zPathBuf)) {
                            closedir(zpDIR);
                            zFREE_SOURCE();
                            zPRINT_ERR_EASY_SYS();
                            return -40;
                        }
                    }
                }

                zCHECK_NOTZERO_EXIT( errno );
                closedir(zpDIR);

                if (0 != zLibGit_.branch_rename(zRun_.p_repoVec[zRepoId]->p_gitCommHandler, "____base.XXXXXXXX", "____baseXXXXXXXX", zTrue)) {
                    if (0 != zLibGit_.add_and_commit(zRun_.p_repoVec[zRepoId]->p_gitCommHandler, "refs/heads/____baseXXXXXXXX", ".", "_")) {
                        zPRINT_ERR_EASY("");
                        return -45;
                    }
                }
            }
        }
    } else {
        /**
         * git clone URL，内部会回调 git_init，
         * 生成本项目 libgit2 Handler
         */
        if (0 != zLibGit_.clone(
                    &zRun_.p_repoVec[zRepoId]->p_gitCommHandler,
                    zpRepoMeta_->pp_fields[2],
                    zRun_.p_repoVec[zRepoId]->p_repoPath, NULL, zFalse)) {
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-42);
        }

        /* git config user.name _ && git config user.email _@_ */
        if (0 != zLibGit_.config_name_email(zRun_.p_repoVec[zRepoId]->p_repoPath)) {
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-43);
        }

        /*
         * 创建 {源分支名称}XXXXXXXX 分支
         * 注：源库不能是空库，即：0 提交、0 分支
         */
        sprintf(zCommonBuf, "%sXXXXXXXX", zpRepoMeta_->pp_fields[3]);
        if (0 != zLibGit_.branch_add(zRun_.p_repoVec[zRepoId]->p_gitCommHandler, zCommonBuf, "HEAD", zFalse)) {
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-44);
        }

        /*
         * 删除所有除 .git 之外的文件与目录，提交到 ____baseXXXXXXXX 分支
         * readdir 的结果为 NULL 时，需要依 errno 判断是否出错
         */
        if (NULL == (zpDIR = opendir(zRun_.p_repoVec[zRepoId]->p_repoPath))) {
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-40);
        }

        errno = 0;
        while (NULL != (zpItem = readdir(zpDIR))) {
            if (DT_DIR == zpItem->d_type) {
                if (0 != strcmp(".git", zpItem->d_name)
                        && 0 != strcmp(".", zpItem->d_name)
                        && 0 != strcmp("..", zpItem->d_name)) {

                    snprintf(zPathBuf, zRun_.p_repoVec[zRepoId]->maxPathLen, "%s/%s",
                            zRun_.p_repoVec[zRepoId]->p_repoPath, zpItem->d_name);
                    if (0 != zNativeUtils_.path_del(zPathBuf)) {
                        closedir(zpDIR);
                        zFREE_SOURCE();
                        zERR_RETURN_OR_EXIT(-40);
                    }
                }
            } else {
                snprintf(zPathBuf, zRun_.p_repoVec[zRepoId]->maxPathLen, "%s/%s",
                        zRun_.p_repoVec[zRepoId]->p_repoPath, zpItem->d_name);
                if (0 != unlink(zPathBuf)) {
                    closedir(zpDIR);
                    zFREE_SOURCE();
                    zERR_RETURN_OR_EXIT(-40);
                }
            }
        }

        if (0 != errno) {
            closedir(zpDIR);
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-40);
        }

        closedir(zpDIR);

        if (0 != zLibGit_.add_and_commit(zRun_.p_repoVec[zRepoId]->p_gitCommHandler, "refs/heads/____baseXXXXXXXX", ".", "_")) {
            zFREE_SOURCE();
            zERR_RETURN_OR_EXIT(-40);
        }
    }
    }////

    /*
     * PostgreSQL 中以 char(1) 类型存储
     * 'G' 代表 git，'S' 代表 svn
     * 历史遗留问题，实质已不支持 SVN
     */
    if ('G' != toupper(zpRepoMeta_->pp_fields[4][0])) {
        zFREE_SOURCE();
        zERR_RETURN_OR_EXIT(-37);
    }

    /* 通用锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zRun_.p_repoVec[zRepoId]->commLock, NULL) );

    /* 布署锁与布署等待锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zRun_.p_repoVec[zRepoId]->dpLock, NULL) );
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zRun_.p_repoVec[zRepoId]->dpWaitLock, NULL) );

    /* libssh2/libgit2 等布署相关的并发锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zRun_.p_repoVec[zRepoId]->dpSyncLock, NULL) );
    zCHECK_PTHREAD_FUNC_EXIT( pthread_cond_init(& zRun_.p_repoVec[zRepoId]->dpSyncCond, NULL) );

    /* 为每个代码库生成一把读写锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_rwlock_init(& zRun_.p_repoVec[zRepoId]->rwLock, NULL) );
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_init(& zRun_.p_repoVec[zRepoId]->rwLockAttr));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_setkind_np(& zRun_.p_repoVec[zRepoId]->rwLockAttr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlock_init(& zRun_.p_repoVec[zRepoId]->rwLock, & zRun_.p_repoVec[zRepoId]->rwLockAttr));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_destroy(& zRun_.p_repoVec[zRepoId]->rwLockAttr));

    /* 读写锁生成之后，立刻取写锁 */
    pthread_rwlock_wrlock(& zRun_.p_repoVec[zRepoId]->rwLock);

    /* 缓存版本初始化 */
    zRun_.p_repoVec[zRepoId]->cacheId = time(NULL);

    /* SQL 临时表命名序号 */
    zRun_.p_repoVec[zRepoId]->tempTableNo = 0;

    /* 本项目 pgSQL 连接的全局 Handler */
    if (NULL == (zRun_.p_repoVec[zRepoId]->p_pgConnHd_ = zPgSQL_.conn(zRun_.pgConnInfo))) {
        zERR_RETURN_OR_EXIT(1);
    }

    /*
     * 每次启动时尝试创建必要的表，
     * 按天分区（1天 == 86400秒）
     * +2 的意义: 防止恰好在临界时间添加记录导致异常
     */
    _i zBaseId = time(NULL) / 86400 + 2;

    /*
     * 新建项目时需要执行一次
     * 后续不需要再执行
     */
    if (0 <= zSdToClose) {
        sprintf(zCommonBuf,
                "CREATE TABLE IF NOT EXISTS dp_log_%d "
                "PARTITION OF dp_log FOR VALUES IN (%d) "
                "PARTITION BY RANGE (time_stamp);"

                "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                "PARTITION OF dp_log_%d FOR VALUES FROM (MINVALUE) TO (%d);",
            zRepoId, zRepoId,
            zRepoId, zBaseId, zRepoId, 86400 * zBaseId);

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zRun_.p_repoVec[zRepoId]->p_pgConnHd_, zCommonBuf, zFalse))) {
            zERR_RETURN_OR_EXIT(1);
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }
    }

    for (_i zId = 0; zId < 10; zId++) {
        sprintf(zCommonBuf,
                "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                "PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                zRepoId, zBaseId + zId + 1, zRepoId, 86400 * (zBaseId + zId), 86400 * (zBaseId + zId + 1));

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zRun_.p_repoVec[zRepoId]->p_pgConnHd_, zCommonBuf, zFalse))) {
            zERR_RETURN_OR_EXIT(1);
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }
    }


    /*
     * ====  提取项目创建时间与路径别名 ====
     * 既有项目，从 DB 中提取其创建时间
     * 项目新建时在 add_repo(...) 中处理
     */
    if (0 > zSdToClose) {
        snprintf(zCommonBuf, zGLOB_COMMON_BUF_SIZ,
                "SELECT create_time,alias_path FROM proj_meta WHERE proj_id = %d",
                zRepoId);

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zRun_.p_repoVec[zRepoId]->p_pgConnHd_, zCommonBuf, zTrue))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zERR_RETURN_OR_EXIT(1);
        }

        /* DB err: 'create_time' miss */
        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zERR_RETURN_OR_EXIT(1);
        }

        /* copy... */
        snprintf(zRun_.p_repoVec[zRepoId]->createdTime,
                24,
                "%s",
                zpPgRes_->tupleRes_[0].pp_fields[0]);
        snprintf(zRun_.p_repoVec[zRepoId]->p_repoAliasPath,
                zRun_.p_repoVec[zRepoId]->maxPathLen,
                "%s",
                zpPgRes_->tupleRes_[0].pp_fields[1]);

        /* clean... */
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

        /**
         * 获取最近一次成功布署的版本号
         * lastDpSig
         */
        sprintf(zCommonBuf,
                "SELECT last_dp_sig,last_try_sig FROM proj_meta "
                "WHERE proj_id = %d",
                zRepoId);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zRun_.p_repoVec[zRepoId]->p_pgConnHd_, zCommonBuf, zTrue))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zERR_RETURN_OR_EXIT(1);
        }

        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zERR_RETURN_OR_EXIT(1);
        }

        if ('\0' == zpPgRes_->tupleRes_[0].pp_fields[0][0]) {
            zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                    zRun_.p_repoVec[zRepoId]->p_gitCommHandler,
                    "refs/heads/____baseXXXXXXXX",
                    0);
            if (NULL != zpRevWalker
                    && 0 < zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf, zRun_.p_repoVec[zRepoId]->p_gitCommHandler, zpRevWalker)) {
                strncpy(zRun_.p_repoVec[zRepoId]->lastDpSig, zCommonBuf, 40);
                zRun_.p_repoVec[zRepoId]->lastDpSig[40] = '\0';

                zLibGit_.destroy_revwalker(zpRevWalker);
            } else {
                zERR_RETURN_OR_EXIT(1);
            }
        } else {
            strncpy(zRun_.p_repoVec[zRepoId]->lastDpSig, zpPgRes_->tupleRes_[0].pp_fields[0], 40);
            zRun_.p_repoVec[zRepoId]->lastDpSig[40] = '\0';
        }

        if ('\0' == zpPgRes_->tupleRes_[0].pp_fields[1][0]) {
            strcpy(zRun_.p_repoVec[zRepoId]->dpingSig, zRun_.p_repoVec[zRepoId]->lastDpSig);

            /* 预置为成功状态 */
            zRun_.p_repoVec[zRepoId]->repoState = zCACHE_GOOD;
        } else {
            strncpy(zRun_.p_repoVec[zRepoId]->dpingSig, zpPgRes_->tupleRes_[0].pp_fields[1], 40);
            zRun_.p_repoVec[zRepoId]->dpingSig[40] = '\0';

            /* 比较最近一次尝试布署的版本号与最近一次成功布署的版本号是否相同 */
            if (0 == strcmp(zRun_.p_repoVec[zRepoId]->dpingSig, zRun_.p_repoVec[zRepoId]->lastDpSig)) {
                zRun_.p_repoVec[zRepoId]->repoState = zCACHE_GOOD;
            } else {
                zRun_.p_repoVec[zRepoId]->repoState = zCACHE_DAMAGED;
            }
        }

        /* clean... */
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

        /**
         * 提取最近一次布署动作的时间戳（无论成功或失败）
         * dpingSig
         */
        sprintf(zCommonBuf,
                "SELECT time_stamp FROM dp_log "
                "WHERE proj_id = %d AND rev_sig = '%s' LIMIT 1",
                zRepoId,
                zRun_.p_repoVec[zRepoId]->dpingSig);

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                        zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                        zCommonBuf,
                        zTrue))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zERR_RETURN_OR_EXIT(1);
        }

        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp = 0;
        } else {
            zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp =
                strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
        }

        /* clean... */
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

        /**
         * 获取日志中记录的最近一次布署的 IP 列表
         * 及相关状态数据
         */
        sprintf(zCommonBuf,
                "SELECT host_ip,"
                "host_res[1],host_res[2],host_res[3],host_res[4],"
                "host_err[1],host_err[2],host_err[3],host_err[4],host_err[5],host_err[6],host_err[7],host_err[8],host_err[9],host_err[10],host_err[11],host_err[12],"
                "host_detail "
                "FROM dp_log "
                "WHERE proj_id = %d AND rev_sig = '%s'",
                zRepoId,
                zRun_.p_repoVec[zRepoId]->dpingSig);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zRun_.p_repoVec[zRepoId]->p_pgConnHd_, zCommonBuf, zTrue))) {
            zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            zERR_RETURN_OR_EXIT(1);
        }

        if (NULL != (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zMEM_C_ALLOC(zRun_.p_repoVec[zRepoId]->p_dpResList_, zDpRes__, zpPgRes_->tupleCnt);
            // memset(zRun_.p_repoVec[zRepoId]->p_dpResHash_, 0, zDP_HASH_SIZ * sizeof(zDpRes__ *));

            /* needed by zDpOps_.show_dp_process */
            zRun_.p_repoVec[zRepoId]->totalHost
                = zRun_.p_repoVec[zRepoId]->dpTotalTask
                = zRun_.p_repoVec[zRepoId]->dpTaskFinCnt
                = zpPgRes_->tupleCnt;
            zRun_.p_repoVec[zRepoId]->resType = 0;

            zDpRes__ *zpTmpDpRes_ = NULL;
            for (_i i = 0; i < zpPgRes_->tupleCnt; i++) {
                /* 检测是否存在重复IP */
                if (0 != zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]
                        || 0 != zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[1]) {
                    zRun_.p_repoVec[zRepoId]->totalHost--;
                    continue;
                }


                /* 线性链表斌值；转换字符串格式 IP 为 _ull 型 */
                if (0 != zCONVERT_IPSTR_TO_NUM(zpPgRes_->tupleRes_[i].pp_fields[0],
                            zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr)) {
                    zERR_RETURN_OR_EXIT(1);
                }

                /* 恢复上一次布署的 resState 与全局 resType */
                if ('1' == zpPgRes_->tupleRes_[i].pp_fields[4][0]) {
                    zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 4);
                    zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 3);
                    zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 2);
                    zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 1);

                    zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState = 0;
                } else {
                    /* 布署环节：未成功即是失败 */
                    zSET_BIT(zRun_.p_repoVec[zRepoId]->resType, 2);

                    /* 非必要，无需恢复... */
                    // if ('1' == zpPgRes_->tupleRes_[i].pp_fields[3][0]) {
                    //     zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 3);
                    // } else if ('1' == zpPgRes_->tupleRes_[i].pp_fields[2][0]) {
                    //     zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 2);
                    // }

                    /* 目标机初始化环节：未成功即是失败 */
                    if ('1' == zpPgRes_->tupleRes_[i].pp_fields[1][0]) {
                        zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 1);
                    } else {
                        zSET_BIT(zRun_.p_repoVec[zRepoId]->resType, 1);
                    }

                    /* 恢复上一次布署的 errState */
                    for (_i j = 5; j < 17; j++) {
                        if ('1' == zpPgRes_->tupleRes_[i].pp_fields[j][0]) {
                            zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState, j - 4);
                            break;
                        }
                    }

                    if (0 == zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState) {
                        /* 若未捕获到错误，一律置为服务端错误类别 */
                        zSET_BIT(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState, 1);
                    }

                    /* 错误详情 */
                    strncpy(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errMsg, zpPgRes_->tupleRes_[i].pp_fields[17], 255);
                    zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errMsg[255] = '\0';
                }

                /* 使用 calloc 分配的清零空间，此项无需复位 */
                // zRun_.p_repoVec[zRepoId]->p_dpResList_[i].p_next = NULL;

                /*
                 * 更新HASH
                 * 若顶层为空，直接指向数组中对应的位置
                 */
                zpTmpDpRes_ = zRun_.p_repoVec[zRepoId]->p_dpResHash_[(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]) % zDP_HASH_SIZ];
                if (NULL == zpTmpDpRes_) {
                    zRun_.p_repoVec[zRepoId]->p_dpResHash_[(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]) % zDP_HASH_SIZ]
                        = & zRun_.p_repoVec[zRepoId]->p_dpResList_[i];
                } else {
                    while (NULL != zpTmpDpRes_->p_next) {
                        zpTmpDpRes_ = zpTmpDpRes_->p_next;
                    }
                    zpTmpDpRes_->p_next = & zRun_.p_repoVec[zRepoId]->p_dpResList_[i];
                }
            }
        }

        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    }

    /* 指针指向自身的静态数据项 */
    zRun_.p_repoVec[zRepoId]->commitVecWrap_.p_vec_ = zRun_.p_repoVec[zRepoId]->commitVec_;
    zRun_.p_repoVec[zRepoId]->commitVecWrap_.p_refData_ = zRun_.p_repoVec[zRepoId]->commitRefData_;

    zRun_.p_repoVec[zRepoId]->dpVecWrap_.p_vec_ = zRun_.p_repoVec[zRepoId]->dpVec_;
    zRun_.p_repoVec[zRepoId]->dpVecWrap_.p_refData_ = zRun_.p_repoVec[zRepoId]->dpRefData_;

    zRun_.p_repoVec[zRepoId]->p_dpCcur_ = zRun_.p_repoVec[zRepoId]->dpCcur_;

    /*
     * 源库相关信息留存
     */
    zMEM_ALLOC(zRun_.p_repoVec[zRepoId]->p_codeSyncURL, char, 1 + zSourceUrlLen);
    zMEM_ALLOC(zRun_.p_repoVec[zRepoId]->p_codeSyncBranch, char, 1 + zSourceBranchLen);
    zMEM_ALLOC(zRun_.p_repoVec[zRepoId]->p_codeSyncRefs, char, 1 + zSyncRefsLen);

    strcpy(zRun_.p_repoVec[zRepoId]->p_codeSyncURL, zpRepoMeta_->pp_fields[2]);
    strcpy(zRun_.p_repoVec[zRepoId]->p_codeSyncBranch, zpRepoMeta_->pp_fields[3]);

    sprintf(zRun_.p_repoVec[zRepoId]->p_codeSyncRefs,
            "+refs/heads/%s:refs/heads/%sXXXXXXXX",
            zRun_.p_repoVec[zRepoId]->p_codeSyncBranch,
            zRun_.p_repoVec[zRepoId]->p_codeSyncBranch);

    /* p_localRef 指向 refs 的后半段，本身不占用空间 */
    zRun_.p_repoVec[zRepoId]->p_localRef =
        zRun_.p_repoVec[zRepoId]->p_codeSyncRefs + sizeof("+refs/heads/:") - 1 + zSourceBranchLen;

    strcpy(zRun_.p_repoVec[zRepoId]->sshUserName, zpRepoMeta_->pp_fields[6]);
    strcpy(zRun_.p_repoVec[zRepoId]->sshPort, zpRepoMeta_->pp_fields[7]);

    /*
     * 内存池初始化，开头留一个指针位置，
     * 用于当内存池容量不足时，指向下一块新开辟的内存区
     */
    if (MAP_FAILED ==
            (zRun_.p_repoVec[zRepoId]->p_memPool = mmap(NULL, zMemPoolSiz, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0))) {
        zERR_RETURN_OR_EXIT(1);
    }

    void **zppPrev = zRun_.p_repoVec[zRepoId]->p_memPool;
    zppPrev[0] = NULL;
    zRun_.p_repoVec[zRepoId]->memPoolOffSet = sizeof(void *);
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zRun_.p_repoVec[zRepoId]->memLock, NULL) );

    /* 生成缓存 */
    zCacheMeta__ zMeta_;
    zMeta_.repoId = zRepoId;

    zMeta_.dataType = zDATA_TYPE_COMMIT;
    zgenerate_cache(&zMeta_);

    zMeta_.dataType = zDATA_TYPE_DP;
    zgenerate_cache(&zMeta_);

    /* 标记初始化动作已全部完成 */
    zRun_.p_repoVec[zRepoId]->initFinished = 'Y';

    /* 释放锁 */
    pthread_rwlock_unlock(& zRun_.p_repoVec[zRepoId]->rwLock);

    /*
     * 启动独立的进程负责定时拉取远程代码
     * OpenSSL 默认不是多线程安全的，使用多进程模型
     */
    if ('Y' == zNeedPull) {
        /*
         * 置为 1，指示创建新进程，而不是停止丢进程
         * 1 号进程是 init 或 systemd
         */
        zRun_.p_repoVec[zRepoId]->codeSyncPid = 1;
        zcode_fetch_mgmt(zRepoId);
    }

    /* 全局实际项目 ID 最大值调整，并通知上层调用者本项目初始化任务完成 */
    pthread_mutex_lock(zRun_.p_commLock);

    zRun_.maxRepoId = (zRepoId > zRun_.maxRepoId) ? zRepoId : zRun_.maxRepoId;

    if (NULL == zpRepoMeta_->p_taskCnt) {
        pthread_mutex_unlock(zRun_.p_commLock);
    } else {
        (* (zpRepoMeta_->p_taskCnt)) ++;
        pthread_mutex_unlock(zRun_.p_commLock);
        pthread_cond_signal(zRun_.p_commCond);
    }

    return 0;
}
#undef zERR_RETURN_OR_EXIT
#undef zFREE_SOURCE


/* 用于线程并发执行的外壳函数 */
static void *
zinit_one_repo_env_thread_wraper(void *zp) {
    /* 主程序本身启动时发生错误，退出 */
    if (0 > zinit_one_repo_env(zp, -1)) {
        exit(1);
    }

    return NULL;
}


#ifndef _Z_BSD
/*
 * 定时获取系统全局负载信息
 */
static void *
zsys_load_monitor(void *zp __attribute__ ((__unused__))) {
    FILE *zpHandler = NULL;
    _ul zTotalMem = 0,
        zAvalMem = 0;

    zCHECK_NULL_EXIT( zpHandler = fopen("/proc/meminfo", "r") );

    while(1) {
        fscanf(zpHandler, "%*s %ld %*s %*s %*ld %*s %*s %ld", &zTotalMem, &zAvalMem);
        zRun_.memLoad = 100 * (zTotalMem - zAvalMem) / zTotalMem;
        fseek(zpHandler, 0, SEEK_SET);

        zNativeUtils_.sleep(0.1);
    }

    return NULL;
}
#endif


/*
 * 负责 fetch 的子进程的启停管理
 */
static _i
zcode_fetch_mgmt(_i zRepoId) {
    _i zPid = 0;

    /*
     * [OPS: 0]
     * 1 号进程是 init 或 systemd
     * ==== 停止旧进程 ====
     */
    if (1 < zRun_.p_repoVec[zRepoId]->codeSyncPid) {
        kill(zRun_.p_repoVec[zRepoId]->codeSyncPid, SIGUSR1);
        waitpid(zRun_.p_repoVec[zRepoId]->codeSyncPid, NULL, 0);
        zRun_.p_repoVec[zRepoId]->codeSyncPid = 1;

        return 0;
    }

    /*
     * [OPS: 1]
     * ==== 启动新进程 ====
     */
    zCHECK_NEGATIVE_EXIT(zPid = fork());

    if (0 == zPid) {
        /*
         * 子进程无限循环，fetch...
         * 二进制项目场景必要：
         *     连续失败超过 10 次
         *     删除本地分支，重新拉取
         */
        _i zCnter = 0;
        while (1) {
            /* 
             * 子进程的 gitCommHandler 可以直接使用，不影响主进程 
             */
                if (0 > zLibGit_.remote_fetch(
                            zRun_.p_repoVec[zRepoId]->p_gitCommHandler,
                            zRun_.p_repoVec[zRepoId]->p_codeSyncURL,
                            & zRun_.p_repoVec[zRepoId]->p_codeSyncRefs, 1, NULL)) {
                zCnter++;

                if (10 < zCnter) {
                    zLibGit_.branch_del(
                            zRun_.p_repoVec[zRepoId]->p_gitCommHandler,
                            zRun_.p_repoVec[zRepoId]->p_localRef);
                }

                /* try clean rubbish... */
                unlink(".git/index.lock");
            } else {
                zCnter = 0;
            }

            sleep(2);
        }
    } else {
        zRun_.p_repoVec[zRepoId]->codeSyncPid = zPid;
    }

    return 0;
}



/*
 * 全量刷新：仅刷新新提交版本号列表，不刷新布署列表
 */
static _i
zrefresh_commit_cache(_i zRepoId) {
    zCacheMeta__ zMeta_ = {
        .repoId = zRepoId,
        .dataType = zDATA_TYPE_COMMIT
    };

    zgenerate_cache(&zMeta_);

    return 0;
}


/*
 * 同步更新缓存
 */
static void *
zcache_sync(void *zp __attribute__ ((__unused__))) {
    char zCommonBuf[64] = {'\0'};

zLoop:
    for (_i i = zRun_.maxRepoId; i > 0; i--) {
        if (NULL != zRun_.p_repoVec[i]
                && 'Y' == zRun_.p_repoVec[i]->initFinished) {
            /* get new revs */
            zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                    zRun_.p_repoVec[i]->p_gitCommHandler,
                    zRun_.p_repoVec[i]->p_localRef,
                    0);

            if (NULL == zpRevWalker) {
                zPRINT_ERR_EASY("");
                continue;
            } else {
                zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf, zRun_.p_repoVec[i]->p_gitCommHandler, zpRevWalker);
                zLibGit_.destroy_revwalker(zpRevWalker);
            }

            if ((NULL == zRun_.p_repoVec[i]->commitRefData_[0].p_data)
                    || (0 != strncmp(zCommonBuf, zRun_.p_repoVec[i]->commitRefData_[0].p_data, 40))) {
                /* 若能取到锁，则更新缓存；OR do nothing... */
                if (0 == pthread_rwlock_trywrlock(& zRun_.p_repoVec[i]->rwLock)) {
                    zrefresh_commit_cache(i);
                    pthread_rwlock_unlock(& zRun_.p_repoVec[i]->rwLock);
                };
            }

            /* 每次有写入内容，都需要复位 */
            zCommonBuf[0] = '\0';
        }
    }

    sleep(2);
    goto zLoop;

    /* never reach here */
    return (void *) -1;
}


/*
 * 定时调整分区表：每天尝试创建之后 10 天的分区表，并删除 30 天之前的表；
 * 以 UNIX 时间戳 / 86400 秒的结果进行数据分区，
 * 表示从 1970-01-01 00:00:00 开始的整天数，每天 0 点整作为临界
 */
static void *
zextend_pg_partition(void *zp __attribute__ ((__unused__))) {
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    char zCmdBuf[1024];

zLoop:
    /*
     * 每天（24 * 60 * 60 秒）尝试创建新日志表
     */
    sleep(86400);

    /* 尝试连接到 pgSQL server */
    while (NULL == (zpPgConnHd_ = zPgSQL_.conn(zRun_.pgConnInfo))) {
        zPRINT_ERR(0, NULL, "Connect to pgSQL failed");
        sleep(60);
    }

    /* 非紧要任务，串行执行即可 */
    _i zBaseId = time(NULL) / 86400,
       zId = 0;
    for (_i zRepoId = 0; zRepoId <= zRun_.maxRepoId; zRepoId++) {
        if (NULL == zRun_.p_repoVec[zRepoId]
                || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
            continue;
        }

        /* 创建之后 10 天的分区表 */
        for (zId = 0; zId < 10; zId ++) {
            sprintf(zCmdBuf,
                    "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                    "PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                    zRepoId, zBaseId + zId + 1, zRepoId, 86400 * (zBaseId + zId), 86400 * (zBaseId + zId + 1));

            if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zCmdBuf, zFalse))) {
                zPRINT_ERR(0, NULL, "(errno: -91) pgSQL exec failed");
                continue;
            } else {
                zPgSQL_.res_clear(zpPgResHd_, NULL);
            }
        }

        /* 清除 30 天之前的分区表 */
        for (zId = 0; zId < 10; zId ++) {
            sprintf(zCmdBuf,
                    "DROP TABLE IF EXISTS dp_log_%d_%d",
                    zRepoId, zBaseId - zId - 30);

            if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zCmdBuf, zFalse))) {
                zPRINT_ERR(0, NULL, "(errno: -91) pgSQL exec failed");
                continue;
            } else {
                zPgSQL_.res_clear(zpPgResHd_, NULL);
            }
        }
    }

    zPgSQL_.conn_clear(zpPgConnHd_);

    goto zLoop;

    /* never reach here */
    return NULL;
}


/*
 * 读取项目信息，初始化配套环境
 */
static void *
zinit_env(zPgLogin__ *zpPgLogin_) {
    char zDBPassFilePath[1024];
    struct stat zS_;

    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

    /*
     * 确保 pgSQL 密钥文件存在并合法
     */
    if (NULL == zpPgLogin_->p_passFilePath) {
        snprintf(zDBPassFilePath, 1024,
                "%s/.pgpass",
                zRun_.p_homePath);
        zpPgLogin_->p_passFilePath = zDBPassFilePath;
    }

    zCHECK_NOTZERO_EXIT( stat(zpPgLogin_->p_passFilePath, &zS_) );

    if (! S_ISREG(zS_.st_mode)) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    zCHECK_NOTZERO_EXIT( chmod(zpPgLogin_->p_passFilePath, 00600) );

    /*
     * 生成连接 pgSQL 的元信息
     */
    snprintf(zRun_.pgConnInfo, 2048,
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "sslmode=allow "
            "connect_timeout=6",
            NULL == zpPgLogin_->p_addr ? "host=" : "",
            NULL == zpPgLogin_->p_addr ? (NULL == zpPgLogin_->p_host ? zRun_.p_servPath : zpPgLogin_->p_host) : "",
            NULL == zpPgLogin_->p_addr ? "" : "hostaddr=",
            NULL == zpPgLogin_->p_addr ? "" : zpPgLogin_->p_addr,
            (NULL == zpPgLogin_->p_addr && NULL == zpPgLogin_->p_host)? "" : (NULL == zpPgLogin_->p_port ? "" : "port="),
            (NULL == zpPgLogin_->p_addr && NULL == zpPgLogin_->p_host)? "" : (NULL == zpPgLogin_->p_port ? "" : zpPgLogin_->p_port),
            "user=",
            NULL == zpPgLogin_->p_userName ? "git" : zpPgLogin_->p_userName,
            "passfile=",
            zpPgLogin_->p_passFilePath,
            "dbname=",
            NULL == zpPgLogin_->p_dbName ? "dpDB": zpPgLogin_->p_dbName);

    /*
     * 尝试连接到 pgSQL server
     */
    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zRun_.pgConnInfo))) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    /*
     * 启动时尝试创建后备的分区表
     */
    zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_,
            "CREATE TABLE IF NOT EXISTS proj_meta "
            "("
            "proj_id         int NOT NULL PRIMARY KEY,"
            "create_time     timestamp with time zone NOT NULL DEFAULT current_timestamp(0),"
            "path_on_host    varchar NOT NULL,"
            "source_url      varchar NOT NULL,"
            "source_branch   varchar NOT NULL,"
            "source_vcs_type char(1) NOT NULL,"  /* 'G': git, 'S': svn */
            "need_pull       char(1) NOT NULL,"
            "ssh_user_name   varchar NOT NULL,"
            "ssh_port        varchar NOT NULL,"
            "alias_path      varchar DEFAULT '',"  /* 最近一次成功布署指定的路径别名 */
            "last_dp_sig     varchar DEFAULT '',"  /* 最近一次成功布署的版本号 */
            "last_try_sig    varchar DEFAULT ''"  /* 最近一次尝试布署的版本号 */
            ");"

            "CREATE TABLE IF NOT EXISTS dp_log "
            "("
            "proj_id         int NOT NULL,"
            "time_stamp      bigint NOT NULL,"
            "rev_sig         char(40) NOT NULL,"  /* '\0' 不会被存入 */
            "host_ip         inet NOT NULL,"  /* postgreSQL 内置 inet 类型，用于存放 ipv4/ipv6 地址 */
            "host_res        char(1)[] NOT NULL DEFAULT '{}',"  /* 无限长度数组，默为空数组，一一对应于布署过程中的各个阶段性成功 */
            "host_err        char(1)[] NOT NULL DEFAULT '{}',"  /* 无限长度数组，默为空数组，每一位代表一种错误码 */
            "host_timespent  smallint NOT NULL DEFAULT 0,"
            "host_detail     varchar"
            ") PARTITION BY LIST (proj_id);",

            zFalse);

    if (NULL == zpPgResHd_) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    /*
     * 查询已有项目信息，之后
     * 此 pg 连接已经用完，断开连接
     */
    zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_,
            "SELECT proj_id,path_on_host,source_url,source_branch,source_vcs_type,need_pull,ssh_user_name,ssh_port FROM proj_meta",
            zTrue);
    zPgSQL_.conn_clear(zpPgConnHd_);

    if (NULL == zpPgResHd_) {
        zPRINT_ERR_EASY("");
        exit(1);
    } else {
        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zPRINT_ERR(0, NULL, "NO VALID REPO FOUND!");
            goto zMarkNotFound;
        }
    }

    zpPgRes_->taskCnt = 0;
    for (_i i = 0; i < zpPgRes_->tupleCnt; i++) {
        zpPgRes_->tupleRes_[i].p_taskCnt = & zpPgRes_->taskCnt;
        zThreadPool_.add(zinit_one_repo_env_thread_wraper, zpPgRes_->tupleRes_ + i);
    }

    pthread_mutex_lock(zRun_.p_commLock);
    while (zpPgRes_->tupleCnt > zpPgRes_->taskCnt) {
        pthread_cond_wait(zRun_.p_commCond, zRun_.p_commLock);
    }
    pthread_mutex_unlock(zRun_.p_commLock);

zMarkNotFound:
    /*
     * 清理资源占用
     * 创建新项目时，需要重新建立连接
     */
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

#ifndef _Z_BSD
    zThreadPool_.add(zsys_load_monitor, NULL);
#endif

    zThreadPool_.add(zcache_sync, NULL);

    /* 升级锁：系统本身升级时，需要排斥IP增量更新动作 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_rwlock_init(& zRun_.p_sysUpdateLock, NULL) );

    return NULL;
}
