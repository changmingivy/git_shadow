#include "zDpOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

extern struct zNetUtils__ zNetUtils_;
extern struct zLibSsh__ zLibSsh_;
extern struct zLibGit__ zLibGit_;
extern struct zNativeOps__ zNativeOps_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zPosixReg__ zPosixReg_;
extern struct zThreadPool__ zThreadPool_;
extern struct zPgSQL__ zPgSQL_;

extern _i zGlobHomePathLen;

extern char *zpGlobSSHPort;
extern char *zpGlobSSHPubKeyPath;
extern char *zpGlobSSHPrvKeyPath;

static _i
zshow_one_repo_meta(cJSON *zpJRoot, _i zSd);

static _i
zadd_repo(cJSON *zpJRoot, _i zSd);

static _i
zprint_record(cJSON *zpJRoot, _i zSd);

static _i
zprint_diff_files(cJSON *zpJRoot, _i zSd);

static _i
zprint_diff_content(cJSON *zpJRoot, _i zSd);

static _i
zself_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__)));

static _i
zbatch_deploy(cJSON *zpJRoot, _i zSd);

static _i
zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__)));

static _i
zlock_repo(cJSON *zpJRoot, _i zSd);

static _i
zunlock_repo(cJSON *zpJRoot, _i zSd);

static _i
zreq_file(cJSON *zpJRoot, _i zSd);

/* 对外公开的统一接口 */
struct zDpOps__ zDpOps_ = {
    .show_meta = zshow_one_repo_meta,
    .print_revs = zprint_record,
    .print_diff_files = zprint_diff_files,
    .print_diff_contents = zprint_diff_content,
    .creat = zadd_repo,
    .req_dp = zself_deploy,
    .dp = zbatch_deploy,
    .state_confirm = zstate_confirm,
    .lock = zlock_repo,
    .unlock = zunlock_repo,
    .req_file = zreq_file
};


/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId(zpMeta_) do {\
    if ((0 > zpMeta_->commitId)\
            || ((zCacheSiz - 1) < zpMeta_->commitId)\
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_data)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        return -3;\
    }\
} while(0)


/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId(zpMeta_) do {\
    if ((0 > zpMeta_->fileId)\
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_)\
            || ((zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_->vecSiz - 1) < zpMeta_->fileId)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        return -4;\
    }\
} while(0)


/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId(zpMeta_) do {\
    if (zpGlobRepo_[zpMeta_->repoId]->cacheId != zpMeta_->cacheId) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        zpMeta_->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;\
        return -8;\
    }\
} while(0)


/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State(zpMeta_) do {\
    if (zDpLocked == zpGlobRepo_[zpMeta_->repoId]->dpLock) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        return -6;\
    }\
} while(0)


// static void *
// zssh_ccur(void  *zpParam) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;
//
//     zLibSsh_.exec(zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort, zpDpCcur_->p_cmd,
//             zpDpCcur_->p_userName, zpDpCcur_->p_pubKeyPath, zpDpCcur_->p_privateKeyPath, zpDpCcur_->p_passWd, zpDpCcur_->authType,
//             zpDpCcur_->p_remoteOutPutBuf, zpDpCcur_->remoteOutPutBufSiz, zpDpCcur_->p_ccurLock, zErrBuf);
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
zssh_exec_simple(char *zpHostIpAddr, char *zpCmd, pthread_mutex_t *zpCcurLock, char *zpErrBufOUT) {
    return zLibSsh_.exec(zpHostIpAddr, zpGlobSSHPort, zpCmd, "git", zpGlobSSHPubKeyPath, zpGlobSSHPrvKeyPath, NULL, 1, NULL, 0, zpCcurLock, zpErrBufOUT);
}


/* 简化参数版函数 */
// static void *
// zssh_ccur_simple(void  *zpParam) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;
//
//     zssh_exec_simple(zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_cmd, zpDpCcur_->p_ccurLock, zErrBuf);
//
//     pthread_mutex_lock(zpDpCcur_->p_ccurLock);
//     (* (zpDpCcur_->p_taskCnt))++;
//     pthread_mutex_unlock(zpDpCcur_->p_ccurLock);
//     pthread_cond_signal(zpDpCcur_->p_ccurCond);
//
//     return NULL;
// };


/* 远程主机初始化专用 */
static void *
zssh_ccur_simple_init_host(void  *zpParam) {
    char zErrBuf[256] = {'\0'};
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;

    _ui zHostId = zNetUtils_.to_bin(zpDpCcur_->p_hostIpStrAddr);
    zDpRes__ *zpTmp_ = zpGlobRepo_[zpDpCcur_->repoId]->p_dpResHash_[zHostId % zDpHashSiz];
    for (; NULL != zpTmp_; zpTmp_ = zpTmp_->p_next) {
        if (zHostId == zpTmp_->clientAddr) {
            if (0 == zssh_exec_simple(zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_cmd, zpDpCcur_->p_ccurLock, zErrBuf)) {
                zpTmp_->initState = 1;
            } else {
                zpTmp_->initState = -1;
                zpGlobRepo_[zpDpCcur_->repoId]->resType[0] = -1;
                strcpy(zpTmp_->errMsg, zErrBuf);
            }

            pthread_mutex_lock(zpDpCcur_->p_ccurLock);
            (* (zpDpCcur_->p_taskCnt))++;
            pthread_mutex_unlock(zpDpCcur_->p_ccurLock);
            pthread_cond_signal(zpDpCcur_->p_ccurCond);

            break;
        }
    }

    return NULL;
};


#define zNative_Fail_Confirm() do {\
    _ui ____zHostId = zNetUtils_.to_bin(zpDpCcur_->p_hostIpStrAddr);\
    zDpRes__ *____zpTmp_ = zpGlobRepo_[zpDpCcur_->repoId]->p_dpResHash_[____zHostId % zDpHashSiz];\
    for (; NULL != ____zpTmp_; ____zpTmp_ = ____zpTmp_->p_next) {\
        if (____zHostId == ____zpTmp_->clientAddr) {\
            pthread_mutex_lock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
            ____zpTmp_->dpState = -1;\
            zpGlobRepo_[zpDpCcur_->repoId]->resType[1] = -1;\
            strcpy(____zpTmp_->errMsg, zErrBuf);\
\
            zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt = zpGlobRepo_[zpDpCcur_->repoId]->dpTotalTask;  /* 发生错误，计数打满，用于通知结束布署等待状态 */\
            pthread_cond_signal(zpGlobRepo_[zpDpCcur_->repoId]->p_dpCcur_->p_ccurCond);\
            pthread_mutex_unlock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
            break;\
        }\
    }\
} while(0)


static void *
zgit_push_ccur(void *zp_) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp_;

    char zErrBuf[256] = {'\0'};
    char zRemoteRepoAddrBuf[64 + zpGlobRepo_[zpDpCcur_->repoId]->repoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_wait(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );

    /* when memory load > 80%，waiting ... */
    pthread_mutex_lock(&zGlobCommonLock);
    while (80 < zGlobMemLoad) {
        pthread_cond_wait(&zGlobCommonCond, &zGlobCommonLock);
    }
    pthread_mutex_unlock(&zGlobCommonLock);

    /* generate remote URL */
    sprintf(zRemoteRepoAddrBuf, "ssh://git@%s/%s/.git", zpDpCcur_->p_hostIpStrAddr, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen);

    /* {'+' == git push --force} push TWO branchs together */
    sprintf(zpGitRefs[0], "+refs/heads/master:refs/heads/server%d", zpDpCcur_->repoId);
    sprintf(zpGitRefs[1], "+refs/heads/master_SHADOW:refs/heads/server%d_SHADOW", zpDpCcur_->repoId);
    if (0 != zLibGit_.remote_push(zpGlobRepo_[zpDpCcur_->repoId]->p_gitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2, NULL)) {
        /* if failed, delete '.git', ReInit the remote host */
        char zCmdBuf[1024 + 7 * zpGlobRepo_[zpDpCcur_->repoId]->repoPathLen];
        sprintf(zCmdBuf,
                "rm -f %s %s_SHADOW;"  /* if symlink, delete it, or do nothing... */
                "mkdir -p %s %s_SHADOW;"
                "cd %s_SHADOW && rm -rf .git; git init . && git config user.name _ && git config user.email _;"
                "cd %s && rm -rf .git; git init . && git config user.name _ && git config user.email _;"
                "echo '%s' > /home/git/.____zself_ip_addr_%d.txt;"

                "exec 777<>/dev/tcp/%s/%s;"
                "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":\\\"%s_SHADOW/tools/post-update}\\\" >&777;"
                "cat <&777 >.git/hooks/post-update;"
                "chmod 0755 .git/hooks/post-update;"
                "exec 777>&-;"
                "exec 777<&-;",

                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen,
                zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->repoId,

                zNetSrv_.p_ipAddr, zNetSrv_.p_port,
                zpDpCcur_->repoId, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath
                );
        if (0 == zssh_exec_simple(zpDpCcur_->p_hostIpStrAddr, zCmdBuf, &(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock), zErrBuf)) {
            /* if init-ops success, then try deploy once more... */
            if (0 != zLibGit_.remote_push(zpGlobRepo_[zpDpCcur_->repoId]->p_gitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2, zErrBuf)) { zNative_Fail_Confirm(); }
        } else {
            zNative_Fail_Confirm();
        }
    }

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );

    return NULL;
}


/*
 * 0: 测试函数
 */
// static _i
// ztest_func(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) { return 0; }


/*
 * 6：显示单个项目元信息
 */
static _i
zshow_one_repo_meta(cJSON *zpJRoot, _i zSd) {
    zPgRes__ *zpPgRes_ = NULL;
    char zCmdBuf[256];

    _i zErrNo = 0,
       zRepoId = -1,
       zIpListSiz = 0,
       zJsonSiz = 0,
       zHostCnt = 0;

    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1;  /* zErrNo = -1; */ }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    sprintf(zCmdBuf, "SELECT DISTINCT host_ip FROM dp_log "
            "WHERE proj_id = %d AND time_stamp = (SELECT max(time_stamp) FROM dp_log WHERE proj_id = %d)",
            zRepoId,
            zRepoId);

    if (0 > (zErrNo = zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, &zpPgRes_))) {
        zPgSQL_.res_clear(NULL, zpPgRes_);
        return zErrNo;
    }

    if (NULL == zpPgRes_) {
        zHostCnt = 0;
        zIpListSiz = 1;
    } else {
        zHostCnt = zpPgRes_->tupleCnt;
        zIpListSiz = (1 + INET6_ADDRSTRLEN) * zpPgRes_->tupleCnt;
    }

    zJsonSiz = 256
        + zpGlobRepo_[zRepoId]->repoPathLen
        + zIpListSiz
        + sizeof("{\"ErrNo\":0,\"content\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs: %s\"}");

    char zIpsBuf[zIpListSiz];
    char zJsonBuf[zJsonSiz];

    zIpsBuf[0] = '\0';
    for (_i i = 0; i < zHostCnt; i++) {
        strcat(zIpsBuf, " ");
        strcat(zIpsBuf, zpPgRes_->tupleRes_[i].pp_fields[0]);
    }

    zPgSQL_.res_clear(NULL, zpPgRes_);

    zJsonSiz = sprintf(zJsonBuf, "{\"ErrNo\":0,\"content\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpRev: %s\nLastDpResult: %s\nLastHostCnt: %d\nLastHostIPs: %s\"}",
            zRepoId,
            zpGlobRepo_[zRepoId]->p_repoPath,
            zDpLocked == zpGlobRepo_[zRepoId]->dpLock ? "No" : "Yes",
            '\0' == zpGlobRepo_[zRepoId]->lastDpSig[0] ? "_" : zpGlobRepo_[zRepoId]->lastDpSig,
            zRepoDamaged == zpGlobRepo_[zRepoId]->repoState ? "fail" : "success",
            zHostCnt,
            zIpsBuf
            );

    /* 发送最终结果 */
    zNetUtils_.sendto(zSd, zJsonBuf, zJsonSiz, 0, NULL);

    return 0;
}


/*
 * 1：添加新项目（代码库）
 */
static _i
zadd_repo(cJSON *zpJRoot, _i zSd) {
    /*
     * [0] zRepoId
     * [1] zPathOnHost
     * [2] zSourceUrl
     * [3] zSourceBranch
     * [4] zSourceVcsType
     * [5] zNeedPull
     */
    char *zpProjInfo[6] = { NULL };

    zPgResTuple__ zRepoMeta_ = {
        .p_taskCnt = NULL,
        .pp_fields = zpProjInfo
    };

    char zSQLBuf[4096] = {'\0'};
    _i zErrNo = 0;
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
    zpProjInfo[0] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "PathOnHost");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
    zpProjInfo[1] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "NeedPull");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
    zpProjInfo[5] = zpJ->valuestring;

    if ('Y' == toupper(zpProjInfo[5][0])) {
        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceUrl");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
        zpProjInfo[2] = zpJ->valuestring;

        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceBranch");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
        zpProjInfo[3] = zpJ->valuestring;

        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceVcsType");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -34; }
        zpProjInfo[4] = zpJ->valuestring;
    } else if ('N' == toupper(zpProjInfo[5][0])) {
        zpProjInfo[2] = "";
        zpProjInfo[3] = "";
        zpProjInfo[4] = "Git";
    } else {
        return -34;
    }

    if (0 == (zErrNo = zNativeOps_.proj_init(&zRepoMeta_, zSd))) {
        /* 写入本项目元数据 */
        sprintf(zSQLBuf, "INSERT INTO proj_meta "
                "(proj_id, path_on_host, source_url, source_branch, source_vcs_type, need_pull) "
                "VALUES ('%s','%s','%s','%s','%c','%c')",
                zRepoMeta_.pp_fields[0],
                zRepoMeta_.pp_fields[1],
                zRepoMeta_.pp_fields[2],
                zRepoMeta_.pp_fields[3],
                toupper(zRepoMeta_.pp_fields[4][0]),
                toupper(zRepoMeta_.pp_fields[5][0])
                );

        zPgResHd__ *zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[strtol(zRepoMeta_.pp_fields[0], NULL, 10)]->p_pgConnHd_, zSQLBuf, zFalse);
        if (NULL == zpPgResHd_) {
            /* 刚刚建立的连接，此处不必尝试 reset */
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            return -91;
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }

        zNetUtils_.sendto(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1, 0, NULL);
    }

    return zErrNo;
}


/*
 * 7：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
static _i
zprint_record(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpSortedTopVecWrap_ = NULL;
    _i zRepoId = -1,
       zDataType = -1;
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'N' == zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zDataType = zpJ->valueint;

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zRepoId]->rwLock))) {
        if (0 == zpGlobRepo_[zRepoId]->whoGetWrLock) { return -5; }
        else { return -11; }
    };

    if (zIsCommitDataType == zDataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zRepoId]->sortedCommitVecWrap_);
    } else if (zIsDpDataType == zDataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zRepoId]->sortedDpVecWrap_);
    } else {
        pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));
        return -10;
    }

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrap_->vecSiz) {
        zNetUtils_.sendto(zSd, zpGlobRepo_[zRepoId]->jsonPrefix, zpGlobRepo_[zRepoId]->jsonPrefixLen, 0, NULL);  /* json 前缀 */
        zNetUtils_.sendmsg(zSd, zpSortedTopVecWrap_->p_vec_, zpSortedTopVecWrap_->vecSiz, 0, NULL);  /* 正文 */
        zNetUtils_.sendto(zSd, "]}", sizeof("]}") - 1, 0, NULL);  /* json 后缀 */
    } else {
        pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));
        return -70;
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));
    return 0;
}


/*
 * 10：显示差异文件路径列表
 */
static _i
zprint_diff_files(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zVecWrap__ zSendVecWrap_;

    zMeta__ zMeta_ = {
        .repoId = -1,
        .dataType = -1,
        .commitId = -1
    };
    zMeta__ *zpMeta_ = &zMeta_;

    _i zSplitCnt = -1;
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zpMeta_->repoId] || 'N' == zpGlobRepo_[zpMeta_->repoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->dataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->commitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->cacheId = zpJ->valueint;

    /* 若上一次布署是部分失败的，返回 -13 错误 */
    if (zRepoDamaged == zpGlobRepo_[zpMeta_->repoId]->repoState) { return -13; }

    zMeta_.repoId = zpMeta_->repoId;

    if (zIsCommitDataType == zpMeta_->dataType) { zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_); }
    else if (zIsDpDataType == zpMeta_->dataType) { zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_); }
    else { return -10; }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
        else { return -11; }
    }

    /* 若检查不通过，宏内部会解锁，之后退出 */
    zCheck_CacheId(zpMeta_);
    zCheck_CommitId(zpMeta_);

    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
            else { return -11; }
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_;
    zSplitCnt = (zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz - 1) / zSendUnitSiz  + 1;

    zNetUtils_.sendto(zSd, zpGlobRepo_[zpMeta_->repoId]->jsonPrefix, zpGlobRepo_[zpMeta_->repoId]->jsonPrefixLen, 0, NULL);  /* json 前缀 */
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {  /* 正文 */
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_->vecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }
    zNetUtils_.sendto(zSd, "]}", sizeof("]}") - 1, 0, NULL);  /* json 后缀 */

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
    return 0;
}


/*
 * 11：显示差异文件内容
 */
static _i
zprint_diff_content(cJSON *zpJRoot, _i zSd) {
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zVecWrap__ zSendVecWrap_;

    zMeta__ zMeta_ = {
        .repoId = -1,
        .dataType = -1,
        .commitId = -1,
        .fileId = -1
    };
    zMeta__ *zpMeta_ = &zMeta_;

    _i zSplitCnt = -1;
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zpMeta_->repoId] || 'N' == zpGlobRepo_[zpMeta_->repoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->dataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->commitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "FileId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->fileId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->cacheId = zpJ->valueint;

    if (zIsCommitDataType == zpMeta_->dataType) { zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_); }
    else if (zIsDpDataType == zpMeta_->dataType) { zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_); }
    else { return -10; }

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
        else { return -11; }
    };

    /* 若检查不通过，宏内部会解锁，之后退出 */
    zCheck_CacheId(zpMeta_);
    zCheck_CommitId(zpMeta_);

    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
            else { return -11; }
        }
    }

    /* 若检查不通过，宏内部会解锁，之后退出 */
    zCheck_FileId(zpMeta_);

    if (NULL == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)) {
        if ((void *) -1 == zNativeOps_.get_diff_contents(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
            else { return -11; }
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_vec_;
    zSplitCnt = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz - 1) / zSendUnitSiz  + 1;

    zNetUtils_.sendto(zSd, zpGlobRepo_[zpMeta_->repoId]->jsonPrefix, zpGlobRepo_[zpMeta_->repoId]->jsonPrefixLen, 0, NULL);  /* json 前缀 */
    zNetUtils_.sendto(zSd, "[{\"content\":\"", sizeof("[{\"content\":\"") - 1, 0, NULL);  /* 差异内容的 data 是纯文本，没有 json 结构，此处添加 data 对应的二维 json */
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {  /* 正文 */
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }
    zNetUtils_.sendto(zSd, "\"}]}", sizeof("\"}]}") - 1, 0, NULL);  /* json 后缀，此处需要配对一个引号与大括号 */

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
    return 0;
}


/*
 * 注：完全内嵌于 zdeploy() 中，不再需要读写锁
 */
#define zConfig_Dp_Host_Ssh_Cmd(zpCmdBuf) do {\
    sprintf(zpCmdBuf,\
            "rm -f %s %s_SHADOW;"\
            "mkdir -p %s %s_SHADOW;"\
            "rm -f %s/.git/index.lock %s_SHADOW/.git/index.lock;"\
            "cd %s_SHADOW && rm -f .git/hooks/post-update; git init . && git config user.name _ && git config user.email _;"\
            "cd %s && git init . && git config user.name _ && git config user.email _;"\
            "echo ${____zSelfIp} > /home/git/.____zself_ip_addr_%d.txt;"\
\
            "exec 777<>/dev/tcp/%s/%s;"\
            "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":\\\"%s_SHADOW/tools/post-update\\\"}\" >&777;"\
            "rm -f .git/hooks/post-update;"\
            "cat <&777 >.git/hooks/post-update;"\
            "chmod 0755 .git/hooks/post-update;"\
            "exec 777>&-;"\
            "exec 777<&-;",\
\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,\
            zpMeta_->repoId,\
\
            zNetSrv_.p_ipAddr, zNetSrv_.p_port,\
            zpMeta_->repoId, zpGlobRepo_[zpMeta_->repoId]->p_repoPath\
            );\
} while(0)


static _i
zupdate_ip_db_all(zMeta__ *zpMeta_, char *zpCommonBuf, zRegRes__ **zppRegRes_Out) {
    zDpRes__ *zpOldDpResList_, *zpTmpDpRes_, *zpOldDpResHash_[zDpHashSiz];

    zRegInit__ zRegInit_[1];
    zRegRes__ *zpRegRes_, zRegRes_[1] = {{.alloc_fn = zNativeOps_.alloc, .repoId = zpMeta_->repoId}};  // 使用项目内存池
    zpRegRes_ = zRegRes_;  /* avoid compile warning... */

    zPosixReg_.init(zRegInit_ , "([0-9]{1,3}\\.){3}[0-9]{1,3}");  /* IPv6 ??? */
    zPosixReg_.match(zRegRes_, zRegInit_, zpMeta_->p_data);
    zPosixReg_.free_meta(zRegInit_);
    *zppRegRes_Out = zpRegRes_;

    if (strtol(zpMeta_->p_extraData, NULL, 10) != zRegRes_->cnt) { return -28; }

    if (zForecastedHostNum < zRegRes_->cnt) {
        /* 若指定的目标主机数量大于预测的主机数量，则另行分配内存 */
        /* 加空格最长16字节，如："123.123.123.123 " */
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_ = zNativeOps_.alloc(zpMeta_->repoId, zRegRes_->cnt * sizeof(zDpCcur__));
    } else {
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_ = zpGlobRepo_[zpMeta_->repoId]->dpCcur_;
    }

    /* 暂留旧数据 */
    zpOldDpResList_ = zpGlobRepo_[zpMeta_->repoId]->p_dpResList_;
    memcpy(zpOldDpResHash_, zpGlobRepo_[zpMeta_->repoId]->p_dpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配
     * 分配清零的空间，用于重置状态及检查重复 IP
     */
    zMem_C_Alloc(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_, zDpRes__, zRegRes_->cnt);

    /* 重置各项状态 */
    zpGlobRepo_[zpMeta_->repoId]->totalHost = zRegRes_->cnt;
    zpGlobRepo_[zpMeta_->repoId]->dpTotalTask = zpGlobRepo_[zpMeta_->repoId]->totalHost;
    //zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->resType[0] = 0;
    zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp = time(NULL);
    memset(zpGlobRepo_[zpMeta_->repoId]->p_dpResHash_, 0, zDpHashSiz * sizeof(zDpRes__ *));  /* Clear hash buf before reuse it!!! */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState = 0;
    }

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zConfig_Dp_Host_Ssh_Cmd(zpCommonBuf);

    for (_ui zCnter = 0; zCnter < zRegRes_->cnt; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr) { return -19; }

        /* 注：需要全量赋值，因为后续的布署会直接复用；否则会造成只布署新加入的主机及内存访问错误 */
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_ = NULL;
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].repoId = zpMeta_->repoId;
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zRegRes_->p_rets[zCnter];
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_cmd = zpCommonBuf;
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock;
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurCond = &zpGlobRepo_[zpMeta_->repoId]->dpSyncCond;
        zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_taskCnt = &zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt;

        /* 线性链表斌值；转换字符串点分格式 IPv4 为 _ui 型 */
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr = zNetUtils_.to_bin(zRegRes_->p_rets[zCnter]);
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState = 0;
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].p_next = NULL;

        /* 更新HASH */
        zpTmpDpRes_ = zpGlobRepo_[zpMeta_->repoId]->p_dpResHash_[(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr) % zDpHashSiz];
        if (NULL == zpTmpDpRes_) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepo_[zpMeta_->repoId]->p_dpResHash_[(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr) % zDpHashSiz]
                = &(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter]);
        } else {
            while (NULL != zpTmpDpRes_->p_next) { zpTmpDpRes_ = zpTmpDpRes_->p_next; }
            zpTmpDpRes_->p_next = &(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter]);
        }

        zpTmpDpRes_ = zpOldDpResHash_[zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr % zDpHashSiz];
        while (NULL != zpTmpDpRes_) {
            /* 若 IPv4 address 已存在，则跳过初始化远程主机的环节 */
            if (zpTmpDpRes_->clientAddr == zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr) {
                /* 先前已被初始化过的主机，状态置 1，防止后续收集结果时误报失败 */
                zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState = 1;
                /* 从总任务数中去除已经初始化的主机数 */
                zpGlobRepo_[zpMeta_->repoId]->dpTotalTask--;
                goto zExistMark;
            }
            zpTmpDpRes_ = zpTmpDpRes_->p_next;
        }

        /* 对新加入的目标机执行初始化动作 */
        zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter]));
zExistMark:;
    }

    /* 释放资源 */
    if (NULL != zpOldDpResList_) { free(zpOldDpResList_); }

    /* 等待所有 SSH 任务完成 */
    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    while (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask) {
        pthread_cond_wait(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond, &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    }
    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);

    /* 检测执行结果，并返回失败列表 */
    if ((-1 == zpGlobRepo_[zpMeta_->repoId]->resType[0])
            || (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask)) {
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _ui zFailedHostCnt = 0;
        _i zOffSet = sprintf(zpMeta_->p_data, "无法连接的主机:");
        for (_ui zCnter = 0; (zOffSet < zpMeta_->dataLen) && (zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost); zCnter++) {
            if (1 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState) {
                zNetUtils_.to_str(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMeta_->p_data + zOffSet, "([%s]%s)",
                        zIpStrAddrBuf,
                        '\0' == zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg
                        );
                zFailedHostCnt++;

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr = 0;
            }
        }

        /* 主机数超过 10 台，且失败率低于 10% 返回成功，否则返回失败 */
        if ((10 < zpGlobRepo_[zpMeta_->repoId]->totalHost) && ( zFailedHostCnt < zpGlobRepo_[zpMeta_->repoId]->totalHost / 10)) { return 0; }
        else { return -23; }
    }

    return 0;
}


/*
 * 实际的布署函数，由外壳函数调用
 * 12：布署／撤销
 */
static _i
zdeploy(zMeta__ *zpMeta_, _i zSd, char **zppCommonBuf, zRegRes__ **zppHostStrAddrRegRes_Out) {
    zVecWrap__ *zpTopVecWrap_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    _i zErrNo = 0;
    _i zFailedHostCnt = 0;

    _ui zCnter = 0;
    time_t zRemoteHostInitTimeSpent = 0;

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
    } else {
        zErrNo = -10;
        goto zEndMark;
    }

    /* 检查 pgSQL 是否可以正常连通 */
    if (zFalse == zPgSQL_.conn_check(zGlobPgConnInfo)) { return -90; }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepo_[zpMeta_->repoId]->dpLock) {
        zErrNo = -6;
        goto zEndMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepo_[zpMeta_->repoId]->cacheId != zpMeta_->cacheId) {
        zpMeta_->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;
        zErrNo = -8;
        goto zEndMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zpMeta_->commitId)
            || ((zCacheSiz - 1) < zpMeta_->commitId)
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_data)) {
        zErrNo = -3;
        goto zEndMark;
    }

    /* 预布署动作：须置于 zupdate_ip_db_all(...) 函数之前，因 post-update 会在初始化远程主机时被首先传输 */
    zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp = time(NULL);
    sprintf(zppCommonBuf[1],
            "cd %s; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            "git stash;"\
            "git stash clear;"\
            "\\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf;"\
            "git reset %s; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            \
            "cd %s_SHADOW; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            "rm -rf ./tools;"\
            "cp -R ${zGitShadowPath}/tools ./;"\
            "chmod 0755 ./tools/post-update;"\
            "eval sed -i 's@__PROJ_PATH@%s@g' ./tools/post-update;"\
            "echo %ld > timestamp;"
            "git add --all .;"\
            "git commit --allow-empty -m _;"\
            "git push --force %s/.git master:master_SHADOW",
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,  // 中控机上的代码库路径
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),  // SHA1 commit sig
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + zGlobHomePathLen,  // 目标机上的代码库路径
            zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp,
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath
            );

    /* 调用 git 命令执行布署前的环境准备；同时用于测算中控机本机所有动作耗时，用作布署超时基数 */
    if (0 != WEXITSTATUS( system(zppCommonBuf[1]) )) {
        zErrNo = -15;
        goto zEndMark;
    }

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMeta_->p_data[0]) {
        if (0 > (zErrNo = zupdate_ip_db_all(zpMeta_, zppCommonBuf[0], zppHostStrAddrRegRes_Out))) {
            goto zEndMark;
        }
        zRemoteHostInitTimeSpent = time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp;
    }

    /* 检查部署目标主机集合是否存在 */
    if (0 == zpGlobRepo_[zpMeta_->repoId]->totalHost) {
        zErrNo = -26;
        goto zEndMark;
    }

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数 */
    strcpy(zpGlobRepo_[zpMeta_->repoId]->dpingSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId));

    /* 重置布署相关状态 */
    for (zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].dpState = 0;
    }
    zpGlobRepo_[zpMeta_->repoId]->dpTotalTask = zpGlobRepo_[zpMeta_->repoId]->totalHost;
    zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->resType[1] = 0;
    //zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit = 0;

    /* 预置本次布署日志 */
    _i zOffSet = sprintf(zppCommonBuf[0], "INSERT INTO dp_log (proj_id,rev_sig,time_stamp,host_ip) VALUES ");
    for (zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
        zOffSet += sprintf(zppCommonBuf[0] + zOffSet, "($1,$2,$3,'%s'),", zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_hostIpStrAddr);
    }
    zppCommonBuf[0][zOffSet - 1] = '\0';  /* 去除最后一个逗号 */

    char zParamBuf[2][16] = {{'\0'}};
    const char *zpParam[3] = {
        zParamBuf[0],
        zpGlobRepo_[zpMeta_->repoId]->dpingSig,
        zParamBuf[1]
    };
    const char **zppParam = zpParam;  // avoid compile warning...

    sprintf(zParamBuf[0], "%d", zpMeta_->repoId);
    sprintf(zParamBuf[1], "%ld", zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp);

    if (NULL == (zpPgResHd_ = zPgSQL_.exec_with_param(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zppCommonBuf[0], 4, zppParam, zFalse))) {
        zPgSQL_.conn_reset(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec_with_param(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zppCommonBuf[0], 4, zppParam, zFalse))) {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
            zPrint_Err(0, NULL, "!!! FATAL !!!");
            exit(1);
        }
    }

    /*
     * 基于 libgit2 实现 zgit_push(...) 函数，在系统负载上限之内并发布署
     * 参数与之前的SSH动作完全相同，此处无需再次赋值
     */
    for (zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
        zThreadPool_.add(zgit_push_ccur, &(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter]));
    }

    /* 测算超时时间 */
    if (('\0' == zpGlobRepo_[zpMeta_->repoId]->lastDpSig[0])
            || (0 == strcmp(zpGlobRepo_[zpMeta_->repoId]->lastDpSig, zpGlobRepo_[zpMeta_->repoId]->dpingSig))) {
        /* 无法测算时: 默认超时时间 ==  60s + 中控机本地所有动作耗时 */
        zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit = 60
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepo_[zpMeta_->repoId]->totalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp);
    } else {
        /*
         * [基数 = 30s + 中控机本地所有动作耗时之和] + [远程主机初始化时间 + 中控机与目标机上计算SHA1 checksum 的时间] + [网络数据总量每增加 ?M，超时上限递增 1 秒]
         * [网络数据总量 == 主机数 X 每台的数据量]
         * [单位：秒]
         */
        zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit = 30
            + zpGlobRepo_[zpMeta_->repoId]->totalHost / 10  /* 临时算式：每增加一台目标机，递增 0.1 秒 */
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepo_[zpMeta_->repoId]->totalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp);  /* 本地动作耗时 */
    }

    /* 最长 10 分钟 */
    if (600 < zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) { zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit = 600; }

    /* DEBUG */
    fprintf(stderr, "\n\033[31;01m[ DEBUG ] 布署时间测算结果：%zd 秒\033[00m\n\n", zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit);

    /* 耗时预测超过 90 秒的情况，通知前端不必阻塞等待，可异步于布署列表中查询布署结果 */
    if (90 < zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) {
        _i zSendLen = sprintf(zppCommonBuf[0], "{\"ErrNo\":-14,\"content\":\"本次布署时间最长可达 %zd 秒，请稍后查看布署结果\"}", zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit);
        zNetUtils_.sendto(zSd, zppCommonBuf[0], zSendLen, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }

    /* 等待所有 git push 任务完成或达到超时时间 */
    struct timespec zAbsoluteTimeStamp_;
    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);

    if (zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask) {
        zAbsoluteTimeStamp_.tv_sec = zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit + time(NULL) + 1;
        zAbsoluteTimeStamp_.tv_nsec = 0;
        pthread_cond_timedwait(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond, &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock, &zAbsoluteTimeStamp_);
    }

    /* 若 8 秒内收到过 keepalive 消息，则延长超时时间 15 秒*/
    while (8 > (time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpKeepAliveStamp)) {
        if (zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask) {
            zAbsoluteTimeStamp_.tv_sec = 15 + time(NULL) + 1;
            zAbsoluteTimeStamp_.tv_nsec = 0;
            pthread_cond_timedwait(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond, &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock, &zAbsoluteTimeStamp_);
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);

    /* 若收到错误，则可确认此次布署一定会失败，进入错误处理环节 */
    if (-1 == zpGlobRepo_[zpMeta_->repoId]->resType[1]) { goto zErrMark; }

    if (zpGlobRepo_[zpMeta_->repoId]->totalHost == zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt) {
        zErrNo = 0;
    } else if ( ((10 <= zpGlobRepo_[zpMeta_->repoId]->totalHost) && ((zpGlobRepo_[zpMeta_->repoId]->totalHost * 9 / 10) <= zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt))) {
        /*
         * 对于10 台及以上的目标机集群，达到 90％ 的主机状态得到确认即返回成功，未成功的部分，在下次新的版本布署之前，持续重试布署
         * 10 台以下，则须全部确认
         */
        zErrNo = -100;

        /* 复制一份正在布署的版本号，用于失败重试 */
        strcpy(zpMeta_->p_extraData, zpGlobRepo_[zpMeta_->repoId]->dpingSig);
    } else {
zErrMark:
        /* 若为部分布署失败，代码库状态置为 "损坏" 状态；若为全部布署失败，则无需此步 */
        if (0 < zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt) {
            //zpGlobRepo_[zpMeta_->repoId]->lastDpSig[0] = '\0';
            zpGlobRepo_[zpMeta_->repoId]->repoState = zRepoDamaged;
        }

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _i zOffSet = 0;
        for (_ui zCnter = 0; (zOffSet < zpMeta_->dataLen) && (zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost); zCnter++) {
            if (1 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].dpState) {
                zNetUtils_.to_str(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMeta_->p_data + zOffSet, "([%s]%s)",
                        zIpStrAddrBuf,
                        '\0' == zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg
                        );

                /* 未返回成功状态的主机 IP 计数并清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zFailedHostCnt++;
                zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr = 0;
            }
        }

        zErrNo = -12;
        goto zEndMark;
    }

    /* 若先前测算的布署耗时 <= 90s ，此处向前端返回布署成功消息 */
    if (90 >= zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) {
        zNetUtils_.sendto(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }
    zpGlobRepo_[zpMeta_->repoId]->repoState = zRepoGood;

    /* 更新最近一次布署的版本号到项目元信息中，复位代码库状态；若请求布署的版本号与最近一次布署的相同，则不必再重复生成缓存 */
    if (0 != strcmp(zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId), zpGlobRepo_[zpMeta_->repoId]->lastDpSig)) {
        /* 更新最新一次布署版本号，并将本次布署信息写入日志 */
        strcpy(zpGlobRepo_[zpMeta_->repoId]->lastDpSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId));

        /* deploy success, create a new "CURRENT" branch */
        sprintf(zppCommonBuf[0], "cd %s; git branch -f `git log CURRENT -1 --format=%%H`; git branch -f CURRENT", zpGlobRepo_[zpMeta_->repoId]->p_repoPath);
        if (0 != WEXITSTATUS( system(zppCommonBuf[0])) ) {
            zPrint_Err(0, NULL, "\"CURRENT\" branch refresh failed");
        }

        /* 若已确认全部成功，重置内存池状态 */
        if (0 == zErrNo) { zReset_Mem_Pool_State(zpMeta_->repoId); }

        /* 如下部分：更新全局缓存 */
        zpGlobRepo_[zpMeta_->repoId]->cacheId = time(NULL);

        zMeta__ zSubMeta_;
        zSubMeta_.repoId = zpMeta_->repoId;

        zSubMeta_.dataType = zIsCommitDataType;  /* 提交列表 */
        zNativeOps_.get_revs(&zSubMeta_);

        zSubMeta_.dataType = zIsDpDataType;  /* 布署列表 */
        zNativeOps_.get_revs(&zSubMeta_);
    }

zEndMark:
    sprintf(zppCommonBuf[0], "UPDATE dp_log SET time_limit = %ld, res = %d WHERE proj_id = %d AND time_stamp = %ld",
            zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit,
            0 == zErrNo ? 0 : (-100 == zErrNo? -1 : -2),
            zpMeta_->repoId,
            zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp
            );
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zppCommonBuf[0], zFalse))) {
        zPgSQL_.conn_reset(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, zppCommonBuf[0], zFalse))) {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
            zPrint_Err(0, NULL, "!!! FATAL !!!");
            exit(1);
        }
    }
    return zErrNo;
}


/*
 * 外壳函数
 * 13：新加入的主机请求布署自身：不拿锁、不刷系统IP列表、不刷新缓存
 */
static _i
zself_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    zMeta__ zMeta_ = {
        .repoId = -1,
        .p_data = NULL,
        .p_extraData =NULL
    };
    zMeta__ *zpMeta_ = &zMeta_;

    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->repoId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "data");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpMeta_->p_data = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ExtraData");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpMeta_->p_extraData = zpJ->valuestring;

    /* 若目标机上已是最新代码，则无需布署 */
    if (0 != strncmp(zpMeta_->p_extraData, zpGlobRepo_[zpMeta_->repoId]->lastDpSig, 40)) {
        zDpCcur__ zDpSelf_ = {
            .repoId = zpMeta_->repoId,
            .p_hostIpStrAddr = zpMeta_->p_data,
            .p_ccurLock = NULL  /* 标记无需发送通知给调用者的条件变量 */
        };

        zgit_push_ccur(&zDpSelf_);
    }

    return 0;
}


/*
 * 外壳函数
 * 12：布署／撤销
 */
static _i
zbatch_deploy(cJSON *zpJRoot, _i zSd) {
    /* check system load */
    if (80 < zGlobMemLoad) { return -16; }

    char *zppCommonBuf[2] = { NULL };
    zRegRes__ *zpIpAddrRegRes_ = NULL;
    _i zErrNo = 0,
       zCommonBufLen = 0;

    zMeta__ zMeta_ = { .repoId = -1 };
    zMeta__ *zpMeta_ = &zMeta_;

    cJSON *zpJ = NULL;

    /* 提取 value[ProjId] */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zpMeta_->repoId] || 'N' == zpGlobRepo_[zpMeta_->repoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->dataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->cacheId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zpMeta_->commitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "data");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpMeta_->p_data = zpJ->valuestring;
    zpMeta_->dataLen = strlen(zpMeta_->p_data);

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ExtraData");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpMeta_->p_extraData = zpJ->valuestring;
    zpMeta_->extraDataLen = strlen(zpMeta_->p_extraData);

    if (0 != pthread_rwlock_trywrlock( &(zpGlobRepo_[zpMeta_->repoId]->rwLock) )) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) { return -5; }
        else { return -11; }
    }

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个 Buf */
    zCommonBufLen = 2048 + 10 * zpGlobRepo_[zpMeta_->repoId]->repoPathLen + 2 * zpMeta_->dataLen;
    zppCommonBuf[0] = zNativeOps_.alloc(zpMeta_->repoId, 2 * zCommonBufLen);
    zppCommonBuf[1] = zppCommonBuf[0] + zCommonBufLen;

    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock = 1;  // 置为 1
    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    pthread_cond_signal(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond);  // 通知旧的版本重试动作中止

    pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );

    zErrNo = zdeploy(zpMeta_, zSd, zppCommonBuf, &zpIpAddrRegRes_);
    zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock = 0;
    pthread_rwlock_unlock( &(zpGlobRepo_[zpMeta_->repoId]->rwLock) );
    pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );

    /* 确认全部成功或确认布署失败这两种情况，直接返回，否则进入不间断重试模式，直到新的布署请求到来 */
    if (-100 != zErrNo) {
        /* 若为目标机初始化失败或部署失败，回返失败的IP列表(p_data)及版本号(p_extraData)，其余错误返回上层统一处理 */
        if (-23 == zErrNo || -12 == zErrNo) {
            _i zLen = 256 + zpMeta_->dataLen;
            char zErrBuf[zLen];
            zLen = snprintf(zErrBuf, zLen, "{\"ErrNo\":%d,\"FailedIpList\":\"%s\",\"FailedRevSig\":\"%s\"}",
                    zErrNo,
                    zpMeta_->p_data,
                    zpGlobRepo_[zpMeta_->repoId]->dpingSig
                    );
            zNetUtils_.sendto(zSd, zErrBuf, zLen, 0, NULL);

            /* 错误信息，打印出一份，防止客户端已断开的场景导致错误信息丢失 */
            zPrint_Err(0, NULL, zErrBuf);

            zErrNo = 0;
        }

        return zErrNo;
    } else {
        /* 在没有新的布署动作之前，持续尝试布署失败的目标机 */
        while(1) {
            /* 等待剩余的所有主机状态都得到确认，不必在锁内执行 */
            for (_l zTimeCnter = 0; zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit > zTimeCnter; zTimeCnter++) {
                if ((0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock)  /* 检测是否有新的布署请求或已全部布署成功 */
                        || ( (zpGlobRepo_[zpMeta_->repoId]->totalHost == zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt) && (-1 != zpGlobRepo_[zpMeta_->repoId]->resType[1]))) {

                    sprintf(zppCommonBuf[0], "UPDATE dp_log SET res = 0 WHERE proj_id = %d AND time_stamp = %ld",
                            zpMeta_->repoId,
                            zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp
                            );

                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zppCommonBuf[0], NULL)) {
                        zPrint_Err(0, NULL, "update dp_log record failed");
                    }

                    return 0;
                }

                zNativeUtils_.sleep(0.1);
            }

            pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
            if (0 !=  strcmp(zpGlobRepo_[zpMeta_->repoId]->dpingSig, zpMeta_->p_extraData)) {
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                return 0;
            }

            /* 重置时间戳，并生成 SSH 指令 */
            zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp = time(NULL);
            zConfig_Dp_Host_Ssh_Cmd(zppCommonBuf[0]);

            /* 预置值，对失败的目标机重新初始化 */
            zpGlobRepo_[zpMeta_->repoId]->dpTotalTask = zpGlobRepo_[zpMeta_->repoId]->totalHost;
            zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt = 0;

            for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                    pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                    return 0;
                }

                if (1 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].dpState) {
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_ = NULL;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].repoId = zpMeta_->repoId;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zpIpAddrRegRes_->p_rets[zCnter];
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_cmd = zppCommonBuf[0];
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurCond = &zpGlobRepo_[zpMeta_->repoId]->dpSyncCond;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_taskCnt = &zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt;

                    zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter]));

                    /* 调整目标机初始化状态数据（布署状态数据不调整！）*/
                    zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState = 0;
                } else {
                    zpGlobRepo_[zpMeta_->repoId]->dpTotalTask -= 1;
                    zpIpAddrRegRes_->p_rets[zCnter] = NULL;  // 去掉已成功的 IP 地址，只保留失败的部分
                }
            }

            /* 等待所有 SSH 任务完成，此处不再检查执行结果 */
            pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
            while ((0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock)
                    && (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask)) {
                pthread_cond_wait(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond, &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
            }
            pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);

            /* 检测是否有新的布署请求 */
            if (0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
                    /* 清理旧的未完工的线程，无需持锁 */
                    if (NULL != zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_) {
                        pthread_cancel(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_->selfTid);
                    }
                }

                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                return 0;
            }

            /* 预置值，对失败的目标机重新布署，任务总量与初始化目标机一致，此处无须再计算 */
            zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt = 0;
            zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp = time(NULL);

            /* 在执行动作之前再检查一次布署结果，防止重新初始化的时间里已全部返回成功状态，从而造成无用的布署重试 */
            if (zpGlobRepo_[zpMeta_->repoId]->totalHost == zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt) {
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );

                sprintf(zppCommonBuf[0], "UPDATE dp_log SET res = 0 WHERE proj_id = %d AND time_stamp = %ld",
                        zpMeta_->repoId,
                        zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp
                        );

                if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zppCommonBuf[0], NULL)) {
                    zPrint_Err(0, NULL, "update dp_log record failed");
                }

                return 0;
            } else {
                /* 对失败的目标主机重试布署 */
                for (_ui zCnter = 0; zCnter < zpIpAddrRegRes_->cnt; zCnter++) {
                    /* 检测是否有新的布署请求 */
                    if (0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                        return 0;
                    }

                    /* 结构体各成员参数与目标机初始化时一致，无需修改，直接复用即可 */
                    if (NULL != zpIpAddrRegRes_->p_rets[zCnter]) {
                        /* when memory load >= 80%，waiting ... */
                        pthread_mutex_lock(&zGlobCommonLock);
                        while (80 <= zGlobMemLoad) {
                            pthread_cond_wait(&zGlobCommonCond, &zGlobCommonLock);
                        }
                        pthread_mutex_unlock(&zGlobCommonLock);

                        zThreadPool_.add(zgit_push_ccur, &(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter]));
                    }
                }

                /* 等待所有 git push 任务完成；重试时不必设置超时 */
                pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
                while ((0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock)
                        && (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask)) {
                    pthread_cond_wait(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond, &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
                }
                pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);

                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
                        /* 清理旧的未完工的线程，无需持锁 */
                        if (NULL != zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_) {
                            pthread_cancel(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_threadSource_->selfTid);
                        }
                    }

                    pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                    return 0;
                }
            }

            /* 超时上限倍增 */
            zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit *= 2;

            pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
        }
    }
}


/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
#define zGenerate_SQL_Cmd(zCmdBuf) do {\
    snprintf(zCmdBuf, zGlobCommonBufSiz,\
            "UPDATE dp_log SET host_res = %d, host_timespent = %ld, host_errno = %d, host_detail = '%s' "\
            "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",\
\
            0 == zErrNo ? 0 : (-102 == zErrNo ? -2 : -1),\
            time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp,\
            zErrNo,\
            zpTmp_->errMsg,\
\
            zRepoId,\
            zIpStrAddr,\
            zTimeStamp,\
            zpRevSig\
            );\
} while (0);

static _i
zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmp_ = NULL;
    _i zErrNo = 0,
       zRepoId = 0;
    _ui zHostId = 0;
    time_t zTimeSpent = 0,
           zTimeStamp = 0;

    char zCmdBuf[zGlobCommonBufSiz] = {'\0'},
         zIpStrAddr[INET6_ADDRSTRLEN] = {'\0'},
         * zpRevSig = NULL,
         * zpReplyType = NULL,
         * zpContent = "";

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'N' == zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "TimeStamp");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zTimeStamp = (time_t)zpJ->valuedouble;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "HostId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zHostId = (_ui)zpJ->valuedouble;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevSig");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpRevSig = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "content");  /* 可以为空，不检查结查 */

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ReplyType");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }
    zpReplyType = zpJ->valuestring;

    /* 正文... */
    zpTmp_ = zpGlobRepo_[zRepoId]->p_dpResHash_[zHostId % zDpHashSiz];
    zNetUtils_.to_str(zHostId, zIpStrAddr);

    for (; zpTmp_ != NULL; zpTmp_ = zpTmp_->p_next) {  // 遍历
        if (zpTmp_->clientAddr == zHostId) {
            pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

            zpTmp_->errMsg[0] = '\0';

            /* 'B' 标识布署状态回复，'C' 目标机的 keep alive 消息 */
            if ('B' == zpReplyType[0]) {
                if (0 != zpTmp_->dpState) {
                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    zErrNo = 0;
                    goto zMarkEnd;
                }

                if (0 != strcmp(zpGlobRepo_[zRepoId]->dpingSig, zpRevSig)
                        /*|| zTimeStamp != zpGlobRepo_[zRepoId]->dpBaseTimeStamp*/) {
                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    zErrNo = -101;

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, NULL, "update dp_log record failed");
                    }

                    goto zMarkEnd;
                }

                if ('+' == zpReplyType[1]) {  // 负号 '-' 表示是异常返回，正号 '+' 表示是成功返回
                    zpGlobRepo_[zRepoId]->dpReplyCnt++;
                    zpTmp_->dpState = 1;

                    /* 调试功能：布署耗时统计，必须在锁内执行 */
                    zTimeSpent = time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp;

                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    if (zpGlobRepo_[zRepoId]->dpReplyCnt == zpGlobRepo_[zRepoId]->dpTotalTask) {
                        pthread_cond_signal(zpGlobRepo_[zRepoId]->p_dpCcur_->p_ccurCond);
                    }
                    zErrNo = 0;

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, NULL, "update dp_log record failed");
                    }

                    goto zMarkEnd;
                } else if ('-' == zpReplyType[1]) {
                    zpGlobRepo_[zRepoId]->dpReplyCnt = zpGlobRepo_[zRepoId]->dpTotalTask;  // 发生错误，计数打满，用于通知结束布署等待状态
                    zpTmp_->dpState = -1;
                    zpGlobRepo_[zRepoId]->resType[1] = -1;
                    zTimeSpent = time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp;

                    snprintf(zpTmp_->errMsg, 256, "%s", zpContent);  // 所有的状态回复前40个字节均是 git SHA1sig

                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    pthread_cond_signal(zpGlobRepo_[zRepoId]->p_dpCcur_->p_ccurCond);
                    zErrNo = -102;

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, NULL, "update dp_log record failed");
                    }

                    goto zMarkEnd;
                } else {
                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    zErrNo = -103;  // 未知的返回内容

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, NULL, "update dp_log record failed");
                    }

                    goto zMarkEnd;
                }
            } else if ('C' == zpReplyType[0]) {
                zpGlobRepo_[zRepoId]->dpKeepAliveStamp = time(NULL);
                pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                zErrNo = 0;
                goto zMarkEnd;
            } else {
                pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                zErrNo = -103;  // 未知的返回内容

                zGenerate_SQL_Cmd(zCmdBuf);
                if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                    zPrint_Err(0, NULL, "update dp_log record failed");
                }

                goto zMarkEnd;
            }
        }
    }

zMarkEnd:
    return zErrNo;
}

#undef zGenerate_SQL_Cmd

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
static _i
zlock_repo(cJSON *zpJRoot, _i zSd) {
    _i zRepoId = -1;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'N' == zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    pthread_rwlock_wrlock( &(zpGlobRepo_[zRepoId]->rwLock) );

    zpGlobRepo_[zRepoId]->dpLock = zDpLocked;

    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));

    zNetUtils_.sendto(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1, 0, NULL);

    return 0;
}

static _i
zunlock_repo(cJSON *zpJRoot, _i zSd) {
    _i zRepoId = -1;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) { return -1; }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'N' == zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    pthread_rwlock_wrlock( &(zpGlobRepo_[zRepoId]->rwLock) );

    zpGlobRepo_[zRepoId]->dpLock = zDpUnLock;

    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));

    zNetUtils_.sendto(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1, 0, NULL);

    return 0;
}


/* 14: 向目标机传输指定的文件 */
static _i
zreq_file(cJSON *zpJRoot, _i zSd) {
    char zDataBuf[4096] = {'\0'};
    _i zFd = -1,
       zDataLen = -1;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "data");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) { return -1; }

    zCheck_Negative_Return( zFd = open(zpJ->valuestring, O_RDONLY), -80 );

    while (0 < (zDataLen = read(zFd, zDataBuf, 4096))) {
        zNetUtils_.sendto(zSd, zDataBuf, zDataLen, 0, NULL);
    }

    close(zFd);
    return 0;
}

#undef zCheck_CommitId
#undef zCheck_FileId
#undef zCheck_CacheId
#undef zCheck_Lock_State
