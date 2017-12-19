#include "zDpOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#define cJSON_V(zpJRoot, zpValueName) cJSON_GetObjectItemCaseSensitive((zpJRoot), (zpValueName))

extern struct zNetUtils__ zNetUtils_;
extern struct zNativeUtils__ zNativeUtils_;

extern struct zThreadPool__ zThreadPool_;
extern struct zPosixReg__ zPosixReg_;
extern struct zPgSQL__ zPgSQL_;

extern struct zLibSsh__ zLibSsh_;
extern struct zLibGit__ zLibGit_;

extern struct zNativeOps__ zNativeOps_;
extern struct zRun__ zRun_;

static _i zadd_repo(cJSON *zpJRoot, _i zSd);
static _i zprint_record(cJSON *zpJRoot, _i zSd);
static _i zprint_diff_files(cJSON *zpJRoot, _i zSd);
static _i zprint_diff_content(cJSON *zpJRoot, _i zSd);
static _i zspec_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__)));
static _i zbatch_deploy(cJSON *zpJRoot, _i zSd);
static _i zprint_dp_process(cJSON *zpJRoot, _i zSd);
static _i zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__)));
static _i zreq_file(cJSON *zpJRoot, _i zSd);
static _i zpang(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd);
static _i zglob_res_confirm(cJSON *zpJRoot, _i zSd);

static _i zsys_update(cJSON *zpJRoot, _i zSd);
static _i zsource_branch_update(cJSON *zpJRoot, _i zSd);

/*
 * Public Interface
 */
struct zDpOps__ zDpOps_ = {
    .show_dp_process = zprint_dp_process,

    .print_revs = zprint_record,
    .print_diff_files = zprint_diff_files,
    .print_diff_contents = zprint_diff_content,

    .creat = zadd_repo,

    .dp = zbatch_deploy,
    .req_dp = zspec_deploy,

    .glob_res_confirm = zglob_res_confirm,
    .state_confirm = zstate_confirm,

    .req_file = zreq_file,

    .pang = zpang,

    .sys_update = zsys_update,
    .SB_update = zsource_branch_update,
};


/*
 * 9：打印版本号列表或布署记录
 */
static _i
zprint_record(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;

    _i zRepoId = -1,
       zDataType = -1;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zRepoId]
            || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    zpJ = cJSON_V(zpJRoot, "DataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zDataType = zpJ->valueint;

    if (0 != pthread_rwlock_tryrdlock(& zRun_.p_repoVec[zRepoId]->rwLock)) {
        zPrint_Err_Easy("");
        return -11;
    };

    /* 数据类型判断 */
    if (zIsCommitDataType == zDataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zRepoId]->commitVecWrap_;
    } else if (zIsDpDataType == zDataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zRepoId]->dpVecWrap_;
    } else {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zRepoId]->rwLock);
        zPrint_Err_Easy("");
        return -10;
    }

    /*
     * 版本号列表，容量固定
     * 最大为 IOV_MAX
     */
    if (0 < zpTopVecWrap_->vecSiz) {
        /*
         * json 前缀
         */
        char zJsonPrefix[sizeof("{\"ErrNo\":0,\"CacheId\":%ld,\"data\":") + 16];
        _i zLen = sprintf(zJsonPrefix,
                "{\"ErrNo\":0,\"CacheId\":%ld,\"data\":",
                zRun_.p_repoVec[zRepoId]->cacheId);

        zNetUtils_.send_nosignal(zSd, zJsonPrefix, zLen);

        /*
         * 正文
         */
        zNetUtils_.sendmsg(zSd,
                zpTopVecWrap_->p_vec_,
                zpTopVecWrap_->vecSiz,
                0, NULL, zIpTypeNone);

        /*
         * json 后缀
         */
        zNetUtils_.send_nosignal(zSd, "]}", sizeof("]}") - 1);
    } else {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zRepoId]->rwLock);
        zPrint_Err_Easy("");
        return -70;
    }

    pthread_rwlock_unlock(& zRun_.p_repoVec[zRepoId]->rwLock);
    return 0;
}


/*
 * 10：显示差异文件路径列表
 */
static _i
zprint_diff_files(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zVecWrap__ zSendVecWrap_;

    zCacheMeta__ zMeta_ = {
        .repoId = -1,
        .cacheId = -1,
        .dataType = -1,
        .commitId = -1
    };

    _i zSplitCnt = -1;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zMeta_.repoId]
            || 'Y' != zRun_.p_repoVec[zMeta_.repoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    /*
     * 若上一次布署结果失败或正在布署过程中
     * 不允许查看文件差异内容
     */
    if (zCacheDamaged == zRun_.p_repoVec[zMeta_.repoId]->repoState) {
        zPrint_Err_Easy("");
        return -13;
    }

    zpJ = cJSON_V(zpJRoot, "DataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "RevId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.commitId = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "CacheId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.cacheId = zpJ->valueint;

    if (zIsCommitDataType == zMeta_.dataType) {
        zpTopVecWrap_= & zRun_.p_repoVec[zMeta_.repoId]->commitVecWrap_;
    } else if (zIsDpDataType == zMeta_.dataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zMeta_.repoId]->dpVecWrap_;
    } else {
        zPrint_Err_Easy("");
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock)) {
        zPrint_Err_Easy("");
        return -11;
    }

    /* CHECK: cacheId */
    if (zMeta_.cacheId != zRun_.p_repoVec[zMeta_.repoId]->cacheId) {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -8;
    }

    /* CHECK: commitId */
    if ((0 > zMeta_.commitId)
            || ((zCacheSiz - 1) < zMeta_.commitId)
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_data)) {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -3;
    }

    pthread_mutex_lock(& zRun_.commonLock);
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId) = (void *) 1;
        pthread_mutex_unlock(& zRun_.commonLock);

        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
            zPrint_Err_Easy("");
            return -71;
        }
    } else if ((void *) 1 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -11;
    } else if ((void *) -1 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        /* 无差异 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -71;
    } else {
        pthread_mutex_unlock(& zRun_.commonLock);
    }

    /*
     * send msg
     */
    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->p_vec_;
    zSplitCnt = (zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->vecSiz - 1) / zSendUnitSiz  + 1;

    /*
     * json 前缀
     */
    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0,\"data\":",
            sizeof("{\"ErrNo\":0,\"data\":") - 1);

    /*
     * 正文
     * 除最后一个分片之外，其余的分片大小都是 zSendUnitSiz
     */
    zSendVecWrap_.vecSiz = zSendUnitSiz;
    for (_i i = zSplitCnt; i > 1; i--) {
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

    /* 最后一个分片可能不足 zSendUnitSiz，需要单独计算 */
    zSendVecWrap_.vecSiz = (zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_->vecSiz - 1) % zSendUnitSiz + 1;
    zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);

    /*
     * json 后缀
     */
    zNetUtils_.send_nosignal(zSd, "]}", sizeof("]}") - 1);

    pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
    return 0;
}


/*
 * 11：显示差异文件内容
 */
static _i
zprint_diff_content(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zVecWrap__ zSendVecWrap_;

    _i zSplitCnt = -1;

    zCacheMeta__ zMeta_ = {
        .repoId = -1,
        .commitId = -1,
        .fileId = -1,
        .cacheId = -1,
        .dataType = -1
    };

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zMeta_.repoId] || 'Y' != zRun_.p_repoVec[zMeta_.repoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    /*
     * 若上一次布署结果失败或正在布署过程中
     * 不允许查看文件差异内容
     */
    if (zCacheDamaged == zRun_.p_repoVec[zMeta_.repoId]->repoState) {
        zPrint_Err_Easy("");
        return -13;
    }

    zpJ = cJSON_V(zpJRoot, "DataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "RevId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.commitId = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "FileId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.fileId = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "CacheId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zMeta_.cacheId = zpJ->valueint;

    if (zIsCommitDataType == zMeta_.dataType) {
        zpTopVecWrap_= & zRun_.p_repoVec[zMeta_.repoId]->commitVecWrap_;
    } else if (zIsDpDataType == zMeta_.dataType) {
        zpTopVecWrap_= & zRun_.p_repoVec[zMeta_.repoId]->dpVecWrap_;
    } else {
        zPrint_Err_Easy("");
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock)) {
        zPrint_Err_Easy("");
        return -11;
    };

    if (zMeta_.cacheId != zRun_.p_repoVec[zMeta_.repoId]->cacheId) {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -8;
    }

    if ((0 > zMeta_.commitId)\
            || ((zCacheSiz - 1) < zMeta_.commitId)\
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_data)) {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -3;
    }

    pthread_mutex_lock(& zRun_.commonLock);
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId) = (void *) 1;
        pthread_mutex_unlock(& zRun_.commonLock);

        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
            zPrint_Err_Easy("");
            return -71;
        }
    } else if ((void *) 1 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -11;
    } else if ((void *) -1 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        /* 无差异 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -71;
    } else {
        pthread_mutex_unlock(& zRun_.commonLock);
    }

    if ((0 > zMeta_.fileId)
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_)
            || ((zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_->vecSiz - 1) < zMeta_.fileId)) {
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        return -4;\
    }\

    pthread_mutex_lock(& zRun_.commonLock);
    if (NULL == zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)) {
        zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId) = (void *) 1;
        pthread_mutex_unlock(& zRun_.commonLock);

        if ((void *) -1 == zNativeOps_.get_diff_contents(&zMeta_)) {
            pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
            zPrint_Err_Easy("");
            return -72;
        }
    } else if ((void *) 1 == zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -11;
    } else if ((void *) -1 == zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)) {
        /* 无差异 */
        pthread_mutex_unlock(& zRun_.commonLock);
        pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
        zPrint_Err_Easy("");
        return -72;
    } else {
        pthread_mutex_unlock(& zRun_.commonLock);
    }

    /*
     * send msg
     */
    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->p_vec_;
    zSplitCnt = (zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->vecSiz - 1) / zSendUnitSiz  + 1;

    /*
     * json 前缀:
     * 差异内容的 data 是纯文本，没有 json 结构
     * 此处添加 data 对应的二维 json
     */
    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0,\"data\":[{\"content\":\"",
            sizeof("{\"ErrNo\":0,\"data\":[{\"content\":\"") - 1);

    /*
     * 正文
     * 除最后一个分片之外，其余的分片大小都是 zSendUnitSiz
     */
    zSendVecWrap_.vecSiz = zSendUnitSiz;
    for (_i i = zSplitCnt; i > 1; i--) {
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

    /* 最后一个分片可能不足 zSendUnitSiz，需要单独计算 */
    zSendVecWrap_.vecSiz = (zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->vecSiz - 1) % zSendUnitSiz + 1;
    zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);

    /*
     * json 后缀，此处需要配对一个引号与大括号
     */
    zNetUtils_.send_nosignal(zSd, "\"}]}", sizeof("\"}]}") - 1);

    pthread_rwlock_unlock(& zRun_.p_repoVec[zMeta_.repoId]->rwLock);
    return 0;
}

////////// ////////// ////////// //////////
// =+=> 以上是'读'操作，以下是'写'操作 <=+=
////////// ////////// ////////// //////////

/*
 * 1：创建新项目
 */
static _i
zadd_repo(cJSON *zpJRoot, _i zSd) {
    _i zErrNo = 0;
    char *zpProjInfo[8] = { NULL };  /* 顺序固定的元信息 */

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zpProjInfo[0] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "PathOnHost");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zpProjInfo[1] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "NeedPull");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zpProjInfo[5] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "SSHUserName");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    if (255 < strlen(zpJ->valuestring)) {
        zErrNo = -31;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zpProjInfo[6] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "SSHPort");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }

    if (5 < strlen(zpJ->valuestring)) {
        zErrNo = -39;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zpProjInfo[7] = zpJ->valuestring;

    if ('Y' == toupper(zpProjInfo[5][0])) {
        zpJ = cJSON_V(zpJRoot, "SourceUrl");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zErrNo = -34;
            zPrint_Err_Easy("");
            goto zEndMark;
        }
        zpProjInfo[2] = zpJ->valuestring;

        zpJ = cJSON_V(zpJRoot, "SourceBranch");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zErrNo = -34;
            zPrint_Err_Easy("");
            goto zEndMark;
        }
        zpProjInfo[3] = zpJ->valuestring;

        zpJ = cJSON_V(zpJRoot, "SourceVcsType");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zErrNo = -34;
            zPrint_Err_Easy("");
            goto zEndMark;
        }
        zpProjInfo[4] = zpJ->valuestring;
    } else if ('N' == toupper(zpProjInfo[5][0])) {
        zpProjInfo[2] = "";
        zpProjInfo[3] = "";
        zpProjInfo[4] = "Git";
    } else {
        zErrNo = -34;
        zPrint_Err_Easy("");
        goto zEndMark;
    }

    /* DO creating... */
    zPgResTuple__ zRepoMeta_ = {
        .p_taskCnt = NULL,
        .pp_fields = zpProjInfo
    };

    if (0 == (zErrNo = zNativeOps_.proj_init(&zRepoMeta_, zSd))) {
        _i zRepoId = strtol(zRepoMeta_.pp_fields[0], NULL, 10);
        /* 新项目元数据写入 DB */
        char zCommonBuf[4096] = {'\0'};
        snprintf(zCommonBuf, 4096, "INSERT INTO proj_meta "
                "(proj_id,path_on_host,source_url,source_branch,source_vcs_type,need_pull,ssh_user_name,ssh_port) "
                "VALUES ('%s','%s','%s','%s','%c','%c','%s','%s')",
                zRepoMeta_.pp_fields[0],
                zRepoMeta_.pp_fields[1],
                zRepoMeta_.pp_fields[2],
                zRepoMeta_.pp_fields[3],
                toupper(zRepoMeta_.pp_fields[4][0]),
                toupper(zRepoMeta_.pp_fields[5][0]),
                zRepoMeta_.pp_fields[6],
                zRepoMeta_.pp_fields[7]);

        zPgResHd__ *zpPgResHd_ = zPgSQL_.exec(
                zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                zCommonBuf,
                zFalse);
        if (NULL == zpPgResHd_) {
            /*
             * 新建立的连接，不必尝试 reset
             */
            zPgSQL_.res_clear(zpPgResHd_, NULL);

            zErrNo = -91;
            zPrint_Err_Easy("");
            goto zEndMark;
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);

            /*
             * 既有项目，直接从DB中提取提取项目创建时间
             * 项目新建时，由于项目创建时间属于参考类数据，直接使用当前时间戳即可
             */
            time_t zCreatedTimeStamp = time(NULL);
            struct tm *zpCreatedTM_ = localtime( & zCreatedTimeStamp);
            sprintf(zRun_.p_repoVec[strtol(zRepoMeta_.pp_fields[0], NULL, 10)]->createdTime, "%d-%d-%d %d:%d:%d",
                    zpCreatedTM_->tm_year + 1900,
                    zpCreatedTM_->tm_mon + 1,  /* Month (0-11) */
                    zpCreatedTM_->tm_mday,
                    zpCreatedTM_->tm_hour,
                    zpCreatedTM_->tm_min,
                    zpCreatedTM_->tm_sec);
        }

        /* 状态预置: repoState/lastDpSig/dpingSig */
        zRun_.p_repoVec[zRepoId]->repoState = zCacheGood;

        zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                zRun_.p_repoVec[zRepoId]->p_gitRepoHandler,
                "refs/heads/____baseXXXXXXXX",
                0);
        if (NULL != zpRevWalker
                && 0 < zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf,
                    zRun_.p_repoVec[zRepoId]->p_gitRepoHandler,
                    zpRevWalker)) {
            strncpy(zRun_.p_repoVec[zRepoId]->lastDpSig, zCommonBuf, 40);
            zRun_.p_repoVec[zRepoId]->lastDpSig[40] = '\0';

            strcpy(zRun_.p_repoVec[zRepoId]->dpingSig, zRun_.p_repoVec[zRepoId]->lastDpSig);

            zLibGit_.destroy_revwalker(zpRevWalker);
        } else {
            zPrint_Err_Easy("");
            exit(1);
        }

        zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1);
    }

zEndMark:
    return zErrNo;
}


// static void *
// zssh_ccur(void  *zp) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp;
//
//     zLibSsh_.exec(zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort,
//             zpDpCcur_->p_cmd,
//             zpDpCcur_->p_userName, zpDpCcur_->p_pubKeyPath, zpDpCcur_->p_privateKeyPath, zpDpCcur_->p_passWd, zpDpCcur_->authType,
//             zpDpCcur_->p_remoteOutPutBuf, zpDpCcur_->remoteOutPutBufSiz,
//             zpDpCcur_->p_ccurLock,
//             zErrBuf);
//
//     pthread_mutex_lock(zpDpCcur_->p_ccurLock);
//     (* (zpDpCcur_->p_taskCnt))++;
//     pthread_mutex_unlock(zpDpCcur_->p_ccurLock);
//     pthread_cond_signal(zpDpCcur_->p_ccurCond);
//
//     return NULL;
// };


/* 简化参数版函数 */
static _i
zssh_exec_simple(const char *zpSSHUserName,
        char *zpHostAddr, char *zpSSHPort,
        char *zpCmd,
        pthread_mutex_t *zpCcurLock, char *zpErrBufOUT) {

    return zLibSsh_.exec(
            zpHostAddr,
            zpSSHPort,
            zpCmd,
            zpSSHUserName,
            zRun_.p_SSHPubKeyPath,
            zRun_.p_SSHPrvKeyPath,
            NULL,
            zPubKeyAuth,
            NULL,
            0,
            zpCcurLock,
            zpErrBufOUT);
}


/* 简化参数版函数 */
// static void *
// zssh_ccur_simple(void  *zp) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp;
//
//     zssh_exec_simple(
//             zpDpCcur_->p_userName,
//             zpDpCcur_->p_hostIpStrAddr,
//             zpDpCcur_->p_hostServPort,
//             zpDpCcur_->p_cmd,
//             zpDpCcur_->p_ccurLock,
//             zErrBuf);
//
//     pthread_mutex_lock(zpDpCcur_->p_ccurLock);
//     (* (zpDpCcur_->p_taskCnt))++;
//     pthread_mutex_unlock(zpDpCcur_->p_ccurLock);
//     pthread_cond_signal(zpDpCcur_->p_ccurCond);
//
//     return NULL;
// };


#define zGenerate_Ssh_Cmd(zpCmdBuf, zRepoId) do {\
    sprintf(zpCmdBuf,\
            "zServPath=%s;zPath=%s;zIP=%s;zPort=%s;"\
\
            "exec 5<>/dev/tcp/${zIP}/${zPort};"\
            "printf '{\"OpsId\":0}'>&5;"\
            "if [[ '!' != `cat<&5` ]];then exit 210;fi;"\
            "exec 5>&-;exec 5<&-;"\
\
            "for x in ${zPath} ${zPath}_SHADOW;"\
            "do "/* do 后直接跟 CMD，不能加分号或 \n */\
                "rm -f $x ${x}/.git/{index.lock,post-update};"\
                "mkdir -p $x;"\
                "cd $x;"\
                "if [[ 0 -ne $? ]];then exit 206;fi;"\
                "if [[ 97 -lt `df .|grep -oP '\\d+(?=%%)'` ]];then exit 203;fi;"\
                "git init .;git config user.name _;git config user.email _;git commit --allow-empty -m _;"\
            "done;"\
\
            "zTcpReq() { "/* bash tcp fd: 5 */\
                "exec 5<>/dev/tcp/${1}/${2};"\
                "printf \"${3}\">&5;"\
                "cat<&5 >${4};"\
                "exec 5<&-;exec 5>&-;"\
            " };"\
\
            "zTcpReq \"${zIP}\" \"${zPort}\" \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"Path\\\":\\\"${zServPath}/tools/post-update\\\"}\" \"${zPath}/.git/hooks/post-update\";"\
            "if [[ 0 -ne $? ]];then exit 212;fi;"\
            "chmod 0755 ${zPath}/.git/hooks/post-update;"\
\
            "zTcpReq \"${zIP}\" \"${zPort}\" \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"Path\\\":\\\"${zServPath}/tools/____req-deploy.sh\\\"}\" \"${HOME}/.____req-deploy.sh\";"\
            "if [[ 0 -ne $? ]];then exit 212;fi;"\
\
            "zTcpReq \"${zIP}\" \"${zPort}\" \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"Path\\\":\\\"${zServPath}/tools/zhost_self_deploy.sh\\\"}\" \"${zPath}_SHADOW/zhost_self_deploy.sh\";"\
            "if [[ 0 -ne $? ]];then exit 212;fi;",\
            zRun_.p_servPath,\
            zRun_.p_repoVec[zRepoId]->p_repoPath + zRun_.homePathLen,\
            zRun_.netSrv_.p_ipAddr, zRun_.netSrv_.p_port,\
            zRepoId, zRepoId, zRepoId);\
} while(0)

#define zDB_Update_OR_Return(zSQLBuf) do {\
    if (NULL == (zpDpCcur_->p_pgResHd_ = zPgSQL_.exec(zpDpCcur_->p_pgConnHd_, zSQLBuf, zFalse))) {\
        zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );\
        zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);\
        zPrint_Err_Easy("");\
\
        zpDpCcur_->errNo = -91;\
        goto zEndMark;\
    }\
} while(0)

#define zDelSingleQuotation(zpStr) {\
    _i zLen = strlen(zpStr);\
    for (_i i = 0; i < zLen; i++) {\
        if ('\'' == zpStr[i]) {\
            zpStr[i] = ' ';\
        }\
    }\
}

#define zNative_Fail_Confirm() do {\
    if (-1 != zpDpCcur_->id) {\
        pthread_mutex_lock(& zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncLock);\
\
        zRun_.p_repoVec[zpDpCcur_->repoId]->dpTaskFinCnt++;\
\
        pthread_mutex_unlock(& zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncLock);\
        if (zRun_.p_repoVec[zpDpCcur_->repoId]->dpTaskFinCnt == zRun_.p_repoVec[zpDpCcur_->repoId]->dpTotalTask) {\
            pthread_cond_signal(&zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncCond);\
        }\
\
        zSet_Bit(zRun_.p_repoVec[zpDpCcur_->repoId]->resType, 2);  /* 出错则置位 bit[1] */\
        zSet_Bit(zpDpCcur_->p_selfNode->errState, -1 * zpDpCcur_->errNo);  /* 错误码置位 */\
        strcpy(zpDpCcur_->p_selfNode->errMsg, zErrBuf);\
        zDelSingleQuotation(zpDpCcur_->p_selfNode->errMsg);  /* 需要清除单引号 */\
\
        snprintf(zSQLBuf, zGlobCommonBufSiz,\
                "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "\
                "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",\
                -1 * zpDpCcur_->errNo, zpDpCcur_->p_selfNode->errMsg,\
                zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,\
                zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig);\
\
        zDB_Update_OR_Return(zSQLBuf);\
    }\
} while(0)

/* 置位 bit[1]，昭示本地 git push 成功 */
#define zNative_Success_Confirm() do {\
    if (-1 != zpDpCcur_->id) {\
        zSet_Bit(zpDpCcur_->p_selfNode->resState, 2);  /* 置位 bit[1] */\
\
        snprintf(zSQLBuf, zGlobCommonBufSiz,\
                "UPDATE dp_log SET host_res[2] = '1' "\
                "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",\
                zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,\
                zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig);\
\
        zDB_Update_OR_Return(zSQLBuf);\
    }\
} while(0)

static void *
zdp_ccur(void *zp) {
    /*
     * 使上层调度者可中止本任务
     * 必须第一时间赋值
     */
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp;
    zpDpCcur_->tid = pthread_self();

    char zErrBuf[256] = {'\0'},
         zSQLBuf[zGlobCommonBufSiz] = {'\0'},
         zHostAddrBuf[INET6_ADDRSTRLEN] = {'\0'};

    char zRemoteRepoAddrBuf[64 + zRun_.p_repoVec[zpDpCcur_->repoId]->repoPathLen];

    char zGitRefsBuf[2][256 + zRun_.p_repoVec[zpDpCcur_->repoId]->repoPathLen],
         *zpGitRefs[2] = {
             zGitRefsBuf[0],
             zGitRefsBuf[1]
         };

    /*
     * 并发布署窗口控制
     */
    zCheck_Negative_Exit( sem_wait(& zRun_.dpTraficControl) );

    /*
     * when memory load > 80%
     * waiting ...
     */
    pthread_mutex_lock(& (zRun_.commonLock));

    while (80 < zRun_.memLoad) {
        pthread_cond_wait(& (zRun_.commonCond), & (zRun_.commonLock));
    }

    pthread_mutex_unlock(& (zRun_.commonLock));

    /* 预置本次动作的日志 */
    if (NULL == (zpDpCcur_->p_pgConnHd_ = zPgSQL_.conn(zRun_.pgConnInfo))) {
        zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );
        zpDpCcur_->errNo = -90;

        zPrint_Err_Easy("");
        goto zEndMark;
    } else {
        snprintf(zSQLBuf, zGlobCommonBufSiz,
                "INSERT INTO dp_log (proj_id,time_stamp,rev_sig,host_ip) "
                "VALUES (%d,%ld,'%s','%s')",
                zpDpCcur_->repoId, zpDpCcur_->id,
                zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig,
                zpDpCcur_->p_hostIpStrAddr);

        zDB_Update_OR_Return(zSQLBuf);
    }

    /*
     * 若 SSH 命令不为 NULL，则需要执行目标机初始化环节
     */
    if (NULL != zpDpCcur_->p_cmd) {
        if (0 == (zpDpCcur_->errNo = zssh_exec_simple(zpDpCcur_->p_userName,
                        zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort, zpDpCcur_->p_cmd,
                        zpDpCcur_->p_ccurLock, zErrBuf))) {

            /*
             * 置位 resState  bit[0]
             */
            zSet_Bit(zpDpCcur_->p_selfNode->resState, 1);

            /*
             * 记录阶段性布署结果
             * postgreSQL 的数组下标是从 1 开始的
             */
            snprintf(zSQLBuf, zGlobCommonBufSiz,
                    "UPDATE dp_log SET host_res[1] = '1' "
                    "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                    zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,
                    zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig);

            zDB_Update_OR_Return(zSQLBuf);
        } else {
            /*
             * 已确定结果为失败，全局任务完成计数 +1
             */
            pthread_mutex_lock(& zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncLock);
            zRun_.p_repoVec[zpDpCcur_->repoId]->dpTaskFinCnt++;
            pthread_mutex_unlock(& zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncLock);

            /*
             * 若计数已满，则发出完工通知
             */
            if (zRun_.p_repoVec[zpDpCcur_->repoId]->dpTaskFinCnt == zRun_.p_repoVec[zpDpCcur_->repoId]->dpTotalTask) {
                pthread_cond_signal(&zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncCond);
            }

            /*
             * 标记有错误发生
             * 置位 resType  bit[0]
             */
            zSet_Bit(zRun_.p_repoVec[zpDpCcur_->repoId]->resType, 1);

            /*
             * 返回的错误码是负数
             * 其绝对值与错误位一一对应
             */
            zSet_Bit(zpDpCcur_->p_selfNode->errState, -1 * zpDpCcur_->errNo);

            /* 留存错误信息，并清除单引号 */
            strcpy(zpDpCcur_->p_selfNode->errMsg, zErrBuf);
            zDelSingleQuotation(zpDpCcur_->p_selfNode->errMsg);

            /* 错误信息写入 DB */
            snprintf(zSQLBuf, zGlobCommonBufSiz,
                    "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "
                    "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                    -1 * zpDpCcur_->errNo,
                    zpDpCcur_->p_selfNode->errMsg,
                    zpDpCcur_->repoId,
                    zpDpCcur_->p_hostIpStrAddr,
                    zpDpCcur_->id,
                    zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig);

            zDB_Update_OR_Return(zSQLBuf);

            /*
             * 若初始化环节失败，则退出
             */
            zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl));

            zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);
            zPgSQL_.res_clear(zpDpCcur_->p_pgResHd_, NULL);

            zpDpCcur_->errNo = -23;
            goto zEndMark;
        }
    }

    /*
     * ====
     * URL 中使用 IPv6 地址必须用中括号包住，否则无法解析
     * ====
     */
    sprintf(zRemoteRepoAddrBuf, "ssh://%s@[%s]:%s%s%s/.git",
            zpDpCcur_->p_userName,
            zpDpCcur_->p_hostIpStrAddr,
            zpDpCcur_->p_hostServPort,
            '/' == zRun_.p_repoVec[zpDpCcur_->repoId]->p_repoPath[0]? "" : "/",
            zRun_.p_repoVec[zpDpCcur_->repoId]->p_repoPath + zRun_.homePathLen);

    /*
     * 将目标机 IPv6 中的 ':' 替换为 '_'
     * 之后将其附加到分支名称上去
     * 分支名称的一个重要用途是用于捎带信息至目标机
     */
    strcpy(zHostAddrBuf, zpDpCcur_->p_hostIpStrAddr);
    for (_i i = 0; '\0' != zHostAddrBuf[i]; i++) {
        if (':' == zHostAddrBuf[i]) {
            zHostAddrBuf[i] = '_';
        }
    }

    /* push TWO branchs together */
    snprintf(zpGitRefs[0],
            256 + zRun_.p_repoVec[zpDpCcur_->repoId]->repoPathLen,
            "+refs/heads/%sXXXXXXXX:refs/heads/s@%s@%s@%d@%s@%ld@%s@%s",
            zRun_.p_repoVec[zpDpCcur_->repoId]->codeSyncBranch,
            zRun_.netSrv_.specStrForGit,
            zRun_.netSrv_.p_port,
            zpDpCcur_->repoId,
            zHostAddrBuf,
            zpDpCcur_->id,
            zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig,
            zRun_.p_repoVec[zpDpCcur_->repoId]->p_repoAliasPath);

    snprintf(zpGitRefs[1],
            256 + zRun_.p_repoVec[zpDpCcur_->repoId]->repoPathLen,
            "+refs/heads/____shadowXXXXXXXX:refs/heads/S@%s@%s@%d@%s@%ld@%s@%s",
            zRun_.netSrv_.specStrForGit,
            zRun_.netSrv_.p_port,
            zpDpCcur_->repoId,
            zHostAddrBuf,
            zpDpCcur_->id,
            zRun_.p_repoVec[zpDpCcur_->repoId]->dpingSig,
            zRun_.p_repoVec[zpDpCcur_->repoId]->p_repoAliasPath);

    /* 向目标机 push 布署内容 */
    if (0 == (zpDpCcur_->errNo = zLibGit_.remote_push(zRun_.p_repoVec[zpDpCcur_->repoId]->p_gitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2, zErrBuf))) {
        zNative_Success_Confirm();
    } else {
        /*
         * 错误码为 -1 时，
         * 表示未完全确定是不可恢复错误，需要重试
         * 否则可确定此台目标机布署失败
         */
        if (-1 == zpDpCcur_->errNo) {
            char zCmdBuf[1024 + 2 * zRun_.p_repoVec[zpDpCcur_->repoId]->repoPathLen];
            if (NULL == zpDpCcur_->p_cmd) {
                zpDpCcur_->p_cmd = zCmdBuf;
                zGenerate_Ssh_Cmd(zpDpCcur_->p_cmd, zpDpCcur_->repoId);
            }

            /*
             * 重试布署时，一律重新初始化目标机环境
             */
            if (0 == (zpDpCcur_->errNo = zssh_exec_simple(zpDpCcur_->p_userName,
                            zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort,
                            zCmdBuf,
                            & zRun_.p_repoVec[zpDpCcur_->repoId]->dpSyncLock,
                            zErrBuf))) {

                /* if init-ops success, then try deploy once more... */
                if (0 == (zpDpCcur_->errNo = zLibGit_.remote_push(
                                zRun_.p_repoVec[zpDpCcur_->repoId]->p_gitRepoHandler,
                                zRemoteRepoAddrBuf,
                                zpGitRefs, 2,
                                zErrBuf))) {

                    zNative_Success_Confirm();
                } else {
                    zNative_Fail_Confirm();

                    zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl));

                    zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);
                    zPgSQL_.res_clear(zpDpCcur_->p_pgResHd_, NULL);

                    zpDpCcur_->errNo = -12;
                    zPrint_Err_Easy("");
                    goto zEndMark;
                }
            } else {
                zNative_Fail_Confirm();

                zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );
                zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);
                zPgSQL_.res_clear(zpDpCcur_->p_pgResHd_, NULL);

                zpDpCcur_->errNo = -23;
                zPrint_Err_Easy("");
                goto zEndMark;
            }
        } else {
            zNative_Fail_Confirm();

            zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );
            zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);
            zPgSQL_.res_clear(zpDpCcur_->p_pgResHd_, NULL);

            zpDpCcur_->errNo = -12;
            zPrint_Err_Easy("");
            goto zEndMark;
        }
    }

    /* clean resource... */
    zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );
    zPgSQL_.conn_clear(zpDpCcur_->p_pgConnHd_);
    zPgSQL_.res_clear(zpDpCcur_->p_pgResHd_, NULL);

    /*
     * ==== 非核心功能 ====
     * 运行用户指定的布署后动作，不提供执行结果保证
     */
    if (NULL != zpDpCcur_->p_postDpCmd) {
        zssh_exec_simple(zpDpCcur_->p_userName,
                zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort,
                zpDpCcur_->p_postDpCmd,
                zpDpCcur_->p_ccurLock, NULL);
    }

zEndMark:
    pthread_mutex_lock(zpDpCcur_->p_ccurLock);
    zpDpCcur_->finMark = 1;
    pthread_mutex_unlock(zpDpCcur_->p_ccurLock);

    return NULL;
}


/*
 * 13：
 * 不拿锁、不刷新系统IP列表、不刷新缓存
 * 主要用于对项目外的非正式目标机进行布署，通常用于测试目的
 * 也可以用于目标机自请布署的情况 (自动请求同步)
 */
static _i
zspec_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    _i zRepoId = 0,
       zIpCnt = 0;
    char *zpIpList = NULL,
         *zpRevSig = NULL,
         *zpDelim = " ";

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "IpCnt");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zIpCnt = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "IpList");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPrint_Err_Easy("");
        return -1;
    }
    zpIpList = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "delim");
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {
        zpDelim = zpJ->valuestring;
    }

    /* 使用系统 alloc */
    zRegRes__ zR_ = {
        .alloc_fn = NULL
    };

    /* 只有一个IP的情况下，简化处理 */
    if (1 == zIpCnt) {
        /*
         * callback 更换为非 NULL 值
         * 则后续的 free_res 就不会试图释放不存在的内存
         */
        zR_.alloc_fn = (void *) -1;
    } else {
        zPosixReg_.str_split(&zR_, zpIpList, zpDelim);
        if (zIpCnt != zR_.cnt) {
            zPosixReg_.free_res(&zR_);
            zPrint_Err_Easy("");
            return -28;
        }
    }

    zpJ = cJSON_V(zpJRoot, "RevSig");
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {
        zpRevSig = zpJ->valuestring;
    }

    /*
     * id 置为 -1
     * 确保其触发的 post-update 可被任何主动布署动作打断
     * 且不会打断任何其它布署动作，无需目标机初始化环节
     */
    zDpCcur__ zDp_ = {
        .id = -1,
        .p_cmd = NULL
    };

    /*
     * 仅在未提供目标机版本号或提供的版本号已过期及无效时，执行布署
     */
    if (NULL == zpRevSig
            || 0 != strncmp(zpRevSig, zRun_.p_repoVec[zRepoId]->lastDpSig, 40)) {

        zDp_.p_userName = zRun_.p_repoVec[zRepoId]->sshUserName;
        zDp_.p_hostServPort = zRun_.p_repoVec[zRepoId]->sshPort;

        /*
         * 此接口的设计目的是针对少量的特殊情况
         * 此处串行布署即可
         */
        for (_i i = 0; i < zR_.cnt; i++) {
            zDp_.p_hostIpStrAddr = zR_.pp_rets[i];

            /*
             * 执行布署
             */
            zdp_ccur(& zDp_);

            if (0 != zDp_.errNo) {
                zPosixReg_.free_res(&zR_);
                zPrint_Err_Easy("");
                return zDp_.errNo;
            }
        }
    }

    zPosixReg_.free_res(&zR_);
    return 0;
}


/*
 * 12：布署／撤销
 */
#define zJson_Parse() do {  /* json 解析 */\
    zpJ = cJSON_V(zpJRoot, "ForceRev");\
    if (cJSON_IsString(zpJ) && 40 == strlen(zpJ->valuestring)) {\
        zpForceSig = zpJ->valuestring;\
    } else {\
        zpJ = cJSON_V(zpJRoot, "CacheId");\
        if (! cJSON_IsNumber(zpJ)) {\
            zErrNo = -1;\
            zPrint_Err_Easy("");\
            goto zEndMark;\
        }\
        zCacheId = zpJ->valueint;\
    }\
\
    zpJ = cJSON_V(zpJRoot, "RevId");\
    if (! cJSON_IsNumber(zpJ)) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zCommitId = zpJ->valueint;\
\
    zpJ = cJSON_V(zpJRoot, "DataType");\
    if (! cJSON_IsNumber(zpJ)) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zDataType = zpJ->valueint;\
\
    zpJ = cJSON_V(zpJRoot, "IpList");\
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zpIpList = zpJ->valuestring;\
    zIpListStrLen = strlen(zpIpList);\
\
    zpJ = cJSON_V(zpJRoot, "IpCnt");\
    if (! cJSON_IsNumber(zpJ)) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zIpCnt = zpJ->valueint;\
\
    /* 同一项目所有目标机的 ssh 用户名必须相同 */\
    zpJ = cJSON_V(zpJRoot, "SSHUserName");\
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zpSSHUserName = zpJ->valuestring;\
\
    /* 同一项目所有目标机的 sshd 端口必须相同 */\
    zpJ = cJSON_V(zpJRoot, "SSHPort");\
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {\
        zErrNo = -1;\
        zPrint_Err_Easy("");\
        goto zEndMark;\
    }\
    zpSSHPort = zpJ->valuestring;\
\
    zpJ = cJSON_V(zpJRoot, "PostDpCmd");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        zpPostDpCmd = zNativeOps_.alloc(zRepoId,\
                sizeof("cd  && ()")\
                + zRun_.p_repoVec[zRepoId]->repoPathLen - zRun_.homePathLen\
                + strlen(zpJ->valuestring));\
        sprintf(zpPostDpCmd, "cd %s && (%s)",\
                zRun_.p_repoVec[zRepoId]->p_repoPath + zRun_.homePathLen,\
                zpJ->valuestring);\
    }\
\
    zpJ = cJSON_V(zpJRoot, "AliasPath");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        snprintf(zRun_.p_repoVec[zRepoId]->p_repoAliasPath, zRun_.p_repoVec[zRepoId]->maxPathLen, "%s", zpJ->valuestring);\
    } else {\
        zRun_.p_repoVec[zRepoId]->p_repoAliasPath[0] = '\0';\
    }\
\
    zpJ = cJSON_V(zpJRoot, "delim");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        zpDelim = zpJ->valuestring;\
    }\
} while(0)

static _i
zbatch_deploy(cJSON *zpJRoot, _i zSd) {
    _i zErrNo = 0;
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    _i zRepoId = -1,
       zCacheId = -1,
       zCommitId = -1,
       zDataType = -1;

    char *zpIpList = NULL;
    _i zIpListStrLen = 0,
       zIpCnt = 0;

    char *zpSSHUserName = NULL,
         *zpSSHPort = NULL;

    /*
     * IP 字符串的分割符
     * 若没有明确指定，则默认为空格
     */
    char *zpDelim = " ",
         *zpForceSig = NULL,
         *zpPostDpCmd = NULL;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zErrNo = -1;
        zPrint_Err_Easy("");
        goto zEndMark;
    }
    zRepoId = zpJ->valueint;

    /*
     * 检查项目存在性
     */
    if (NULL == zRun_.p_repoVec[zRepoId]
            || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
        zErrNo = -2;
        zPrint_Err_Easy("");
        goto zEndMark;
    }

    /*
     * 提取其余的 json 信息
     */
    zJson_Parse();

    /*
     * 检查 pgSQL 是否可以正常连通
     */
    if (zFalse == zPgSQL_.conn_check(zRun_.pgConnInfo)) {
        zErrNo = -90;
        zPrint_Err_Easy("pgSQL conn failed");
        goto zEndMark;
    }

    /*
     * check system load
     */
    if (80 < zRun_.memLoad) {
        zErrNo = -16;
        zPrint_Err_Easy("mem load too high");
        goto zEndMark;
    }

    /*
     * 必须首先取得 dpWaitLock，才能改变 dpingMark 的值
     * 保证不会同时有多个线程将 dpingMark 置 0
     */
    if (0 != pthread_mutex_trylock( & zRun_.p_repoVec[zRepoId]->dpWaitLock )) {
        zErrNo = -11;
        zPrint_Err_Easy("");
        goto zEndMark;
    }

    /*
     * 预算本函数用到的最大 BufSiz
     * 置于取得 dpWaitLock 之后，避免取不到锁浪费内存
     */
    char *zpCommonBuf = zNativeOps_.alloc(zRepoId,
            2048 + 4 * zRun_.p_repoVec[zRepoId]->repoPathLen + 2 * zIpListStrLen);

    /*
     * ==== 布署过程标志 ====
     * 每个布署动作开始后，会将此值置为 1，新布署请求到来，会将此值置为 0
     * 旧的布署流程在此值不再为 1 时，会主动退出，并清理相关的工作线程
     */
    pthread_mutex_lock(& (zRun_.p_repoVec[zRepoId]->dpSyncLock));
    zRun_.p_repoVec[zRepoId]->dpingMark = 0;
    pthread_mutex_unlock(& (zRun_.p_repoVec[zRepoId]->dpSyncLock));

    /*
     * 通知可能存在的旧的布署动作终止
     */
    pthread_cond_signal( &zRun_.p_repoVec[zRepoId]->dpSyncCond );

    /*
     * 阻塞等待布署锁：==== 主锁 ====
     * 此锁用于保证同一时间，只有一个布署动作在运行
     */
    pthread_mutex_lock( & zRun_.p_repoVec[zRepoId]->dpLock );

    /*
     * dpingMark 置 1 之后，释放 dpWaitLock
     */
    zRun_.p_repoVec[zRepoId]->dpingMark = 1;
    pthread_mutex_unlock( & zRun_.p_repoVec[zRepoId]->dpWaitLock );

    /*
     * 布署过程中，标记缓存状态为 Damaged
     */
    zRun_.p_repoVec[zRepoId]->repoState = zCacheDamaged;

    /*
     * 若 SSH 认证信息有变动，则更新之
     * 包括项目元信息与 DB 信息
     */
    if (0 != strcmp(zpSSHUserName, zRun_.p_repoVec[zRepoId]->sshUserName)
            || 0 != strcmp(zpSSHPort, zRun_.p_repoVec[zRepoId]->sshPort)) {

        /* 更新元信息 */
        snprintf(zRun_.p_repoVec[zRepoId]->sshUserName, 256, "%s", zpSSHUserName);
        snprintf(zRun_.p_repoVec[zRepoId]->sshPort, 6, "%s", zpSSHPort);

        /* 更新 DB */
        sprintf(zpCommonBuf,
                "UPDATE proj_meta SET ssh_user_name = %s, ssh_port = %s, WHERE proj_id = %d",
                zpSSHUserName,
                zpSSHPort,
                zRepoId);

        zpPgResHd_ = zPgSQL_.exec(
                zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                zpCommonBuf,
                zFalse);
        if (NULL == zpPgResHd_) {
            /* 长连接可能意外中断，失败重连，再试一次 */
            zPgSQL_.conn_reset(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
            if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                            zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                            zpCommonBuf,
                            zFalse))) {
                zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);

                /* 数据库不可用，停止服务 ? */
                zPrint_Err_Easy("==== FATAL ====");
                exit(1);
            }
        }

        zPgSQL_.res_clear(zpPgResHd_, NULL);
    }

    /*
     * 非强制指定版本号的情况下，
     * 检查布署请求中标记的 CacheId 是否有效
     */
    if (NULL == zpForceSig
            && zCacheId != zRun_.p_repoVec[zRepoId]->cacheId) {
        zErrNo = -8;
        zPrint_Err_Easy("cacheId invalid");
        goto zCleanMark;
    }

    /*
     * 判断是新版本布署，还是旧版本回撤
     */
    if (zIsCommitDataType == zDataType) {
        zpTopVecWrap_= & zRun_.p_repoVec[zRepoId]->commitVecWrap_;
    } else if (zIsDpDataType == zDataType) {
        zpTopVecWrap_ = & zRun_.p_repoVec[zRepoId]->dpVecWrap_;
    } else {
        zErrNo = -10;  /* 无法识别 */
        zPrint_Err_Easy("==== BUG ====");
        goto zCleanMark;
    }

    /*
     * 检查指定的版本号是否有效
     */
    if ((0 > zCommitId)
            || ((zCacheSiz - 1) < zCommitId)
            || (NULL == zpTopVecWrap_->p_refData_[zCommitId].p_data)) {
        zErrNo = -3;
        zPrint_Err_Easy("commitId invalid");
        goto zCleanMark;
    }

    /*
     * 每次尝试将 ____shadowXXXXXXXX 分支删除
     * 避免该分支体积过大，不必关心执行结果
     */
    zLibGit_.branch_del(
            zRun_.p_repoVec[zRepoId]->p_gitRepoHandler,
            "____shadowXXXXXXXX");

    /*
     * 执行一次空提交到 ____shadowXXXXXXXX 分支
     * 确保每次 push 都能触发 post-update 勾子
     */
    if (0 != zLibGit_.add_and_commit(
                zRun_.p_repoVec[zRepoId]->p_gitRepoHandler,
                "refs/heads/____shadowXXXXXXXX", ".", "_")) {
        zErrNo = -15;
        zPrint_Err_Easy("libgit2 err");
        goto zCleanMark;
    }

    /*
     * 目标机 IP 列表处理
     * 使用定制的 alloc 函数，从项目内存池中分配内存
     * 需要检查目标机集合是否为空，或数量不符
     */
    zRegRes__ zRegRes_ = {
        .alloc_fn = zNativeOps_.alloc,
        .repoId = zRepoId
    };

    zPosixReg_.str_split(&zRegRes_, zpIpList, zpDelim);

    if (0 == zRegRes_.cnt || zIpCnt != zRegRes_.cnt) {
        zPrint_Err_Easy("host IPs'cnt err");
        zErrNo = -28;
        goto zCleanMark;
    }

    /*
     * 布署环境检测无误
     * 此后不会再产生同步的错误信息，断开连接
     */
    shutdown(zSd, SHUT_RDWR);

    /*
     * 开始布署动作之前
     * 各项状态复位
     */

    /* 目标机总数 */
    zRun_.p_repoVec[zRepoId]->totalHost = zRegRes_.cnt;

    /* 总任务数，初始赋值为目标机总数，后续会依不同条件动态变动 */
    zRun_.p_repoVec[zRepoId]->dpTotalTask = zRun_.p_repoVec[zRepoId]->totalHost;

    /* 任务完成数：确定成功或确定失败均视为完成任务 */
    zRun_.p_repoVec[zRepoId]->dpTaskFinCnt = 0;

    /*
     * 用于标记已完成的任务中，是否存在失败的结果
     * 目标机初始化环节出错置位 bit[0]
     * 布署环节出错置位 bit[1]
     */
    zRun_.p_repoVec[zRepoId]->resType = 0;

    /*
     * 转存正在布署的版本号
     */
    if (NULL == zpForceSig) {
        strcpy(zRun_.p_repoVec[zRepoId]->dpingSig, zGet_OneCommitSig(zpTopVecWrap_, zCommitId));
    } else {
        strcpy(zRun_.p_repoVec[zRepoId]->dpingSig, zpForceSig);
    }

    /*
     * 若目标机数量超限，则另行分配内存
     * 否则使用预置的静态空间
     */
    if (zForecastedHostNum < zRun_.p_repoVec[zRepoId]->totalHost) {
        zRun_.p_repoVec[zRepoId]->p_dpCcur_ =
            zNativeOps_.alloc(zRepoId, zRegRes_.cnt * sizeof(zDpCcur__));
    } else {
        zRun_.p_repoVec[zRepoId]->p_dpCcur_ =
            zRun_.p_repoVec[zRepoId]->dpCcur_;
    }

    /*
     * 暂留上一次布署的 HashMap
     * 用于判断目标机增减差异，并据此决定每台目标机需要执行的动作
     */
    zDpRes__ *zpOldDpResHash_[zDpHashSiz];
    memcpy(zpOldDpResHash_, zRun_.p_repoVec[zRepoId]->p_dpResHash_,
            zDpHashSiz * sizeof(zDpRes__ *));
    zDpRes__ *zpOldDpResList_ = zRun_.p_repoVec[zRepoId]->p_dpResList_;

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询
     * 因此不能在项目内存池中分配
     * 分配清零的空间，简化状态重置及重复 IP 检查
     */
    zMem_C_Alloc(zRun_.p_repoVec[zRepoId]->p_dpResList_, zDpRes__, zRegRes_.cnt);

    /*
     * Clear hash buf before reuse it!!!
     */
    memset(zRun_.p_repoVec[zRepoId]->p_dpResHash_,
            0, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 生成目标机初始化的命令
     */
    zGenerate_Ssh_Cmd(zpCommonBuf, zRepoId);

    /*
     * ==========================
     * ==== 正式开始布署动作 ====
     * ==========================
     */
    zbool_t zIsSameSig = zFalse;
    if (0 == strcmp(zRun_.p_repoVec[zRepoId]->dpingSig,
                zRun_.p_repoVec[zRepoId]->lastDpSig)) {
        zIsSameSig = zTrue;
    }

    /*
     * 布署耗时基准：相同版本号重复布署时不更新
     */
    if (! zIsSameSig) {
        zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp = time(NULL);
    }

    /* get sys_update_lock */
    pthread_rwlock_rdlock(& zRun_.p_sysUpdateLock);

    zDpRes__ *zpTmp_ = NULL;
    for (_i i = 0; i < zRun_.p_repoVec[zRepoId]->totalHost; i++) {
        /*
         * 检测是否存在重复IP
         */
        // if (NULL != zpOldDpResHash_[zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0] % zDpHashSiz]
        //         && (0 != zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]
        //             || 0 != zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[1])) {

        //     /* 总任务计数递减 */
        //     zRun_.p_repoVec[zRepoId]->totalHost--;

        //     zPrint_Err_Easy("same IP");
        //     continue;
        // }

        /*
         * IPnum 链表赋值
         * 转换字符串格式的 IPaddr 为数组 _ull[2]
         */
        if (0 != zConvert_IpStr_To_Num(zRegRes_.pp_rets[i],
                    zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr)) {

            /* 此种错误信息记录到哪里 ??? */
            zPrint_Err_Easy("Invalid IP");
            continue;
        }

        /*
         * 所在空间是使用 calloc 分配的，此处不必再手动置零
         */
        // zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState = 0;  /* 成功状态 */
        // zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState = 0;  /* 错误状态 */
        // zRun_.p_repoVec[zRepoId]->p_dpResList_[i].p_next = NULL;

        /*
         * 生成工作线程参数
         */

        /* 此项用作每次布署动的唯一标识 */
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].id = zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp;

        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].repoId = zRepoId;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_userName = zRun_.p_repoVec[zRepoId]->sshUserName;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_hostIpStrAddr = zRegRes_.pp_rets[i];
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_hostServPort = zRun_.p_repoVec[zRepoId]->sshPort;

        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgConnHd_ = NULL;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgResHd_ = NULL;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgRes_ = NULL;

        /*
         * 如下两项最终的值由工作线程填写，此处置 0
         */
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].errNo = 0;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].finMark = 0;

        /* 目标机初始化与布署后执行命令 */
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_cmd = zpCommonBuf;
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_postDpCmd = zpPostDpCmd;

        /*
         * 清理线程时需要保持即时的状态不变
         * 另，libssh2 部分环节需要持锁才能安全并发
         */
        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_ccurLock =
            & zRun_.p_repoVec[zRepoId]->dpSyncLock;

        /*
         * 更新 HashMap
         */
        zpTmp_ = zRun_.p_repoVec[zRepoId]->p_dpResHash_[
            zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]
                % zDpHashSiz
        ];

        /*
         * 若 HashMap 顶层为空，直接指向链表中对应的位置
         */
        if (NULL == zpTmp_) {
            zRun_.p_repoVec[zRepoId]->p_dpResHash_[
                zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]
                    % zDpHashSiz
            ] = & zRun_.p_repoVec[zRepoId]->p_dpResList_[i];

            /* 生成工作线程参数 */
            zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_selfNode = & zRun_.p_repoVec[zRepoId]->p_dpResList_[i];
        } else {
            while (NULL != zpTmp_->p_next) {
                zpTmp_ = zpTmp_->p_next;
            }

            zpTmp_->p_next = & zRun_.p_repoVec[zRepoId]->p_dpResList_[i];

            /* 生成工作线程参数 */
            zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_selfNode = zpTmp_->p_next;
        }

        /*
         * 基于旧的 HashMap 检测是否是新加入的目标机
         */
        zpTmp_ = zpOldDpResHash_[
            zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr[0]
                % zDpHashSiz
        ];

        while (NULL != zpTmp_) {
            /*
             * 若目标机 IPaddr 已存在
             * 且初始化结果是成功的
             * 则跳过远程初始化环节
             */
            if ( zIpVecCmp(zpTmp_->clientAddr,
                        zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr)
                    && zCheck_Bit(zpTmp_->resState, 1)) {

                /* 置为 NULL，则布署时就不会执行目标机初始化命令 */
                zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_cmd = NULL;

                /*
                 * 若是相同版本号重复布署
                 * 且上一次布署的结果成功，则不需要执行布署
                 * ==== 此处的执行效率有待优化 ====
                 */
                if (zIsSameSig && zCheck_Bit(zpTmp_->resState, 4)) {
                    /* 复制上一次的全部状态位 */
                    zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState = zpTmp_->resState;

                    /* 总任务数递减 */
                    zRun_.p_repoVec[zRepoId]->dpTotalTask--;
                    goto zSkipMark;
                }

                break;
            }

            zpTmp_ = zpTmp_->p_next;
        }

        /*
         * 执行布署
         */
        zThreadPool_.add(zdp_ccur, & zRun_.p_repoVec[zRepoId]->p_dpCcur_[i]);
zSkipMark:;
    }

    /* release sys_update_lock */
    pthread_rwlock_unlock(& zRun_.p_sysUpdateLock);

    /*
     * 释放旧的资源占用
     */
    if (NULL != zpOldDpResList_) {
        free(zpOldDpResList_);
    }

    /*
     * 等待所有工作线程完成任务
     * 或新的布署请求到达
     */
    pthread_mutex_lock(& (zRun_.p_repoVec[zRepoId]->dpSyncLock));
    while (zRun_.p_repoVec[zRepoId]->dpTaskFinCnt < zRun_.p_repoVec[zRepoId]->dpTotalTask
            && 1 == zRun_.p_repoVec[zRepoId]->dpingMark) {

        pthread_cond_wait(
                & zRun_.p_repoVec[zRepoId]->dpSyncCond,
                & zRun_.p_repoVec[zRepoId]->dpSyncLock);
    }
    pthread_mutex_unlock( (& zRun_.p_repoVec[zRepoId]->dpSyncLock) );

    /*
     * 运行至此，首先要判断：是被新的布署请求中断 ？
     * 还是返回全部的部署结果 ?
     */
    if (1 == zRun_.p_repoVec[zRepoId]->dpingMark) {
        /*
         * 若布署成功且版本号与上一次成功布署的不同时
         * 才需要刷新缓存
         */
        if (0 == zRun_.p_repoVec[zRepoId]->resType
                && ! zIsSameSig) {
            /*
             * 获取写锁
             * 此时将拒绝所有查询类请求
             */
            pthread_rwlock_wrlock(&zRun_.p_repoVec[zRepoId]->rwLock);

            /*
             * 以上一次成功布署的版本号为名称
             * 创建一个新分支，用于保证回撤的绝对可行性
             */
            if (0 != zLibGit_.branch_add(
                        zRun_.p_repoVec[zRepoId]->p_gitRepoHandler,
                        zRun_.p_repoVec[zRepoId]->lastDpSig,
                        zRun_.p_repoVec[zRepoId]->lastDpSig,
                        zTrue)) {
                zPrint_Err_Easy("branch create err");
            }

            /*
             * 更新最新一次布署版本号
             * 并写入 DB
             */
            strcpy(zRun_.p_repoVec[zRepoId]->lastDpSig,
                    zRun_.p_repoVec[zRepoId]->dpingSig);

            sprintf(zpCommonBuf,
                    "UPDATE proj_meta SET last_dp_sig = '%s',alias_path = '%s' "
                    "WHERE proj_id = %d",
                    zRun_.p_repoVec[zRepoId]->dpingSig,
                    zRun_.p_repoVec[zRepoId]->p_repoAliasPath,
                    zRepoId);

            zpPgResHd_ = zPgSQL_.exec(
                    zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                    zpCommonBuf,
                    zFalse);
            if (NULL == zpPgResHd_) {
                /* 长连接可能意外中断，失败重连，再试一次 */
                zPgSQL_.conn_reset(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);
                if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                                zRun_.p_repoVec[zRepoId]->p_pgConnHd_,
                                zpCommonBuf,
                                zFalse))) {
                    zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_pgConnHd_);

                    /* 数据库不可用，停止服务 ? */
                    zPrint_Err_Easy("==== FATAL ====");
                    exit(1);
                }
            }

            zPgSQL_.res_clear(zpPgResHd_, NULL);

            /*
             * 项目内存池复位
             */
            zReset_Mem_Pool_State( zRepoId );

            /* 刷新缓存 */
            zCacheMeta__ zSubMeta_;
            zSubMeta_.repoId = zRepoId;

            zSubMeta_.dataType = zIsCommitDataType;
            zNativeOps_.get_revs(& zSubMeta_);

            zSubMeta_.dataType = zIsDpDataType;
            zNativeOps_.get_revs(& zSubMeta_);

            /* update cacheId */
            zRun_.p_repoVec[zRepoId]->cacheId = time(NULL);

            /* 标记缓存为可用状态 */
            zRun_.p_repoVec[zRepoId]->repoState = zCacheGood;

            /* 释放缓存锁 */
            pthread_rwlock_unlock(&zRun_.p_repoVec[zRepoId]->rwLock);
        }
    } else {
        /*
         * 若是被新布署请求打断
         * 则清理所有尚未退出的工作线程
         * 所有线程使的同一把锁，故而循环外统一持锁／放锁即可
         */
        pthread_mutex_lock(& (zRun_.p_repoVec[zRepoId]->dpSyncLock));

        _i i;
        for (i = 0; i < zRun_.p_repoVec[zRepoId]->totalHost; i++) {
            if (0 == zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].finMark) {
                zPgSQL_.res_clear(zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgResHd_,
                        zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgRes_);
                zPgSQL_.conn_clear(zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].p_pgConnHd_);

                pthread_cancel(zRun_.p_repoVec[zRepoId]->p_dpCcur_[i].tid);
            }
        }

        /* 被清理的线程可能没来得及释放信号量 */
        zCheck_Negative_Exit( sem_getvalue(& zRun_.dpTraficControl, &i) );
        i = zRun_.dpTraficLimit - i;

        while(i > 0) {
            zCheck_Negative_Exit( sem_post(& zRun_.dpTraficControl) );
            i--;
        }

        pthread_mutex_unlock(& (zRun_.p_repoVec[zRepoId]->dpSyncLock));

        zErrNo = -127;
        zPrint_Err_Easy("Deploy interrupted");
    }

zCleanMark:
    /* ==== 释放布署主锁 ==== */
    pthread_mutex_unlock(& zRun_.p_repoVec[zRepoId]->dpLock);

zEndMark:
    return zErrNo;
}


/*
 * 9：布署成功目标机自动确认
 */
static _i
zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmp_ = NULL;
    _i zErrNo = 0,
       zRetBit = 0,
       zRepoId = 0;
    _ull zHostId[2] = {0};
    time_t zTimeStamp = 0;

    char zCmdBuf[zGlobCommonBufSiz] = {'\0'},
         * zpHostAddr = NULL,
         * zpRevSig = NULL,
         * zpReplyType = NULL;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zRepoId] || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    zpJ = cJSON_V(zpJRoot, "TimeStamp");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zTimeStamp = (time_t)zpJ->valuedouble;

    zpJ = cJSON_V(zpJRoot, "HostAddr");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPrint_Err_Easy("");
        return -1;
    }
    zpHostAddr = zpJ->valuestring;
    if (0 != zConvert_IpStr_To_Num(zpHostAddr, zHostId)) {
        zPrint_Err_Easy("");
        return -18;
    }

    zpJ = cJSON_V(zpJRoot, "RevSig");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPrint_Err_Easy("");
        return -1;
    }
    zpRevSig = zpJ->valuestring;

    /* 格式，SN: S1..S9，EN: E3..E8 */
    zpJ = cJSON_V(zpJRoot, "ReplyType");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]
            || 2 > strlen(zpJ->valuestring)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zpReplyType = zpJ->valuestring;


    /* 正文...遍历信息链 */
    for (zpTmp_ = zRun_.p_repoVec[zRepoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];
            zpTmp_ != NULL;
            zpTmp_ = zpTmp_->p_next) {
        if ( zIpVecCmp(zpTmp_->clientAddr, zHostId) ) {
            /* 检查信息类型是否合法 */
            zRetBit = strtol(zpReplyType + 1, NULL, 10);
            if (0 >= zRetBit || 24 < zRetBit) {
                zErrNo = -1;
                zPrint_Err_Easy("UNknown reply type");
                goto zMarkEnd;
            }

            /*
             * 'S[N]'：每个阶段的布署成果上报
             * 'E[N]'：错误信息分类上报
             */
            if ('E' == zpReplyType[0]) {
                /* 错误信息允许为空，不需要检查提取到的内容 */
                zpJ = cJSON_V(zpJRoot, "content");
                strncpy(zpTmp_->errMsg, zpJ->valuestring, 255);
                zpTmp_->errMsg[255] = '\0';

                /* 需要清除单引号 */
                zDelSingleQuotation(zpTmp_->errMsg);

                /* postgreSQL 的数组下标是从 1 开始的 */
                snprintf(zCmdBuf, zGlobCommonBufSiz,
                        "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "
                        "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                        zRetBit, zpTmp_->errMsg,
                        zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                /* 设计 SQL 连接池 ??? */
                if (0 > zPgSQL_.exec_once(zRun_.pgConnInfo, zCmdBuf, NULL)) {
                    zPrint_Err_Easy("DB record update err");
                }

                /* 判断是否是延迟到达的信息 */
                if (0 != strcmp(zRun_.p_repoVec[zRepoId]->dpingSig, zpRevSig)
                        || zTimeStamp != zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp) {

                    zErrNo = -101;
                    zPrint_Err_Easy("msg out-of-date");
                    goto zMarkEnd;
                }

                zSet_Bit(zpTmp_->errState, zRetBit);

                /* 发生错误，bit[1] 置位表示目标机布署出错返回 */
                zSet_Bit(zRun_.p_repoVec[zRepoId]->resType, 2);

                /*
                 * 确认此台目标机会布署失败
                 * 全局计数原子性+1
                 * 若任务计数已满，则通知上层调度者
                 */
                pthread_mutex_lock(& zRun_.p_repoVec[zRepoId]->dpSyncLock);
                zRun_.p_repoVec[zRepoId]->dpTaskFinCnt++;
                pthread_mutex_unlock(& zRun_.p_repoVec[zRepoId]->dpSyncLock);
                if (zRun_.p_repoVec[zRepoId]->dpTaskFinCnt == zRun_.p_repoVec[zRepoId]->dpTotalTask) {
                    pthread_cond_signal(&zRun_.p_repoVec[zRepoId]->dpSyncCond);
                }

                zErrNo = -102;
                zPrint_Err_Easy("");
                goto zMarkEnd;
            } else if ('S' == zpReplyType[0]) {
                snprintf(zCmdBuf, zGlobCommonBufSiz,
                        "UPDATE dp_log SET host_res[%d] = '1' "
                        "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                        zRetBit,
                        zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                if (0 > zPgSQL_.exec_once(zRun_.pgConnInfo, zCmdBuf, NULL)) {
                    zPrint_Err_Easy("DB record update err");
                }

                /* 判断是否是延迟到达的信息 */
                if (0 != strcmp(zRun_.p_repoVec[zRepoId]->dpingSig, zpRevSig)
                        || zTimeStamp != zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp) {
                    zErrNo = -101;
                    zPrint_Err_Easy("msg out-of-date");
                    goto zMarkEnd;
                }

                zSet_Bit(zpTmp_->resState, zRetBit);

                /* 最终成功的状态到达时，才需要递增全局计数并记录布署耗时 */
                if ('4' == zpReplyType[1]) {
                    /*
                     * 全局计数原子性+1
                     * 若任务计数已满，则通知上层调度者
                     */
                    pthread_mutex_lock( & zRun_.p_repoVec[zRepoId]->dpSyncLock );
                    zRun_.p_repoVec[zRepoId]->dpTaskFinCnt++;
                    pthread_mutex_unlock(& zRun_.p_repoVec[zRepoId]->dpSyncLock);
                    if (zRun_.p_repoVec[zRepoId]->dpTaskFinCnt == zRun_.p_repoVec[zRepoId]->dpTotalTask) {
                        pthread_cond_signal(&zRun_.p_repoVec[zRepoId]->dpSyncCond);
                    }

                    /* [DEBUG]：每台目标机的布署耗时统计 */
                    snprintf(zCmdBuf, zGlobCommonBufSiz,
                            "UPDATE dp_log SET host_timespent = %ld "
                            "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                            time(NULL) - zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp,
                            zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                    if (0 > zPgSQL_.exec_once(zRun_.pgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err_Easy("DB record update err");
                    }
                }

                zErrNo = 0;
                goto zMarkEnd;
            } else {
                zErrNo = -1;
                zPrint_Err_Easy("UNdefined reply type");
                goto zMarkEnd;
            }
        }
    }

zMarkEnd:
    return zErrNo;
}
#undef zGenerate_SQL_Cmd


/*
 * 14: 向目标机传输指定的文件
 */
static _i
zreq_file(cJSON *zpJRoot, _i zSd) {
    char zDataBuf[4096] = {'\0'};
    _i zFd = -1,
       zDataLen = -1;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "Path");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPrint_Err_Easy("");
        return -1;
    }

    if (0 > (zFd = open(zpJ->valuestring, O_RDONLY))) {
        shutdown(zSd, SHUT_RDWR);

        zPrint_Err_Easy_Sys();
        return -80;
    }

    /* 此处并未保证传输过程的绝对可靠性 */
    while (0 < (zDataLen = read(zFd, zDataBuf, 4096))) {
        if (zDataLen != zNetUtils_.send_nosignal(zSd, zDataBuf, zDataLen)) {
            close(zFd);
            shutdown(zSd, SHUT_RDWR);

            zPrint_Err_Easy("file trans err");
            return -126;
        }
    }

    close(zFd);
    return 0;
}

/*
 * 0：Ping、Pang
 * 目标机使用此接口测试与服务端的连通性
 */
static _i
zpang(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd) {
    /*
     * 目标机发送 "?"
     * 服务端回复 "!"
     */
    zNetUtils_.send_nosignal(zSd, "!", zBytes(1));

    return 0;
}

/*
 * 7：目标机自身布署成功之后，向服务端核对全局结果
 * 若全局结果是失败，则执行回退
 * 全局成功，回复 "S"，否则回复 "F"，尚未确定最终结果回复 "W"
 */
static _i
zglob_res_confirm(cJSON *zpJRoot, _i zSd) {
    _i zRepoId = -1;
    time_t zTimeStamp = 0;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zRepoId]
            || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {

        zPrint_Err_Easy("");
        return -2;
    }

    zpJ = cJSON_V(zpJRoot, "TimeStamp");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zTimeStamp = (time_t)zpJ->valuedouble;

    if (zTimeStamp < zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp) {
        /*
         * 若已有新的布署动作产生，统一返回失身标识，
         * 避免目标机端无谓的执行已经过期的布署后动作
         */
        zNetUtils_.send_nosignal(zSd, "F", sizeof('F'));
    } else {
        if (zRun_.p_repoVec[zRepoId]->dpTaskFinCnt == zRun_.p_repoVec[zRepoId]->dpTotalTask) {
            if (0 == zRun_.p_repoVec[zRepoId]->resType) {
                /* 确定成功 */
                zNetUtils_.send_nosignal(zSd, "S", sizeof('S'));
            } else {
                /* 确定失败 */
                zNetUtils_.send_nosignal(zSd, "F", sizeof('F'));
            }
        } else {
            if (0 == zRun_.p_repoVec[zRepoId]->resType) {
                /* 结果尚未确定，正在 waiting... */
                zNetUtils_.send_nosignal(zSd, "W", sizeof('W'));
            } else {
                /* 确定失败 */
                zNetUtils_.send_nosignal(zSd, "F", sizeof('F'));
            }
        }
    }

    return 0;
}


/*
 * 15：布署进度实时查询接口，同时包含项目元信息，示例如下：
 */
/** ==== 样例 ==== **
 * {
 *   "ProjMeta": {
 *     "id": 9,
 *     "path": "/home/git/miaopai",
 *     "AliasPath": "/home/git/www",
 *     "CreatedTime": "2017-10-01 09:30:00",  // 置于 zRepo__ 中
 *     "PermitDp": "Yes"
 *   },
 *   "RecentDpInfo": {
 *     "RevSig": "abcdefgh1234abcdefgh123456785678abcdefgh",
 *     "result": "in_process",
 *     "TimeStamp": 1561000101,
 *     "TimeSpent": 10,
 *     "process": {
 *       "total": 200,
 *       "success": 189,
 *       "fail": {
 *         "cnt": 8,
 *         "detail": {
 *           "ServErr": [
 *
 *           ],
 *           "NetServToHost": [
 *             "::1|can't connect to ...",
 *             "10.1.2.9|can't connect to ..."
 *           ],
 *           "SSHAuth": [
 *
 *           ],
 *           "HostDisk": [
 *
 *           ],
 *           "HostPermission": [
 *             "abcd::1:2|permisson denied"
 *           ],
 *           "HostFileConflict": [
 *             "1.1.1.1|git reset failed"
 *           ],
 *           "HostPathNotExist": [
 *             "1.1.1.1|/home/git/xxx"
 *           ],
 *           "HostDupDeploy": [
 *             "1.1.1.1|"
 *           ],
 *           "HostAddrInvalid": [
 *             "1.1.1.1|"
 *           ],
 *           "NetHostToServ": [
 *
 *           ],
 *           "HostLoad": [
 *             "fec0::8|cpu 99%",
 *           ]
 *         }
 *       },
 *       "InProcess": {
 *         "cnt": 3,
 *         "stage": {
 *           "HostInit": [
 *             "172.0.0.9"
 *           ],
 *           "ServDpOps": [
 *             "2.3.4.5"
 *           ],
 *           "HostRecvWaiting": [
 *             "1:2::f"
 *           ],
 *           "HostConfirmWaiting": [
 *
 *           ]
 *         }
 *       }
 *     }
 *   },
 *   "DpDataAnalysis": {
 *     "SuccessRate": 99.99,
 *     "AvgTimeSpent": 12,
 *     "ErrClassification": {
 *       "total": 10,
 *       "NetServToHost": 1,
 *       "NetHostToServ": 1,
 *       "NetAuth": 2,
 *       "HostDisk": 1,
 *       "HostLoad": 1,
 *       "HostPermission": 1,
 *       "HostFileConflict": 3,
 *       "unknown": 0
 *     }
 *   },
 *   "HostDataAnalysis": {
 *     "cpu": {
 *       "AvgLoad": 56.00,
 *       "LoadBalance": 1.51
 *     },
 *     "mem": {
 *       "AvgLoad": 56.00,
 *       "LoadBalance": 1.51
 *     },
 *     "IO/Net": {
 *       "AvgLoad": 56.00,
 *       "LoadBalance": 1.51
 *     },
 *     "IO/Disk": {
 *       "AvgLoad": 56.00,
 *       "LoadBalance": 1.51
 *     },
 *     "DiskUsage": {
 *       "Current": 70.00,
 *       "Max": 90.01,
 *       "Avg": 56.00
 *     }
 *   }
 * }
 *
*********************************/
#define zSQL_Exec() do {\
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zTrue))) {\
            zPgSQL_.conn_clear(zpPgConnHd_);\
            free(zpStageBuf[0]);\
            zPrint_Err_Easy("SQL exec err");\
            return -91;\
        }\
\
        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {\
            zPgSQL_.conn_clear(zpPgConnHd_);\
            zPgSQL_.res_clear(zpPgResHd_, NULL);\
            free(zpStageBuf[0]);\
            zPrint_Err_Easy("SQL result err");\
            return -92;\
        }\
} while (0)
/*********************************
 * << 错误类型 >>
 * err1 bit[0]: 服务端错误
 * err2 bit[1]: server ==> host 网络不通
 * err3 bit[2]: SSH 连接认证失败
 * err4 bit[3]: 目标端磁盘容量不足
 * err5 bit[4]: 目标端权限不足
 * err6 bit[5]: 目标端文件冲突
 * err7 bit[6]: 目标端路径不存在
 * err8 bit[7]: 目标端收到重复布署指令(同一目标机的多个不同IP)
 * err9 bit[8]: 目标机 IP 格式错误/无法解析
 * err10 bit[9]: host ==> server 网络不通
 * err11 bit[10]: 目标端负载过高
 ********************************/
/* 错误类别数量 */
#define zErrClassNum 12

static _i
zprint_dp_process(cJSON *zpJRoot, _i zSd) {
    cJSON *zpJ = NULL;

    char zSQLBuf[zGlobCommonBufSiz] = {'\0'};
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

    _s zRepoId = -1;

    /*
     * 整体布署耗时：
     * 最后一台目标机确认结果的时间 - 布署开始时间
     */
    _s zGlobTimeSpent = 0;

    /*
     * 全局布署状态：
     * 预置为 'W'/waiting，成功置为 'S'，失败置为 'F'
     */
    _c zGlobRes = 'W';

    /* 提取项目 ID */
    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zRepoId]
            || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    /* 从 DB 中提取项目创建时间 */
    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zRun_.pgConnInfo))) {
        zPrint_Err_Easy("DB connect failed");
        return -90;
    }

    /*
     * 成功、失败、正在进行中的计数
     */
    _i zSuccessCnt = 0,
       zFailCnt = 0,
       zWaitingCnt = 0;

    /* 存放目标机实时信息的 BUF */
    _i zStageBufLen = zRun_.p_repoVec[zRepoId]->dpTotalTask * (INET6_ADDRSTRLEN + 3),
       zErrBufLen = zRun_.p_repoVec[zRepoId]->dpTotalTask * (INET6_ADDRSTRLEN + 256);

    char *zpStageBuf[4],
         *zpErrBuf[zErrClassNum];

    /* 一次性分配所需的全部空间 */
    zMem_Alloc(zpStageBuf[0], char, 4 * zStageBufLen + zErrClassNum * zErrBufLen);
    zpStageBuf[0][1] = '\0';

    _i i, j;
    for (i = 1; i < 4; i++) {
        zpStageBuf[i] = zpStageBuf[i - 1] + zStageBufLen;
        zpStageBuf[i][1] = '\0';
    }

    zpErrBuf[0] = zpStageBuf[3] + zStageBufLen;
    zpErrBuf[0][1] = '\0';
    for (i = 1; i < zErrClassNum; i++) {
        zpErrBuf[i] = zpErrBuf[i - 1] + zErrBufLen;
        zpErrBuf[i][1] = '\0';
    }

    _i zStageOffSet[4] = {0},
       zErrOffSet[zErrClassNum] = {0};

    char zIpStrBuf[INET6_ADDRSTRLEN];
    for (i = 0; i < zRun_.p_repoVec[zRepoId]->totalHost; i++) {
        if (0 == zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState) {  /* 无错 */
            if (zCheck_Bit(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 4)) {  /* 已确定成功 */
                zSuccessCnt++;
            } else {  /* 阶段性成功，即未确认完全成功 */
                if (0 != zConvert_IpNum_To_Str(
                            zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr,
                            zIpStrBuf)) {
                    zPrint_Err_Easy("IPConvert err");
                } else {
                    if (! zCheck_Bit(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, 1)) {
                        zStageOffSet[1] += snprintf(
                                zpStageBuf[1] + zStageOffSet[1],
                                zStageBufLen - zStageOffSet[1],
                                ",\"%s\"",
                                zIpStrBuf);
                    } else {
                        for (j = 4; j > 1; j--) {
                            if (zCheck_Bit(zRun_.p_repoVec[zRepoId]->p_dpResList_[i].resState, j - 1)) {
                                zStageOffSet[j] += snprintf(
                                        zpStageBuf[j] + zStageOffSet[j],
                                        zStageBufLen - zStageOffSet[j],
                                        ",\"%s\"",
                                        zIpStrBuf);
                                break;
                            }
                        }
                    }
                }
            }
        } else {  /* 有错 */
            zFailCnt++;
            if (0 != zConvert_IpNum_To_Str(
                        zRun_.p_repoVec[zRepoId]->p_dpResList_[i].clientAddr,
                        zIpStrBuf)) {
                zPrint_Err_Easy("IPConvert err");
            } else {
                for (j = 0; j < zErrClassNum; j++) {
                    if (zCheck_Bit(
                                zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errState,
                                j + 1)) {
                        zErrOffSet[j] += snprintf(
                                zpErrBuf[j] + zErrOffSet[j],
                                zErrBufLen - zErrOffSet[j],
                                ",\"%s|%s\"",
                                zIpStrBuf,
                                zRun_.p_repoVec[zRepoId]->p_dpResList_[i].errMsg);
                        break;
                    }
                }
            }
        }
    }

    /* 总数减去已确定结果的，剩余的就是正在进行中的... */
    zWaitingCnt = zRun_.p_repoVec[zRepoId]->totalHost - zSuccessCnt - zFailCnt;

    /*
     * 从 DB 中提取最近 30 天的记录
     * 首先生成一张临时表，以提高效率
     */
    pthread_mutex_lock(& (zRun_.commonLock));
    _i zTbNo = ++zRun_.p_repoVec[zRepoId]->tempTableNo;
    pthread_mutex_unlock(& (zRun_.commonLock));

    sprintf(zSQLBuf,
            "CREATE TABLE tmp%d as SELECT host_ip,host_res,host_err,host_timespent,time_stamp FROM dp_log "
            "WHERE proj_id = %d AND time_stamp > %ld",
            zTbNo,
            zRepoId, time(NULL) - 3600 * 24 * 30);

    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
        /*
         * 执行失败，则尝试
         * 删除可能存在的重名表
         */
        char zTmpBuf[64];
        snprintf(zTmpBuf, 64, "DROP TABLE tmp%u", zTbNo);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zTmpBuf, zFalse))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            free(zpStageBuf[0]);

            zPrint_Err_Easy("SQL exec err");
            return -91;
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            free(zpStageBuf[0]);
            zPrint_Err_Easy("SQL exec failed");
            return -91;
        }
    } else {
        zPgSQL_.res_clear(zpPgResHd_, NULL);
    }

    /* 目标机总台次 */
    sprintf(zSQLBuf, "SELECT count(host_ip) FROM tmp%u", zTbNo);
    zSQL_Exec();

    _f zTotalTimes = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /* 布署成功的总台次 */
    sprintf(zSQLBuf,
            "SELECT count(host_ip) FROM tmp%u "
            "WHERE host_res[4] = '1'",
            zTbNo);
    zSQL_Exec();

    _f zSuccessTimes = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /* 所有布署成功台次的耗时之和 */
    sprintf(zSQLBuf,
            "SELECT sum(host_timespent) FROM tmp%u "
            "WHERE host_res[4] = '1'",
            zTbNo);
    zSQL_Exec();

    _f zSuccessTimeSpentAll = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /*
     * 各类错误计数
     * 由于值只会为 '1' 或空，使用 sum(to_number()) 加和取值，以简化逻辑
     */
    sprintf(zSQLBuf, "SELECT "
            "sum(to_number(host_err[1], '9')),"
            "sum(to_number(host_err[2], '9')),"
            "sum(to_number(host_err[3], '9')),"
            "sum(to_number(host_err[4], '9')),"
            "sum(to_number(host_err[5], '9')),"
            "sum(to_number(host_err[6], '9')),"
            "sum(to_number(host_err[7], '9')),"
            "sum(to_number(host_err[8], '9')),"
            "sum(to_number(host_err[9], '9')),"
            "sum(to_number(host_err[10], '9')),"
            "sum(to_number(host_err[11], '9')),"
            "sum(to_number(host_err[12], '9')) "
            "FROM tmp%u", zTbNo);
    zSQL_Exec();

    _c zErrCnt[zErrClassNum];
    zErrCnt[0] = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zErrCnt[1] = strtol(zpPgRes_->tupleRes_[0].pp_fields[1], NULL, 10);
    zErrCnt[2] = strtol(zpPgRes_->tupleRes_[0].pp_fields[2], NULL, 10);
    zErrCnt[3] = strtol(zpPgRes_->tupleRes_[0].pp_fields[3], NULL, 10);
    zErrCnt[4] = strtol(zpPgRes_->tupleRes_[0].pp_fields[4], NULL, 10);
    zErrCnt[5] = strtol(zpPgRes_->tupleRes_[0].pp_fields[5], NULL, 10);
    zErrCnt[6] = strtol(zpPgRes_->tupleRes_[0].pp_fields[6], NULL, 10);
    zErrCnt[7] = strtol(zpPgRes_->tupleRes_[0].pp_fields[7], NULL, 10);
    zErrCnt[8] = strtol(zpPgRes_->tupleRes_[0].pp_fields[8], NULL, 10);
    zErrCnt[9] = strtol(zpPgRes_->tupleRes_[0].pp_fields[9], NULL, 10);
    zErrCnt[10] = strtol(zpPgRes_->tupleRes_[0].pp_fields[10], NULL, 10);
    zErrCnt[11] = strtol(zpPgRes_->tupleRes_[0].pp_fields[11], NULL, 11);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /* 全局布署结果 */
    if (zRun_.p_repoVec[zRepoId]->dpTaskFinCnt == zRun_.p_repoVec[zRepoId]->dpTotalTask) {
        if (0 == zRun_.p_repoVec[zRepoId]->resType) {
            zGlobRes = 'S';
        } else {
            zGlobRes = 'F';
        }
    } else {
        if (0 == zRun_.p_repoVec[zRepoId]->resType) {
            zGlobRes = 'W';
        } else {
            zGlobRes = 'F';
        }
    }

    /*
     * 全局布署耗时：
     * 布署中的取当前时间，
     * 已布署完成的，取耗时最长的目标机时间
     */
    if ('W' == zGlobRes) {
        zGlobTimeSpent = time(NULL) - zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp;
    } else {
        sprintf(zSQLBuf,
                "SELECT max(host_timespent) FROM tmp%u "
                "WHERE time_stamp = %ld",
                zTbNo,
                zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp);
        zSQL_Exec();

        zGlobTimeSpent = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    }

    /* 删除临时表 */
    sprintf(zSQLBuf, "DROP TABLE tmp%u", zTbNo);
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
        zPgSQL_.conn_clear(zpPgConnHd_);
        free(zpStageBuf[0]);

        zPrint_Err_Easy("SQL exec err");
        return -91;
    } else {
        zPgSQL_.res_clear(zpPgResHd_, NULL);
    }

    zPgSQL_.conn_clear(zpPgConnHd_);

    /****************
     * 生成最终结果 *
     ****************/
    char zResBuf[8192];
    _i zResSiz = snprintf(zResBuf, 8192,
            "{\"ErrNo\":0,\"ProjMeta\":{\"id\":%d,\"path\":\"%s\",\"AliasPath\":\"%s\",\"CreatedTime\":\"%s\"},\"RecentDpInfo\":{\"RevSig\":\"%s\",\"result\":\"%s\",\"TimeStamp\":%ld,\"TimeSpent\":%d,\"process\":{\"total\":%d,\"success\":%d,\"fail\":{\"cnt\":%d,\"detail\":{\"ServErr\":[%s],\"NetServToHost\":[%s],\"SSHAuth\":[%s],\"HostDisk\":[%s],\"HostPermission\":[%s],\"HostFileConflict\":[%s],\"HostPathNotExist\":[%s],\"HostDupDeploy\":[%s],\"HostAddrInvalid\":[%s],\"NetHostToServ\":[%s],\"HostLoad\":[%s],\"ReqFileNotExist\":[%s]}},\"InProcess\":{\"cnt\":%d,\"stage\":{\"HostInit\":[%s],\"ServDpOps\":[%s],\"HostRecvWaiting\":[%s],\"HostConfirmWaiting\":[%s]}}}},\"DpDataAnalysis\":{\"SuccessRate\":%.2f,\"AvgTimeSpent\":%.2f,\"ErrClassification\":{\"total\":%d,\"ServErr\":%d,\"NetServToHost\":%d,\"SSHAuth\":%d,\"HostDisk\":%d,\"HostPermission\":%d,\"HostFileConflict\":%d,\"HostPathNotExist\":%d,\"HostDupDeploy\":%d,\"HostAddrInvalid\":%d,\"NetHostToServ\":%d,\"HostLoad\":%d,\"ReqFileNotExist\":%d}},\"HostDataAnalysis\":{\"cpu\":{\"AvgLoad\":%.2f,\"LoadBalance\":%.2f},\"mem\":{\"AvgLoad\":%.2f,\"LoadBalance\":%.2f},\"IO/Net\":{\"AvgLoad\":%.2f,\"LoadBalance\":%.2f},\"IO/Disk\":{\"AvgLoad\":%.2f,\"LoadBalance\":%.2f},\"DiskUsage\":{\"current\":%.2f,\"avg\":%.2f,\"max\":%.2f}}}",
            zRepoId,
            zRun_.p_repoVec[zRepoId]->p_repoPath + zRun_.homePathLen,
            zRun_.p_repoVec[zRepoId]->p_repoAliasPath,
            zRun_.p_repoVec[zRepoId]->createdTime,

            zRun_.p_repoVec[zRepoId]->dpingSig,
            'S' == zGlobRes ? "success" : ('F' == zGlobRes ? "fail" : "in_process"),
            zRun_.p_repoVec[zRepoId]->dpBaseTimeStamp,
            zGlobTimeSpent,
            zRun_.p_repoVec[zRepoId]->totalHost,
            zSuccessCnt,
            zFailCnt,
            zpErrBuf[0] + 1,  /* +1 跳过开头的逗号 */
            zpErrBuf[1] + 1,
            zpErrBuf[2] + 1,
            zpErrBuf[3] + 1,
            zpErrBuf[4] + 1,
            zpErrBuf[5] + 1,
            zpErrBuf[6] + 1,
            zpErrBuf[7] + 1,
            zpErrBuf[8] + 1,
            zpErrBuf[9] + 1,
            zpErrBuf[10] + 1,
            zpErrBuf[11] + 1,
            zWaitingCnt,
            zpStageBuf[0] + 1,
            zpStageBuf[1] + 1,
            zpStageBuf[2] + 1,
            zpStageBuf[3] + 1,

            (0 == zTotalTimes) ? 1.0 : zSuccessTimes / zTotalTimes,
            (0 == zSuccessTimes) ? 0 : zSuccessTimeSpentAll / zSuccessTimes,

            zErrCnt[0] + zErrCnt[1] + zErrCnt[2] + zErrCnt[3] + zErrCnt[4] + zErrCnt[5] + zErrCnt[6] + zErrCnt[7] + zErrCnt[8] + zErrCnt[9] + zErrCnt[10] + zErrCnt[11],
            zErrCnt[0],
            zErrCnt[1],
            zErrCnt[2],
            zErrCnt[3],
            zErrCnt[4],
            zErrCnt[5],
            zErrCnt[6],
            zErrCnt[7],
            zErrCnt[8],
            zErrCnt[9],
            zErrCnt[10],
            zErrCnt[11],

            /* TODO below all... */
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0);

    /* 发送最终结果 */
    zNetUtils_.send_nosignal(zSd, zResBuf, zResSiz);

    /* clean... */
    free(zpStageBuf[0]);

    return 0;
}
#undef zErrClassNum


/*
 * IP_HASH 清零，保证下一次布署动作会初始化所有目标机
 */
static _i
zsys_update(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd __attribute__ ((__unused__))) {
    pthread_rwlock_wrlock(& zRun_.p_sysUpdateLock);

    _i i, j;
    for (i = 0; i <= zRun_.maxRepoId; i++) {
        if (NULL != zRun_.p_repoVec[i]
                && 'Y' == zRun_.p_repoVec[i]->initFinished) {

            for (j = 0; j < zDpHashSiz; j++) {
                zRun_.p_repoVec[i]->p_dpResHash_[j] = NULL;
            }

            /* 使用上述的循环方式赋值，因为并不是所有平台上的 NULL 均被声明为 (void *)0 */
            // memset(zRun_.p_repoVec[i]->p_dpResHash_, 0, zDpHashSiz * sizeof(void *));
        }
    }

    pthread_rwlock_unlock(& zRun_.p_sysUpdateLock);

    return 0;
}


/*
 * 更新源库的代码同步分支名称
 */
static _i
zsource_branch_update(cJSON *zpJRoot, _i zSd) {
    _i zRepoId = 0;
    char *zpNewBranch = NULL;

    cJSON *zpJ = NULL;

    /* 提取项目 ID */
    zpJ = cJSON_V(zpJRoot, "ProjId");
    if (! cJSON_IsNumber(zpJ)) {
        zPrint_Err_Easy("");
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zRun_.p_repoVec[zRepoId]
            || 'Y' != zRun_.p_repoVec[zRepoId]->initFinished) {
        zPrint_Err_Easy("");
        return -2;
    }

    zpJ= cJSON_V(zpJRoot, "CodeSyncBranch");
    if (! cJSON_IsString(zpJ)
            || '\0' == zpJ->valuestring[0]) {
        zPrint_Err_Easy("");
        return -1;
    }
    zpNewBranch = zpJ->valuestring;

    if (255 < strlen(zpNewBranch)) {
        zPrint_Err_Easy("");
        return -47;
    }

    /* 取 rwLock 执行更新 */
    if (0 != pthread_rwlock_trywrlock(& zRun_.p_repoVec[zRepoId]->rwLock)) {
        zPrint_Err_Easy("");
        return -11;
    }

    /* 更新源库对接分支相关的数据 */
    snprintf(zRun_.p_repoVec[zRepoId]->codeSyncBranch, 256, "%s", zpNewBranch);

    snprintf(zRun_.p_repoVec[zRepoId]->p_codeSyncRefs, 560,
            "+refs/heads/%s:refs/heads/%sXXXXXXXX",
            zRun_.p_repoVec[zRepoId]->codeSyncBranch,
            zRun_.p_repoVec[zRepoId]->codeSyncBranch);

    zRun_.p_repoVec[zRepoId]->p_singleLocalRefs =
        zRun_.p_repoVec[zRepoId]->p_codeSyncRefs + (strlen(zRun_.p_repoVec[zRepoId]->p_codeSyncRefs) - 8) / 2 + 1;

    pthread_rwlock_unlock(& zRun_.p_repoVec[zRepoId]->rwLock);

    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1);
    return 0;
}


#undef cJSON_V
