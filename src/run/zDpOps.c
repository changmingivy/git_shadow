#include "zDpOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>
#include <libpq-fe.h>

#include "zNativeUtils.h"
#include "zNetUtils.h"

#include "zLibSsh.h"
#include "zLibGit.h"

#include "zNativeOps.h"

#include "zPosixReg.h"
#include "zThreadPool.h"
#include "zPgSQL.h"
//#include "zMd5Sum.h"

#include "zRun.h"

#define cJSON_V(zpJRoot, zpValueName) cJSON_GetObjectItemCaseSensitive((zpJRoot), (zpValueName))

#define zUN_PATH_SIZ\
        sizeof(struct sockaddr_un)-((size_t) (& ((struct sockaddr_un*) 0)->sun_path))

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

extern struct zNetUtils__ zNetUtils_;
extern struct zNativeUtils__ zNativeUtils_;

extern struct zThreadPool__ zThreadPool_;
extern struct zPosixReg__ zPosixReg_;
extern struct zPgSQL__ zPgSQL_;

extern struct zLibSsh__ zLibSsh_;
extern struct zLibGit__ zLibGit_;

extern struct zNativeOps__ zNativeOps_;

static _i zadd_repo(cJSON *zpJRoot, _i zSd);

static _i zprint_record(cJSON *zpJRoot, _i zSd);
static _i zprint_diff_files(cJSON *zpJRoot, _i zSd);
static _i zprint_diff_content(cJSON *zpJRoot, _i zSd);
static _i zprint_dp_process(cJSON *zpJRoot, _i zSd);

static _i zbatch_deploy(cJSON *zpJRoot, _i zSd);

static _i zglob_res_confirm(cJSON *zpJRoot, _i zSd);
static _i zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__)));
static _i zstate_confirm_ops(_ui zDpID, _i zSelfNodeID, char *zpHostAddr, time_t zTimeStamp, char *zpReplyType, char *zpErrContent);
static _i zstate_confirm_inner(void *zp, _i zSd, struct sockaddr *zpPeerAddr, socklen_t zPeerAddrLen);

static _i zreq_file(cJSON *zpJRoot, _i zSd);

static _i ztcp_pang(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd);
static _i zudp_pang(void *zp, _i zSd,
        struct sockaddr *zpPeerAddr __attribute__ ((__unused__)),
        socklen_t zPeerAddrLen __attribute__ ((__unused__)));

static _i zsource_info_update(cJSON *zpJRoot, _i zSd);

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

    .glob_res_confirm = zglob_res_confirm,
    .state_confirm = zstate_confirm,
    .state_confirm_inner = zstate_confirm_inner,

    .req_file = zreq_file,

    .tcp_pang = ztcp_pang,
    .udp_pang = zudp_pang,

    .repo_update = zsource_info_update,
};


/*
 * 9：打印版本号列表或布署记录
 */
static _i
zprint_record(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;

    _i zDataType = -1;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "dataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zDataType = zpJ->valueint;

    if (0 != pthread_rwlock_tryrdlock(& zpRepo_->cacheLock)) {
        zPRINT_ERR_EASY("");
        return -11;
    };

    /* 数据类型判断 */
    if (zDATA_TYPE_COMMIT == zDataType) {
        zpTopVecWrap_ = & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zDataType) {
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;
    } else {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
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
        char zJsonPrefix[sizeof("{\"errNo\":0,\"cacheID\":%ld,\"data\":") + 16];
        _i zLen = sprintf(zJsonPrefix,
                "{\"errNo\":0,\"cacheID\":%ld,\"data\":",
                zpRepo_->cacheID);

        zNetUtils_.send(zSd, zJsonPrefix, zLen);

        /*
         * 正文
         */
        zNetUtils_.sendmsg(zSd,
                zpTopVecWrap_->p_vec_, zpTopVecWrap_->vecSiz,
                NULL, 0);

        /*
         * json 后缀
         */
        zNetUtils_.send(zSd, "]}", sizeof("]}") - 1);
    } else {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -70;
    }

    pthread_rwlock_unlock(& zpRepo_->cacheLock);
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
        .cacheID = -1,
        .dataType = -1,
        .commitID = -1
    };

    _i zSplitCnt = -1;

    cJSON *zpJ = NULL;

    /*
     * 若上一次布署结果失败或正在布署过程中
     * 不允许查看文件差异内容
     */
    if (zCACHE_DAMAGED == zpRepo_->repoState) {
        zPRINT_ERR_EASY("");
        return -13;
    }

    zpJ = cJSON_V(zpJRoot, "dataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "revID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.commitID = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "cacheID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.cacheID = zpJ->valueint;

    if (zDATA_TYPE_COMMIT == zMeta_.dataType) {
        zpTopVecWrap_= & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zMeta_.dataType) {
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(& zpRepo_->cacheLock)) {
        zPRINT_ERR_EASY("");
        return -11;
    }

    /* CHECK: cacheID */
    if (zMeta_.cacheID != zpRepo_->cacheID) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -8;
    }

    /* CHECK: commitID */
    if ((0 > zMeta_.commitID)
            || ((zCACHE_SIZ - 1) < zMeta_.commitID)
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitID].p_data)) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -3;
    }

    pthread_mutex_lock(& zpRepo_->commLock);
    if (NULL == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID) = (void *) 1;
        pthread_mutex_unlock(& zpRepo_->commLock);

        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(& zpRepo_->cacheLock);
            zPRINT_ERR_EASY("");
            return -71;
        }
    } else if ((void *) 1 == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -11;
    } else if ((void *) -1 == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        /* 无差异 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -71;
    } else {
        pthread_mutex_unlock(& zpRepo_->commLock);
    }

    /*
     * send msg
     */
    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)->p_vec_;
    zSplitCnt = (zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)->vecSiz - 1) / zSEND_UNIT_SIZ  + 1;

    /*
     * json 前缀
     */
    zNetUtils_.send(zSd, "{\"errNo\":0,\"data\":", sizeof("{\"errNo\":0,\"data\":") - 1);

    /*
     * 正文
     * 除最后一个分片之外，其余的分片大小都是 zSEND_UNIT_SIZ
     */
    zSendVecWrap_.vecSiz = zSEND_UNIT_SIZ;
    for (_i i = zSplitCnt; i > 1; i--) {
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, NULL, 0);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

    /* 最后一个分片可能不足 zSEND_UNIT_SIZ，需要单独计算 */
    zSendVecWrap_.vecSiz = (zpTopVecWrap_->p_refData_[zMeta_.commitID].p_subVecWrap_->vecSiz - 1) % zSEND_UNIT_SIZ + 1;
    zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, NULL, 0);

    /*
     * json 后缀
     */
    zNetUtils_.send(zSd, "]}", sizeof("]}") - 1);

    pthread_rwlock_unlock(& zpRepo_->cacheLock);
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
        .commitID = -1,
        .fileID = -1,
        .cacheID = -1,
        .dataType = -1
    };

    cJSON *zpJ = NULL;

    /*
     * 若上一次布署结果失败或正在布署过程中
     * 不允许查看文件差异内容
     */
    if (zCACHE_DAMAGED == zpRepo_->repoState) {
        zPRINT_ERR_EASY("");
        return -13;
    }

    zpJ = cJSON_V(zpJRoot, "dataType");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "revID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.commitID = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "fileID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.fileID = zpJ->valueint;

    zpJ = cJSON_V(zpJRoot, "cacheID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zMeta_.cacheID = zpJ->valueint;

    if (zDATA_TYPE_COMMIT == zMeta_.dataType) {
        zpTopVecWrap_= & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zMeta_.dataType) {
        zpTopVecWrap_= & zpRepo_->dpVecWrap_;
    } else {
        zPRINT_ERR_EASY("");
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(& zpRepo_->cacheLock)) {
        zPRINT_ERR_EASY("");
        return -11;
    };

    if (zMeta_.cacheID != zpRepo_->cacheID) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -8;
    }

    if ((0 > zMeta_.commitID)
            || ((zCACHE_SIZ - 1) < zMeta_.commitID)
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitID].p_data)) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -3;
    }

    pthread_mutex_lock(& zpRepo_->commLock);
    if (NULL == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID) = (void *) 1;
        pthread_mutex_unlock(& zpRepo_->commLock);

        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(& zpRepo_->cacheLock);
            zPRINT_ERR_EASY("");
            return -71;
        }
    } else if ((void *) 1 == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -11;
    } else if ((void *) -1 == zGET_ONE_COMMIT_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID)) {
        /* 无差异 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -71;
    } else {
        pthread_mutex_unlock(& zpRepo_->commLock);
    }

    if ((0 > zMeta_.fileID)
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitID].p_subVecWrap_)
            || ((zpTopVecWrap_->p_refData_[zMeta_.commitID].p_subVecWrap_->vecSiz - 1) < zMeta_.fileID)) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        return -4;\
    }\

    pthread_mutex_lock(& zpRepo_->commLock);
    if (NULL == zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)) {
        zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID) = (void *) 1;
        pthread_mutex_unlock(& zpRepo_->commLock);

        if ((void *) -1 == zNativeOps_.get_diff_contents(&zMeta_)) {
            pthread_rwlock_unlock(& zpRepo_->cacheLock);
            zPRINT_ERR_EASY("");
            return -72;
        }
    } else if ((void *) 1 == zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)) {
        /* 缓存正在生成过程中 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -11;
    } else if ((void *) -1 == zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)) {
        /* 无差异 */
        pthread_mutex_unlock(& zpRepo_->commLock);
        pthread_rwlock_unlock(& zpRepo_->cacheLock);
        zPRINT_ERR_EASY("");
        return -72;
    } else {
        pthread_mutex_unlock(& zpRepo_->commLock);
    }

    /*
     * send msg
     */
    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)->p_vec_;
    zSplitCnt = (zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)->vecSiz - 1) / zSEND_UNIT_SIZ  + 1;

    /*
     * json 前缀:
     * 差异内容的 data 是纯文本，没有 json 结构
     * 此处添加 data 对应的二维 json
     */
    zNetUtils_.send(zSd, "{\"errNo\":0,\"data\":[{\"content\":\"",
            sizeof("{\"errNo\":0,\"data\":[{\"content\":\"") - 1);

    /*
     * 正文
     * 除最后一个分片之外，其余的分片大小都是 zSEND_UNIT_SIZ
     */
    zSendVecWrap_.vecSiz = zSEND_UNIT_SIZ;
    for (_i i = zSplitCnt; i > 1; i--) {
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, NULL, 0);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

    /* 最后一个分片可能不足 zSEND_UNIT_SIZ，需要单独计算 */
    zSendVecWrap_.vecSiz = (zGET_ONE_FILE_VEC_WRAP(zpTopVecWrap_, zMeta_.commitID, zMeta_.fileID)->vecSiz - 1) % zSEND_UNIT_SIZ + 1;
    zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, NULL, 0);

    /*
     * json 后缀，此处需要配对一个引号与大括号
     */
    zNetUtils_.send(zSd, "\"}]}", sizeof("\"}]}") - 1);

    pthread_rwlock_unlock(& zpRepo_->cacheLock);
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
    /* 顺序固定的元信息 */
    char *zpRepoInfo[8] = { NULL };

    pid_t zPid = -1;
    _i zResNo = -1,
       zRepoID = -1;

    char zRepoIDStr[16];

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "repoID");
    if (! cJSON_IsNumber(zpJ)) {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    zRepoID = zpJ->valueint;

    if (0 >= zRepoID
            || zRun_.p_sysInfo_->globRepoNumLimit <= zRepoID) {
        zResNo = -32;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }

    sprintf(zRepoIDStr, "%d", zRepoID);
    zpRepoInfo[0] = zRepoIDStr;

    zpJ = cJSON_V(zpJRoot, "pathOnHost");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    zpRepoInfo[1] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "needPull");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    zpRepoInfo[5] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "sshUserName");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    if (255 < strlen(zpJ->valuestring)) {
        zResNo = -31;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    zpRepoInfo[6] = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "sshPort");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }

    if (5 < strlen(zpJ->valuestring)) {
        zResNo = -39;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }
    zpRepoInfo[7] = zpJ->valuestring;

    if ('Y' == toupper(zpRepoInfo[5][0])) {
        zpJ = cJSON_V(zpJRoot, "sourceURL");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zResNo = -34;
            zPRINT_ERR_EASY("");
            goto zEndMark;
        }
        zpRepoInfo[2] = zpJ->valuestring;

        zpJ = cJSON_V(zpJRoot, "sourceBranch");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zResNo = -34;
            zPRINT_ERR_EASY("");
            goto zEndMark;
        }
        zpRepoInfo[3] = zpJ->valuestring;

        zpJ = cJSON_V(zpJRoot, "sourceVcsType");
        if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            zResNo = -34;
            zPRINT_ERR_EASY("");
            goto zEndMark;
        }
        zpRepoInfo[4] = zpJ->valuestring;
    } else if ('N' == toupper(zpRepoInfo[5][0])) {
        zpRepoInfo[2] = "";
        zpRepoInfo[3] = "";
        zpRepoInfo[4] = "Git";
    } else {
        zResNo = -34;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }

    /* 检查项目 ID 是否冲突 */
    pthread_mutex_lock(zRun_.p_commLock);
    if (zRun_.p_sysInfo_->masterPid
            != zRun_.p_sysInfo_->repoPidVec[zRepoID]) {
        pthread_mutex_unlock(zRun_.p_commLock);

        zResNo = -35;
        goto zEndMark;
    }

    {////
        char *zpData;
        _i zLen = 8;
        _i j;
        for (j = 0; j < 8; j++) {
            zLen += strlen(zpRepoInfo[j]);
        }

        /* 不需要释放，留存用于项目重启 */
        zMEM_ALLOC(zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID], char, 8 * sizeof(void *) + zLen);
        zpData = (char *) (zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID] + 8);

        zLen = 0;
        for (j = 0; j < 8; j++) {
            zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID][j] = zpData + zLen;

            zLen += sprintf(zpData + zLen, "%s", zpRepoInfo[j]);
            zLen++;
        }

        /* 子进程中的副本需要清理 */
        cJSON_Delete(zpJRoot);
    }////

    if (0 > (zPid = fork())) {
        pthread_mutex_unlock(zRun_.p_commLock);

        zResNo = -126;
        goto zEndMark;
    }

    /*
     * DO creating...
     * 创建的最终结果通知，会由子进程发出
     */
    if (0 == zPid) {
        pthread_mutex_unlock(zRun_.p_commLock);

        zNativeOps_.repo_init(zRun_.p_sysInfo_->pp_repoMetaVec[zRepoID], zSd);
    } else {
        zRun_.p_sysInfo_->repoPidVec[zRepoID] = zPid;
        pthread_mutex_unlock(zRun_.p_commLock);

        zResNo = 0;
        goto zEndMark;
    }

zEndMark:
    return zResNo;
}


/* 简化参数版函数 */
static _i
zssh_exec_simple(const char *zpSSHUserName,
        char *zpHostAddr, char *zpSSHPort,
        char *zpCmd,
        sem_t *zpCcurSem, char *zpErrBufOUT) {

    return zLibSsh_.exec(
            zpHostAddr,
            zpSSHPort,
            zpCmd,
            zpSSHUserName,
            zRun_.p_sysInfo_->p_sshPubKeyPath,
            zRun_.p_sysInfo_->p_sshPrvKeyPath,
            NULL,
            zPubKeyAuth,
            NULL,
            0,
            zpCcurSem,
            zpErrBufOUT);
}


#define zGENERATE_SSH_CMD(zpCmdBuf) do {\
    sprintf(zpCmdBuf,\
            "zServPath=%s;zPath=%s;zIP=%s;zPort=%s;zMd5=%s"\
            "kill `ps ax -o pid,ppid,cmd|grep -oP \"^.*(?=git-receive-pack\\s+${zPath}/.git)\"`;"\
\
            "exec 5<>/dev/tcp/${zIP}/${zPort};"\
            "printf '{\"opsID\":0}'>&5;"\
            "if [[ '!' != `cat<&5` ]];then exit 210;fi;"\
            "exec 5>&-;exec 5<&-;"\
            "git;if [[ 127 -eq $? ]];then exit 207;fi;"/* git 环境是否已安装 */\
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
            "if [[ ${zMd5} != `md5sum post-update|grep -oE '^.{32}'|tr '[A-Z]' '[a-z]'` ]];then "\
                "zTcpReq \"${zIP}\" \"${zPort}\" \"{\\\"opsID\\\":14,\\\"path\\\":\\\"${zServPath}/tools/post-update\\\"}\" \"${zPath}/.git/hooks/post-update\";"\
                "if [[ ${zMd5} != `md5sum post-update|grep -oE '^.{32}'|tr '[A-Z]' '[a-z]'` ]];then exit 212;fi;"\
            "fi;"\
            "chmod 0755 ${zPath}/.git/hooks/post-update;",\
            zRun_.p_sysInfo_->p_servPath,\
            zpRepo_->p_path + zRun_.p_sysInfo_->homePathLen,\
            zRun_.p_sysInfo_->netSrv_.p_ipAddr,\
            zRun_.p_sysInfo_->netSrv_.p_port,\
            zRun_.p_sysInfo_->gitHookMD5);\
} while(0)

#define zDEL_SINGLE_QUOTATION(zpStr) {\
    _i zLen = strlen(zpStr);\
    for (_i i = 0; i < zLen; i++) {\
        if ('\'' == zpStr[i]) {\
            zpStr[i] = ' ';\
        }\
    }\
}

#define zSTATE_CONFIRM() {\
    zpInnerState_->selfNodeIndex = zpDpCcur_->selfNodeIndex;\
    zpInnerState_->dpID = zpDpCcur_->dpID;\
\
    strcpy(zpInnerState_->hostAddr, zpDpCcur_->p_hostAddr);\
\
    sendto(zpRepo_->unSd, zData, 1 + sizeof(struct zInnerState__), MSG_NOSIGNAL,\
            (struct sockaddr *) & zRun_.p_sysInfo_->unAddrVec_[zpRepo_->id], zRun_.p_sysInfo_->unAddrLenVec[zpRepo_->id]);\
}

struct zInnerState__ {
    _i selfNodeIndex;
    _ui dpID;
    char replyType[4];
    char hostAddr[INET6_ADDRSTRLEN];
    char errMsg[256];
};

static _i
zstate_confirm_inner(void *zp,
        _i zSd __attribute__ ((__unused__)),
        struct sockaddr *zpPeerAddr __attribute__ ((__unused__)),
        socklen_t zPeerAddrLen __attribute__ ((__unused__))) {

    struct zInnerState__ *zpState_ = (struct zInnerState__ *) zp;
    _i zErrNo = 0;

    pthread_rwlock_rdlock(& zpRepo_->cacheLock);

    /* 判断是否是延迟到达的信息 */
    if (zpState_->dpID != zpRepo_->dpID) {
        pthread_rwlock_unlock(& zpRepo_->cacheLock);

        zErrNo = -101;
        zPRINT_ERR_EASY(zpState_->hostAddr);
    } else {
        /* 调用此函数时，必须加锁 */
        zErrNo = zstate_confirm_ops(zpState_->dpID, zpState_->selfNodeIndex, zpState_->hostAddr,
                zpRepo_->dpBaseTimeStamp, zpState_->replyType, zpState_->errMsg);

        pthread_rwlock_unlock(& zpRepo_->cacheLock);
    }

    return zErrNo;
}

static void
zdp_ccur(zDpCcur__ *zpDpCcur_) {
    char zHostAddrBuf[INET6_ADDRSTRLEN] = {'\0'};
    char zRemoteRepoAddrBuf[64 + zpRepo_->pathLen];

    char zGitRefsBuf[2][256 + zpRepo_->pathLen],
         *zpGitRefs[2] = {
             zGitRefsBuf[0],
             zGitRefsBuf[1]
         };

    _i zErrNo = 0;

    char zData[1 + sizeof(struct zInnerState__)];

    zData[0] = '1';
    struct zInnerState__ *zpInnerState_ = (struct zInnerState__ *) (zData + 1);

    // zpDpCcur_->errNo = 0;

    /* 判断是否需要执行目标机初始化环节 */
    if ('Y' == zpDpCcur_->needInit) {
        if (0 == (zErrNo = zssh_exec_simple(
                        zpRepo_->sshUserName,
                        zpDpCcur_->p_hostAddr,
                        zpRepo_->sshPort,
                        zpRepo_->p_sysDpCmd,
                        NULL,
                        zpInnerState_->replyType))) {
            strcpy(zpInnerState_->replyType, "S1");
            zSTATE_CONFIRM();
        } else {
            zpDpCcur_->errNo = -23;

            snprintf(zpInnerState_->replyType, 4, "E%d", -zErrNo);
            zSTATE_CONFIRM();
            goto zEndMark;
        }
    }

    /*
     * !!!!
     * URL 中使用 IPv6 地址必须用中括号包住，否则无法解析
     * !!!!
     */
    sprintf(zRemoteRepoAddrBuf, "ssh://%s@[%s]:%s%s%s/.git",
            zpRepo_->sshUserName,
            zpDpCcur_->p_hostAddr,
            zpRepo_->sshPort,
            '/' == zpRepo_->p_path[0]? "" : "/",
            zpRepo_->p_path + zRun_.p_sysInfo_->homePathLen);

    /*
     * 将目标机 IPv6 中的 ':' 替换为 '_'
     * 之后将其附加到分支名称上去
     * 分支名称的一个重要用途是用于捎带信息至目标机
     */
    strcpy(zHostAddrBuf, zpDpCcur_->p_hostAddr);
    for (_i i = 0; '\0' != zHostAddrBuf[i]; i++) {
        if (':' == zHostAddrBuf[i]) {
            zHostAddrBuf[i] = '_';
        }
    }

    /* push TWO branchs together */
    snprintf(zpGitRefs[0], 256 + zpRepo_->pathLen,
            "+refs/heads/%sXXXXXXXX:refs/heads/s@%s@%s@%d@%s@%u@%ld@%s@%s@%c",
            zpRepo_->p_codeSyncBranch,
            zRun_.p_sysInfo_->netSrv_.specStrForGit,
            zRun_.p_sysInfo_->netSrv_.p_port,
            zpRepo_->id,
            zHostAddrBuf,
            zpRepo_->dpID,
            zpRepo_->dpBaseTimeStamp,
            zpRepo_->dpingSig,
            zpRepo_->p_aliasPath,
            zpRepo_->forceDpMark);

    snprintf(zpGitRefs[1], 256 + zpRepo_->pathLen,
            "+refs/heads/____shadowXXXXXXXX:refs/heads/S@%s@%s@%d@%s@%u@%ld@%s@%s@%c",
            zRun_.p_sysInfo_->netSrv_.specStrForGit,
            zRun_.p_sysInfo_->netSrv_.p_port,
            zpRepo_->id,
            zHostAddrBuf,
            zpRepo_->dpID,
            zpRepo_->dpBaseTimeStamp,
            zpRepo_->dpingSig,
            zpRepo_->p_aliasPath,
            zpRepo_->forceDpMark);

    /* 向目标机 push 布署内容 */
    if (0 == (zErrNo = zLibGit_.remote_push(
                    zpRepo_->p_gitHandler,
                    zRemoteRepoAddrBuf,
                    zpGitRefs, 2,
                    zpInnerState_->errMsg))) {
        strcpy(zpInnerState_->replyType, "S2");
        zSTATE_CONFIRM();
        goto zEndMark;
    } else {
        /*
         * 错误码为 -1 时，
         * 表示未完全确定是不可恢复错误，需要重试
         * 否则可确定此台目标机布署失败
         */
        if (-1 == zErrNo) {
            /*
             * 重试布署时，一律重新初始化目标机环境
             */
            if (0 == (zErrNo = zssh_exec_simple(
                            zpRepo_->sshUserName,
                            zpDpCcur_->p_hostAddr,
                            zpRepo_->sshPort,
                            zpRepo_->p_sysDpCmd,
                            NULL,
                            NULL))) {

                /* if init-ops success, then try deploy once more... */
                if (0 == (zErrNo = zLibGit_.remote_push(
                                zpRepo_->p_gitHandler,
                                zRemoteRepoAddrBuf,
                                zpGitRefs, 2,
                                zpInnerState_->errMsg))) {
                    strcpy(zpInnerState_->replyType, "S2");
                    zSTATE_CONFIRM();
                    goto zEndMark;
                } else {
                    zpDpCcur_->errNo = -12;

                    snprintf(zpInnerState_->replyType, 4, "E%d", -zErrNo);
                    zSTATE_CONFIRM();
                    goto zEndMark;
                }
            } else {
                zpDpCcur_->errNo = -23;

                snprintf(zpInnerState_->replyType, 4, "E%d", -zErrNo);
                zSTATE_CONFIRM();
                goto zEndMark;
            }
        } else {
            zpDpCcur_->errNo = -12;

            snprintf(zpInnerState_->replyType, 4, "E%d", -zErrNo);
            zSTATE_CONFIRM();
            goto zEndMark;
        }
    }

    /*
     * !!!! 非核心功能 !!!!
     * 运行用户指定的布署后动作，不提供执行结果保证
     */
    if (NULL != zpRepo_->p_userDpCmd) {
        if (0 != zssh_exec_simple(
                    zpRepo_->sshUserName,
                    zpDpCcur_->p_hostAddr,
                    zpRepo_->sshPort,
                    zpRepo_->p_userDpCmd,
                    NULL,
                    NULL)) {

            zpDpCcur_->errNo = -14;
            zPRINT_ERR_EASY(zpDpCcur_->p_hostAddr);
        }
    }

zEndMark:
    return;
}


/*
 * 12：布署／撤销
 */
#define zJSON_PARSE() do {  /* json 解析 */\
    zpJ = cJSON_V(zpJRoot, "revSig");\
    if (cJSON_IsString(zpJ) && 40 == strlen(zpJ->valuestring)) {\
        zpForceSig = zpJ->valuestring;\
    } else {\
        zpJ = cJSON_V(zpJRoot, "cacheID");\
        if (! cJSON_IsNumber(zpJ)) {\
            zResNo = -1;\
            zPRINT_ERR_EASY("");\
            goto zEndMark;\
        }\
        zCacheID = zpJ->valueint;\
\
        zpJ = cJSON_V(zpJRoot, "revID");\
        if (! cJSON_IsNumber(zpJ)) {\
            zResNo = -1;\
            zPRINT_ERR_EASY("");\
            goto zEndMark;\
        }\
        zCommitID = zpJ->valueint;\
    }\
\
    zpJ = cJSON_V(zpJRoot, "dataType");\
    if (! cJSON_IsNumber(zpJ)) {\
        zResNo = -1;\
        zPRINT_ERR_EASY("");\
        goto zEndMark;\
    }\
    zDataType = zpJ->valueint;\
\
    zpJ = cJSON_V(zpJRoot, "ipList");\
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {\
        zResNo = -1;\
        zPRINT_ERR_EASY("");\
        goto zEndMark;\
    }\
    zpIpList = zpJ->valuestring;\
    zIpListStrLen = strlen(zpIpList);\
\
    zpJ = cJSON_V(zpJRoot, "ipCnt");\
    if (! cJSON_IsNumber(zpJ)) {\
        zResNo = -1;\
        zPRINT_ERR_EASY("");\
        goto zEndMark;\
    }\
    zIpCnt = zpJ->valueint;\
\
    /* 同一项目所有目标机的 ssh 用户名必须相同 */\
    zpJ = cJSON_V(zpJRoot, "sshUserName");\
    if (cJSON_IsString(zpJ) || '\0' != zpJ->valuestring[0]) {\
        zpSSHUserName = zpJ->valuestring;\
    }\
\
    /* 同一项目所有目标机的 sshd 端口必须相同 */\
    zpJ = cJSON_V(zpJRoot, "sshPort");\
    if (cJSON_IsString(zpJ) || '\0' != zpJ->valuestring[0]) {\
        zpSSHPort = zpJ->valuestring;\
    }\
\
    zpJ = cJSON_V(zpJRoot, "postDpCmd");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        zpPostDpCmd = zNativeOps_.alloc(\
                sizeof("cd  && ()")\
                + zpRepo_->pathLen - zRun_.p_sysInfo_->homePathLen\
                + strlen(zpJ->valuestring));\
        sprintf(zpPostDpCmd, "cd %s && (%s)",\
                zpRepo_->p_path + zRun_.p_sysInfo_->homePathLen,\
                zpJ->valuestring);\
    }\
\
    zpJ = cJSON_V(zpJRoot, "aliasPath");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        snprintf(zpRepo_->p_aliasPath, zpRepo_->maxPathLen, "%s", zpJ->valuestring);\
    } else {\
        zpRepo_->p_aliasPath[0] = '\0';\
    }\
\
    zpJ = cJSON_V(zpJRoot, "delim");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        zpDelim = zpJ->valuestring;\
    }\
\
    zpJ = cJSON_V(zpJRoot, "forceDp");\
    if (cJSON_IsString(zpJ) && '\0' != zpJ->valuestring[0]) {\
        zForceDpMark = toupper(zpJ->valuestring[0]);\
    }\
} while(0)

/* atexit() 释放信号量 */
static void
zdp_exit_clean(void) {
    sem_post(zThreadPool_.p_threadPoolSem);
}

static _i
zbatch_deploy(cJSON *zpJRoot, _i zSd) {
    _i zResNo = 0;
    zVecWrap__ *zpTopVecWrap_ = NULL;

    char *zpCommonBuf = NULL;
    char *zpSQLBuf = NULL;
    size_t zSQLLen = 0;

    zbool_t zIsSameSig = zFalse;
    zDpRes__ *zpTmp_ = NULL;

    _i zCacheID = -1,
       zCommitID = -1,
       zDataType = -1,
       i = 0;

    char *zpIpList = NULL;
    _i zIpListStrLen = 0,
       zIpCnt = 0;

    char *zpSSHUserName = NULL,
         *zpSSHPort = NULL;

    /*
     * 强制布署标志：是否可直接删除有冲穾的文件或路径
     */
    char zForceDpMark = 'N';

    /*
     * IP 字符串的分割符
     * 若没有明确指定，则默认为空格
     */
    char *zpDelim = " ",
         *zpForceSig = NULL,
         *zpPostDpCmd = NULL;

    cJSON *zpJ = NULL;

    /*
     * 提取其余的 json 信息
     */
    zJSON_PARSE();

    /*
     * 检查 pgSQL 是否可以正常连通
     */
    if (zFalse == zPgSQL_.conn_check(zRun_.p_sysInfo_->pgConnInfo)) {
        zResNo = -90;
        zPRINT_ERR_EASY("pgSQL conn failed");
        goto zEndMark;
    }

    /*
     * check system load
     */
    if (80 < zRun_.p_sysInfo_->memLoad) {
        zResNo = -16;
        zPRINT_ERR_EASY("mem load too high");
        goto zEndMark;
    }

    /*
     * dpWaitLock 用于确保同一时间不会有多个新布署请求阻塞排队，
     * 避免拥塞持续布署的混乱情况
     */
    if (0 != pthread_mutex_trylock(& (zpRepo_->dpWaitLock))) {
        zResNo = -11;
        zPRINT_ERR_EASY("");
        goto zEndMark;
    }

    /*
     * 通知可能存在的旧的布署动作终止：
     * 阻塞等待布署主锁，用于保证同一时间，只有一个布署动作在运行
     */
    pthread_mutex_trylock(& zpRepo_->dpSyncLock);
    zpRepo_->dpWaitMark = 1;
    pthread_mutex_unlock(& zpRepo_->dpSyncLock);
    pthread_cond_signal(& zpRepo_->dpSyncCond);

    /*
     * 持有 dpLock 的布署动作，
     * 将 dpWaitMark 置 1
     */
    pthread_mutex_lock(& zpRepo_->dpLock);
    zpRepo_->dpWaitMark = 0;

    pthread_mutex_unlock(& (zpRepo_->dpWaitLock));

    /*
     * 布署过程中，标记缓存状态为 Damaged
     */
    zpRepo_->repoState = zCACHE_DAMAGED;

    /*
     * 判断是新版本布署，还是旧版本回撤
     */
    if (zDATA_TYPE_COMMIT == zDataType) {
        zpTopVecWrap_= & zpRepo_->commitVecWrap_;
    } else if (zDATA_TYPE_DP == zDataType) {
        zpTopVecWrap_ = & zpRepo_->dpVecWrap_;
    } else {
        zResNo = -10;  /* 无法识别 */
        zPRINT_ERR_EASY("BUG !");
        goto zCleanMark;
    }

    /*
     * 非强制指定版本号的情况下，
     * 检查布署请求中标记的 CacheID 是否有效
     * 检查指定的版本号是否有效
     */
    if (NULL == zpForceSig) {
        if (zCacheID != zpRepo_->cacheID) {
            zResNo = -8;
            zPRINT_ERR_EASY("cacheID invalid");
            goto zCleanMark;
        }

        if (0 > zCommitID
                || (zCACHE_SIZ - 1) < zCommitID
                || NULL == zpTopVecWrap_->p_refData_[zCommitID].p_data) {
            zResNo = -3;
            zPRINT_ERR_EASY("commitID invalid");
            goto zCleanMark;
        }
    }

    /*
     * 转存正在布署的版本号
     */
    if (NULL == zpForceSig) {
        strcpy(zpRepo_->dpingSig,
                zGET_ONE_COMMIT_SIG(zpTopVecWrap_, zCommitID));
    } else {
        strcpy(zpRepo_->dpingSig, zpForceSig);
    }

    /*
     * 每次尝试将 ____shadowXXXXXXXX 分支删除
     * 避免该分支体积过大，不必关心执行结果
     */
    zLibGit_.branch_del(zpRepo_->p_gitHandler, "____shadowXXXXXXXX");

    /*
     * 执行一次空提交到 ____shadowXXXXXXXX 分支
     * 确保每次 push 都能触发 post-update 勾子
     */
    if (0 != zLibGit_.add_and_commit(zpRepo_->p_gitHandler,
                "refs/heads/____shadowXXXXXXXX", ".", "_")) {
        zResNo = -15;
        zPRINT_ERR_EASY("libgit2 err");
        goto zCleanMark;
    }

    /*
     * 目标机 IP 列表处理
     * 使用定制的 alloc 函数，从项目内存池中分配内存
     * 需要检查目标机集合是否为空，或数量不符
     */
    zRegRes__ zRegRes_ = {
        .alloc_fn = zNativeOps_.alloc,
    };

    zPosixReg_.str_split(&zRegRes_, zpIpList, zpDelim);

    if (0 == zRegRes_.cnt || zIpCnt != zRegRes_.cnt) {
        zPRINT_ERR_EASY("host IPs'cnt err");
        zResNo = -28;
        goto zCleanMark;
    }

    /*
     * 预算本函数用到的最大 BufSiz
     * 在所有同步错误检查通过之后分配
     */
    zpCommonBuf = zNativeOps_.alloc(
            2048
            + zRun_.p_sysInfo_->servPathLen
            + zpRepo_->pathLen);

    zpSQLBuf = zNativeOps_.alloc(
                sizeof("INSERT INTO dp_log (repo_id,dp_id,time_stamp,rev_sig,host_ip) VALUES ") - 1
                + (sizeof("($1,$2,$3,$4,''),") - 1) * zRegRes_.cnt
                + zIpListStrLen);
    {////
        /*
         * 更新最近一次布署尝试的版本号
         * 若 SSH 认证信息有变动，亦更新之
         */
        _i zLen = 0;
        zLen = sprintf(zpCommonBuf,
                "UPDATE repo_meta SET last_try_sig = '%s',last_dp_id = %u",
                zpRepo_->dpingSig,zpRepo_->dpID);

        if (NULL != zpSSHUserName
                && 0 != strcmp(zpSSHUserName, zpRepo_->sshUserName)) {
            snprintf(zpRepo_->sshUserName, 256,
                    "%s", zpSSHUserName);

            zLen += sprintf(zpCommonBuf + zLen,
                    ",ssh_user_name = '%s'",
                    zpSSHUserName);
        }

        if (NULL != zpSSHPort
                && 0 != strcmp(zpSSHPort, zpRepo_->sshPort)) {
            snprintf(zpRepo_->sshPort, 6,
                    "%s", zpSSHPort);

            zLen += sprintf(zpCommonBuf + zLen,
                    ",ssh_port = '%s'",
                    zpSSHPort);
        }

        sprintf(zpCommonBuf + zLen,
                " WHERE repo_id = %d",
                zpRepo_->id);


        if (0 != zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zpCommonBuf, NULL)) {
            /* 数据库不可用，停止服务 ? */
            zPRINT_ERR_EASY("!!!! FATAL !!!!");
            exit(1);
        }
    }////

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
    zpRepo_->totalHost = zRegRes_.cnt;

    /* 总任务数，初始赋值为目标机总数，后续会依不同条件动态变动 */
    zpRepo_->dpTotalTask = zpRepo_->totalHost;

    /* 任务完成数：确定成功或确定失败均视为完成任务 */
    zpRepo_->dpTaskFinCnt = 0;

    /*
     * 用于标记已完成的任务中，是否存在失败的结果
     * 目标机初始化环节出错置位 bit[0]
     * 布署环节出错置位 bit[1]
     */
    zpRepo_->resType = 0;

    /*
     * 若目标机数量超限，则另行分配内存
     * 否则使用预置的静态空间
     */
    if (zFORECASTED_HOST_NUM < zpRepo_->totalHost) {
        zpRepo_->p_dpCcur_ =
            zNativeOps_.alloc(zRegRes_.cnt * sizeof(zDpCcur__));
    } else {
        zpRepo_->p_dpCcur_ = zpRepo_->dpCcur_;
    }

    /*
     * 暂留上一次布署的 HashMap
     * 用于判断目标机增减差异，并据此决定每台目标机需要执行的动作
     */
    zDpRes__ *zpOldDpResHash_[zDP_HASH_SIZ];
    memcpy(zpOldDpResHash_, zpRepo_->p_dpResHash_,
            zDP_HASH_SIZ * sizeof(zDpRes__ *));
    zDpRes__ *zpOldDpResList_ = zpRepo_->p_dpResList_;

    /* get cacheLock */
    pthread_rwlock_wrlock(& zpRepo_->cacheLock);

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询
     * 因此不能在项目内存池中分配
     * 分配清零的空间，简化状态重置及重复 IP 检查
     */
    zMEM_C_ALLOC(zpRepo_->p_dpResList_, zDpRes__, zRegRes_.cnt);

    /*
     * Clear hash buf before reuse it!!!
     */
    memset(zpRepo_->p_dpResHash_,
            0, zDP_HASH_SIZ * sizeof(zDpRes__ *));

    /*
     * !!!!!!!!!!!!!!!!!!!!!!!!==
     * !!!! 正式开始布署动作 !!!!
     * !!!!!!!!!!!!!!!!!!!!!!!!==
     */
    if (0 == strcmp(zpRepo_->dpingSig,
                zpRepo_->lastDpSig)) {
        zIsSameSig = zTrue;
    }

    /* 生成目标机初始化的命令 */
    zGENERATE_SSH_CMD(zpCommonBuf);
    zpRepo_->p_sysDpCmd = zpCommonBuf;

    /* 于此处更新项目结构中的强制布署标志 */
    zpRepo_->forceDpMark = zForceDpMark;

    /* 布署耗时基准 */
    zpRepo_->dpBaseTimeStamp = time(NULL);

    /* 拼接预插入布署记录的 SQL 命令 */
    zSQLLen = sizeof("INSERT INTO dp_log (repo_id,dp_id,time_stamp,rev_sig,host_ip) VALUES ") - 1;

    for (i = 0; i < zpRepo_->totalHost; i++) {
        /* 检测是否存在重复IP */
        if (NULL != zpOldDpResHash_[zpRepo_->p_dpResList_[i].clientAddr[0] % zDP_HASH_SIZ]
                && (0 != zpRepo_->p_dpResList_[i].clientAddr[0]
                    || 0 != zpRepo_->p_dpResList_[i].clientAddr[1])) {

            /* 总任务计数递减 */
            zpRepo_->dpTotalTask--;

            zPRINT_ERR_EASY("same IP found");
            continue;
        }

        /*
         * IPnum 链表赋值
         * 并转换字符串格式的 IPaddr 为数组 _ull[2]
         */
        zpRepo_->p_dpResList_[i].selfNodeIndex = i;
        if (0 != zCONVERT_IPSTR_TO_NUM(zRegRes_.pp_rets[i],
                    zpRepo_->p_dpResList_[i].clientAddr)) {

            /* 此种错误信息记录到哪里 ??? */
            zPRINT_ERR_EASY("invalid IP");
            continue;
        }

        /* 所在空间是使用 calloc 分配的，此处不必再手动置零 */
        // zpRepo_->p_dpResList_[i].resState = 0;  /* 成功状态 */
        // zpRepo_->p_dpResList_[i].errState = 0;  /* 错误状态 */
        // zpRepo_->p_dpResList_[i].p_next = NULL;

        /* 是否需要初始化 */
        zpRepo_->p_dpCcur_[i].needInit = 'Y';

        /* 目标机 IP */
        zpRepo_->p_dpCcur_[i].p_hostAddr = zRegRes_.pp_rets[i];

        /* 工作线程返回的错误码及 pid，预置为 0 */
        zpRepo_->p_dpCcur_[i].errNo = 0;
        zpRepo_->p_dpCcur_[i].pid = 0;

        /* dpID and selfNodeIndex */
        zpRepo_->p_dpCcur_[i].dpID = zpRepo_->dpID;
        zpRepo_->p_dpCcur_[i].selfNodeIndex = i;

        /*
         * 更新 HashMap
         */
        zpTmp_ = zpRepo_->p_dpResHash_[
            zpRepo_->p_dpResList_[i].clientAddr[0] % zDP_HASH_SIZ
        ];

        /*
         * 若 HashMap 顶层为空，直接指向链表中对应的位置
         */
        if (NULL == zpTmp_) {
            zpRepo_->p_dpResHash_[
                zpRepo_->p_dpResList_[i].clientAddr[0] % zDP_HASH_SIZ
            ] = & zpRepo_->p_dpResList_[i];
        } else {
            while (NULL != zpTmp_->p_next) {
                zpTmp_ = zpTmp_->p_next;
            }

            zpTmp_->p_next = & zpRepo_->p_dpResList_[i];
        }

        /*
         * 基于旧的 HashMap 检测是否是新加入的目标机
         */
        zpTmp_ = zpOldDpResHash_[
            zpRepo_->p_dpResList_[i].clientAddr[0]
                % zDP_HASH_SIZ
        ];

        while (NULL != zpTmp_) {
            /*
             * 若目标机 IPaddr 已存在
             * 且初始化结果是成功的
             * 则跳过远程初始化环节
             */
            if (zIPVEC_CMP(zpTmp_->clientAddr, zpRepo_->p_dpResList_[i].clientAddr)
                    && zCHECK_BIT(zpTmp_->resState, 4)) {

                if (zIsSameSig) {
                    /* 复制上一次的全部状态位 */
                    zpRepo_->p_dpResList_[i].resState = zpTmp_->resState;

                    /* 总任务数递减 */
                    zpRepo_->dpTotalTask--;

                    /* 务必置为 -1，表示不存在对应的工作进程 */
                    zpRepo_->p_dpCcur_[i].pid = -1;

                    goto zSkipMark;
                } else {
                    /* 是否执行目标机初始化命令 */
                    zpRepo_->p_dpCcur_[i].needInit = 'N';

                    break;
                }
            }

            zpTmp_ = zpTmp_->p_next;
        }

        /*
         * 拼接预插入布署记录的 SQL 命令
         * 不能在 dp_ccur 内执行插入，
         * 否则当其所在线程被中断时，其占用的 DB 资源将无法释放
         */
        zSQLLen += sprintf(zpSQLBuf + zSQLLen,
                "($1,$2,$3,$4,'%s'),",
                zRegRes_.pp_rets[i]);
zSkipMark:;
    }

    /* release cacheLock */
    pthread_rwlock_unlock(& zpRepo_->cacheLock);

    if (0 == zpRepo_->dpTotalTask) {
        goto zCleanMark;
    }

    /* 去除末尾多余的逗号 */
    zpSQLBuf[zSQLLen - 1] = '\0';

    memcpy(zpSQLBuf,
            "INSERT INTO dp_log (repo_id,dp_id,time_stamp,rev_sig,host_ip) VALUES ",
            sizeof("INSERT INTO dp_log (repo_id,dp_id,time_stamp,rev_sig,host_ip) VALUES ") - 1);

    {////
        char zRepoIDStr[24];
        char zDpIDStr[24];
        char zTimeStamp[24];
        const char *zpParam[4] = {
            zRepoIDStr,
            zDpIDStr,
            zTimeStamp,
            zpRepo_->dpingSig
        };

        sprintf(zRepoIDStr, "%d", zpRepo_->id);
        sprintf(zDpIDStr, "%u", zpRepo_->dpID);
        sprintf(zTimeStamp, "%ld", zpRepo_->dpBaseTimeStamp);

        /* 预插入本次布署记录条目 */
        if (0 != zPgSQL_.exec_with_param_once(
                    zRun_.p_sysInfo_->pgConnInfo,
                    zpSQLBuf,
                    4,
                    zpParam,
                    NULL)) {
            /* 数据库不可用，停止服务 ? */
            zPRINT_ERR_EASY("!!!! FATAL !!!!");
            exit(1);
        }
    }////

    /* 执行布署 */
    for (i = 0; i < zpRepo_->totalHost; i++) {
        if (-1 != zpRepo_->dpCcur_[i].pid) {
            /* 工作进程退出时会释放信号量 */
            sem_wait(zThreadPool_.p_threadPoolSem);

            zCHECK_NEGATIVE_EXIT(
                    zpRepo_->dpCcur_[i].pid = fork()
                    );

            if (0 == zpRepo_->dpCcur_[i].pid) {
                atexit(zdp_exit_clean);
                zdp_ccur(& zpRepo_->dpCcur_[i]);
                exit(0);
            }
        }
    }

    /* 释放旧的资源占用 */
    if (NULL != zpOldDpResList_) {
        free(zpOldDpResList_);
    }

    /*
     * 等待所有工作线程完成任务
     * 或新的布署请求到达
     */
    pthread_mutex_lock(& (zpRepo_->dpSyncLock));
    while (0 == zpRepo_->dpWaitMark
            && zpRepo_->dpTaskFinCnt < zpRepo_->dpTotalTask) {
        pthread_cond_wait(
                & zpRepo_->dpSyncCond,
                & zpRepo_->dpSyncLock);
    }
    pthread_mutex_unlock( (& zpRepo_->dpSyncLock) );

    /*
     * 运行至此，首先要判断：是被新的布署请求中断 ？
     * 还是返回全部的部署结果 ?
     */
    if (0 == zpRepo_->dpWaitMark) {
        /*
         * 若布署成功且版本号与上一次成功布署的不同时
         * 才需要刷新缓存
         */
        if (0 == zpRepo_->resType && !zIsSameSig) {
            /*
             * 以上一次成功布署的版本号为名称
             * 创建一个新分支，用于保证回撤的绝对可行性
             */
            if (0 != zLibGit_.branch_add(
                        zpRepo_->p_gitHandler,
                        zpRepo_->lastDpSig,
                        zpRepo_->lastDpSig,
                        zTrue)) {
                zPRINT_ERR_EASY("branch create err");
            }

            /* 更新最新一次布署版本号 */
            strcpy(zpRepo_->lastDpSig, zpRepo_->dpingSig);

            /* 发送待写的 SQL(含末尾 '\0') 至 DB write 服务 */
            i = 1;
            zpCommonBuf[0] = '8';
            i += sprintf(zpCommonBuf + 1,
                    "UPDATE repo_meta SET last_dp_sig = '%s',alias_path = '%s' "
                    "WHERE repo_id = %d",
                    zpRepo_->lastDpSig,
                    zpRepo_->p_aliasPath,
                    zpRepo_->id);

            sendto(zpRepo_->unSd, zpCommonBuf, 1 + i, MSG_NOSIGNAL,
                    (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                    zRun_.p_sysInfo_->unAddrLenMaster);

            /*
             * 无论布署成功还是被中断，均需要 wait，以消除僵尸进程
             * 必须在重置内存池之前执行，否则可能丢失 pid
             */
            for (i = 0; i < zpRepo_->totalHost; i++) {
                if (0 < zpRepo_->p_dpCcur_[i].pid) {
                    waitpid(zpRepo_->p_dpCcur_[i].pid, NULL, 0);
                }
            }

            /* 获取写锁，此时将拒绝所有查询类请求 */
            pthread_rwlock_wrlock(&zpRepo_->cacheLock);

            /* 更新布署动作 ID，必须在 cacheLock 内执行 */
            zpRepo_->dpID++;

            /* 项目内存池复位 */
            zMEM_POOL_REST( zpRepo_->id );

            /* 刷新缓存 */
            zCacheMeta__ zSubMeta_;

            zSubMeta_.dataType = zDATA_TYPE_COMMIT;
            zNativeOps_.get_revs(& zSubMeta_);

            zSubMeta_.dataType = zDATA_TYPE_DP;
            zNativeOps_.get_revs(& zSubMeta_);

            /* update cacheID */
            zpRepo_->cacheID++;

            pthread_rwlock_unlock(&zpRepo_->cacheLock);

            /* 标记缓存为可用状态 */
            zpRepo_->repoState = zCACHE_GOOD;
        } else {
            pthread_rwlock_wrlock(&zpRepo_->cacheLock);
            zpRepo_->dpID++;
            pthread_rwlock_unlock(&zpRepo_->cacheLock);

            for (i = 0; i < zpRepo_->totalHost; i++) {
                if (0 < zpRepo_->p_dpCcur_[i].pid) {
                    waitpid(zpRepo_->p_dpCcur_[i].pid, NULL, 0);
                }
            }
        }
    } else {
        /* 更新布署动作 ID，必须在 cacheLock 内执行 */
        pthread_rwlock_wrlock(&zpRepo_->cacheLock);
        zpRepo_->dpID++;
        pthread_rwlock_unlock(&zpRepo_->cacheLock);

        /* 若是被新布署请求打断 */
        for (i = 0; i < zpRepo_->totalHost; i++) {
            /* 终止尚未完工的布署进程 */
            if (0 < zpRepo_->p_dpCcur_[i].pid) {
                kill(zpRepo_->p_dpCcur_[i].pid, SIGUSR1);
            }
        }

        for (i = 0; i < zpRepo_->totalHost; i++) {
            if (0 < zpRepo_->p_dpCcur_[i].pid) {
                waitpid(zpRepo_->p_dpCcur_[i].pid, NULL, 0);
            }
        }

        zResNo = -127;
        zPRINT_ERR_EASY("Deploy interrupted");
    }

zCleanMark:
    /* !!!! 释放布署主锁 !!!! */
    pthread_mutex_unlock(& zpRepo_->dpLock);

zEndMark:
    return zResNo;
}


/*
 * TCP 9：布署成功目标机返回的确认
 */
static _i
zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    _ui zDpID = 0;
    time_t zTimeStamp = 0;
    char *zpHostAddr = NULL;
    char *zpReplyType = NULL;
    char *zpErrContent = NULL;

    _ull zHostID[2] = {0};
    zDpRes__ *zpTmp_ = NULL;
    _i zIndex = -1;
    _i zResNo = 0;

    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "timeStamp");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zTimeStamp = (time_t)zpJ->valuedouble;

    zpJ = cJSON_V(zpJRoot, "hostAddr");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zpHostAddr = zpJ->valuestring;

    zpJ = cJSON_V(zpJRoot, "dpID");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zDpID = zpJ->valuedouble;

    /* 格式，SN: S1..S9，EN: E3..E8 */
    zpJ = cJSON_V(zpJRoot, "replyType");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]
            || 2 > strlen(zpJ->valuestring)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zpReplyType = zpJ->valuestring;

    /* 错误信息允许为空，不需要检查提取到的内容 */
    zpJ = cJSON_V(zpJRoot, "content");
    if (cJSON_IsString(zpJ)
            || '\0' != zpJ->valuestring[0]) {
        zpErrContent = zpJ->valuestring;
    } else {
        zpErrContent = "";
    }

    /* 检查IP合法性 */
    if (0 != zCONVERT_IPSTR_TO_NUM(zpHostAddr, zHostID)) {
        zPRINT_ERR_EASY("");
        return -18;
    }

    /* 正文...遍历信息链 */
    pthread_rwlock_rdlock(& zpRepo_->cacheLock);

    /*
     * 判断是否是延迟到达的信息
     * 已失效信息，直接略过，不记录
     */
    if (zDpID != zpRepo_->dpID) {

        pthread_rwlock_unlock(& zpRepo_->cacheLock);

        zResNo = -101;
        zPRINT_ERR_EASY(zpHostAddr);
    } else {
        for (zpTmp_ = zpRepo_->p_dpResHash_[zHostID[0] % zDP_HASH_SIZ];
                zpTmp_ != NULL;
                zpTmp_ = zpTmp_->p_next) {
            if ( zIPVEC_CMP(zpTmp_->clientAddr, zHostID) ) {
                zIndex = zpTmp_->selfNodeIndex;

                zResNo = zstate_confirm_ops(zDpID, zIndex,
                        zpHostAddr, zTimeStamp, zpReplyType, zpErrContent);

                break;
            }
        }

        pthread_rwlock_unlock(& zpRepo_->cacheLock);

        if (-1 == zIndex) {
            zResNo = -103;
            zPRINT_ERR_EASY(zpHostAddr);
        }
    }

    return zResNo;
}


/*
 * 阶段成功或出错确认
 * 本地直接调用
 */
#define zTASK_FIN_NOTICE() {\
    /* 全局计数原子性 +1 */\
    pthread_mutex_lock(& zpRepo_->dpSyncLock);\
    zpRepo_->dpTaskFinCnt++;\
    pthread_mutex_unlock(& zpRepo_->dpSyncLock);\
\
    /* 若任务计数已满，则通知调度者 */\
    if (zpRepo_->dpTaskFinCnt == zpRepo_->dpTotalTask) {\
        pthread_cond_signal(& zpRepo_->dpSyncCond);\
    }\
}

static _i
zstate_confirm_ops(_ui zDpID, _i zSelfNodeID, char *zpHostAddr, time_t zTimeStamp,
        char *zpReplyType, char *zpErrContent) {

    _i zResNo = 0,
       zRetBit = 0,
       i;

    char zCmdBuf[zGLOB_COMMON_BUF_SIZ] = {'\0'};

    /* 检查信息类型是否合法 */
    zRetBit = strtol(zpReplyType + 1, NULL, 10);
    if (0 >= zRetBit || 24 < zRetBit) {
        zResNo = -1;
        zPRINT_ERR_EASY(zpHostAddr);
        goto zEndMark;
    }

    /*
     * 'S[N]'：每个阶段的布署成果上报
     * 'E[N]'：错误信息分类上报
     */
    if ('E' == zpReplyType[0]) {
        zSET_BIT(zpRepo_->p_dpResList_[zSelfNodeID].errState, zRetBit);

        /* 发生错误，置位表示出错返回 */
        zSET_BIT(zpRepo_->resType, 1);

        /* 转存错误信息 */
        strncpy(zpRepo_->p_dpResList_[zSelfNodeID].errMsg, zpErrContent, 255);
        zpRepo_->p_dpResList_[zSelfNodeID].errMsg[255] = '\0';

        zTASK_FIN_NOTICE();

        /* 写入 DB 时需要清除单引号 */
        zDEL_SINGLE_QUOTATION(zpErrContent);

        /*
         * 发送待写的 SQL(含末尾 '\0') 至 DB write 服务
         * postgreSQL 的数组下标是从 1 开始的
         * dp_id 可唯一定位单次布署动作，不需要对比 revSig
         */
        i = 1;
        zCmdBuf[0] = '8';
        i += snprintf(zCmdBuf + 1, zGLOB_COMMON_BUF_SIZ - 1,
                "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "
                "WHERE repo_id = %d AND host_ip = '%s' AND time_stamp = %ld AND dp_id = %d",
                zRetBit, zpErrContent,
                zpRepo_->id, zpHostAddr, zTimeStamp, zDpID);

        sendto(zpRepo_->unSd, zCmdBuf, 1 + i, MSG_NOSIGNAL,
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                zRun_.p_sysInfo_->unAddrLenMaster);

        zResNo = -102;
        goto zEndMark;
    } else if ('S' == zpReplyType[0]) {
        zSET_BIT(zpRepo_->p_dpResList_[zSelfNodeID].resState, zRetBit);

        /*
         * 最终成功的状态到达时，
         * 才需要递增全局计数，并通知完工信息；
         * 发送待写的 SQL(含末尾 '\0') 至 DB write 服务
         */
        i = 1;
        zCmdBuf[0] = '8';
        if ('4' == zpReplyType[1]) {
            zTASK_FIN_NOTICE();

            i += snprintf(zCmdBuf + 1, zGLOB_COMMON_BUF_SIZ - 1,
                    "UPDATE dp_log SET host_res[%d] = '1',host_timespent = %ld "
                    "WHERE repo_id = %d AND host_ip = '%s' AND time_stamp = %ld AND dp_id = %d",
                    zRetBit, 1 + time(NULL) - zpRepo_->dpBaseTimeStamp,
                    zpRepo_->id, zpHostAddr, zTimeStamp, zDpID);
        } else {
            i += snprintf(zCmdBuf + 1, zGLOB_COMMON_BUF_SIZ - 1,
                    "UPDATE dp_log SET host_res[%d] = '1' "
                    "WHERE repo_id = %d AND host_ip = '%s' AND time_stamp = %ld AND dp_id = %d",
                    zRetBit,
                    zpRepo_->id, zpHostAddr, zTimeStamp, zDpID);
        }


        sendto(zpRepo_->unSd, zCmdBuf, 1 + i, MSG_NOSIGNAL,
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster,
                zRun_.p_sysInfo_->unAddrLenMaster);

        zResNo = 0;
        goto zEndMark;
    } else {
        zResNo = -1;
        zPRINT_ERR_EASY(zpHostAddr);
        goto zEndMark;
    }

zEndMark:
    return zResNo;
}


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

    zpJ = cJSON_V(zpJRoot, "path");
    if (! cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zPRINT_ERR_EASY("");
        return -7;
    }

    if (0 > (zFd = open(zpJ->valuestring, O_RDONLY))) {
        shutdown(zSd, SHUT_RDWR);

        zPRINT_ERR_EASY_SYS();
        return -80;
    }

    /* 此处并未保证传输过程的绝对可靠性 */
    while (0 < (zDataLen = read(zFd, zDataBuf, 4096))) {
        if (zDataLen != zNetUtils_.send(zSd, zDataBuf, zDataLen)) {
            close(zFd);
            shutdown(zSd, SHUT_RDWR);

            zPRINT_ERR_EASY("file trans err");
            return -126;
        }
    }

    close(zFd);
    return 0;
}


/*
 * TCP 0：Ping、Pang
 * 目标机使用此接口测试与服务端的连通性
 */
static _i
ztcp_pang(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd) {
    /*
     * 目标机发送 "?"
     * 服务端回复 "!"
     */
    return zNetUtils_.send(zSd, "!", zBYTES(1));
}


/*
 * 7：目标机自身布署成功之后，向服务端核对全局结果
 * 若全局结果是失败，则执行回退
 * 全局成功，回复 "S"，否则回复 "F"，尚未确定最终结果回复 "W"
 */
static _i
zglob_res_confirm(cJSON *zpJRoot, _i zSd) {
    time_t zTimeStamp = 0;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_V(zpJRoot, "timeStamp");
    if (! cJSON_IsNumber(zpJ)) {
        zPRINT_ERR_EASY("");
        return -1;
    }
    zTimeStamp = (time_t)zpJ->valuedouble;

    if (zTimeStamp < zpRepo_->dpBaseTimeStamp) {
        /*
         * 若已有新的布署动作产生，统一返回成功标识，
         */
        zNetUtils_.send(zSd, "S", zBYTES(1));
    } else {
        if (zpRepo_->dpTaskFinCnt == zpRepo_->dpTotalTask) {
            if (0 == zpRepo_->resType) {
                /* 确定成功 */
                zNetUtils_.send(zSd, "S", zBYTES(1));
            } else {
                /* 确定失败 */
                zNetUtils_.send(zSd, "F", zBYTES(1));
            }
        } else {
            if (0 == zpRepo_->resType) {
                /* 结果尚未确定，正在 waiting... */
                zNetUtils_.send(zSd, "W", zBYTES(1));
            } else {
                /* 确定失败 */
                zNetUtils_.send(zSd, "F", zBYTES(1));
            }
        }
    }

    return 0;
}


/*
 * 15：布署进度实时查询接口，同时包含项目元信息，示例如下：
 */
/** !!!! 样例 !!!! **
 * {
 *   "RepoMeta": {
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
 *           "HostGitInvalid": [
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
#define zSQL_EXEC() do {\
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zTrue))) {\
            zPgSQL_.conn_clear(zpPgConnHd_);\
            free(zpStageBuf[0]);\
            zPRINT_ERR_EASY("SQL exec err");\
            return -91;\
        }\
\
        if (NULL == (zpPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {\
            zPgSQL_.conn_clear(zpPgConnHd_);\
            zPgSQL_.res_clear(zpPgResHd_, NULL);\
            free(zpStageBuf[0]);\
            zPRINT_ERR_EASY("SQL result err");\
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
#define zERR_CLASS_NUM 12

static _i
zprint_dp_process(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd) {
    char zSQLBuf[zGLOB_COMMON_BUF_SIZ] = {'\0'};
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;
    zPgRes__ *zpPgRes_ = NULL;

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

    /* 从 DB 中提取项目创建时间 */
    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zRun_.p_sysInfo_->pgConnInfo))) {
        zPRINT_ERR_EASY("DB connect failed");
        return -90;
    }

    /*
     * 成功、失败、正在进行中的计数
     */
    _i zSuccessCnt = 0,
       zFailCnt = 0,
       zWaitingCnt = 0;

    /* 存放目标机实时信息的 BUF */
    _i zStageBufLen = zpRepo_->dpTotalTask * (INET6_ADDRSTRLEN + 3),
       zErrBufLen = zpRepo_->dpTotalTask * (INET6_ADDRSTRLEN + 256);

    char *zpStageBuf[4],
         *zpErrBuf[zERR_CLASS_NUM];

    /* 一次性分配所需的全部空间 */
    zMEM_ALLOC(zpStageBuf[0], char, 4 * zStageBufLen + zERR_CLASS_NUM * zErrBufLen);
    zpStageBuf[0][1] = '\0';

    _i i, j;
    for (i = 1; i < 4; i++) {
        zpStageBuf[i] = zpStageBuf[i - 1] + zStageBufLen;
        zpStageBuf[i][1] = '\0';
    }

    zpErrBuf[0] = zpStageBuf[3] + zStageBufLen;
    zpErrBuf[0][1] = '\0';
    for (i = 1; i < zERR_CLASS_NUM; i++) {
        zpErrBuf[i] = zpErrBuf[i - 1] + zErrBufLen;
        zpErrBuf[i][1] = '\0';
    }

    _i zStageOffSet[4] = {0},
       zErrOffSet[zERR_CLASS_NUM] = {0};

    char zIpStrBuf[INET6_ADDRSTRLEN];
    for (i = 0; i < zpRepo_->totalHost; i++) {
        if (0 == zpRepo_->p_dpResList_[i].errState) {  /* 无错 */
            if (zCHECK_BIT(zpRepo_->p_dpResList_[i].resState, 4)) {  /* 已确定成功 */
                zSuccessCnt++;
            } else {  /* 阶段性成功，即未确认完全成功 */
                if (0 != zCONVERT_IPNUM_TO_STR(
                            zpRepo_->p_dpResList_[i].clientAddr,
                            zIpStrBuf)) {
                    zPRINT_ERR_EASY("IPConvert err");
                } else {
                    if (! zCHECK_BIT(zpRepo_->p_dpResList_[i].resState, 1)) {
                        zStageOffSet[1] += snprintf(
                                zpStageBuf[1] + zStageOffSet[1],
                                zStageBufLen - zStageOffSet[1],
                                ",\"%s\"",
                                zIpStrBuf);
                    } else {
                        for (j = 4; j > 1; j--) {
                            if (zCHECK_BIT(zpRepo_->p_dpResList_[i].resState, j - 1)) {
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
            if (0 != zCONVERT_IPNUM_TO_STR(
                        zpRepo_->p_dpResList_[i].clientAddr,
                        zIpStrBuf)) {
                zPRINT_ERR_EASY("IPConvert err");
            } else {
                for (j = 0; j < zERR_CLASS_NUM; j++) {
                    if (zCHECK_BIT(
                                zpRepo_->p_dpResList_[i].errState,
                                j + 1)) {
                        zErrOffSet[j] += snprintf(
                                zpErrBuf[j] + zErrOffSet[j],
                                zErrBufLen - zErrOffSet[j],
                                ",\"%s|%s\"",
                                zIpStrBuf,
                                zpRepo_->p_dpResList_[i].errMsg);
                        break;
                    }
                }
            }
        }
    }

    /* 总数减去已确定结果的，剩余的就是正在进行中的... */
    zWaitingCnt = zpRepo_->totalHost - zSuccessCnt - zFailCnt;

    /*
     * 从 DB 中提取最近 30 天的记录
     * 首先生成一张临时表，以提高效率
     */
    pthread_mutex_lock(& (zpRepo_->commLock));
    _ui zTbNo = ++zpRepo_->tempTableNo;
    pthread_mutex_unlock(& (zpRepo_->commLock));

    sprintf(zSQLBuf,
            "CREATE TABLE tmp_%d_%u as SELECT host_ip,host_res,host_err,host_timespent,time_stamp FROM dp_log "
            "WHERE repo_id = %d AND time_stamp > %ld",
            zpRepo_->id, zTbNo,
            zpRepo_->id, time(NULL) - 3600 * 24 * 30);

    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
        /*
         * 执行失败，则尝试
         * 删除可能存在的重名表
         */
        char zTmpBuf[64];
        snprintf(zTmpBuf, 64, "DROP TABLE tmp_%d_%u", zpRepo_->id, zTbNo);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zTmpBuf, zFalse))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            free(zpStageBuf[0]);

            zPRINT_ERR_EASY("SQL exec err");
            return -91;
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }

        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            free(zpStageBuf[0]);
            zPRINT_ERR_EASY("SQL exec failed");
            return -91;
        }
    } else {
        zPgSQL_.res_clear(zpPgResHd_, NULL);
    }

    /* 目标机总台次 */
    sprintf(zSQLBuf, "SELECT count(host_ip) FROM tmp_%d_%u", zpRepo_->id, zTbNo);
    zSQL_EXEC();

    _f zTotalTimes = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /* 布署成功的总台次 */
    sprintf(zSQLBuf,
            "SELECT count(host_ip) FROM tmp_%d_%u "
            "WHERE host_res[4] = '1'",
            zpRepo_->id, zTbNo);
    zSQL_EXEC();

    _f zSuccessTimes = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);

    /* 所有布署成功台次的耗时之和 */
    sprintf(zSQLBuf,
            "SELECT sum(host_timespent) FROM tmp_%d_%u "
            "WHERE host_res[4] = '1'",
            zpRepo_->id, zTbNo);
    zSQL_EXEC();

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
            "FROM tmp_%d_%u",
            zpRepo_->id, zTbNo);
    zSQL_EXEC();

    _c zErrCnt[zERR_CLASS_NUM];
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
    if (zpRepo_->dpTaskFinCnt == zpRepo_->dpTotalTask) {
        if (0 == zpRepo_->resType) {
            zGlobRes = 'S';
        } else {
            zGlobRes = 'F';
        }
    } else {
        if (0 == zpRepo_->resType) {
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
        zGlobTimeSpent = time(NULL) - zpRepo_->dpBaseTimeStamp;
    } else {
        sprintf(zSQLBuf,
                "SELECT max(host_timespent) FROM tmp_%d_%u "
                "WHERE time_stamp = %ld",
                zpRepo_->id, zTbNo,
                zpRepo_->dpBaseTimeStamp);
        zSQL_EXEC();

        zGlobTimeSpent = strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
        zPgSQL_.res_clear(zpPgResHd_, zpPgRes_);
    }

    /* 删除临时表 */
    sprintf(zSQLBuf, "DROP TABLE tmp_%d_%d", zpRepo_->id, zTbNo);
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {
        zPgSQL_.conn_clear(zpPgConnHd_);
        free(zpStageBuf[0]);

        zPRINT_ERR_EASY("SQL exec err");
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
            "{\"errNo\":0,\"repoMeta\":{\"id\":%d,\"path\":\"%s\",\"aliasPath\":\"%s\",\"createdTime\":\"%s\"},\"recentDpInfo\":{\"revSig\":\"%s\",\"result\":\"%s\",\"timeStamp\":%ld,\"timeSpent\":%d,\"process\":{\"total\":%d,\"success\":%d,\"fail\":{\"cnt\":%d,\"detail\":{\"servErr\":[%s],\"netServToHost\":[%s],\"sshAuth\":[%s],\"hostDisk\":[%s],\"hostPermission\":[%s],\"hostFileConflict\":[%s],\"hostPathNotExist\":[%s],\"hostGitInvalid\":[%s],\"hostAddrInvalid\":[%s],\"netHostToServ\":[%s],\"hostLoad\":[%s],\"reqFileNotExist\":[%s]}},\"inProcess\":{\"cnt\":%d,\"stage\":{\"hostInit\":[%s],\"servDpOps\":[%s],\"hostRecvWaiting\":[%s],\"hostConfirmWaiting\":[%s]}}}},\"dpDataAnalysis\":{\"successRate\":%.2f,\"avgTimeSpent\":%.2f,\"errClassification\":{\"total\":%d,\"servErr\":%d,\"netServToHost\":%d,\"sshAuth\":%d,\"hostDisk\":%d,\"hostPermission\":%d,\"hostFileConflict\":%d,\"hostPathNotExist\":%d,\"hostGitInvalid\":%d,\"hostAddrInvalid\":%d,\"netHostToServ\":%d,\"hostLoad\":%d,\"reqFileNotExist\":%d}},\"hostDataAnalysis\":{\"cpu\":{\"avgLoad\":%.2f,\"loadBalance\":%.2f},\"mem\":{\"avgLoad\":%.2f,\"loadBalance\":%.2f},\"io/Net\":{\"avgLoad\":%.2f,\"loadBalance\":%.2f},\"io/Disk\":{\"avgLoad\":%.2f,\"loadBalance\":%.2f},\"diskUsage\":{\"current\":%.2f,\"avg\":%.2f,\"max\":%.2f}}}",
            zpRepo_->id,
            zpRepo_->p_path + zRun_.p_sysInfo_->homePathLen,
            zpRepo_->p_aliasPath,
            zpRepo_->createdTime,

            zpRepo_->dpingSig,
            'S' == zGlobRes ? "success" : ('F' == zGlobRes ? "fail" : "in_process"),
            zpRepo_->dpBaseTimeStamp,
            zGlobTimeSpent,
            zpRepo_->totalHost,
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
    zNetUtils_.send(zSd, zResBuf, zResSiz);

    /* clean... */
    free(zpStageBuf[0]);

    return 0;
}


/*
 * NO.3
 * 更新源库的代码同步分支名称及源库的 URL
 */
static _i
zsource_info_update(cJSON *zpJRoot, _i zSd) {
    char zErrBuf[256] = {'\0'};

    char *zpNewURL = NULL;
    char *zpNewBranch = NULL;

    _uc zNullMark = 0;

    _us zSourceUrlLen = 0,
        zSourceBranchLen = 0,
        zSyncRefsLen = 0;

    cJSON *zpJ = NULL;

    zpJ= cJSON_V(zpJRoot, "sourceURL");
    if (cJSON_IsString(zpJ)
            && '\0' != zpJ->valuestring[0]) {
        zpNewURL = zpJ->valuestring;
    }

    zpJ= cJSON_V(zpJRoot, "sourceBranch");
    if (cJSON_IsString(zpJ)
            && '\0' != zpJ->valuestring[0]) {
        zpNewBranch = zpJ->valuestring;
    }

    /* 若指定的信息与原有信息无差别，跳过之后的操作 */
    if ((NULL == zpNewBranch
                || 0 == strcmp(zpNewBranch, zpRepo_->p_codeSyncBranch))
            && (NULL == zpNewURL
                || 0 == strcmp(zpNewURL, zpRepo_->p_codeSyncURL))) {

        zNetUtils_.send(zSd, "{\"errNo\":0}", sizeof("{\"errNo\":0}") - 1);
    } else {
        if (NULL == zpNewURL) {
            zSET_BIT(zNullMark, 1);
            zpNewURL = zpRepo_->p_codeSyncURL;
        }

        if (NULL == zpNewBranch) {
            zSET_BIT(zNullMark, 2);
            zpNewBranch = zpRepo_->p_codeSyncBranch;
        }

        zSourceUrlLen = strlen(zpNewURL);
        zSourceBranchLen = strlen(zpNewBranch);
        zSyncRefsLen = sizeof("+refs/heads/:refs/heads/XXXXXXXX") -1 + 2 * zSourceBranchLen;

        {////
            /* 测试新的信息是否有效... */
            char zRefs[sizeof("+refs/heads/:refs/heads/XXXXXXXX") + 2 * strlen(zpNewBranch)],
                 *zpRefs = zRefs;
            sprintf(zRefs, "+refs/heads/%s:refs/heads/%sXXXXXXXX",
                    zpNewBranch, zpNewBranch);
            if (0 > zLibGit_.remote_fetch(
                        zpRepo_->p_gitHandler,
                        zpNewURL,
                        &zpRefs, 1,
                        zErrBuf)) {
                zPRINT_ERR_EASY(zErrBuf);
                return -49;
            }

            /*
             * 若源库分支不存在，上述的 fetch 动作检测不到，
             * 依然会返回 0，故需要此步进一步检测
             */
            zGitRevWalk__ *zpRevWalker = zLibGit_.generate_revwalker(
                    zpRepo_->p_gitHandler,
                    zRefs + sizeof("+refs/heads/:") -1 + zSourceBranchLen,
                    0);
            if (NULL == zpRevWalker) {
                zPRINT_ERR_EASY("");
                return -49;
            } else {
                zLibGit_.destroy_revwalker(zpRevWalker);
            }
        }////

        {////
            char zSQLBuf[128 + zSourceUrlLen + zSourceBranchLen];
            _i zResNo = -1;

            /* 首先更新 DB，无错则继续之后的动作 */
            sprintf(zSQLBuf,
                    "UPDATE repo_meta SET source_url = '%s', source_branch = '%s' WHERE repo_id = %d",
                    zpNewURL,
                    zpNewBranch,
                    zpRepo_->id);

            if (0 != (zResNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zSQLBuf, NULL))) {
                pthread_rwlock_unlock(& zpRepo_->cacheLock);
                return zResNo;
            }
        }////

        /* 取锁，暂停 code_sync 动作 */
        pthread_mutex_lock(& zpRepo_->sourceUpdateLock);

        /*
         * update...
         * 值为 NULL 的项已被置位
         */
        if (! zCHECK_BIT(zNullMark, 1)) {
            free(zpRepo_->p_codeSyncURL);
            zMEM_ALLOC(zpRepo_->p_codeSyncURL, char, 1 + zSourceUrlLen);
            strcpy(zpRepo_->p_codeSyncURL, zpNewURL);
        }

        if (! zCHECK_BIT(zNullMark, 2)) {
            free(zpRepo_->p_codeSyncBranch);
            zMEM_ALLOC(zpRepo_->p_codeSyncBranch, char, 1 + zSourceBranchLen);
            strcpy(zpRepo_->p_codeSyncBranch, zpNewBranch);

            free(zpRepo_->p_codeSyncRefs);
            zMEM_ALLOC(zpRepo_->p_codeSyncRefs, char, 1 + zSyncRefsLen);
            sprintf(zpRepo_->p_codeSyncRefs,
                    "+refs/heads/%s:refs/heads/%sXXXXXXXX",
                    zpNewBranch,
                    zpNewBranch);
        }

        zpRepo_->p_localRef =
            zpRepo_->p_codeSyncRefs + sizeof("+refs/heads/:") - 1 + zSourceBranchLen;

        pthread_mutex_unlock(& zpRepo_->sourceUpdateLock);

        zNetUtils_.send(zSd, "{\"errNo\":0}", sizeof("{\"errNo\":0}") - 1);
    }

    return 0;
}


/*
 ********************************
 * ******** UDP SERVER ******** *
 ********************************
 */
/*
 * UDP 0：Ping、Pang
 * 目标机使用此接口测试与服务端的连通性
 */
static _i
zudp_pang(void *zp __attribute__ ((__unused__)),
        _i zSd,
        struct sockaddr *zpPeerAddr,
        socklen_t zPeerAddrLen) {

    /*
     * 目标机发送 "?"
     * 服务端回复 "!"
     */
    if (NULL == zpPeerAddr) {
        return zNetUtils_.send(zSd, "!", zBYTES(1));
    } else {
        return zNetUtils_.sendto(zSd, "!", zBYTES(1),
                zpPeerAddr, zPeerAddrLen);
    }
}


#undef zUN_PATH_SIZ
#undef zERR_CLASS_NUM
#undef zJSON_PARSE
#undef zGENERATE_SSH_CMD
#undef zSTATE_CONFIRM
#undef zDEL_SINGLE_QUOTATION
#undef cJSON_V
