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
extern struct zNativeUtils__ zNativeUtils_;

extern struct zThreadPool__ zThreadPool_;
extern struct zPosixReg__ zPosixReg_;
extern struct zPgSQL__ zPgSQL_;

extern struct zLibSsh__ zLibSsh_;
extern struct zLibGit__ zLibGit_;

extern struct zNativeOps__ zNativeOps_;

extern _i zGlobHomePathLen;

extern char *zpGlobLoginName;
extern char *zpGlobSSHPubKeyPath;
extern char *zpGlobSSHPrvKeyPath;

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
zprint_dp_process(cJSON *zpJRoot, _i zSd);

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
    .show_dp_process = zprint_dp_process,

    .print_revs = zprint_record,
    .print_diff_files = zprint_diff_files,
    .print_diff_contents = zprint_diff_content,

    .creat = zadd_repo,

    .dp = zbatch_deploy,
    .req_dp = zself_deploy,

    .state_confirm = zstate_confirm,

    .lock = zlock_repo,
    .unlock = zunlock_repo,

    .req_file = zreq_file
};


/*
 * 0: 测试函数
 */
_i
ztest_conn(cJSON *zpJRoot __attribute__ ((__unused__)), _i zSd __attribute__ ((__unused__))) {
    return -126;
}


/*
 * 7：显示布署进度
 */
/* 目标机初化与布署失败的错误回显 */
// #define zErrMeta ("{\"ErrNo\":%d,\"FailedDetail\":\"%s\",\"FailedRevSig\":\"%s\"}")
// #define zErrMetaSiz zSizeOf("{\"ErrNo\":%d,\"FailedDetail\":\"%s\",\"FailedRevSig\":\"%s\"}")
// static _i
// zprint_dp_process(cJSON *zpJRoot, _i zSd) {
//        /* ：：：留作实时进度接口备用
//         * 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
//        char zIpStrAddrBuf[INET6_ADDRSTRLEN];
//        _i zOffSet = 0;
//        for (_ui zCnter = 0; (zOffSet < (zBufLen - zErrMetaSiz))
//                && (zCnter < zpGlobRepo_[zRepoId]->totalHost); zCnter++) {
//            //if (1 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].dpState) {
//            if (1/* test */) {
//                if (0 != zConvert_IpNum_To_Str(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf)) {
//                    zPrint_Err(0, NULL, "Convert IP num to str failed");
//                } else {
//                    zOffSet += snprintf(zppCommonBuf[0] + zOffSet, zBufLen - zErrMetaSiz - zOffSet, "([%s]%s)",
//                            zIpStrAddrBuf,
//                            '\0' == zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg);
//
//                    /* 未返回成功状态的目标机 IP 计数并清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
//                    zFailedHostCnt++;
//                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] = 0;
//                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1] = 0;
//                }
//            }
//        }
// }


//    /* 检测执行结果，并返回失败列表 */
//    if (zCheck_Bit(zpGlobRepo_[zRepoId]->resType, 1)  /* 检查 bit[0] 是否置位 */
//            || (zpGlobRepo_[zRepoId]->dpTaskFinCnt < zpGlobRepo_[zRepoId]->dpTotalTask)) {
//        char zIpStrAddrBuf[INET6_ADDRSTRLEN];
//        _ui zFailedHostCnt = 0;
//        _i zOffSet = sprintf(zpCommonBuf, "无法连接的目标机:");
//        for (_ui zCnter = 0; (zOffSet < (zBufLen - zErrMetaSiz)) && (zCnter < zpGlobRepo_[zRepoId]->totalHost); zCnter++) {
//            if (!zCheck_Bit(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].resState, 1)
//                    /* || 0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errState */) {
//                if (0 != zConvert_IpNum_To_Str(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf)) {
//                    zPrint_Err(0, NULL, "Convert IP num to str failed");
//                } else {
//                    zOffSet += snprintf(zpCommonBuf+ zOffSet,
//                            zBufLen - zErrMetaSiz - zOffSet,
//                            "([%s]%s)",
//                            zIpStrAddrBuf,
//                            '\0' == zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg);
//                    zFailedHostCnt++;
//
//                    /* 未返回成功状态的目标机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
//                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] = 0;
//                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1] = 0;
//                }
//            }
//        }
//
//        zErrNo = -23;
//        goto zEndMark;
//    }

/*
项目ID
项目路径
创建日期
是否允许布署(锁定状态)
最近一次布署的相关信息
    版本号
    总状态: success/fail/in process
    实时进度: total: 100, success: 90, failed:3, in process:7
    开始布署的时间:
    耗时(秒):
    失败的目标机及原因
    正在布署的目标机及所处阶段

最近30天布署数据分析
    布署成功率(成功的台次/总台次) 1186/1200
    平均布署耗时(所有布署成功的目标机总耗时/总台次)
    错误分类及占比

目标机平均负载及正态分布系数(负载均衡性)数据分析
    CPU 负载(1h/6h/12h/7d)
    MEM 负载(1h/6h/12h/7d)
    网络 IO 负载(1h/6h/12h/7d)
    磁盘 IO 负载(1h/6h/12h/7d)
    磁盘容量负载(1h/6h/12h/7d)
*/


/*
 * 7：显示布署进度
 */
static _i
zprint_dp_process(cJSON *zpJRoot, _i zSd) {
    _ui zTbNo = 0;

    zPgRes__ *zpPgRes_ = NULL;
    char zCmdBuf[256];
    _i zErrNo = 0,
       zRepoId = -1,
       zIpListSiz = 0,
       zJsonSiz = 0,
       zHostCnt = 0;

    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;  /* zErrNo = -1; */
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    /* 创建临时表 */
    pthread_mutex_lock(&zGlobCommonLock);
    zTbNo = ++zpGlobRepo_[zRepoId]->tempTableNo;
    pthread_mutex_unlock(&zGlobCommonLock);
    sprintf(zCmdBuf, "CREATE TABLE tmp%u as SELECT host_ip,host_res,host_err,host_timespent FROM dp_log "
            "WHERE proj_id = %d AND time_stamp > %ld",
            zTbNo,
            zRepoId, time(NULL) - 3600 * 24 * 30);

    /* 布署成功率 */
    /* 布署平均耗时 */
    /* 各类错误计数与占比 */

    // 目标机总台次
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u", zTbNo);

    // 布署成功的总台次
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHERE host_res[5] = '1'", zTbNo);

    // 所有布署成功台次的耗时之和
    sprintf(zCmdBuf, "SELECT sum(host_timespent) FROM tmp%u WHERE host_res[5] = '1'", zTbNo);

    /* 各类错误计数 */
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[1] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[2] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[3] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[4] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[5] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[6] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[7] = '1'", zTbNo);
    sprintf(zCmdBuf, "SELECT count(host_ip) FROM tmp%u WHEREhost_err[8] = '1'", zTbNo);

    /* 删除临时表 */
    sprintf(zCmdBuf, "DROP TABLE tmp%u", zTbNo);

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

    zJsonSiz = sprintf(zJsonBuf,
            "{\"ErrNo\":0,\"content\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpRev: %s\nLastDpResult: %s\nLastHostCnt: %d\nLastHostIPs:%s\"}",
            zRepoId,
            zpGlobRepo_[zRepoId]->p_repoPath,
            zDpLocked == zpGlobRepo_[zRepoId]->repoLock ? "No" : "Yes",
            '\0' == zpGlobRepo_[zRepoId]->lastDpSig[0] ? "_" : zpGlobRepo_[zRepoId]->lastDpSig,
            zCacheDamaged == zpGlobRepo_[zRepoId]->repoState ? (1 == zpGlobRepo_[zRepoId]->dpingMark) ? "in process" : "fail" : "success",
            zHostCnt,
            zIpsBuf);

    /* 发送最终结果 */
    zNetUtils_.send_nosignal(zSd, zJsonBuf, zJsonSiz);

    return 0;
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
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zDataType = zpJ->valueint;

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zRepoId]->rwLock))) {
        return -11;
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
        /* json 前缀 */
        char zJsonPrefix[sizeof("{\"ErrNo\":0,\"CacheId\":%ld,\"data\":") + 16];
        _i zLen = sprintf(zJsonPrefix,
                "{\"ErrNo\":0,\"CacheId\":%ld,\"data\":",
                zpGlobRepo_[zRepoId]->cacheId
                );
        zNetUtils_.send_nosignal(zSd, zJsonPrefix, zLen);

        /* 正文 */
        zNetUtils_.sendmsg(zSd,
                zpSortedTopVecWrap_->p_vec_,
                zpSortedTopVecWrap_->vecSiz,
                0, NULL, zIpTypeNone);

        /* json 后缀 */
        zNetUtils_.send_nosignal(zSd, "]}", sizeof("]}") - 1);
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

    zCacheMeta__ zMeta_ = {
        .repoId = -1,
        .cacheId = -1,
        .dataType = -1,
        .commitId = -1
    };

    _i zSplitCnt = -1;

    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zMeta_.repoId] || 'Y' != zpGlobRepo_[zMeta_.repoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.commitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.cacheId = zpJ->valueint;

//    /* 若上一次布署是部分失败的或正在布署过程中，如何提示用户??? */
//    if (zCacheDamaged == zpGlobRepo_[zMeta_.repoId]->repoState) {
//        return -13;
//    }

    if (zIsCommitDataType == zMeta_.dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zMeta_.repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zMeta_.dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zMeta_.repoId]->dpVecWrap_);
    } else {
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock))) {
        return -11;
    }

    /* 若检查不通过，之后退出 */
    if (zpGlobRepo_[zMeta_.repoId]->cacheId != zMeta_.cacheId) {
        pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
        return -8;
    }

    if ((0 > zMeta_.commitId)\
            || ((zCacheSiz - 1) < zMeta_.commitId)\
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_data)) {
        pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
        return -3;
    }

    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -11;
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->p_vec_;
    zSplitCnt = (zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->vecSiz - 1) / zSendUnitSiz  + 1;

    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0,\"data\":", sizeof("{\"ErrNo\":0,\"data\":") - 1);  /* json 前缀 */
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {  /* 正文 */
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_->vecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }
    zNetUtils_.send_nosignal(zSd, "]}", sizeof("]}") - 1);  /* json 后缀 */

    pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
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

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.repoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zMeta_.repoId] || 'Y' != zpGlobRepo_[zMeta_.repoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.dataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.commitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "FileId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.fileId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zMeta_.cacheId = zpJ->valueint;

    if (zIsCommitDataType == zMeta_.dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zMeta_.repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zMeta_.dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zMeta_.repoId]->dpVecWrap_);
    } else {
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock))) {
        return -11;
    };

    /* 若检查不通过，则退出 */
    if (zpGlobRepo_[zMeta_.repoId]->cacheId != zMeta_.cacheId) {
        pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
        return -8;
    }

    if ((0 > zMeta_.commitId)\
            || ((zCacheSiz - 1) < zMeta_.commitId)\
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_data)) {
        pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
        return -3;
    }

    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(&zMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zMeta_.commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -11;
        }
    }

    /* 若检查不通过，宏内部会解锁，之后退出 */
    if ((0 > zMeta_.fileId)\
            || (NULL == zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_)\
            || ((zpTopVecWrap_->p_refData_[zMeta_.commitId].p_subVecWrap_->vecSiz - 1) < zMeta_.fileId)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));\
        return -4;\
    }\

    if (NULL == zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)) {
        if ((void *) -1 == zNativeOps_.get_diff_contents(&zMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
            return -11;
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->p_vec_;
    zSplitCnt = (zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->vecSiz - 1) / zSendUnitSiz  + 1;

    /* json 前缀: 差异内容的 data 是纯文本，没有 json 结构，此处添加 data 对应的二维 json */
    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0,\"data\":[{\"content\":\"", sizeof("{\"ErrNo\":0,\"data\":[{\"content\":\"") - 1);

    /* 正文 */
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zGet_OneFileVecWrap_(zpTopVecWrap_, zMeta_.commitId, zMeta_.fileId)->vecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL, zIpTypeNone);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

    /* json 后缀，此处需要配对一个引号与大括号 */
    zNetUtils_.send_nosignal(zSd, "\"}]}", sizeof("\"}]}") - 1);

    pthread_rwlock_unlock(&(zpGlobRepo_[zMeta_.repoId]->rwLock));
    return 0;
}


//////////
//////////
//////////
//////////
//////////
//////////
//////////
//////////


/*
 * 1：添加新项目（代码库）
 */
static _i
zadd_repo(cJSON *zpJRoot, _i zSd) {
    char *zpProjInfo[8] = { NULL };

    zPgResTuple__ zRepoMeta_ = {
        .p_taskCnt = NULL,
        .pp_fields = zpProjInfo
    };

    char zSQLBuf[4096] = {'\0'};
    _i zErrNo = 0;
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -34;
    }
    zpProjInfo[0] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "PathOnHost");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -34;
    }
    zpProjInfo[1] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "NeedPull");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -34;
    }
    zpProjInfo[5] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHUserName");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -34;
    }
    if (255 < strlen(zpJ->valuestring)) {
        return -31;
    }
    zpProjInfo[6] = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHPort");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -34;
    }
    if (5 < strlen(zpJ->valuestring)) {
        return -39;
    }
    zpProjInfo[7] = zpJ->valuestring;

    if ('Y' == toupper(zpProjInfo[5][0])) {
        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceUrl");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            return -34;
        }
        zpProjInfo[2] = zpJ->valuestring;

        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceBranch");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            return -34;
        }
        zpProjInfo[3] = zpJ->valuestring;

        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SourceVcsType");
        if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
            return -34;
        }
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
                "(proj_id, path_on_host, source_url, source_branch, source_vcs_type, need_pull, ssh_user_name, ssh_port) "
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
                zpGlobRepo_[strtol(zRepoMeta_.pp_fields[0], NULL, 10)]->p_pgConnHd_,
                zSQLBuf,
                zFalse
                );
        if (NULL == zpPgResHd_) {
            /* 刚刚建立的连接，此处不必尝试 reset */
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            return -91;
        } else {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
        }

        zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1);
    }

    return zErrNo;
}


// static void *
// zssh_ccur(void  *zpParam) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;
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
            zpGlobSSHPubKeyPath,
            zpGlobSSHPrvKeyPath,
            NULL,
            zPubKeyAuth,
            NULL,
            0,
            zpCcurLock,
            zpErrBufOUT
            );
}


/* 简化参数版函数 */
// static void *
// zssh_ccur_simple(void  *zpParam) {
//     char zErrBuf[256] = {'\0'};
//     zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;
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
            "zPath=%s;"\
            "for x in ${zPath} ${zPath}_SHADOW;"\
            "do;"\
                "rm -f $x ${x}/.git/{index.lock,post-update}"\
                "mkdir -p $x;"\
                "cd $x;"\
                "if [[ 0 -ne $? ]];then exit 206;fi;"\
                "if [[ 97 -lt `df . | grep -oP '\\d+(?=%%)'` ]];then exit 203;fi;"\
                "git init .;git config user.name _;git config user.email _;"\
            "done;"\
            "exec 777<>/dev/tcp/%s/%s;"\
            "printf '{\"OpsId\":14,\"ProjId\":%d,\"Path\":\"%s_SHADOW/tools/post-update\"}'>&777;"\
            "cat<&777 >${zPath}/.git/post-update;"\
            "exec 777>&-;"\
            "exec 777<&-;",\
            zpGlobRepo_[zRepoId]->p_repoPath + zGlobHomePathLen,\
            zNetSrv_.p_ipAddr, zNetSrv_.p_port,\
            zRepoId, zpGlobRepo_[zRepoId]->p_repoPath);\
} while(0)

#define zDB_Update_OR_Return(zSQLBuf) do {\
    if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, zSQLBuf, zFalse))) {\
        zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );\
        zPgSQL_.conn_clear(zpPgConnHd_);\
        zPrint_Err(0, zpDpCcur_->p_hostIpStrAddr, "SQL exec failed");\
        return (void *) -91;\
    }\
} while(0)

#define zNative_Fail_Confirm() do {\
    if (-1 != zpDpCcur_->id) {\
        pthread_mutex_lock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
\
        zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt++;\
\
        pthread_mutex_unlock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
        if (zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt == zpGlobRepo_[zpDpCcur_->repoId]->dpTotalTask) {\
            pthread_cond_signal(&zpGlobRepo_[zpDpCcur_->repoId]->dpSyncCond);\
        }\
\
        zSet_Bit(zpGlobRepo_[zpDpCcur_->repoId]->resType, 2);  /* 出错则置位 bit[1] */\
        zSet_Bit(zpDpCcur_->p_selfNode->errState, -1 * zErrNo);  /* 错误码置位 */\
        strcpy(zpDpCcur_->p_selfNode->errMsg, zErrBuf);\
\
        snprintf(zSQLBuf, zGlobCommonBufSiz,\
                "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "  /* postgreSQL 的数据下标是从 1 开始的 */\
                "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",\
                -1 * zErrNo, zpDpCcur_->p_selfNode->errMsg,\
                zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,\
                zpGlobRepo_[zpDpCcur_->repoId]->dpingSig);\
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
                "UPDATE dp_log SET host_res[2] = '1' "  /* postgreSQL 的数据下标是从 1 开始的 */\
                "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",\
                zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,\
                zpGlobRepo_[zpDpCcur_->repoId]->dpingSig);\
\
        zDB_Update_OR_Return(zSQLBuf);\
    }\
} while(0)

static void *
zdp_ccur(void *zp_) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp_;

    _i zErrNo = 0;
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    char zErrBuf[256] = {'\0'},
         zSQLBuf[zGlobCommonBufSiz] = {'\0'},
         zHostAddrBuf[INET6_ADDRSTRLEN] = {'\0'};
    char zRemoteRepoAddrBuf[64 + zpGlobRepo_[zpDpCcur_->repoId]->repoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* 并发布署流量控制 */
    zCheck_Negative_Exit( sem_wait(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );

    /* when memory load > 80%，waiting ... */
    // pthread_mutex_lock(&zGlobCommonLock);
    // while (80 < zGlobMemLoad) {
    //     pthread_cond_wait(&zGlobCommonCond, &zGlobCommonLock);
    // }
    // pthread_mutex_unlock(&zGlobCommonLock);

    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zGlobPgConnInfo))) {
        zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );
        zPrint_Err(0, zpDpCcur_->p_hostIpStrAddr, "DB connect failed");
        return (void *) -90;
    } else {
        snprintf(zSQLBuf, zGlobCommonBufSiz,
                "INSERT INTO dp_log (proj_id,time_stamp,rev_sig,host_ip) "
                "VALUES (%d,%ld,'%s','%s')",
                zpDpCcur_->repoId, zpDpCcur_->id,
                zpGlobRepo_[zpDpCcur_->repoId]->dpingSig,
                zpDpCcur_->p_hostIpStrAddr);

        zDB_Update_OR_Return(zSQLBuf);
    }

    /*
     * 若 SSH 命令内容不为空，则代表需要执行目标机初始化环节
     */
    if (NULL != zpDpCcur_->p_cmd) {
        /* 初始化远程目标机 */
        if (0 == (zErrNo = zssh_exec_simple(zpDpCcur_->p_userName,
                        zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort, zpDpCcur_->p_cmd,
                        zpDpCcur_->p_ccurLock, zErrBuf))) {
            /* 成功则置位 bit[0] */
            zSet_Bit(zpDpCcur_->p_selfNode->resState, 1);

            snprintf(zSQLBuf, zGlobCommonBufSiz,
                    "UPDATE dp_log SET host_res[1] = '1' "  /* postgreSQL 的数据下标是从 1 开始的 */
                    "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                    zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,
                    zpGlobRepo_[zpDpCcur_->repoId]->dpingSig);

            zDB_Update_OR_Return(zSQLBuf);
        } else {
            pthread_mutex_lock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));
            
            /* 已确定结果为失败，计数 +1 */
            zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt++;
            
            pthread_mutex_unlock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));
            if (zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt == zpGlobRepo_[zpDpCcur_->repoId]->dpTotalTask) {
                pthread_cond_signal(&zpGlobRepo_[zpDpCcur_->repoId]->dpSyncCond);
            }

            zSet_Bit(zpGlobRepo_[zpDpCcur_->repoId]->resType, 1);  /* 出错则置位 bit[0] */
            zSet_Bit(zpDpCcur_->p_selfNode->errState, -1 * zErrNo);  /* 返回的错误码是负数，其绝对值与错误位一一对应 */
            strcpy(zpDpCcur_->p_selfNode->errMsg, zErrBuf);

            snprintf(zSQLBuf, zGlobCommonBufSiz,
                    "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "  /* postgreSQL 的数据下标是从 1 开始的 */
                    "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                    -1 * zErrNo, zpDpCcur_->p_selfNode->errMsg,
                    zpDpCcur_->repoId, zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->id,
                    zpGlobRepo_[zpDpCcur_->repoId]->dpingSig);

            zDB_Update_OR_Return(zSQLBuf);

            /* 初始化失败则退出工作线程 */
            zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );
            zPgSQL_.conn_clear(zpPgConnHd_);
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            return (void *) -1;
        }
    }

    /*
     * ====> IPv6 地址段必须用中括号包住，否则无法解析 <====
     */
    sprintf(zRemoteRepoAddrBuf, "ssh://%s@[%s]:%s%s%s/.git",
            zpDpCcur_->p_userName,
            zpDpCcur_->p_hostIpStrAddr,
            zpDpCcur_->p_hostServPort,
            '/' == zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath[0]? "" : "/",
            zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen);

    /*
     * 将目标机 IPv6 中的 ':' 替换为 '_'，之后将其附加到分支名称上去
     * 分支名称的一个重要用途是用于捎带信息至目标机
     */
    strcpy(zHostAddrBuf, zpDpCcur_->p_hostIpStrAddr);
    for (_i i = 0; '\0' != zHostAddrBuf[i]; i++) {
        if (':' == zHostAddrBuf[i]) {
            zHostAddrBuf[i] = '_';
        }
    }

    /* {+refs/heads/... == git push --force}, push TWO branchs together */
    sprintf(zpGitRefs[0], "+refs/heads/master:refs/heads/serv@%d@%s@%ld",
            zpDpCcur_->repoId,
            zHostAddrBuf,
            zpDpCcur_->id);
    sprintf(zpGitRefs[1], "+refs/heads/master_SHADOW:refs/heads/serv_SHADOW@%d@%s@%ld",
            zpDpCcur_->repoId,
            zHostAddrBuf,
            zpDpCcur_->id);

    if (0 == (zErrNo = zLibGit_.remote_push(zpGlobRepo_[zpDpCcur_->repoId]->p_gitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2, NULL))) {
        zNative_Success_Confirm();
    } else {
        /*
         * -1 == zErrNo 表示未完全确定是不可恢复错误
         * 否则 ====> 此台目标机布署失败
         * 再次执行初始化后重试布署
         */
        if (-1 == zErrNo) {
            /* if failed, delete '.git', ReInit the remote host */
            char zCmdBuf[1024 + 2 * zpGlobRepo_[zpDpCcur_->repoId]->repoPathLen];
            if (NULL == zpDpCcur_->p_cmd) {
                zpDpCcur_->p_cmd = zCmdBuf;
                zGenerate_Ssh_Cmd(zpDpCcur_->p_cmd, zpDpCcur_->repoId);
            }

            if (0 == (zErrNo = zssh_exec_simple(zpDpCcur_->p_userName,
                            zpDpCcur_->p_hostIpStrAddr, zpDpCcur_->p_hostServPort,
                            zCmdBuf,
                            &(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock),
                            zErrBuf))) {
                /* if init-ops success, then try deploy once more... */
                if (0 == (zErrNo = zLibGit_.remote_push(
                                zpGlobRepo_[zpDpCcur_->repoId]->p_gitRepoHandler,
                                zRemoteRepoAddrBuf,
                                zpGitRefs, 2,
                                zErrBuf))) {
                    zNative_Success_Confirm();
                } else {
                    zNative_Fail_Confirm();
                }
            } else {
                zNative_Fail_Confirm();
            }
        } else {
            zNative_Fail_Confirm();
        }
    }

    /* 资源清理 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );
    zPgSQL_.conn_clear(zpPgConnHd_);
    zPgSQL_.res_clear(zpPgResHd_, NULL);

    return NULL;
}


/*
 * 13：新加入的目标机请求布署自身：不拿锁、不刷新系统IP列表、不刷新缓存
 */
static _i
zself_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    _i zRepoId = 0;
    zDpCcur__ zDpSelf_ = {
        .p_ccurLock = NULL,  /* 标记无需发送通知给调用者的条件变量 */
        .id = -1,  /* id 置为 -1，确保其触发的 post-update 可被任何主动布署动作打断且不会打断任何主动布署 */
        .p_cmd = NULL  /* 自请布署，无需初始化 */
    };

    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "HostAddr");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zDpSelf_.p_hostIpStrAddr  = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevSig");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }

    /* 若目标机上已是最新代码，则无需布署 */
    if (0 != strncmp(zpJ->valuestring, zpGlobRepo_[zRepoId]->lastDpSig, 40)) {
        zDpSelf_.p_userName = zpGlobRepo_[zRepoId]->sshUserName;
        zDpSelf_.p_hostServPort = zpGlobRepo_[zRepoId]->sshPort;
        zdp_ccur(&zDpSelf_);
    }

    return 0;
}


static _i
zupdate_ip_db_all(_i zRepoId,
        char *zpSSHUserName, char *zpSSHPort,
        zRegRes__ *zpRegRes_, _ui zIpCnt,
        char *zpCommonBuf) {

    _i zErrNo = 0;
    zDpRes__ *zpTmp_ = NULL,
             *zpOldDpResList_ = NULL,
             *zpOldDpResHash_[zDpHashSiz] = { NULL };

    if (zIpCnt != zpRegRes_->cnt) {
        zErrNo = -28;
        //goto zEndMark;
    }

    if (zForecastedHostNum < zpRegRes_->cnt) {
        /* 若指定的目标机数量大于预测的目标机数量，则另行分配内存 */
        zpGlobRepo_[zRepoId]->p_dpCcur_ = zNativeOps_.alloc(zRepoId, zpRegRes_->cnt * sizeof(zDpCcur__));
    } else {
        zpGlobRepo_[zRepoId]->p_dpCcur_ = zpGlobRepo_[zRepoId]->dpCcur_;
    }

    /* 暂留旧数据 */
    zpOldDpResList_ = zpGlobRepo_[zRepoId]->p_dpResList_;
    memcpy(zpOldDpResHash_, zpGlobRepo_[zRepoId]->p_dpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配
     * 分配清零的空间，用于重置状态及检查重复 IP
     */
    zMem_C_Alloc(zpGlobRepo_[zRepoId]->p_dpResList_, zDpRes__, zpRegRes_->cnt);

    /* Clear hash buf before reuse it!!! */
    memset(zpGlobRepo_[zRepoId]->p_dpResHash_, 0, zDpHashSiz * sizeof(zDpRes__ *));

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zGenerate_Ssh_Cmd(zpCommonBuf, zRepoId);

    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]
                || 0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1]) {
            // zSetBit(, 10)
            // errmsg = "IP 重复"
            // DB Write
            // continue;
        }

        /* 线性链表斌值；转换字符串格式 IP 为 _ull 型 */
        if (0 != zConvert_IpStr_To_Num(zpRegRes_->pp_rets[zCnter], zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
            // zSetBit(, 9)
            // errmsg = "目标机格式错误"
            // DB Write
            // continue;
        }

        /* 所在空间是使用 calloc 分配的，此处不必再手动置零 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].resState = 0;  /* 目标机结果状态复位 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errState = 0;  /* 目标机错误状态复位 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].p_next = NULL;

        /* 注：
         * 需要全量赋值，因为后续的布署会直接复用
         * 否则会造成只布署新加入的目标机及内存访问错误
         */
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_ = NULL;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].repoId = zRepoId;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].id = zpGlobRepo_[zRepoId]->dpBaseTimeStamp;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_userName = zpSSHUserName;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zpRegRes_->pp_rets[zCnter];
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostServPort = zpSSHPort;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_cmd = zpCommonBuf;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zRepoId]->dpSyncLock;

        /* 更新HASH */
        zpTmp_ = zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz];
        if (NULL == zpTmp_) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz]
                = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);

            zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_selfNode = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);
        } else {
            while (NULL != zpTmp_->p_next) { zpTmp_ = zpTmp_->p_next; }
            zpTmp_->p_next = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);

            zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_selfNode = zpTmp_->p_next;
        }

        zpTmp_ = zpOldDpResHash_[zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] % zDpHashSiz];
        while (NULL != zpTmp_) {
            /* 若 IPaddr 已存在，则跳过初始化远程目标机的环节 */
            if (zIpVecCmp(zpTmp_->clientAddr, zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
                zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_cmd = NULL;

                //////////////////////////////////////////////////////////////////////////
                /*
                 * 先前已被初始化过的目标机，resState 复制上一次的所有bit位状态
                 * 布署环节会用到，用于同一版本号布署时判断单台目标机是否需要执行布署动作的依据
                 */
                //zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].resState = zpTmp_->resState;  // 同一sig布署时使用
                //////////////////////////////////////////////////////////////////////////

                /* 从总任务数中去除已经初始化的目标机数 */
                zpGlobRepo_[zRepoId]->dpTotalTask--;
                break;
            }
            zpTmp_ = zpTmp_->p_next;
        }

        /* 执行布署 */
        zThreadPool_.add(zdp_ccur, &(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter]));
    }

    /* 释放旧资源 */
    if (NULL != zpOldDpResList_) {
        free(zpOldDpResList_);
    }
}


/*
 * 12：布署／撤销
 */
static _i
zbatch_deploy(cJSON *zpJRoot, _i zSd) {
    /* check system load */
    if (80 < zGlobMemLoad) {
        return -16;
    }

    char *zpCommonBuf = NULL;

    char *zpSSHUserName = NULL;
    char *zpIpList = NULL;
    _i zIpCnt = -1;
    char *zpSSHPort = NULL;

    zPgResHd__ *zpPgResHd_ = NULL;

    _i zErrNo = 0,
       zRepoId = -1,
       zIpListStrLen = -1,
       zCacheId = -1,
       zCommitId = -1,
       zDataType = -1;

    cJSON *zpJ = NULL;

    /* 提取 value[ProjId] */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        zErrNo = -1;
        goto zEndMark;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        zErrNo = -2;  /* zErrNo = -2; */
        goto zEndMark;
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
        zErrNo = -1;
        goto zEndMark;
    }
    zDataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) {
        zErrNo = -1;
        goto zEndMark;
    }
    zCacheId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) {
        zErrNo = -1;
        goto zEndMark;
    }
    zCommitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "IpList");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -1;
        goto zEndMark;
    }
    zpIpList = zpJ->valuestring;
    zIpListStrLen = strlen(zpIpList);

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "IpCnt");
    if (!cJSON_IsNumber(zpJ)) {
        zErrNo = -1;
        goto zEndMark;
    }
    zIpCnt = zpJ->valueint;

    /* 同一项目所有目标机的 ssh 用户名必须相同 */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHUserName");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -1;
        goto zEndMark;
    }
    zpSSHUserName = zpJ->valuestring;

    /* 同一项目所有目标机的 sshd 端口必须相同 */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHPort");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        zErrNo = -1;
        goto zEndMark;
    }
    zpSSHPort = zpJ->valuestring;

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个 Buf */
    zpCommonBuf = zNativeOps_.alloc(zRepoId, 2048 + 4 * zpGlobRepo_[zRepoId]->repoPathLen + 2 * zIpListStrLen);

    /* 通知旧的版本布署动作终止 */
    pthread_mutex_lock(&zpGlobRepo_[zRepoId]->dpSyncLock);
    zpGlobRepo_[zRepoId]->dpingMark = 0;
    pthread_mutex_unlock(&zpGlobRepo_[zRepoId]->dpSyncLock);
    pthread_cond_signal( &zpGlobRepo_[zRepoId]->dpSyncCond );

    /* 阻塞等待布署锁 */
    pthread_mutex_lock( &(zpGlobRepo_[zRepoId]->dpLock) );
    zpGlobRepo_[zRepoId]->dpingMark = 1;

    /* 若 SSH 认证信息有变动，则更新之 */
    if (0 != strcmp(zpSSHUserName, zpGlobRepo_[zRepoId]->sshUserName)
            || 0 != strcmp(zpSSHPort, zpGlobRepo_[zRepoId]->sshPort)) {
        sprintf(zpCommonBuf,
                "UPDATE proj_meta SET ssh_user_name = %s, ssh_port = %s, WHERE proj_id = %d",
                zpSSHUserName,
                zpSSHPort,
                zRepoId);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                        zpGlobRepo_[zRepoId]->p_pgConnHd_,
                        zpCommonBuf,
                        zFalse))) {
            zPgSQL_.conn_reset(zpGlobRepo_[zRepoId]->p_pgConnHd_);
            if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                            zpGlobRepo_[zRepoId]->p_pgConnHd_,
                            zpCommonBuf,
                            zFalse))) {
                zPgSQL_.res_clear(zpPgResHd_, NULL);
                zPgSQL_.conn_clear(zpGlobRepo_[zRepoId]->p_pgConnHd_);
                zPrint_Err(0, NULL, "!!! FATAL !!!");
                exit(1);
            }
        }
    }

    /* 布署过程中，标记缓存为不可用，但允许查询 */
    zpGlobRepo_[zRepoId]->repoState = zCacheDamaged;

    /* ==== 布署正文 ==== */
    zVecWrap__ *zpTopVecWrap_ = NULL;

    if (zIsCommitDataType == zDataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zRepoId]->commitVecWrap_);
    } else if (zIsDpDataType == zDataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zRepoId]->dpVecWrap_);
    } else {
        zErrNo = -10;
        goto zCleanMark;
    }

    /* 检查 pgSQL 是否可以正常连通 */
    if (zFalse == zPgSQL_.conn_check(zGlobPgConnInfo)) {
        zErrNo = -90;
        goto zCleanMark;
    }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepo_[zRepoId]->repoLock) {
        zErrNo = -6;
        goto zCleanMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepo_[zRepoId]->cacheId != zCacheId) {
        zCacheId = zpGlobRepo_[zRepoId]->cacheId;
        zErrNo = -8;
        goto zCleanMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zCommitId)
            || ((zCacheSiz - 1) < zCommitId)
            || (NULL == zpTopVecWrap_->p_refData_[zCommitId].p_data)) {
        zErrNo = -3;
        goto zCleanMark;
    }

    /* 预布署动作：须置于 zupdate_ip_db_all(...) 函数之前，因 post-update 会在初始化目标机时被首先传输 */
    sprintf(zpCommonBuf,
            "cd %s; if [[ 0 -ne $? ]]; then exit 1; fi;"
            "\\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf;"
            "git reset %s; if [[ 0 -ne $? ]]; then exit 1; fi;"

            "cd %s_SHADOW; if [[ 0 -ne $? ]]; then exit 1; fi;"
            "rm -rf ./tools;"
            "cp -R ${zGitShadowPath}/tools ./;"
            "chmod 0755 ./tools/post-update;"
            "eval sed -i 's@__PROJ_PATH@%s@g' ./tools/post-update;"
            "git add --all .;"
            "git commit --allow-empty -m _;"
            "git push --force %s/.git master:master_SHADOW",

            zpGlobRepo_[zRepoId]->p_repoPath,  // 中控机上的代码库路径
            zGet_OneCommitSig(zpTopVecWrap_, zCommitId),  // SHA1 commit sig
            zpGlobRepo_[zRepoId]->p_repoPath,
            zpGlobRepo_[zRepoId]->p_repoPath + zGlobHomePathLen,  // 目标机上的代码库路径
            zpGlobRepo_[zRepoId]->p_repoPath);

    /* 调用 git 命令执行布署前的环境准备；同时用于测算中控机本机所有动作耗时，用作布署超时基数 */
    if (0 != WEXITSTATUS( system(zpCommonBuf) )) {
        zErrNo = -15;
        goto zCleanMark;
    }

    /* 正则匹配 IP 列表 */
    zRegInit__ zRegInit_;
    zRegRes__ zRegRes_ = {
        .alloc_fn = zNativeOps_.alloc,  /* 使用项目内存池 */
        .repoId = zRepoId
    };

    zPosixReg_.init(&zRegInit_ , "[^ ]+");  /* IP 之间必须以空格分割 */
    zPosixReg_.match(&zRegRes_, &zRegInit_, zpIpList);
    zPosixReg_.free_meta(&zRegInit_);

    /* 重置各项状态 */
    zpGlobRepo_[zRepoId]->totalHost = zRegRes_.cnt;
    zpGlobRepo_[zRepoId]->dpTotalTask = zpGlobRepo_[zRepoId]->totalHost;
    zpGlobRepo_[zRepoId]->dpReplyCnt = 0;
    zpGlobRepo_[zRepoId]->dpTaskFinCnt = 0;
    zpGlobRepo_[zRepoId]->resType = 0;  /* 目标机初始化出错置位 bit[0]，布署出错则置位 bit[1] */

    /* 检查部署目标机集合是否存在 */
    if (0 == zpGlobRepo_[zRepoId]->totalHost) {
        zErrNo = -26;
        goto zCleanMark;
    }

    /* 确认布署环境无误，此时断开连接 */
    shutdown(zSd, SHUT_RDWR);

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数 */
    strcpy(zpGlobRepo_[zRepoId]->dpingSig, zGet_OneCommitSig(zpTopVecWrap_, zCommitId));

    /* 布署耗时基准：相同版本号重复布署时不更新 */
    if (0 != strcmp(zpGlobRepo_[zRepoId]->dpingSig, zpGlobRepo_[zRepoId]->lastDpSig)) {
        zpGlobRepo_[zRepoId]->dpBaseTimeStamp = time(NULL);
    }

    ////////////////////////////////////////////////////////

    zDpRes__ *zpTmp_ = NULL,
             *zpOldDpResList_ = NULL,
             *zpOldDpResHash_[zDpHashSiz] = { NULL };

    if (zIpCnt != zRegRes_.cnt) {
        zErrNo = -28;
        //goto zEndMark;
    }

    if (zForecastedHostNum < zRegRes_.cnt) {
        /* 若指定的目标机数量大于预测的目标机数量，则另行分配内存 */
        zpGlobRepo_[zRepoId]->p_dpCcur_ = zNativeOps_.alloc(zRepoId, zRegRes_.cnt * sizeof(zDpCcur__));
    } else {
        zpGlobRepo_[zRepoId]->p_dpCcur_ = zpGlobRepo_[zRepoId]->dpCcur_;
    }

    /* 暂留旧数据 */
    zpOldDpResList_ = zpGlobRepo_[zRepoId]->p_dpResList_;
    memcpy(zpOldDpResHash_, zpGlobRepo_[zRepoId]->p_dpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配
     * 分配清零的空间，用于重置状态及检查重复 IP
     */
    zMem_C_Alloc(zpGlobRepo_[zRepoId]->p_dpResList_, zDpRes__, zRegRes_.cnt);

    /* Clear hash buf before reuse it!!! */
    memset(zpGlobRepo_[zRepoId]->p_dpResHash_, 0, zDpHashSiz * sizeof(zDpRes__ *));

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zGenerate_Ssh_Cmd(zpCommonBuf, zRepoId);

    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]
                || 0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1]) {
            // zSetBit(, 10)
            // errmsg = "IP 重复"
            // DB Write
            // continue;
        }

        /* 线性链表斌值；转换字符串格式 IP 为 _ull 型 */
        if (0 != zConvert_IpStr_To_Num(zRegRes_.pp_rets[zCnter], zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
            // zSetBit(, 9)
            // errmsg = "目标机格式错误"
            // DB Write
            // continue;
        }

        /* 所在空间是使用 calloc 分配的，此处不必再手动置零 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].resState = 0;  /* 目标机结果状态复位 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errState = 0;  /* 目标机错误状态复位 */
        // zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].p_next = NULL;

        /* 注：
         * 需要全量赋值，因为后续的布署会直接复用
         * 否则会造成只布署新加入的目标机及内存访问错误
         */
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_ = NULL;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].repoId = zRepoId;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].id = zpGlobRepo_[zRepoId]->dpBaseTimeStamp;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_userName = zpSSHUserName;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zRegRes_.pp_rets[zCnter];
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostServPort = zpSSHPort;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_cmd = zpCommonBuf;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zRepoId]->dpSyncLock;

        /* 更新HASH */
        zpTmp_ = zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz];
        if (NULL == zpTmp_) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz]
                = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);

            zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_selfNode = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);
        } else {
            while (NULL != zpTmp_->p_next) { zpTmp_ = zpTmp_->p_next; }
            zpTmp_->p_next = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);

            zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_selfNode = zpTmp_->p_next;
        }

        zpTmp_ = zpOldDpResHash_[zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] % zDpHashSiz];
        while (NULL != zpTmp_) {
            /* 若 IPaddr 已存在，则跳过初始化远程目标机的环节 */
            if (zIpVecCmp(zpTmp_->clientAddr, zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
                zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_cmd = NULL;

                /* 从总任务数中去除已经初始化的目标机数 */
                zpGlobRepo_[zRepoId]->dpTotalTask--;
                break;
            }
            zpTmp_ = zpTmp_->p_next;
        }

        /* 执行布署 */
        zThreadPool_.add(zdp_ccur, &(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter]));
    }

    /* 释放旧资源 */
    if (NULL != zpOldDpResList_) {
        free(zpOldDpResList_);
    }

    //////////////////////////////////////////////////////////

    /* 等待所有 git push 任务完成 */
    pthread_mutex_lock(&zpGlobRepo_[zRepoId]->dpSyncLock);

    while (zpGlobRepo_[zRepoId]->dpReplyCnt < zpGlobRepo_[zRepoId]->dpTotalTask
            && 1 == zpGlobRepo_[zRepoId]->dpingMark) {
        pthread_cond_wait(&zpGlobRepo_[zRepoId]->dpSyncCond, &zpGlobRepo_[zRepoId]->dpSyncLock);
    }

    pthread_mutex_unlock( &zpGlobRepo_[zRepoId]->dpSyncLock );

    /* 若版本号与最近一次成功布署的相同，则不再刷新缓存 */
    if (1 == zpGlobRepo_[zRepoId]->dpingMark) {
        if (0 != strcmp(zGet_OneCommitSig(zpTopVecWrap_, zCommitId), zpGlobRepo_[zRepoId]->lastDpSig)) {
            /* get wrlock */
            pthread_rwlock_wrlock(&zpGlobRepo_[zRepoId]->rwLock);

            /* 布署成功，标记缓存可用 */
            zpGlobRepo_[zRepoId]->repoState = zCacheGood;

            /*
             * deploy success
             * create a new "CURRENT" branch,
             * and a lastSIG branch
             */
            if (0 != zLibGit_.branch_add(zpGlobRepo_[zRepoId]->p_gitRepoHandler, "CURRENT", zTrue)) {
                zPrint_Err(0, "CURRENT", "create branch failed");
            }

            if (0 != zLibGit_.branch_add(zpGlobRepo_[zRepoId]->p_gitRepoHandler, zpGlobRepo_[zRepoId]->lastDpSig, zTrue)) {
                zPrint_Err(0, zpGlobRepo_[zRepoId]->lastDpSig, "create branch failed");
            }

            /* 更新最新一次布署版本号，!!! 务必在创建新分支之后执行 !!! */
            strcpy(zpGlobRepo_[zRepoId]->lastDpSig, zGet_OneCommitSig(zpTopVecWrap_, zCommitId));

            /* 重置内存池状态 */
            zReset_Mem_Pool_State(zRepoId);

            /* 如下部分：更新全局缓存 */
            zpGlobRepo_[zRepoId]->cacheId = time(NULL);

            zCacheMeta__ zSubMeta_;
            zSubMeta_.repoId = zRepoId;

            zSubMeta_.dataType = zIsCommitDataType;  /* 提交列表 */
            zNativeOps_.get_revs(&zSubMeta_);

            zSubMeta_.dataType = zIsDpDataType;  /* 布署列表 */
            zNativeOps_.get_revs(&zSubMeta_);

            /* release wrlock */
            pthread_rwlock_unlock(&zpGlobRepo_[zRepoId]->rwLock);
        }
    } else {
        /* 若是被新布署请求打断，则 kill all old dping threads */
        for (_ui zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
            /* 清理旧的未完工的线程 */
            if (NULL != zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_) {
                pthread_cancel(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_->selfTid);
            }
        }

        zErrNo = -127;
    }

zCleanMark:
    zpGlobRepo_[zRepoId]->dpingMark = 0;
    pthread_mutex_unlock( &(zpGlobRepo_[zRepoId]->dpLock) );

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

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "TimeStamp");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zTimeStamp = (time_t)zpJ->valuedouble;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "HostAddr");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zpHostAddr = zpJ->valuestring;
    if (0 != zConvert_IpStr_To_Num(zpHostAddr, zHostId)) {
        return -18;
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevSig");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zpRevSig = zpJ->valuestring;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ReplyType");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]
            || 3 > strlen(zpJ->valuestring)) {
        return -1;
    }
    zpReplyType = zpJ->valuestring;

    /* 正文...遍历信息链 */
    for (zpTmp_ = zpGlobRepo_[zRepoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];
            zpTmp_ != NULL; zpTmp_ = zpTmp_->p_next) {
        if (zIpVecCmp(zpTmp_->clientAddr, zHostId)) {
            /* 检查信息类型是否合法 */
            zRetBit = strtol(zpReplyType + 2, NULL, 10);
            if (0 >= zRetBit || 8 < zRetBit) {
                zErrNo = -103;
                goto zMarkEnd;
            }

            /* 'B' 标识布署状态回复 */
            if ('B' == zpReplyType[0]) {
                /*
                 * 正号 B'+' 表示是阶段性成功返回
                 * 负号 B'-' 表示是异常返回，不需要记录布署时间
                 */
                if ('-' == zpReplyType[1]) {
                    /* 错误信息允许为空，不需要检查提取到的内容 */
                    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "content");
                    strncpy(zpTmp_->errMsg, zpJ->valuestring, 255);
                    zpTmp_->errMsg[255] = '\0';

                    snprintf(zCmdBuf, zGlobCommonBufSiz,
                            "UPDATE dp_log SET host_err[%d] = '1',host_detail = '%s' "  /* postgreSQL 的数据下标是从 1 开始的 */
                            "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                            zRetBit, zpTmp_->errMsg,
                            zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                    /* 设计 SQL 连接池??? */
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, zpHostAddr, "record update failed");
                    }

                    /* 判断是否是延迟到达的信息 */
                    if (0 != strcmp(zpGlobRepo_[zRepoId]->dpingSig, zpRevSig)
                            || zTimeStamp != zpGlobRepo_[zRepoId]->dpBaseTimeStamp) {
                        zErrNo = -101;
                        goto zMarkEnd;
                    }

                    zSet_Bit(zpTmp_->errState, zRetBit);

                    /* 发生错误，bit[1] 置位表示目标机布署出错返回 */
                    zSet_Bit(zpGlobRepo_[zRepoId]->resType, 2);

                    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

                    /*
                     * 确认此台目标机会布署失败
                     * 全局计数原子性+1
                     */
                    zpGlobRepo_[zRepoId]->dpReplyCnt++;

                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

                    if (zpGlobRepo_[zRepoId]->dpReplyCnt == zpGlobRepo_[zRepoId]->dpTotalTask) {
                        pthread_cond_signal(&zpGlobRepo_[zRepoId]->dpSyncCond);
                    }

                    zErrNo = -102;
                    goto zMarkEnd;
                } else if ('+' == zpReplyType[1]) {
                    snprintf(zCmdBuf, zGlobCommonBufSiz,
                            "UPDATE dp_log SET host_res[%d] = '1' "  /* postgreSQL 的数据下标是从 1 开始的 */
                            "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                            zRetBit,
                            zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, zpHostAddr, "record update failed");
                    }

                    /* 判断是否是延迟到达的信息 */
                    if (0 != strcmp(zpGlobRepo_[zRepoId]->dpingSig, zpRevSig)
                            || zTimeStamp != zpGlobRepo_[zRepoId]->dpBaseTimeStamp) {
                        zErrNo = -101;
                        goto zMarkEnd;
                    }

                    zSet_Bit(zpTmp_->resState, zRetBit);

                    /* 最终成功的状态到达时，才需要递增全局计数并记当布署耗时 */
                    if ('5' == zpReplyType[2]) {
                        pthread_mutex_lock( &(zpGlobRepo_[zRepoId]->dpSyncLock) );

                        /* 全局计数原子性 +1 */
                        zpGlobRepo_[zRepoId]->dpReplyCnt++;

                        pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

                        if (zpGlobRepo_[zRepoId]->dpReplyCnt == zpGlobRepo_[zRepoId]->dpTotalTask) {
                            pthread_cond_signal(&zpGlobRepo_[zRepoId]->dpSyncCond);
                        }

                        /* [DEBUG]：每台目标机的布署耗时统计 */
                        snprintf(zCmdBuf, zGlobCommonBufSiz,
                                "UPDATE dp_log SET host_timespent = %ld "
                                "WHERE proj_id = %d AND host_ip = '%s' AND time_stamp = %ld AND rev_sig = '%s'",
                                time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp,
                                zRepoId, zpHostAddr, zTimeStamp, zpRevSig);

                        if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                            zPrint_Err(0, zpHostAddr, "record update failed");
                        }
                    }

                    zErrNo = 0;
                    goto zMarkEnd;
                } else {
                    zErrNo = -103;  // 无法识别的返回内容
                    goto zMarkEnd;
                }
            } else {
                zErrNo = -103;  // 无法识别的返回内容
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
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    pthread_rwlock_wrlock( &(zpGlobRepo_[zRepoId]->rwLock) );

    zpGlobRepo_[zRepoId]->repoLock = zDpLocked;

    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));

    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1);

    return 0;
}

static _i
zunlock_repo(cJSON *zpJRoot, _i zSd) {
    _i zRepoId = -1;

    /* 提取 value[key] */
    cJSON *zpJ = NULL;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    pthread_rwlock_wrlock( &(zpGlobRepo_[zRepoId]->rwLock) );

    zpGlobRepo_[zRepoId]->repoLock = zDpUnLock;

    pthread_rwlock_unlock(&(zpGlobRepo_[zRepoId]->rwLock));

    zNetUtils_.send_nosignal(zSd, "{\"ErrNo\":0}", sizeof("{\"ErrNo\":0}") - 1);

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

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "Path");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }

    zCheck_Negative_Return( zFd = open(zpJ->valuestring, O_RDONLY), -80 );

    while (0 < (zDataLen = read(zFd, zDataBuf, 4096))) {
        zNetUtils_.send_nosignal(zSd, zDataBuf, zDataLen);
    }

    close(zFd);
    return 0;
}
