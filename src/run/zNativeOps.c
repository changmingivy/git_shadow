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

#include "zPosixReg.h"
#include "zNativeUtils.h"
#include "zNetUtils.h"
#include "zThreadPool.h"
#include "zLibGit.h"
#include "zPgSQL.h"
#include "zRun.h"
//#include "zMd5Sum.h"

extern struct zPosixReg__ zPosixReg_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zNetUtils__ zNetUtils_;
extern struct zThreadPool__ zThreadPool_;
extern struct zLibGit__ zLibGit_;
extern struct zPgSQL__ zPgSQL_;

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

static void * zalloc_cache(size_t zSiz);
static void * zget_diff_content(void *zp);
static void * zget_file_list(void *zp);
static void zgenerate_cache(void *zp);
static void zinit_one_repo_env(char **zppRepoMeta, _i zSd);
static void zinit_env(void);

static void * zcron_ops(void *zp);

struct zNativeOps__ zNativeOps_ = {
    .get_revs = zgenerate_cache,
    .get_diff_files = zget_file_list,
    .get_diff_contents = zget_diff_content,

    .repo_init = zinit_one_repo_env,
    .repo_init_all = zinit_env,

    .alloc = zalloc_cache,

    .cron_ops = zcron_ops,
};


/*
 * 专用于项目缓存的 alloc 函数
 * 适用多线程环境，与布署动作同步开拓与释放资源
 */
static void *
zalloc_cache(size_t zSiz) {
    pthread_mutex_lock(& zpRepo_->memLock);

    /* 检测当前内存池片区剩余空间是否充裕 */
    if ((zSiz + zpRepo_->memPoolOffSet) > zMEM_POOL_SIZ) {
        /* 请求的内存不能超过单片区最大容量 */
        if (zSiz > (zMEM_POOL_SIZ - sizeof(void *))) {
            zPRINT_ERR_EASY("");
            exit(1);
        }

        /* 新增一片内存，加入内存池 */
        void *zpCur = NULL;
        zMEM_ALLOC(zpCur, char, zMEM_POOL_SIZ);

        /*
         * 首部指针位，指向内存池中的前一片区
         */
        void **zppPrev = zpCur;
        zppPrev[0] = zpRepo_->p_memPool;

        /*
         * 内存池指针更新
         */
        zpRepo_->p_memPool = zpCur;

        /*
         * 新内存片区开头的一个指针大小的空间已经被占用
         * 不能再分配，需要跳过
         */
        zpRepo_->memPoolOffSet = sizeof(void *);
    }

    /*
     * 分配内存
     */
    void *zpX = zpRepo_->p_memPool + zpRepo_->memPoolOffSet;
    zpRepo_->memPoolOffSet += zSiz;

    pthread_mutex_unlock(& zpRepo_->memLock);

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
        zpTopVecWrap_ = & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zpMeta_->dataType) {
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return NULL;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 128 + zpRepo_->pathLen + 40 + 40 + zpRepo_->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zCommonBuf,
            "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zpRepo_->p_path,
            zpRepo_->lastDpSig,
            zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zpMeta_->commitID),
            zGET_ONE_FILE_PATH(zpTopVecWrap_, zpMeta_->commitID, zpMeta_->fileID));

    FILE *zpShellRetHandler = NULL;
    zCHECK_NULL_EXIT( zpShellRetHandler = popen(zCommonBuf, "r") );

    /*
     * 读取差异内容
     * 没有下一级数据，大片读取，不再分行
     */
    zCnter = 0;
    if (0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBYTES(1448), zpShellRetHandler))) {
        zpTmpBaseData_[0] = zalloc_cache(sizeof(zBaseData__) + zBaseDataLen);
        zpTmpBaseData_[0]->dataLen = zBaseDataLen;
        memcpy(zpTmpBaseData_[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseData_[2] = zpTmpBaseData_[1] = zpTmpBaseData_[0];
        zpTmpBaseData_[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zNativeUtils_.read_hunk(zRes, zBYTES(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseData_[0] = zalloc_cache(sizeof(zBaseData__) + zBaseDataLen);
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
        zpVecWrap = zalloc_cache(sizeof(zVecWrap__));
        zpVecWrap->vecSiz = zCnter;
        zpVecWrap->p_refData_ = NULL;
        zpVecWrap->p_vec_ = zalloc_cache(zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
            zpVecWrap->p_vec_[i].iov_base = zpTmpBaseData_[2]->p_data;
            zpVecWrap->p_vec_[i].iov_len = zpTmpBaseData_[2]->dataLen;
        }
    }

    /* 数据完全生成之后，再插入到缓存结构中，保障可用性 */
    pthread_mutex_lock(& zpRepo_->commLock);
    zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zpMeta_->commitID, zpMeta_->fileID) = zpVecWrap;
    pthread_mutex_unlock(& zpRepo_->commLock);

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
    zpTmpNode_[0] = zalloc_cache(sizeof(zCacheMeta__));\
\
    zpTmpNode_[0]->lineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNode_[0]->offSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNode_[0]->p_firstChild = NULL;\
    zpTmpNode_[0]->p_left = NULL;\
    zpTmpNode_[0]->p_treeData = zalloc_cache(6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_.p_resLen[zNodeCnter]);\
    strcpy(zpTmpNode_[0]->p_treeData + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_.pp_rets[zNodeCnter]);\
\
    if (zNodeCnter == (zRegRes_.cnt - 1)) {\
        zpTmpNode_[0]->fileID = zpTmpNode_[0]->lineNum;\
        zpTmpNode_[0]->p_filePath = zalloc_cache(zBaseDataLen);\
        memcpy(zpTmpNode_[0]->p_filePath, zCommonBuf, zBaseDataLen);\
    } else {\
        zpTmpNode_[0]->fileID = -1;\
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
        zpTmpNode_[0]->p_firstChild = zalloc_cache(sizeof(zCacheMeta__));\
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
        zpTmpNode_[0]->p_treeData = zalloc_cache(6 * zpTmpNode_[0]->offSet + 10 + 1 + zRegRes_.p_resLen[zNodeCnter]);\
        strcpy(zpTmpNode_[0]->p_treeData + 6 * zpTmpNode_[0]->offSet + 10, zRegRes_.pp_rets[zNodeCnter]);\
\
        zpTmpNode_[0]->fileID = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNode_[0]->p_filePath = NULL;\
    }\
    zpTmpNode_[0]->fileID = zpTmpNode_[0]->lineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNode_[0]->p_filePath = zalloc_cache(zBaseDataLen);\
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
        zpTopVecWrap_ = & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zpMeta_->dataType) {
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return (void *) -1;
    }

    /* 计算本函数需要用到的最大 BufSiz */
    _i zMaxBufLen = 256 + zpRepo_->pathLen + 4 * 40 + zpRepo_->maxPathLen;
    char zCommonBuf[zMaxBufLen];

    /* 必须首先在 shell 命令中切换到正确的工作路径 */
    sprintf(zCommonBuf,
            "cd \"%s\" "
            "&& git diff --shortstat \"%s\" \"%s\"|grep -oP '\\d+(?=\\s*file)' "
            "&& git diff --name-only \"%s\" \"%s\"",
            zpRepo_->p_path,
            zpRepo_->lastDpSig,
            zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zpMeta_->commitID),
            zpRepo_->lastDpSig,
            zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zpMeta_->commitID));

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
                zpTmpBaseData_[0] = zalloc_cache(sizeof(zBaseData__) + zBaseDataLen);
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
            zpVecWrap = zalloc_cache(sizeof(zVecWrap__));
            zpVecWrap->vecSiz = zCnter;
            zpVecWrap->p_refData_ = zalloc_cache(zCnter * sizeof(zRefData__));
            zpVecWrap->p_vec_ = zalloc_cache(zCnter * sizeof(struct iovec));

            for (_i i = 0; i < zCnter; i++, zpTmpBaseData_[2] = zpTmpBaseData_[2]->p_next) {
                zpVecWrap->p_refData_[i].p_data = zpTmpBaseData_[2]->p_data;

                /* 转换为 JSON 字符串 */
                zVecDataLen = sprintf(zCommonBuf,
                        ",{\"fileID\":%d,\"filePath\":\"%s\"}",
                        i,
                        zpTmpBaseData_[2]->p_data);

                zpVecWrap->p_vec_[i].iov_len = zVecDataLen;
                zpVecWrap->p_vec_[i].iov_base = zalloc_cache(zVecDataLen);
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
    zpVecWrap = zalloc_cache(sizeof(zVecWrap__));
    if (NULL == zpRootNode_) {
        zpVecWrap->vecSiz = 1;

        zpVecWrap->p_refData_ = NULL;
        zpVecWrap->p_vec_ = zalloc_cache(sizeof(struct iovec));

        /* 转换为 JSON 文本 */
        zVecDataLen = sprintf(zCommonBuf,
                "[{\"fileID\":-1,\"filePath\":\"%s\"}",
                (0 == strcmp(zpRepo_->lastDpSig,
                             zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zpMeta_->commitID))) ?
                "===> 最新的已布署版本 <===" : "=> 无差异 <=");

        zpVecWrap->p_vec_[0].iov_len = zVecDataLen;
        zpVecWrap->p_vec_[0].iov_base = zalloc_cache(zVecDataLen);
        memcpy(zpVecWrap->p_vec_[0].iov_base, zCommonBuf, zVecDataLen);
    } else {
        zpVecWrap->vecSiz = zLineCnter;

        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNode_->pp_resHash = zalloc_cache(zLineCnter * sizeof(zCacheMeta__ *));

        /* 生成最终的 Tree 图 */
        zdistribute_task(zpRootNode_);

        zpVecWrap->p_refData_
            = zalloc_cache(zLineCnter * sizeof(zRefData__));
        zpVecWrap->p_vec_
            = zalloc_cache(zLineCnter * sizeof(struct iovec));

        for (_i i = 0; i < zLineCnter; i++) {
            /* 转换为 json 文本 */
            zVecDataLen = sprintf(zCommonBuf, ",{\"fileID\":%d,\"filePath\":\"%s\"}",
                    zpRootNode_->pp_resHash[i]->fileID,
                    zpRootNode_->pp_resHash[i]->p_treeData);

            zpVecWrap->p_vec_[i].iov_len = zVecDataLen;
            zpVecWrap->p_vec_[i].iov_base = zalloc_cache(zVecDataLen);
            memcpy(zpVecWrap->p_vec_[i].iov_base, zCommonBuf, zVecDataLen);

            zpVecWrap->p_refData_[i].p_data = zpRootNode_->pp_resHash[i]->p_filePath;
            zpVecWrap->p_refData_[i].p_subVecWrap_ = NULL;
        }
    }

zMarkLarge:
    /* 修饰第一项，形成 json 结构；后半个 ']' 会在网络服务中通过单独一个 send 发出 */
    ((char *) (zpVecWrap->p_vec_[0].iov_base))[0] = '[';

    /* 数据完全生成之后，再插入到缓存结构中，保障可用性 */
    pthread_mutex_lock(& zpRepo_->commLock);
    zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zpMeta_->commitID) = zpVecWrap;
    pthread_mutex_unlock(& zpRepo_->commLock);

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

    _i zErrNo = 0,
       zVecDataLen = 0,
       i = 0;

    zPgRes__ *zpPgRes_ = NULL;

    /* 计算本函数需要用到的最大 BufSiz */
    char zCommonBuf[256 + zpRepo_->pathLen + 12];

    if (zDATA_TYPE_COMMIT == zpMeta_->dataType) {
        zpTopVecWrap_ = & zpRepo_->commitVecWrap_;

        /* use: refs/remotes/origin/%sXXXXXXXX ??? */
        zGitRevWalk__ *zpRevWalker = NULL;
        sprintf(zCommonBuf, "refs/heads/%sXXXXXXXX",
                zpRepo_->p_codeSyncBranch);
        if (NULL == (zpRevWalker = zLibGit_.generate_revwalker(
                        zpRepo_->p_gitHandler,
                        zCommonBuf,
                        0))) {
            zPRINT_ERR_EASY("");
            exit(1);
        } else {
            for (i = 0; i < zCACHE_SIZ; i++) {
                zpRevSig[i] = zalloc_cache(zBYTES(44));
                if (0 < (zTimeStamp = zLibGit_.get_one_commitsig_and_timestamp(
                                zpRevSig[i],
                                zpRepo_->p_gitHandler,
                                zpRevWalker))
                        &&
                        0 != strcmp(zpRepo_->lastDpSig, zpRevSig[i])) {
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
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;

        /* 须使用 DISTINCT 关键字去重 */
        sprintf(zCommonBuf,
                "SELECT DISTINCT rev_sig, time_stamp FROM dp_log "
                "WHERE repo_id = %d ORDER BY time_stamp DESC LIMIT %d",
                zpRepo_->id,
                zCACHE_SIZ);

        if (0 == (zErrNo = zPgSQL_.exec_once(
                        zRun_.p_sysInfo_->pgConnInfo,
                        zCommonBuf,
                        &zpPgRes_))) {

            /* 存储的是实际的对象数量 */
            zpTopVecWrap_->vecSiz = (zCACHE_SIZ < zpPgRes_->tupleCnt) ? zCACHE_SIZ : zpPgRes_->tupleCnt;

            zpRevSig[0] = zalloc_cache(zBYTES(41) * zpTopVecWrap_->vecSiz);
            for (i = 0; i < zpTopVecWrap_->vecSiz; i++) {
                zpRevSig[i] = zpRevSig[0] + i * zBYTES(41);

                /*
                 * 存储 RevSig 的 SQL 数据类型是 char(40)，只会存数据正文，不存 '\0'
                 * 使用 libpq 取出来的值是 41 位，最后又会追加一个 '\0'
                 */
                strcpy(zpRevSig[i], zpPgRes_->tupleRes_[i].pp_fields[0]);
                strcpy(zTimeStampVec + 16 * i, zpPgRes_->tupleRes_[i].pp_fields[1]);
            }

            zPgSQL_.res_clear(zpPgRes_->p_pgResHd_, zpPgRes_);
        } else if (-92 == zErrNo) {
            zpTopVecWrap_->vecSiz = 0;
        } else {
            zPRINT_ERR_EASY("");
            exit(1);
        }
    } else {
        /* BUG! */
        zPRINT_ERR_EASY("");
        exit(1);
    }

    if (NULL != zpRevSig[0]) {
        for (i = 0; i < zCACHE_SIZ && NULL != zpRevSig[i]; i++) {
            /* 转换为JSON 文本 */
            zVecDataLen = sprintf(zCommonBuf,
                    ",{\"revID\":%d,\"revSig\":\"%s\",\"revTimeStamp\":\"%s\"}",
                    i,
                    zpRevSig[i],
                    zTimeStampVec + 16 * i);

            zpTopVecWrap_->p_vec_[i].iov_len = zVecDataLen;
            zpTopVecWrap_->p_vec_[i].iov_base = zalloc_cache(zVecDataLen);
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


/*
 * 源库代码同步
 */
static void
zcode_sync(_ui *zpCnter) {
    _i zErrNo = 0;

    char zSigBuf[41];
    zCacheMeta__ zMeta_ = {
        .dataType = zDATA_TYPE_COMMIT,
    };

    zErrNo = zLibGit_.remote_fetch(
                zpRepo_->p_gitHandler, zpRepo_->p_codeSyncURL,
                & zpRepo_->p_codeSyncRefs, 1,
                NULL);

    if (0 > zErrNo) {
        (*zpCnter)++;

        /*
         * 二进制项目场景必要：
         *     连续失败超过 10 次
         *     删除本地分支，重新拉取
         */
        if (10 < *zpCnter) {
            zLibGit_.branch_del(zpRepo_->p_gitHandler, zpRepo_->p_localRef);
        }

        /* try clean rubbish... */
        unlink(".git/index.lock");
    } else {
        *zpCnter = 0;

        /* get new revs */
        zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                zpRepo_->p_gitHandler,
                zpRepo_->p_localRef,
                0);

        if (NULL == zpRevWalker) {
            zPRINT_ERR_EASY("");
            return;
        } else {
            zLibGit_.get_one_commitsig_and_timestamp(
                    zSigBuf,
                    zpRepo_->p_gitHandler,
                    zpRevWalker);

            zLibGit_.destroy_revwalker(zpRevWalker);
        }

        if ((NULL == zpRepo_->commitRefData_[0].p_data)
                || (0 != strncmp(zSigBuf, zpRepo_->commitRefData_[0].p_data, 40))) {
            /*
             * 若能取到锁，则更新缓存；
             * OR do nothing...
             */
            if (0 == pthread_rwlock_trywrlock(& zpRepo_->cacheLock)) {
                zgenerate_cache(& zMeta_);
                pthread_rwlock_unlock(& zpRepo_->cacheLock);
            };
        }
    }
}


/*
 * 定时调整分区表：每天尝试创建之后 10 天的分区表，并删除 30 天之前的表；
 * 以 UNIX 时间戳 / 86400 秒的结果进行数据分区，
 * 表示从 1970-01-01 00:00:00 开始的整天数，每天 0 点整作为临界
 */
static void
zpg_partition_mgmt(_ui *zpCnter) {
    _i zBaseID = time(NULL) / 86400,
       zID = 0,
       i;

    char zCmdBuf[512];
    zCmdBuf[0] = '8';

    /* 创建之后 10 天的 dp_log 分区表 */
    for (zID = 0; zID < 10; zID++) {
        i = 1;
        i += sprintf(zCmdBuf + 1,
                "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                "PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                zpRepo_->id,
                zBaseID + zID + 1,
                zpRepo_->id,
                86400 * (zBaseID + zID),
                86400 * (zBaseID + zID + 1));

        if (0 > sendto(zpRepo_->unSd, zCmdBuf, 1 + i, MSG_NOSIGNAL,
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                zRun_.p_sysInfo_->unAddrLenMaster)) {

            /* 若失败，将计数器调至 > 86400，以重新被 cron_ops() 调用 */
            *zpCnter = 86400 + 1;
        }
    }

    /* 清除 30 天之前的 dp_log 分区表 */
    for (zID = 0; zID < 10; zID++) {
        i = 1;
        i += sprintf(zCmdBuf + 1,
                "DROP TABLE IF EXISTS dp_log_%d_%d",
                zpRepo_->id, zBaseID - zID - 30);

        if (0 > sendto(zpRepo_->unSd, zCmdBuf, 1 + i, MSG_NOSIGNAL,
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                zRun_.p_sysInfo_->unAddrLenMaster)) {

            /* 若失败，将计数器调至 > 86400，以重新被 cron_ops() 调用 */
            *zpCnter = 86400 + 1;
        }
    }
}


/*
 * 定时任务：
 *     源库代码同步管理
 *     DB 分区表管理
 */
static void *
zcron_ops(void *zp) {
    char *zpNeedPull = zp;

    _ui zCodeFetchCnter = 0,
        zDBPartitionCnter = 0;

    if ('Y' == zpNeedPull[0]) {
        while (1) {
            /* 可能会长时间阻塞，不能与关键动作使用同一把锁 */
            pthread_mutex_lock(& zpRepo_->sourceUpdateLock);
            zcode_sync(& zCodeFetchCnter);
            pthread_mutex_unlock(& zpRepo_->sourceUpdateLock);

            zDBPartitionCnter += 5;
            if (86400 < zDBPartitionCnter) {
                zDBPartitionCnter = 0;
                zpg_partition_mgmt(& zDBPartitionCnter);
            }

            sleep(5);
        }
    } else {
        while (1) {
            zDBPartitionCnter += 600;
            if (86400 < zDBPartitionCnter) {
                zDBPartitionCnter = 0;
                zpg_partition_mgmt(& zDBPartitionCnter);
            }

            sleep(600);
        }
    }

    return NULL;
}


/*
 * 参数：项目基本信息
 * @zSd 用以识别是项目载入还是项目新建，负数表示既有项目载入，否则表示新建
 * zpRepoMeta_->pp_field[i]: [0 repoID] [1 pathOnHost] [2 sourceURL] [3 sourceBranch] [4 sourceVcsType] [5 needPull]
 */
#define zFREE_SOURCE() do {\
    free(zpRepo_->p_path);\
    free(zpRepo_);\
    zpRepo_ = NULL;\
} while(0)

#define zERR_CLEAN_AND_EXIT(zResNo) do {\
    zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[-1 * zResNo]);\
    if (0 <= zSd) {  /* 新建项目失败返回，则删除路径 */\
        zNativeUtils_.path_del(zpRepo_->p_path);\
    }\
    exit(1);\
} while(0)

static void
zinit_one_repo_env(char **zppRepoMeta, _i zSd) {
    zRegInit__ zRegInit_;
    zRegRes__ zRegRes_ = { .alloc_fn = NULL };

    _s zErrNo = 0,
       zLen = 0;
    _c zNeedPull = 'N';

    char *zpOrigPath = NULL,
         zKeepValue = 0;

    zPgRes__ *zpPgRes_ = NULL;

    char zCommonBuf[zGLOB_COMMON_BUF_SIZ];

    _us zSourceUrlLen = strlen(zppRepoMeta[2]),
        zSourceBranchLen = strlen(zppRepoMeta[3]),
        zSyncRefsLen = sizeof("+refs/heads/:refs/heads/XXXXXXXX") -1 + 2 * zSourceBranchLen;

    /* 项目进程名称更改为 git_shadow: <repoID> */
    memset(zpProcName, 0, zProcNameBufLen);
    snprintf(zpProcName, zProcNameBufLen,
            "git_shadow|==> ID: %s",
            zppRepoMeta[0]);

    /* 关闭从父进程继承的除参数中的 sd 之外的所有文件描述符 */
    zNativeUtils_.close_fds(getpid(), zSd);

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMEM_C_ALLOC(zpRepo_, zRepo__, 1);

    /* 提取项目ID */
    zpRepo_->id = strtol(zppRepoMeta[0], NULL, 10);

    /*
     * 返回的 udp socket 已经做完 bind，若出错，其内部会 exit
     * 必须尽早生成，否则在启动项目 udp server 之前的错误，将无法被记录
     */
    zpRepo_->unSd = zNetUtils_.gen_serv_sd(
            NULL,
            NULL,
            zRun_.p_sysInfo_->unAddrVec_[zpRepo_->id].sun_path,
            zProtoUDP);

    /* 检查项目 ID 是否超限 */
    if (zGLOB_REPO_NUM_LIMIT <= zpRepo_->id
            || 0 >= zpRepo_->id) {
        free(zpRepo_);

        zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[32]);
        exit(1);
    }

    /* needPull */
    zNeedPull = toupper(zppRepoMeta[5][0]);

    /*
     * 提取项目绝对路径
     * 去掉 basename 部分，之后拼接出最终的服务端路径字符串
     * 服务端项目路径格式：${HOME}/`dirname $zPathOnHost`/.____DpSystem/${zRepoNo}/`basename $zPathOnHost`
     */
    zPosixReg_.init(&zRegInit_, "[^/]+[/]*$");
    zPosixReg_.match(&zRegRes_, &zRegInit_, zppRepoMeta[1]);
    zPosixReg_.free_meta(&zRegInit_);

    if (0 == zRegRes_.cnt) {
        zPosixReg_.free_res(&zRegRes_);
        free(zpRepo_);

        zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[29]);
        exit(1);
    }

    zpOrigPath = zppRepoMeta[1];
    zLen = strlen(zpOrigPath);
    zKeepValue = zpOrigPath[zLen - zRegRes_.p_resLen[0] - 1];
    zpOrigPath[zLen - zRegRes_.p_resLen[0] - 1] = '\0';

    while ('/' == zpOrigPath[0]) {  /* 去除多余的 '/' */
        zpOrigPath++;
    }

    zMEM_ALLOC(zpRepo_->p_path, char,
            sizeof("//.____DpSystem//") + zRun_.p_sysInfo_->homePathLen + zLen + 16 + zRegRes_.p_resLen[0]);

    zpRepo_->pathLen =
        sprintf(zpRepo_->p_path,
            "%s/%s/.____DpSystem/%d/%s",
            zRun_.p_sysInfo_->p_homePath,
            zpOrigPath,
            zpRepo_->id,
            zRegRes_.pp_rets[0]);

    zPosixReg_.free_res(&zRegRes_);

    zppRepoMeta[1][zLen - zRegRes_.p_resLen[0] - 1]
        = zKeepValue;  /* 恢复原始字符串，上层调用者需要使用 */

    /*
     * 取出本项目所在文件系统支持的最大路径长度
     * 用于度量 git 输出的差异文件相对路径最大可用空间
     */
    zpRepo_->maxPathLen =
        pathconf(zpRepo_->p_path, _PC_PATH_MAX);

    /*
     * 项目别名路径，由用户每次布署时指定
     * 长度限制为 maxPathLen
     */
    zMEM_ALLOC(zpRepo_->p_aliasPath, char, zpRepo_->maxPathLen);
    zpRepo_->p_aliasPath[0] = '\0';

    {////
        /*
         * ================
         *  服务端创建项目
         * ================
         */
        struct stat zS_;
        DIR *zpDIR = NULL;
        struct dirent *zpItem = NULL;
        char zPathBuf[zpRepo_->maxPathLen];
        if (0 == stat(zpRepo_->p_path, &zS_)) {
            /**
             * 若是项目新建，则不允许存在同名路径
             */
            if (0 <= zSd) {
                zFREE_SOURCE();
                zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[36]);
                exit(1);
            } else {
                if (! S_ISDIR(zS_.st_mode)) {
                    zFREE_SOURCE();
                    zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[30]);
                    exit(1);
                } else {
                    /* 全局 libgit2 Handler 初始化 */
                    if (NULL == (zpRepo_->p_gitHandler
                                = zLibGit_.env_init(zpRepo_->p_path))) {
                        zFREE_SOURCE();
                        zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[46]);
                        exit(1);
                    }

                    /*
                     * 兼容旧版本，创建空白分支
                     * 删除所有除 .git 之外的文件与目录
                     * readdir 的结果为 NULL 时，需要依 errno 判断是否出错
                     */
                    zCHECK_NULL_EXIT( zpDIR = opendir(zpRepo_->p_path) );

                    errno = 0;
                    while (NULL != (zpItem = readdir(zpDIR))) {
                        if (DT_DIR == zpItem->d_type) {
                            if (0 != strcmp(".git", zpItem->d_name)
                                    && 0 != strcmp(".", zpItem->d_name)
                                    && 0 != strcmp("..", zpItem->d_name)) {

                                snprintf(zPathBuf, zpRepo_->maxPathLen, "%s/%s",
                                        zpRepo_->p_path, zpItem->d_name);
                                if (0 != zNativeUtils_.path_del(zPathBuf)) {
                                    closedir(zpDIR);
                                    zFREE_SOURCE();
                                    zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[40]);
                                    exit(1);
                                }
                            }
                        } else {
                            snprintf(zPathBuf, zpRepo_->maxPathLen, "%s/%s",
                                    zpRepo_->p_path, zpItem->d_name);
                            if (0 != unlink(zPathBuf)) {
                                closedir(zpDIR);
                                zFREE_SOURCE();
                                zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[40]);
                                exit(1);
                            }
                        }
                    }

                    zCHECK_NOTZERO_EXIT( errno );
                    closedir(zpDIR);

                    zLibGit_.branch_del(zpRepo_->p_gitHandler,
                            "refs/heads/____baseXXXXXXXX");
                    if (0 != zLibGit_.add_and_commit(zpRepo_->p_gitHandler,
                                "refs/heads/____baseXXXXXXXX", ".", "_")) {
                        zPRINT_ERR_EASY(zRun_.p_sysInfo_->p_errVec[45]);
                        exit(1);
                    }

                    /* 兼容旧版本，不必关心执行结果 */
                    sprintf(zCommonBuf, "%sXXXXXXXX", zppRepoMeta[3]);
                    zLibGit_.branch_add(zpRepo_->p_gitHandler,
                            zCommonBuf, "HEAD", zFalse);
                }
            }
        } else {
            /*
             * git clone URL，内部会回调 git_init，
             * 生成本项目 libgit2 Handler
             * 会自动递归创建各级目录
             */
            if (0 != zLibGit_.clone(
                        &zpRepo_->p_gitHandler,
                        zppRepoMeta[2],
                        zpRepo_->p_path, NULL, zFalse)) {
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-42);
            }

            /* git config user.name _ && git config user.email _@_ */
            if (0 != zLibGit_.config_name_email(zpRepo_->p_path)) {
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-43);
            }

            /*
             * 创建 {源分支名称}XXXXXXXX 分支
             * 注：源库不能是空库，即：0 提交、0 分支
             */
            sprintf(zCommonBuf, "%sXXXXXXXX", zppRepoMeta[3]);
            if (0 != zLibGit_.branch_add(zpRepo_->p_gitHandler, zCommonBuf, "HEAD", zFalse)) {
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-44);
            }

            /*
             * 删除所有除 .git 之外的文件与目录，提交到 ____baseXXXXXXXX 分支
             * readdir 的结果为 NULL 时，需要依 errno 判断是否出错
             */
            if (NULL == (zpDIR = opendir(zpRepo_->p_path))) {
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-40);
            }

            errno = 0;
            while (NULL != (zpItem = readdir(zpDIR))) {
                if (DT_DIR == zpItem->d_type) {
                    if (0 != strcmp(".git", zpItem->d_name)
                            && 0 != strcmp(".", zpItem->d_name)
                            && 0 != strcmp("..", zpItem->d_name)) {

                        snprintf(zPathBuf, zpRepo_->maxPathLen, "%s/%s",
                                zpRepo_->p_path, zpItem->d_name);
                        if (0 != zNativeUtils_.path_del(zPathBuf)) {
                            closedir(zpDIR);
                            zFREE_SOURCE();
                            zERR_CLEAN_AND_EXIT(-40);
                        }
                    }
                } else {
                    snprintf(zPathBuf, zpRepo_->maxPathLen, "%s/%s",
                            zpRepo_->p_path, zpItem->d_name);
                    if (0 != unlink(zPathBuf)) {
                        closedir(zpDIR);
                        zFREE_SOURCE();
                        zERR_CLEAN_AND_EXIT(-40);
                    }
                }
            }

            if (0 != errno) {
                closedir(zpDIR);
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-40);
            }

            closedir(zpDIR);

            if (0 != zLibGit_.add_and_commit(zpRepo_->p_gitHandler,
                        "refs/heads/____baseXXXXXXXX", ".", "_")) {
                zFREE_SOURCE();
                zERR_CLEAN_AND_EXIT(-40);
            }
        }
    }////

    /*
     * PostgreSQL 中以 char(1) 类型存储
     * 'G' 代表 git，'S' 代表 svn
     * 历史遗留问题，实质已不支持 SVN
     */
    if ('G' != toupper(zppRepoMeta[4][0])) {
        zFREE_SOURCE();
        zERR_CLEAN_AND_EXIT(-37);
    }

    /* 通用锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->commLock, NULL) );

    /* libssh2 并发信号量，值为 1 */
    //zCHECK_PTHREAD_FUNC_EXIT( sem_init(& zpRepo_->sshSem, 0, 1) );

    /* 布署锁与布署等待锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->dpLock, NULL) );
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->dpWaitLock, NULL) );

    /* libssh2/libgit2 等布署相关的并发锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->dpSyncLock, NULL) );
    zCHECK_PTHREAD_FUNC_EXIT( pthread_cond_init(& zpRepo_->dpSyncCond, NULL) );

    /* 为每个代码库生成一把读写锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_rwlock_init(& zpRepo_->cacheLock, NULL) );
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_init(& zpRepo_->cacheLockAttr));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_setkind_np(& zpRepo_->cacheLockAttr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlock_init(& zpRepo_->cacheLock, & zpRepo_->cacheLockAttr));
    // zCHECK_PTHREAD_FUNC_EXIT(pthread_rwlockattr_destroy(& zpRepo_->cacheLockAttr));

    /* 控制源库 URL 与分支名称更新 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->sourceUpdateLock, NULL) );

    /* 内存池锁 */
    zCHECK_PTHREAD_FUNC_EXIT( pthread_mutex_init(& zpRepo_->memLock, NULL) );

    /* SQL 临时表命名序号 */
    zpRepo_->tempTableNo = 0;

    /* 指针指向自身的静态数据项 */
    zpRepo_->commitVecWrap_.p_vec_ = zpRepo_->commitVec_;
    zpRepo_->commitVecWrap_.p_refData_ = zpRepo_->commitRefData_;

    zpRepo_->dpVecWrap_.p_vec_ = zpRepo_->dpVec_;
    zpRepo_->dpVecWrap_.p_refData_ = zpRepo_->dpRefData_;

    zpRepo_->p_dpCcur_ = zpRepo_->dpCcur_;

    /* 源库相关信息留存 */
    zMEM_ALLOC(zpRepo_->p_codeSyncURL, char, 1 + zSourceUrlLen);
    zMEM_ALLOC(zpRepo_->p_codeSyncBranch, char, 1 + zSourceBranchLen);
    zMEM_ALLOC(zpRepo_->p_codeSyncRefs, char, 1 + zSyncRefsLen);

    strcpy(zpRepo_->p_codeSyncURL, zppRepoMeta[2]);
    strcpy(zpRepo_->p_codeSyncBranch, zppRepoMeta[3]);

    sprintf(zpRepo_->p_codeSyncRefs,
            "+refs/heads/%s:refs/heads/%sXXXXXXXX",
            zpRepo_->p_codeSyncBranch,
            zpRepo_->p_codeSyncBranch);

    /* p_localRef 指向 refs 的后半段，本身不占用空间 */
    zpRepo_->p_localRef =
        zpRepo_->p_codeSyncRefs + sizeof("+refs/heads/:") - 1 + zSourceBranchLen;

    strcpy(zpRepo_->sshUserName, zppRepoMeta[6]);
    strcpy(zpRepo_->sshPort, zppRepoMeta[7]);

    /*
     * 每次启动时尝试创建必要的表，
     * 按天分区（1天 == 86400秒）
     * +2 的意义: 防止恰好在临界时间添加记录导致异常
     */
    _i zBaseID = time(NULL) / 86400 + 2;

    /*
     * 新建项目时需要执行一次
     * 后续不需要再执行
     */
    if (0 <= zSd) {
        sprintf(zCommonBuf,
                "CREATE TABLE IF NOT EXISTS dp_log_%d "
                "PARTITION OF dp_log FOR VALUES IN (%d) "
                "PARTITION BY RANGE (time_stamp);"

                "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                "PARTITION OF dp_log_%d FOR VALUES FROM (MINVALUE) TO (%d);",
            zpRepo_->id, zpRepo_->id,
            zpRepo_->id, zBaseID, zpRepo_->id, 86400 * zBaseID);

        if (0 != (zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zCommonBuf, NULL))) {
            zERR_CLEAN_AND_EXIT(zErrNo);
        }
    }

    zCommonBuf[0] = '8';
    for (_i zID = 0; zID < 10; zID++) {
        zLen = 1;
        zLen += sprintf(zCommonBuf + 1,
                "CREATE TABLE IF NOT EXISTS dp_log_%d_%d "
                "PARTITION OF dp_log_%d FOR VALUES FROM (%d) TO (%d);",
                zpRepo_->id, zBaseID + zID + 1,
                zpRepo_->id, 86400 * (zBaseID + zID), 86400 * (zBaseID + zID + 1));

        sendto(zpRepo_->unSd, zCommonBuf, 1 + zLen, MSG_NOSIGNAL,
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                zRun_.p_sysInfo_->unAddrLenMaster);
    }

    /*
     * ====  提取项目元信息 ====
     * 项目新建即时生成
     * 既有项目从 DB 中提取
     */
    if (0 <= zSd) {
        zLen = 0;
        for (_i i = 0; i < 7; i++) {
            zLen += strlen(zppRepoMeta[i]);
        }

        /* 数据量不可控，另辟缓存区 */
        char zSQLBuf[256 + zLen];

        /* 新项目元数据写入 DB */
        sprintf(zSQLBuf, "INSERT INTO repo_meta "
                "(repo_id,path_on_host,source_url,source_branch,source_vcs_type,need_pull,ssh_user_name,ssh_port) "
                "VALUES ('%s','%s','%s','%s','%c','%c','%s','%s')",
                zppRepoMeta[0],
                zppRepoMeta[1],
                zppRepoMeta[2],
                zppRepoMeta[3],
                toupper(zppRepoMeta[4][0]),
                toupper(zppRepoMeta[5][0]),
                zppRepoMeta[6],
                zppRepoMeta[7]);

        if (0 == (zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zSQLBuf, NULL))) {
            time_t zCreatedTimeStamp = time(NULL);
            struct tm *zpCreatedTM_ = localtime( & zCreatedTimeStamp);
            sprintf(zpRepo_->createdTime, "%d-%d-%d %d:%d:%d",
                    zpCreatedTM_->tm_year + 1900,
                    zpCreatedTM_->tm_mon + 1,  /* Month (0-11) */
                    zpCreatedTM_->tm_mday,
                    zpCreatedTM_->tm_hour,
                    zpCreatedTM_->tm_min,
                    zpCreatedTM_->tm_sec);
        } else {
            /* 子进程中的 sd 副本需要关闭 */
            zNetUtils_.send(zSd, "{\"errNo\":-91}", sizeof("{\"errNo\":-91}") - 1);
            close(zSd);

            zERR_CLEAN_AND_EXIT(zErrNo);
        }

        /* 状态预置: repoState/lastDpSig/dpingSig */
        zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                zpRepo_->p_gitHandler,
                "refs/heads/____baseXXXXXXXX",
                0);
        if (NULL != zpRevWalker
                && 0 < zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf,
                    zpRepo_->p_gitHandler,
                    zpRevWalker)) {
            strncpy(zpRepo_->lastDpSig, zCommonBuf, 40);
            zpRepo_->lastDpSig[40] = '\0';

            strcpy(zpRepo_->dpingSig, zpRepo_->lastDpSig);

            zLibGit_.destroy_revwalker(zpRevWalker);
        } else {
            /* 子进程中的 sd 副本需要关闭 */
            zNetUtils_.send(zSd, "{\"errNo\":-47}", sizeof("{\"errNo\":-47}") - 1);
            close(zSd);

            zERR_CLEAN_AND_EXIT(-47);
        }

        zpRepo_->repoState = zCACHE_GOOD;
        zpRepo_->dpID = 0;

        /* 子进程中的 sd 副本需要关闭 */
        zNetUtils_.send(zSd, "{\"errNo\":0}", sizeof("{\"errNo\":0}") - 1);
        close(zSd);
    } else {
        snprintf(zCommonBuf, zGLOB_COMMON_BUF_SIZ,
                "SELECT create_time,alias_path FROM repo_meta WHERE repo_id = %d",
                zpRepo_->id);

        if (0 == (zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zCommonBuf, &zpPgRes_))) {
            /* copy... */
            snprintf(zpRepo_->createdTime, 24, "%s",
                    zpPgRes_->tupleRes_[0].pp_fields[0]);

            snprintf(zpRepo_->p_aliasPath, zpRepo_->maxPathLen, "%s",
                    zpPgRes_->tupleRes_[0].pp_fields[1]);

            /* clean... */
            zPgSQL_.res_clear(zpPgRes_->p_pgResHd_, zpPgRes_);
        } else {
            zERR_CLEAN_AND_EXIT(zErrNo);
        }

        /**
         * 获取最近一次成功布署的版本号
         * lastDpSig
         * dpID
         */
        sprintf(zCommonBuf,
                "SELECT last_success_sig,last_dp_sig,last_dp_id,last_dp_ts FROM repo_meta "
                "WHERE repo_id = %d",
                zpRepo_->id);
        if (0 == (zErrNo = zPgSQL_.exec_once(
                        zRun_.p_sysInfo_->pgConnInfo,
                        zCommonBuf,
                        &zpPgRes_))) {
            strncpy(zpRepo_->lastDpSig, zpPgRes_->tupleRes_[0].pp_fields[0], 40);
            zpRepo_->lastDpSig[40] = '\0';

            strncpy(zpRepo_->dpingSig, zpPgRes_->tupleRes_[0].pp_fields[1], 40);
            zpRepo_->dpingSig[40] = '\0';

            /* 比较最近一次尝试布署的版本号与最近一次成功布署的版本号是否相同 */
            if (0 == strcmp(zpRepo_->dpingSig, zpRepo_->lastDpSig)) {
                zpRepo_->repoState = zCACHE_GOOD;
            } else {
                zpRepo_->repoState = zCACHE_DAMAGED;
            }

            zpRepo_->dpID = 1 + strtol(zpPgRes_->tupleRes_[0].pp_fields[2], NULL, 10);
            zpRepo_->dpBaseTimeStamp = strtol(zpPgRes_->tupleRes_[0].pp_fields[3], NULL, 10);

            /* clean... */
            zPgSQL_.res_clear(zpPgRes_->p_pgResHd_, zpPgRes_);
        } else if (-92 == zErrNo) {
            zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                    zpRepo_->p_gitHandler,
                    "refs/heads/____baseXXXXXXXX",
                    0);
            if (NULL != zpRevWalker
                    && 0 < zLibGit_.get_one_commitsig_and_timestamp(
                        zCommonBuf,
                        zpRepo_->p_gitHandler,
                        zpRevWalker)) {
                strncpy(zpRepo_->lastDpSig, zCommonBuf, 40);
                zpRepo_->lastDpSig[40] = '\0';

                zLibGit_.destroy_revwalker(zpRevWalker);
            } else {
                zERR_CLEAN_AND_EXIT(-47);
            }

            strcpy(zpRepo_->dpingSig, zpRepo_->lastDpSig);

            zpRepo_->repoState = zCACHE_GOOD;
            zpRepo_->dpID = 0;
            zpRepo_->dpBaseTimeStamp = 0;
        } else {
            zERR_CLEAN_AND_EXIT(zErrNo);
        }

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
                "WHERE repo_id = %d AND rev_sig = '%s' AND time_stamp = %ld",
                zpRepo_->id,
                zpRepo_->dpingSig,
                zpRepo_->dpBaseTimeStamp);

        if (0 == (zErrNo = zPgSQL_.exec_once(
                        zRun_.p_sysInfo_->pgConnInfo,
                        zCommonBuf,
                        &zpPgRes_))) {

            zMEM_C_ALLOC(zpRepo_->p_dpResList_, zDpRes__, zpPgRes_->tupleCnt);
            // memset(zpRepo_->p_dpResHash_, 0, zDP_HASH_SIZ * sizeof(zDpRes__ *));

            /* needed by zDpOps_.show_dp_process */
            zpRepo_->totalHost
                = zpRepo_->dpTotalTask
                = zpRepo_->dpTaskFinCnt
                = zpPgRes_->tupleCnt;
            zpRepo_->resType = 0;

            for (_i i = 0; i < zpPgRes_->tupleCnt; i++) {
                /* 检测是否存在重复IP */
                if (0 != zpRepo_->p_dpResList_[i].clientAddr[0]
                        || 0 != zpRepo_->p_dpResList_[i].clientAddr[1]) {
                    zpRepo_->totalHost--;
                    continue;
                }


                /* 线性链表斌值；转换字符串格式 IP 为 _ull 型 */
                if (0 != zCONVERT_IPSTR_TO_NUM(zpPgRes_->tupleRes_[i].pp_fields[0],
                            zpRepo_->p_dpResList_[i].clientAddr)) {
                    zERR_CLEAN_AND_EXIT(-18);
                }

                /* 恢复上一次布署的 resState 与全局 resType */
                if ('1' == zpPgRes_->tupleRes_[i].pp_fields[4][0]) {
                    zSET_BIT(zpRepo_->p_dpResList_[i].resState, 4);
                    zSET_BIT(zpRepo_->p_dpResList_[i].resState, 3);
                    zSET_BIT(zpRepo_->p_dpResList_[i].resState, 2);
                    zSET_BIT(zpRepo_->p_dpResList_[i].resState, 1);

                    zpRepo_->p_dpResList_[i].errState = 0;
                } else {
                    /* 布署环节：未成功即是失败 */
                    zSET_BIT(zpRepo_->resType, 2);

                    /* 非必要，无需恢复... */
                    // if ('1' == zpPgRes_->tupleRes_[i].pp_fields[3][0]) {
                    //     zSET_BIT(zpRepo_->p_dpResList_[i].resState, 3);
                    // } else if ('1' == zpPgRes_->tupleRes_[i].pp_fields[2][0]) {
                    //     zSET_BIT(zpRepo_->p_dpResList_[i].resState, 2);
                    // }

                    /* 目标机初始化环节：未成功即是失败 */
                    if ('1' == zpPgRes_->tupleRes_[i].pp_fields[1][0]) {
                        zSET_BIT(zpRepo_->p_dpResList_[i].resState, 1);
                    } else {
                        zSET_BIT(zpRepo_->resType, 1);
                    }

                    /* 恢复上一次布署的 errState */
                    for (_i j = 5; j < 17; j++) {
                        if ('1' == zpPgRes_->tupleRes_[i].pp_fields[j][0]) {
                            zSET_BIT(zpRepo_->p_dpResList_[i].errState, j - 4);
                            break;
                        }
                    }

                    if (0 == zpRepo_->p_dpResList_[i].errState) {
                        /* 若未捕获到错误，一律置为服务端错误类别 */
                        zSET_BIT(zpRepo_->p_dpResList_[i].errState, 1);
                    }

                    /* 错误详情 */
                    strncpy(zpRepo_->p_dpResList_[i].errMsg, zpPgRes_->tupleRes_[i].pp_fields[17], 255);
                    zpRepo_->p_dpResList_[i].errMsg[255] = '\0';
                }

                /* 使用 calloc 分配的清零空间，此项无需复位 */
                // zpRepo_->p_dpResList_[i].p_next = NULL;

                /*
                 * 启动时不再执行！以确保系统组件升级后，重启生效
                 * 更新HASH，若顶层为空，直接指向数组中对应的位置
                 */
                // zDpRes__ *zpTmpDpRes_ = zpRepo_->p_dpResHash_[(zpRepo_->p_dpResList_[i].clientAddr[0]) % zDP_HASH_SIZ];
                // if (NULL == zpTmpDpRes_) {
                //     zpRepo_->p_dpResHash_[(zpRepo_->p_dpResList_[i].clientAddr[0]) % zDP_HASH_SIZ]
                //         = & zpRepo_->p_dpResList_[i];
                // } else {
                //     while (NULL != zpTmpDpRes_->p_next) {
                //         zpTmpDpRes_ = zpTmpDpRes_->p_next;
                //     }
                //     zpTmpDpRes_->p_next = & zpRepo_->p_dpResList_[i];
                // }
            }

            zPgSQL_.res_clear(zpPgRes_->p_pgResHd_, zpPgRes_);
        } else if (-92 == zErrNo) {
            // do nothing...
        } else {
            zPRINT_ERR_EASY("");
            exit(1);
        }
    }

    /*
     * 内存池初始化，开头留一个指针位置，
     * 用于当内存池容量不足时，指向下一块新开辟的内存区
     */
    if (NULL == (zpRepo_->p_memPool = malloc(zMEM_POOL_SIZ))) {
        zERR_CLEAN_AND_EXIT(-126);
    }

    void **zppPrev = zpRepo_->p_memPool;
    zppPrev[0] = NULL;
    zpRepo_->memPoolOffSet = sizeof(void *);

    /* cacheID 初始化 */
    zpRepo_->cacheID = 0;

    /* 生成缓存 */
    zCacheMeta__ zMeta_;

    zMeta_.dataType = zDATA_TYPE_COMMIT;
    zgenerate_cache(&zMeta_);

    zMeta_.dataType = zDATA_TYPE_DP;
    zgenerate_cache(&zMeta_);

    /*
     * 项目进程线程池初始化
     * 常备线程数量：4
     */
    zThreadPool_.init(4, -1);

    /* 启动定时任务 */
    zThreadPool_.add(zcron_ops, ('Y' == zNeedPull) ? "Y" : "N");

    /* 释放项目进程的副本 */
    free(zRun_.p_sysInfo_->pp_repoMetaVec[zpRepo_->id]);

    /*
     * 只运行于项目进程
     * 服务器内部使用的基于 PF_UNIX 的 UDP 服务器
     */
    zRun_.p_sysInfo_->udp_daemon(& zpRepo_->unSd);

    /*
     * NEVER! REACH! HERE!
     */
    exit(1);
}


/*
 * 读取项目信息，初始化配套环境
 */
#define zUN_PATH_SIZ\
        sizeof(struct sockaddr_un)-((size_t) (& ((struct sockaddr_un*) 0)->sun_path))
static void
zinit_env(void) {
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

    pid_t zPid = -1;
    _i zRepoID = -1;

    char *zpData = NULL;
    _i zLen = 0;
    _i j = 0;

    /*
     * 尝试连接到 pgSQL server
     */
    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zRun_.p_sysInfo_->pgConnInfo))) {
        zPRINT_ERR_EASY("conn to postgreSQL failed");
        exit(1);
    }

    /*
     * 启动时尝试创建基础表
     */
    zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_,
            "CREATE TABLE IF NOT EXISTS repo_meta "
            "("
            "repo_id               int NOT NULL PRIMARY KEY,"
            "create_time           timestamp with time zone NOT NULL DEFAULT current_timestamp(0),"
            "path_on_host          varchar NOT NULL,"
            "source_url            varchar NOT NULL,"
            "source_branch         varchar NOT NULL,"
            "source_vcs_type       char(1) NOT NULL,"  /* 'G': git, 'S': svn */
            "need_pull             char(1) NOT NULL,"
            "ssh_user_name         varchar NOT NULL,"
            "ssh_port              varchar NOT NULL,"
            "alias_path            varchar DEFAULT '',"  /* 最近一次成功布署指定的路径别名 */
            "last_success_sig      varchar DEFAULT '',"  /* 最近一次成功布署的版本号 */
            "last_dp_sig           varchar DEFAULT '',"  /* 最近一次尝试布署的版本号 */
            "last_dp_ts            bigint DEFAULT 0,"  /* 最近一次尝试布署的时间戳 */
            "last_dp_id            bigint DEFAULT 0"  /* 最近一次尝试布署的 dpID */
            ");"

            "CREATE TABLE IF       NOT EXISTS dp_log "
            "("
            "repo_id               int NOT NULL,"
            "dp_id                 bigint NOT NULL,"
            "time_stamp            bigint NOT NULL,"
            "rev_sig               char(40) NOT NULL,"  /* '\0' 不会被存入 */
            "host_ip               inet NOT NULL,"  /* postgreSQL 内置 inet 类型，用于存放 ipv4/ipv6 地址 */
            "host_res              char(1)[] NOT NULL DEFAULT '{}',"  /* 无限长度数组，默为空数组，一一对应于布署过程中的各个阶段性成功 */
            "host_err              char(1)[] NOT NULL DEFAULT '{}',"  /* 无限长度数组，默为空数组，每一位代表一种错误码 */
            "host_timespent        smallint NOT NULL DEFAULT 0,"
            "host_detail           varchar"
            ") PARTITION BY LIST (repo_id);",

            zFalse);

    if (NULL == zpPgResHd_) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    /* 查询已有项目信息 */
    zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_,
            "SELECT repo_id,path_on_host,source_url,source_branch,source_vcs_type,need_pull,ssh_user_name,ssh_port "
            "FROM repo_meta",
            zTrue);

    zPgSQL_.conn_clear(zpPgConnHd_);

    if (NULL == zpPgResHd_) {
        zPRINT_ERR_EASY("");
        exit(1);
    } else {
        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zPRINT_ERR_EASY("NO VALID REPO FOUND!");
        } else {
            for (_i i = 0; i < zpPgRes_->tupleCnt; i++) {
                zRepoID = strtol(zpPgRes_->tupleRes_[i].pp_fields[0], NULL, 10);
                zLen = zpPgRes_->fieldCnt;

                for (j = 0; j < zpPgRes_->fieldCnt; j++) {
                    zLen += strlen(zpPgRes_->tupleRes_[i].pp_fields[j]);
                }

                zMEM_ALLOC(zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID], char, zpPgRes_->fieldCnt * sizeof(void *) + zLen);
                zpData = (char *) (zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID] + zpPgRes_->fieldCnt);

                zLen = 0;
                for (j = 0; j < zpPgRes_->fieldCnt; j++) {
                    zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID][j] = zpData + zLen;

                    zLen += sprintf(zpData + zLen, "%s", zpPgRes_->tupleRes_[i].pp_fields[j]);
                    zLen++;
                }

                zCHECK_NEGATIVE_EXIT( zPid = fork() );

                if (0 == zPid) {
                    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
                    zinit_one_repo_env(zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID], -1);
                } else {
                    //pthread_mutex_lock(zRun_.p_commLock);
                    zRun_.p_sysInfo_->repoPidVec[zRepoID] = zPid;
                    //pthread_mutex_unlock(zRun_.p_commLock);
                }
            }
        }

        /* 每个子进程均有副本，主进程可以释放资源 */
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    }
}


#undef zUN_PATH_SIZ
#undef zERR_CLEAN_AND_EXIT
#undef zFREE_SOURCE
