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
extern char zGlobNoticeMd5[34];

extern char *zpGlobLoginName;
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
    .show_meta = zshow_one_repo_meta,
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


/* 目标机初化与布署失败的错误回显 */
#define zErrMeta ("{\"ErrNo\":%d,\"FailedDetail\":\"%s\",\"FailedRevSig\":\"%s\"}")
#define zErrMetaSiz zSizeOf("{\"ErrNo\":%d,\"FailedDetail\":\"%s\",\"FailedRevSig\":\"%s\"}")

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
            1,
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


/* 远程目标机初始化专用 */
static void *
zssh_ccur_simple_init_host(void  *zpParam) {
    char zErrBuf[256] = {'\0'};
    _i zErrNo = 0;
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;

    _ull zHostId[2] = {0};
    if (0 > zConvert_IpStr_To_Num(zpDpCcur_->p_hostIpStrAddr, zHostId)) {
        zPrint_Err(0, zpDpCcur_->p_hostIpStrAddr, "Convert IP to num failed");
        return NULL;
    }

    zDpRes__ *zpTmp_ = zpGlobRepo_[zpDpCcur_->repoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];
    for (; NULL != zpTmp_; zpTmp_ = zpTmp_->p_next) {
        if (zIpVecCmp(zHostId, zpTmp_->clientAddr)) {
            if (0 == (zErrNo = zssh_exec_simple(
                        zpDpCcur_->p_userName,
                        zpDpCcur_->p_hostIpStrAddr,
                        zpDpCcur_->p_hostServPort,
                        zpDpCcur_->p_cmd,
                        zpDpCcur_->p_ccurLock,
                        zErrBuf))) {
                zSet_Bit(zpTmp_->resState, 1);  /* 成功则置位 bit[0] */
            } else {
                zSet_Bit(zpGlobRepo_[zpDpCcur_->repoId]->resType, 1);  /* 出错则置位 bit[0] */
                zSet_Bit(zpTmp_->errState, -1 * zErrNo);  /* 返回的错误码是负数，其绝对值与错误位一一对应 */
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


#define zNative_Fail_Confirm(zErrNo) do {\
    if (0 != zConvert_IpStr_To_Num(zpDpCcur_->p_hostIpStrAddr, zHostId)) {\
        zPrint_Err(0, zpDpCcur_->p_hostIpStrAddr, "Convert IP to num failed");\
    } else {\
        zpTmp_ = zpGlobRepo_[zpDpCcur_->repoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];\
        for (; NULL != zpTmp_; zpTmp_ = zpTmp_->p_next) {\
            if (zIpVecCmp(zHostId, zpTmp_->clientAddr)) {\
                pthread_mutex_lock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
                zSet_Bit(zpGlobRepo_[zpDpCcur_->repoId]->resType, 2);  /* 出错则置位 bit[1] */\
                zSet_Bit(zpTmp_->errState, -1 * zErrNo);  /* 错误码置位 */\
                strcpy(zpTmp_->errMsg, zErrBuf);\
\
                zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt++;\
                pthread_mutex_unlock(&(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock));\
                if (zpGlobRepo_[zpDpCcur_->repoId]->dpReplyCnt == zpGlobRepo_[zpDpCcur_->repoId]->dpTotalTask) {\
                    pthread_cond_signal(&zpGlobRepo_[zpDpCcur_->repoId]->dpSyncCond);\
                }\
                break;\
            }\
        }\
    }\
} while(0)

/* 置位 bit[1]，昭示本地 git push 成功 */
#define zNative_Success_Confirm() do {\
    if (0 != zConvert_IpStr_To_Num(zpDpCcur_->p_hostIpStrAddr, zHostId)) {\
        zPrint_Err(0, zpDpCcur_->p_hostIpStrAddr, "Convert IP to num failed");\
    } else {\
        zpTmp_ = zpGlobRepo_[zpDpCcur_->repoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];\
        for (; NULL != zpTmp_; zpTmp_ = zpTmp_->p_next) {\
            if (zIpVecCmp(zHostId, zpTmp_->clientAddr)) {\
                zSet_Bit(zpTmp_->resState, 2);  /* 置位 bit[1] */\
                break;\
            }\
        }\
    }\
} while(0)

static void *
zgit_push_ccur(void *zp_) {
    _i zErrNo = 0;
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp_;
    _ull zHostId[2] = {0};
    zDpRes__ *zpTmp_ = NULL;

    char zErrBuf[256] = {'\0'},
         zHostAddrBuf[INET6_ADDRSTRLEN] = {'\0'};
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

    /*
     * generate remote URL
     * 注：IPv6 地址段必须用中括号包住，否则无法解析
     */
    sprintf(zRemoteRepoAddrBuf, "ssh://%s@[%s]:%s%s%s/.git",
            zpDpCcur_->p_userName,
            zpDpCcur_->p_hostIpStrAddr,
            zpDpCcur_->p_hostServPort,
            '/' == zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath[0]? "" : "/",
            zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen);

    /* 将目标机 IPv6 中的 ':' 替换为 '_'，之后将其附加到分支名称上去 */
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
        if (-1 == zErrNo) {
            /* if failed, delete '.git', ReInit the remote host */
            char zCmdBuf[1024 + 7 * zpGlobRepo_[zpDpCcur_->repoId]->repoPathLen];
            sprintf(zCmdBuf,\
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
                    zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + zGlobHomePathLen,\
                    zNetSrv_.p_ipAddr, zNetSrv_.p_port,\
                    zpDpCcur_->repoId, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath);\
            if (0 == (zErrNo = zssh_exec_simple(
                        zpDpCcur_->p_userName,
                        zpDpCcur_->p_hostIpStrAddr,
                        zpDpCcur_->p_hostServPort,
                        zCmdBuf,
                        &(zpGlobRepo_[zpDpCcur_->repoId]->dpSyncLock),
                        zErrBuf))) {
                /* if init-ops success, then try deploy once more... */
                if (0 == (zErrNo = zLibGit_.remote_push(
                            zpGlobRepo_[zpDpCcur_->repoId]->p_gitRepoHandler,
                            zRemoteRepoAddrBuf,
                            zpGitRefs,
                            2,
                            zErrBuf))) {
                    zNative_Success_Confirm();
                } else {
                    zNative_Fail_Confirm(zErrNo);
                }
            } else {
                zNative_Fail_Confirm(zErrNo);
            }
        } else {
            zNative_Fail_Confirm(zErrNo);
        }
    }

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->repoId]->dpTraficControl)) );

    return NULL;
}


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


/*
项目ID
项目路径
创建日期
是否允许布署(锁定状态)
最近一次布署的相关信息
    版本号
    总状态: success/fail/in process
    实时进度: total: 100 success: 90 failed:3 in process:7
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
    if (!cJSON_IsNumber(zpJ)) {
        return -1;  /* zErrNo = -1; */
    }
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


/*
 * 注：完全内嵌于 zdeploy() 中，不再需要读写锁
 */
#define zConfig_Dp_Host_Ssh_Cmd(zpCmdBuf) do {\
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

static _i
zupdate_ip_db_all(_i zRepoId,
        char *zpSSHUserName, char *zpSSHPort,
        zRegRes__ *zpRegRes_, _ui zIpCnt, _l zTimeStamp,
        char *zpCommonBuf, _i zBufLen) {

    _i zErrNo = 0;
    zDpRes__ *zpTmpDpRes_ = NULL,
             *zpOldDpResList_ = NULL,
             *zpOldDpResHash_[zDpHashSiz] = { NULL };

    if (zIpCnt != zpRegRes_->cnt) {
        zErrNo = -28;
        goto zEndMark;
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

    /* 重置各项状态 */
    zpGlobRepo_[zRepoId]->totalHost = zpRegRes_->cnt;
    zpGlobRepo_[zRepoId]->dpTotalTask = zpGlobRepo_[zRepoId]->totalHost;
    zpGlobRepo_[zRepoId]->dpTaskFinCnt = 0;
    zpGlobRepo_[zRepoId]->resType = 0;  /* 出错则置位 bit[0]；此处统一置0，布署环节无需再次重置 */
    /* Clear hash buf before reuse it!!! */
    memset(zpGlobRepo_[zRepoId]->p_dpResHash_, 0, zDpHashSiz * sizeof(zDpRes__ *));

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zConfig_Dp_Host_Ssh_Cmd(zpCommonBuf);

    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]
                || 0 != zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1]) {
            free(zpGlobRepo_[zRepoId]->p_dpResList_);

            /* 状态回退 */
            zpGlobRepo_[zRepoId]->p_dpResList_ = zpOldDpResList_;
            memcpy(zpGlobRepo_[zRepoId]->p_dpResHash_, zpOldDpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

            zErrNo = -19;
            goto zEndMark;
        }

        /* 注：需要全量赋值，因为后续的布署会直接复用；否则会造成只布署新加入的目标机及内存访问错误 */
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_ = NULL;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].repoId = zRepoId;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].id = zTimeStamp;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_userName = zpSSHUserName;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zpRegRes_->pp_rets[zCnter];
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostServPort = zpSSHPort;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_cmd = zpCommonBuf;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zRepoId]->dpSyncLock;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_ccurCond = &zpGlobRepo_[zRepoId]->dpSyncCond;
        zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_taskCnt = &zpGlobRepo_[zRepoId]->dpTaskFinCnt;

        /* 线性链表斌值；转换字符串格式 IP 为 _ull 型 */
        if (0 != zConvert_IpStr_To_Num(zpRegRes_->pp_rets[zCnter], zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
            free(zpGlobRepo_[zRepoId]->p_dpResList_);

            /* 状态回退 */
            zpGlobRepo_[zRepoId]->p_dpResList_ = zpOldDpResList_;
            memcpy(zpGlobRepo_[zRepoId]->p_dpResHash_, zpOldDpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

            zErrNo = -18;
            goto zEndMark;
        }

        zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].state = 0;  /* 目标机状态复位 */
        zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].p_next = NULL;

        /* 更新HASH */
        zpTmpDpRes_ = zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz];
        if (NULL == zpTmpDpRes_) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepo_[zRepoId]->p_dpResHash_[(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0]) % zDpHashSiz]
                = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);
        } else {
            while (NULL != zpTmpDpRes_->p_next) { zpTmpDpRes_ = zpTmpDpRes_->p_next; }
            zpTmpDpRes_->p_next = &(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter]);
        }

        zpTmpDpRes_ = zpOldDpResHash_[zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] % zDpHashSiz];
        while (NULL != zpTmpDpRes_) {
            /* 若 IPv4 address 已存在，则跳过初始化远程目标机的环节 */
            if (zIpVecCmp(zpTmpDpRes_->clientAddr, zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr)) {
                /* 先前已被初始化过的目标机，状态置 1，防止后续收集结果时误报失败 */
                zSet_Bit(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].state, 1);  /* 将 bit[0] 置位 */
                /* 从总任务数中去除已经初始化的目标机数 */
                zpGlobRepo_[zRepoId]->dpTotalTask--;
                goto zExistMark;
            }
            zpTmpDpRes_ = zpTmpDpRes_->p_next;
        }

        /* 对新加入的目标机执行初始化动作 */
        zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter]));
zExistMark:;
    }

    /* 释放资源 */
    if (NULL != zpOldDpResList_) {
        free(zpOldDpResList_);
    }

    /* 等待所有 SSH 任务完成 */
    pthread_mutex_lock(&zpGlobRepo_[zRepoId]->dpSyncLock);
    while (zpGlobRepo_[zRepoId]->dpTaskFinCnt < zpGlobRepo_[zRepoId]->dpTotalTask
            && 1 == zpGlobRepo_[zRepoId]->dpingMark) {
        pthread_cond_wait(&zpGlobRepo_[zRepoId]->dpSyncCond, &zpGlobRepo_[zRepoId]->dpSyncLock);
    }

    pthread_mutex_unlock(&zpGlobRepo_[zRepoId]->dpSyncLock);

    /* 若是被新布署请求打断，则 kill all old dping threads */
    if (1 != zpGlobRepo_[zRepoId]->dpingMark) {
        for (_ui zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
            /* 清理旧的未完工的线程 */
            if (NULL != zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_) {
                pthread_cancel(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_threadSource_->selfTid);
            }
        }

        zErrNo = -127;
        goto zEndMark;
    }

    /* 检测执行结果，并返回失败列表 */
    if (zCheck_Bit(zpGlobRepo_[zRepoId]->resType, 1)  /* 检查 bit[0] 是否置位 */
            || (zpGlobRepo_[zRepoId]->dpTaskFinCnt < zpGlobRepo_[zRepoId]->dpTotalTask)) {
        char zIpStrAddrBuf[INET6_ADDRSTRLEN];
        _ui zFailedHostCnt = 0;
        _i zOffSet = sprintf(zpCommonBuf, "无法连接的目标机:");
        for (_ui zCnter = 0; (zOffSet < (zBufLen - zErrMetaSiz)) && (zCnter < zpGlobRepo_[zRepoId]->totalHost); zCnter++) {
            if (!zCheck_Bit(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].state, 1)) {
                if (0 != zConvert_IpNum_To_Str(zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf)) {
                    zPrint_Err(0, NULL, "Convert IP num to str failed");
                } else {
                    zOffSet += snprintf(zpCommonBuf+ zOffSet,
                            zBufLen - zErrMetaSiz - zOffSet,
                            "([%s]%s)",
                            zIpStrAddrBuf,
                            '\0' == zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].errMsg);
                    zFailedHostCnt++;

                    /* 未返回成功状态的目标机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[0] = 0;
                    zpGlobRepo_[zRepoId]->p_dpResList_[zCnter].clientAddr[1] = 0;
                }
            }
        }

        zErrNo = -23;
        goto zEndMark;
    }

zEndMark:
    return zErrNo;
}


/*
 * 外壳函数
 * 13：新加入的目标机请求布署自身：不拿锁、不刷系统IP列表、不刷新缓存
 */
static _i
zself_deploy(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    _i zRepoId = 0;
    zDpCcur__ zDpSelf_ = {
        .p_ccurLock = NULL  /* 标记无需发送通知给调用者的条件变量 */
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
        zgit_push_ccur(&zDpSelf_);
    }

    return 0;
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
       zCommonBufLen = -1,
       zRepoId = -1,
       zIpListStrLen = -1,
       zCacheId = -1,
       zCommitId = -1,
       zDataType = -1;

    cJSON *zpJ = NULL;

    /* 提取 value[ProjId] */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "ProjId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zRepoId = zpJ->valueint;

    /* 检查项目存在性 */
    if (NULL == zpGlobRepo_[zRepoId] || 'Y' != zpGlobRepo_[zRepoId]->initFinished) {
        return -2;  /* zErrNo = -2; */
    }

    /* 布署耗时基准 */
    zpGlobRepo_[zRepoId]->dpBaseTimeStamp = time(NULL);

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "DataType");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zDataType = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "CacheId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zCacheId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "RevId");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zCommitId = zpJ->valueint;

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "IpList");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zpIpList = zpJ->valuestring;
    zIpListStrLen = strlen(zpIpList);

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "IpCnt");
    if (!cJSON_IsNumber(zpJ)) {
        return -1;
    }
    zIpCnt = zpJ->valueint;

    /* 同一项目所有目标机的 ssh 用户名必须相同 */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHUserName");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zpSSHUserName = zpJ->valuestring;

    /* 同一项目所有目标机的 sshd 端口必须相同 */
    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "SSHPort");
    if (!cJSON_IsString(zpJ) || '\0' == zpJ->valuestring[0]) {
        return -1;
    }
    zpSSHPort = zpJ->valuestring;

    if (0 != pthread_rwlock_trywrlock( &(zpGlobRepo_[zRepoId]->rwLock) )) {
        return -11;
    }

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个 Buf */
    zCommonBufLen = 2048 + 4 * zpGlobRepo_[zRepoId]->repoPathLen + 2 * zIpListStrLen;
    zpCommonBuf = zNativeOps_.alloc(zRepoId, zCommonBufLen);

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

    pthread_mutex_lock(&zpGlobRepo_[zRepoId]->dpSyncLock);
    zpGlobRepo_[zRepoId]->dpingMark = 0;
    pthread_mutex_unlock(&zpGlobRepo_[zRepoId]->dpSyncLock);
    pthread_cond_signal( &zpGlobRepo_[zRepoId]->dpSyncCond );  // 通知旧的版本布署动作终止

    pthread_mutex_lock( &(zpGlobRepo_[zRepoId]->dpLock) );
    zpGlobRepo_[zRepoId]->dpingMark = 1;

    /* 布署过程中，标记缓存为不可用，但允许查询 */
    zpGlobRepo_[zRepoId]->repoState = zCacheDamaged;

    /* ==== 布署正文 ==== */
    zVecWrap__ *zpTopVecWrap_ = NULL;
    _ui zCnter = 0;

    if (zIsCommitDataType == zDataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zRepoId]->commitVecWrap_);
    } else if (zIsDpDataType == zDataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zRepoId]->dpVecWrap_);
    } else {
        zErrNo = -10;
        goto zEndMark;
    }

    /* 检查 pgSQL 是否可以正常连通 */
    if (zFalse == zPgSQL_.conn_check(zGlobPgConnInfo)) {
        return -90;
    }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepo_[zRepoId]->repoLock) {
        zErrNo = -6;
        goto zEndMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepo_[zRepoId]->cacheId != zCacheId) {
        zCacheId = zpGlobRepo_[zRepoId]->cacheId;
        zErrNo = -8;
        goto zEndMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zCommitId)
            || ((zCacheSiz - 1) < zCommitId)
            || (NULL == zpTopVecWrap_->p_refData_[zCommitId].p_data)) {
        zErrNo = -3;
        goto zEndMark;
    }

    /* 预布署动作：须置于 zupdate_ip_db_all(...) 函数之前，因 post-update 会在初始化目标机时被首先传输 */
    sprintf(zpCommonBuf,
            "cd %s; if [[ 0 -ne $? ]]; then exit 1; fi;"
            "git stash;"
            "git stash clear;"
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
        goto zEndMark;
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

    /*
     * 新增目标机扫描并初始化
     */
    if (0 > (zErrNo = zupdate_ip_db_all(zRepoId,
                    zpSSHUserName, zpSSHPort,
                    &zRegRes_, zIpCnt, zpGlobRepo_[zRepoId]->dpBaseTimeStamp,
                    zpCommonBuf, zCommonBufLen))) {
        goto zEndMark;
    }

    /* 检查部署目标机集合是否存在 */
    if (0 == zpGlobRepo_[zRepoId]->totalHost) {
        zErrNo = -26;
        goto zEndMark;
    }

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数 */
    strcpy(zpGlobRepo_[zRepoId]->dpingSig, zGet_OneCommitSig(zpTopVecWrap_, zCommitId));

    /* 重置布署相关状态 */
    zpGlobRepo_[zRepoId]->dpTotalTask = zpGlobRepo_[zRepoId]->totalHost;
    zpGlobRepo_[zRepoId]->dpReplyCnt = 0;

    /* 预置本次布署日志 */
    _i zOffSet = sprintf(zpCommonBuf, "INSERT INTO dp_log (proj_id,time_stamp,rev_sig,host_ip) VALUES ");
    for (zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
        zOffSet += sprintf(zpCommonBuf + zOffSet,
                "($1,$2,$3,'%s'),",
                zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter].p_hostIpStrAddr
                );
    }
    zpCommonBuf[zOffSet - 1] = '\0';  /* 去除最后一个逗号 */

    char zParamBuf[2][16] = {{'\0'}};
    const char *zpParam[3] = {
        zParamBuf[0],
        zParamBuf[1],
        zpGlobRepo_[zRepoId]->dpingSig
    };
    const char **zppParam = zpParam;  // avoid compile warning...

    sprintf(zParamBuf[0], "%d", zRepoId);
    sprintf(zParamBuf[1], "%ld", zpGlobRepo_[zRepoId]->dpBaseTimeStamp);

    if (NULL == (zpPgResHd_ = zPgSQL_.exec_with_param(
                    zpGlobRepo_[zRepoId]->p_pgConnHd_,
                    zpCommonBuf,
                    3, zppParam,
                    zFalse))) {
        zPgSQL_.conn_reset(zpGlobRepo_[zRepoId]->p_pgConnHd_);
        if (NULL == (zpPgResHd_ = zPgSQL_.exec_with_param(
                        zpGlobRepo_[zRepoId]->p_pgConnHd_,
                        zpCommonBuf,
                        3, zppParam,
                        zFalse))) {
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpGlobRepo_[zRepoId]->p_pgConnHd_);
            zPrint_Err(0, NULL, "!!!==== FATAL ====!!!");
            exit(1);
        }
    }

    /* 确认布署环境无误，此时断开连接 */
    shutdown(zSd, SHUT_RDWR);

    /*
     * 基于 libgit2 实现 zgit_push(...) 函数，在系统负载上限之内并发布署
     * 参数与之前的 ssh 动作完全相同，此处无需再次赋值
     */
    for (zCnter = 0; zCnter < zpGlobRepo_[zRepoId]->totalHost; zCnter++) {
        zThreadPool_.add(zgit_push_ccur, &(zpGlobRepo_[zRepoId]->p_dpCcur_[zCnter]));
    }

    /* 等待所有 git push 任务完成 */
    pthread_mutex_lock(&zpGlobRepo_[zRepoId]->dpSyncLock);

    while (zpGlobRepo_[zRepoId]->dpReplyCnt < zpGlobRepo_[zRepoId]->dpTotalTask
            && 1 == zpGlobRepo_[zRepoId]->dpingMark) {
        pthread_cond_wait(&zpGlobRepo_[zRepoId]->dpSyncCond, &zpGlobRepo_[zRepoId]->dpSyncLock);
    }

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
    }

    pthread_mutex_unlock( &zpGlobRepo_[zRepoId]->dpSyncLock );

zEndMark:
    zpGlobRepo_[zRepoId]->dpingMark = 0;
    pthread_mutex_unlock( &(zpGlobRepo_[zRepoId]->dpLock) );
    return zErrNo;
}


/*
 * 8：布署成功人工确认
 * 9：布署成功目标机自动确认
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
            zpHostAddr,\
            zTimeStamp,\
            zpRevSig);\
} while (0);

static _i
zstate_confirm(cJSON *zpJRoot, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmp_ = NULL;
    _i zErrNo = 0,
       zRepoId = 0;
    _ull zHostId[2] = {0};
    time_t zTimeSpent = 0,
           zTimeStamp = 0;

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

    /* 正文... */
    zpTmp_ = zpGlobRepo_[zRepoId]->p_dpResHash_[zHostId[0] % zDpHashSiz];
    for (; zpTmp_ != NULL; zpTmp_ = zpTmp_->p_next) {  // 遍历
        if (zIpVecCmp(zpTmp_->clientAddr, zHostId)) {
            /* 'B' 标识布署状态回复，'C' 目标机的 keep alive 消息 */
            if ('B' == zpReplyType[0]) {
                if (0 != strcmp(zpGlobRepo_[zRepoId]->dpingSig, zpRevSig)
                        || zTimeStamp != zpGlobRepo_[zRepoId]->dpBaseTimeStamp) {
                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    zErrNo = -101;

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, zpHostAddr, "record update failed");

                        /* 若更新失败，则尝试添加一条新记录 */
                        snprintf(zCmdBuf, zGlobCommonBufSiz, "INSERT INTO dp_log "
                                "(proj_id, time_stamp, rev_sig, host_ip, host_errno, host_detail) "
                                "VALUES (%d,%ld,%s,%s,%d,%s)",
                                zRepoId,
                                zTimeStamp,
                                zpRevSig,
                                zpHostAddr,
                                zErrNo,
                                zpTmp_->errMsg);
                        zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL);
                        zPrint_Err(0, zpHostAddr, "record insert failed");
                    }

                    goto zMarkEnd;
                }

                /*
                 * 已确定完全成功或必然失败的目标机状态，不允许进一步改变
                 * 理论上不会出现
                 */
                if (zCheck_Bit(zpTmp_->state, 5)
                        || zCheck_Bit(zpTmp_->state, 6)) {
                    zErrNo = -81;
                    // TO DO: insert a new record ???
                    goto zMarkEnd;
                }

                /* 负号 '-' 表示是异常返回，正号 '+' 表示是阶段性成功返回 */
                if ('-' == zpReplyType[1]) {
                    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

                    /* 发生严重错误，确认此台目标机布署失败，计数加一 */
                    zSet_Bit(zpTmp_->state, 6);  /* bit[5] 标识已发生错误 */
                    zSet_Bit(zpGlobRepo_[zRepoId]->resType, 2);  /* bit[1] 置位表示目标机布署出错返回 */
                    zpGlobRepo_[zRepoId]->dpReplyCnt++;

                    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "content");  /* 可以为空，不检查结果 */
                    strncpy(zpTmp_->errMsg, zpJ->valuestring, 255);
                    zpTmp_->errMsg[255] = '\0';

                    pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                    if (zpGlobRepo_[zRepoId]->dpReplyCnt == zpGlobRepo_[zRepoId]->dpTotalTask) {
                        pthread_cond_signal(&zpGlobRepo_[zRepoId]->dpSyncCond);
                    }
                    zErrNo = -102;

                    /* [DEBUG]：布署耗时统计 */
                    zTimeSpent = time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp;

                    zGenerate_SQL_Cmd(zCmdBuf);
                    if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                        zPrint_Err(0, zpHostAddr, "record update failed");
                    }

                    goto zMarkEnd;
                } else if ('+' == zpReplyType[1]) {
                    pthread_mutex_lock(&(zpGlobRepo_[zRepoId]->dpSyncLock));

                    switch (zpReplyType[2]) {
                        case '3':
                            zSet_Bit(zpTmp_->state, 3);
                            pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                            zErrNo = 0;
                            break;
                        case '4':
                            zSet_Bit(zpTmp_->state, 4);
                            pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                            zErrNo = 0;
                            break;
                        case '5':
                            zSet_Bit(zpTmp_->state, 5);
                            zpGlobRepo_[zRepoId]->dpReplyCnt++;

                            pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                            if (zpGlobRepo_[zRepoId]->dpReplyCnt == zpGlobRepo_[zRepoId]->dpTotalTask) {
                                pthread_cond_signal(&zpGlobRepo_[zRepoId]->dpSyncCond);
                            }
                            zErrNo = 0;

                            /* [DEBUG]：布署耗时统计 */
                            zTimeSpent = time(NULL) - zpGlobRepo_[zRepoId]->dpBaseTimeStamp;

                            zGenerate_SQL_Cmd(zCmdBuf);
                            if (0 > zPgSQL_.exec_once(zGlobPgConnInfo, zCmdBuf, NULL)) {
                                zPrint_Err(0, zpHostAddr, "record update failed");
                            }

                            break;
                        default:
                            zErrNo = -103;  // 无法识别的返回内容
                            pthread_mutex_unlock(&(zpGlobRepo_[zRepoId]->dpSyncLock));
                            break;
                    }

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
