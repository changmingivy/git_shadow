#include "zDpOps.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
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

static _i
zconvert_json_str_to_struct(char *zpJsonStr, zMeta__ *zpMeta_);

static void
zconvert_struct_to_json_str(char *zpJsonStrBuf, zMeta__ *zpMeta_);

static _i
zshow_all_repo_meta(zMeta__ *zpMeta_ __attribute__ ((__unused__)), _i zSd);

static _i
zshow_one_repo_meta(zMeta__ *zpParam, _i zSd);

static _i
zadd_repo(zMeta__ *zpMeta_, _i zSd);

static _i
zprint_record(zMeta__ *zpMeta_, _i zSd);

static _i
zprint_diff_files(zMeta__ *zpMeta_, _i zSd);

static _i
zprint_diff_content(zMeta__ *zpMeta_, _i zSd);

static _i
zupdate_ip_db_all(zMeta__ *zpMeta_, char *zpCommonBuf, zRegRes__ **zppRegRes_Out);

static _i
zself_deploy(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__)));

static _i
zbatch_deploy(zMeta__ *zpMeta_, _i zSd);

static _i
zstate_confirm(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__)));

static _i
zlock_repo(zMeta__ *zpMeta_, _i zSd);

static _i
zreq_file(zMeta__ *zpMeta_, _i zSd);

/* 对外公开的统一接口 */
struct zDpOps__ zDpOps_ = {
    .show_meta = zshow_one_repo_meta,
    .show_meta_all = zshow_all_repo_meta,
    .print_revs = zprint_record,
    .print_diff_files = zprint_diff_files,
    .print_diff_contents = zprint_diff_content,
    .creat = zadd_repo,
    .req_dp = zself_deploy,
    .dp = zbatch_deploy,
    .state_confirm = zstate_confirm,
    .lock = zlock_repo,
    .req_file = zreq_file,
    .json_to_struct = zconvert_json_str_to_struct,
    .struct_to_json = zconvert_struct_to_json_str
};

/*
 *  接收数据时使用
 *  将json文本转换为zMeta__结构体
 *  返回：出错返回-1，正常返回0
 */
static _i
zconvert_json_str_to_struct(char *zpJsonStr, zMeta__ *zpMeta_) {
    zRegInit__ zRegInit_[1];
    zRegRes__ zRegRes_[1] = {{.alloc_fn = NULL}};  // 此时尚没取得 zpMeta_->repo_ 之值，不可使用项目内存池

    zPosixReg_.compile(zRegInit_, "[^][}{\",:][^][}{\",]*");  // posix 的扩展正则语法中，中括号中匹配'[' 或 ']' 时需要将后一半括号放在第一个位置，而且不能转义
    zPosixReg_.match(zRegRes_, zRegInit_, zpJsonStr);
    zPosixReg_.free_meta(zRegInit_);

    zRegRes_->cnt -= zRegRes_->cnt % 2;  // 若末端有换行、空白之类字符，忽略之

    void *zpBuf[128];
    zpBuf['O'] = &(zpMeta_->opsId);
    zpBuf['P'] = &(zpMeta_->repoId);
    zpBuf['R'] = &(zpMeta_->commitId);
    zpBuf['F'] = &(zpMeta_->fileId);
    zpBuf['H'] = &(zpMeta_->hostId);
    zpBuf['C'] = &(zpMeta_->cacheId);
    zpBuf['D'] = &(zpMeta_->dataType);
    zpBuf['d'] = zpMeta_->p_data;
    zpBuf['E'] = zpMeta_->p_extraData;

    for (_ui zCnter = 0; zCnter < zRegRes_->cnt; zCnter += 2) {
        if (NULL == zNativeOps_.json_parser[(_i)(zRegRes_->p_rets[zCnter][0])]) {
            strcpy(zpMeta_->p_data, zpJsonStr);  // 必须复制，不能调整指针，zpJsonStr 缓存区会被上层调用者复用
            zPosixReg_.free_res(zRegRes_);
            return -7;
        }

        zNativeOps_.json_parser[(_i)(zRegRes_->p_rets[zCnter][0])](zRegRes_->p_rets[zCnter + 1], zpBuf[(_i)(zRegRes_->p_rets[zCnter][0])]);
    }

    zPosixReg_.free_res(zRegRes_);
    return 0;
}


/*
 * 生成缓存时使用
 * 将结构体数据转换成生成json文本
 */
static void
zconvert_struct_to_json_str(char *zpJsonStrBuf, zMeta__ *zpMeta_) {
    sprintf(
            zpJsonStrBuf, ",{\"OpsId\":%d,\"CacheId\":%d,\"ProjId\":%d,\"RevId\":%d,\"FileId\":%d,\"DataType\":%d,\"data\":\"%s\",\"ExtraData\":\"%s\"}",
            zpMeta_->opsId,
            zpMeta_->cacheId,
            zpMeta_->repoId,
            zpMeta_->commitId,
            zpMeta_->fileId,
            zpMeta_->dataType,
            (NULL == zpMeta_->p_data) ? "_" : zpMeta_->p_data,
            (NULL == zpMeta_->p_extraData) ? "_" : zpMeta_->p_extraData
            );
}


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
    return zLibSsh_.exec(zpHostIpAddr, "22", zpCmd, "git", "/home/git/.ssh/id_rsa.pub", "/home/git/.ssh/id_rsa", NULL, 1, NULL, 0, zpCcurLock, zpErrBufOUT);
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
    sprintf(zRemoteRepoAddrBuf, "ssh://git@%s/%s/.git", zpDpCcur_->p_hostIpStrAddr, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9);

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
                "echo '%s' >/home/git/.____zself_ip_addr_%d.txt;"

                "exec 777<>/dev/tcp/%s/%s;"
                "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":%s_SHADOW/tools/post-update}\" >&777;"
                "cat <&777 >.git/hooks/post-update;"
                "chmod 0755 .git/hooks/post-update;"
                "exec 777>&-;"
                "exec 777<&-;",

                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9, zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9,
                zpGlobRepo_[zpDpCcur_->repoId]->p_repoPath + 9,
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


/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if ((0 > zpMeta_->commitId)\
            || ((zCacheSiz - 1) < zpMeta_->commitId)\
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_data)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_extraData[0] = '\0';\
        return -3;\
    }\
} while(0)


/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMeta_->fileId)\
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_)\
            || ((zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_->vecSiz - 1) < zpMeta_->fileId)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_extraData[0] = '\0';\
        return -4;\
    }\
} while(0)


/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zpGlobRepo_[zpMeta_->repoId]->cacheId != zpMeta_->cacheId) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_extraData[0] = '\0';\
        zpMeta_->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;\
        return -8;\
    }\
} while(0)


/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDpLocked == zpGlobRepo_[zpMeta_->repoId]->dpLock) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_extraData[0] = '\0';\
        return -6;\
    }\
} while(0)


/*
 * 0: 测试函数
 */
// static _i
// ztest_func(zMeta__ *zpParam, _i zSd __attribute__ ((__unused__))) { return 0; }


/*
 * 5：显示所有项目及其元信息
 * 6：显示单个项目及其元信息
 */
static _i
zshow_all_repo_meta(zMeta__ *zpMeta_ __attribute__ ((__unused__)), _i zSd) {
    char zSendBuf[zGlobCommonBufSiz];

    zNetUtils_.sendto(zSd, "[", zBytes(1), 0, NULL);  // 凑足json格式
    for(_i zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
        if (NULL == zpGlobRepo_[zCnter] || 0 == zpGlobRepo_[zCnter]->initRepoFinMark) { continue; }

        if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zCnter]->rwLock))) {
            sprintf(zSendBuf, "{\"OpsId\":-11,\"data\":\"Id %d\"},", zCnter);
            zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);
            continue;
        };

        sprintf(zSendBuf, "{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"},",
                zCnter,
                zpGlobRepo_[zCnter]->p_repoPath,
                zDpLocked == zpGlobRepo_[zCnter]->dpLock ? "No" : "Yes",
                '\0' == zpGlobRepo_[zCnter]->lastDpSig[0] ? "_" : zpGlobRepo_[zCnter]->lastDpSig,
                zRepoDamaged == zpGlobRepo_[zCnter]->repoState ? "fail" : "success",
                zpGlobRepo_[zCnter]->totalHost
                );
        zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

        pthread_rwlock_unlock(&(zpGlobRepo_[zCnter]->rwLock));
    }

    zNetUtils_.sendto(zSd, "{\"OpsId\":0,\"data\":\"__END__\"}]", sizeof("{\"OpsId\":0,\"data\":\"__END__\"}]") - 1, 0, NULL);  // 凑足json格式，同时防止内容为空时，前端无法解析
    return 0;
}


/*
 * 6：显示单个项目及其元信息
 */
static _i
zshow_one_repo_meta(zMeta__ *zpParam, _i zSd) {
    zMeta__ *zpMeta_ = (zMeta__ *) zpParam;
    char zSendBuf[zGlobCommonBufSiz];

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    sprintf(zSendBuf, "[{\"OpsId\":0,\"data\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"}]",
            zpMeta_->repoId,
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,
            zDpLocked == zpGlobRepo_[zpMeta_->repoId]->dpLock ? "No" : "Yes",
            '\0' == zpGlobRepo_[zpMeta_->repoId]->lastDpSig[0] ? "_" : zpGlobRepo_[zpMeta_->repoId]->lastDpSig,
            zRepoDamaged == zpGlobRepo_[zpMeta_->repoId]->repoState ? "fail" : "success",
            zpGlobRepo_[zpMeta_->repoId]->totalHost
            );
    zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
    return 0;
}


/*
 * 1：添加新项目（代码库）
 */
static _i
zadd_repo(zMeta__ *zpMeta_, _i zSd) {
    zRegInit__ *zpRegInit_ = NULL;
    zRegRes__ *zpRegRes_ = NULL;
    _i zErrNo = 0;

    zPgResTuple__ zRepoMeta_ = { .p_taskCnt = NULL };
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    zPosixReg_.compile(zpRegInit_, "(\\w|[[:punct:]])+");
    zPosixReg_.match(zpRegRes_, zpRegInit_, zpMeta_->p_data);
    zPosixReg_.free_meta(zpRegInit_);

    if (5 > zpRegRes_->cnt) { return -34; }
    char *zpStrPtr[zpRegRes_->cnt];
    zRepoMeta_.pp_fields = zpStrPtr;

    zRepoMeta_.pp_fields[0] = zpRegRes_->p_rets[0];
    zRepoMeta_.pp_fields[1] = zpRegRes_->p_rets[1];
    zRepoMeta_.pp_fields[2] = zpRegRes_->p_rets[2];
    zRepoMeta_.pp_fields[3] = zpRegRes_->p_rets[3];
    zRepoMeta_.pp_fields[4] = zpRegRes_->p_rets[4];
    if (5 == zpRegRes_->cnt) {
        zRepoMeta_.pp_fields[5]= NULL;
    } else {
        zRepoMeta_.pp_fields[5]= zpRegRes_->p_rets[5];
    }

    if (0 == (zErrNo = zNativeOps_.proj_init(&zRepoMeta_))) {
        /* 连接 pgSQL server */
        if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zGlobPgConnInfo))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            zErrNo = -31;
            goto zMarkEnd;
        }

        /* 执行 SQL cmd */
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(zpPgConnHd_, "...", false))) {  // TO DO: SQL command
            zPgSQL_.res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpPgConnHd_);
            zErrNo = -31;
            goto zMarkEnd;
        }

        zNetUtils_.sendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    }

zMarkEnd:
    zPosixReg_.free_res(zpRegRes_);
    return zErrNo;
}


/*
 * 全量刷新：只刷新版本号列表
 * 需要继承下层已存在的缓存
 */
static _i
zrefresh_cache(zMeta__ *zpMeta_) {
//    _i zCnter[2];
//    struct iovec zOldVec_[zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.vecSiz];
//    zRefData__ zOldRefData_[zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.vecSiz];
//
//    for (zCnter[0] = 0; zCnter[0] < zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.vecSiz; zCnter[0]++) {
//        zOldVec_[zCnter[0]].iov_base = zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_vec_[zCnter[0]].iov_base;
//        zOldVec_[zCnter[0]].iov_len = zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_vec_[zCnter[0]].iov_len;
//        zOldRefData_[zCnter[0]].p_data  = zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_refData_[zCnter[0]].p_data;
//        zOldRefData_[zCnter[0]].p_subVecWrap_ = zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_refData_[zCnter[0]].p_subVecWrap_;
//    }

    zNativeOps_.get_revs(zpMeta_);  // 复用了 zops_route 函数传下来的 Meta__ 结构体(栈内存)

//    zCnter[1] = zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.vecSiz;
//    if (zCnter[1] > zCnter[0]) {
//        for (zCnter[0]--, zCnter[1]--; zCnter[0] >= 0; zCnter[0]--, zCnter[1]--) {
//            if (NULL == zOldRefData_[zCnter[0]].p_subVecWrap_) { continue; }
//            if (NULL == zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_refData_[zCnter[1]].p_subVecWrap_) { break; }  // 若新内容为空，说明已经无法一一对应，后续内容无需再比较
//            if (0 == (strcmp(zOldRefData_[zCnter[0]].p_data, zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_refData_[zCnter[1]].p_data))) {
//                zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_vec_[zCnter[1]].iov_base = zOldVec_[zCnter[0]].iov_base;
//                zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_vec_[zCnter[1]].iov_len = zOldVec_[zCnter[0]].iov_len;
//                zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_.p_refData_[zCnter[1]].p_subVecWrap_ = zOldRefData_[zCnter[0]].p_subVecWrap_;
//            } else {
//                break;  // 若不能一一对应，则中断
//            }
//        }
//    }

    return 0;
}


/*
 * 7：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
static _i
zprint_record(zMeta__ *zpMeta_, _i zSd) {
    zVecWrap__ *zpSortedTopVecWrap_;

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->sortedCommitVecWrap_);
        /*
         * 如果该项目被标记为被动拉取模式（相对的是主动推送模式），则：
         *     若距离最近一次 “git pull“ 的时间间隔超过 10 秒，尝试拉取远程代码
         *     放在取得读写锁之后执行，防止与布署过程中的同类运作冲突
         *     取到锁，则拉取；否则跳过此步，直接打印列表
         *     打印布署记录时不需要执行
         */
        if (10 < (time(NULL) - zpGlobRepo_[zpMeta_->repoId]->lastPullTime)) {
            if ((0 == zpGlobRepo_[zpMeta_->repoId]->selfPushMark)
                    && (0 == pthread_mutex_trylock( &(zpGlobRepo_[zpMeta_->repoId]->pullLock) ))) {

                system(zpGlobRepo_[zpMeta_->repoId]->p_pullCmd);  /* 不能多线程，因为多个 git pull 会产生文件锁竞争 */
                zpGlobRepo_[zpMeta_->repoId]->lastPullTime = time(NULL); /* 以取完远程代码的时间重新赋值 */

                zGitRevWalk__ *zpRevWalker;
                char zCommonBuf[64] = {'\0'};
                sprintf(zCommonBuf, "refs/heads/server%d", zpMeta_->repoId);
                if (NULL != (zpRevWalker = zLibGit_.generate_revwalker(zpGlobRepo_[zpMeta_->repoId]->p_gitRepoHandler, zCommonBuf, 0))) {
                    zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf, zpGlobRepo_[zpMeta_->repoId]->p_gitRepoHandler, zpRevWalker);
                    zLibGit_.destroy_revwalker(zpRevWalker);
                }
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->pullLock) );

                if ((NULL == zpGlobRepo_[zpMeta_->repoId]->commitRefData_[0].p_data)
                        || (0 != strncmp(zCommonBuf, zpGlobRepo_[zpMeta_->repoId]->commitRefData_[0].p_data, 40))) {
                    zpMeta_->dataType = zIsCommitDataType;

                    /* 此处进行换锁：读锁与写锁进行两次互换 */
                    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
                    if (0 != pthread_rwlock_trywrlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
                        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };

                    zrefresh_cache(zpMeta_);

                    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
                    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
                        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };
                }
            }
        }
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->sortedDpVecWrap_);
    } else {
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
        return -10;
    }

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrap_->vecSiz) {
        if (0 < zNetUtils_.sendmsg(zSd, zpSortedTopVecWrap_->p_vec_, zpSortedTopVecWrap_->vecSiz, 0, NULL)) {
            zNetUtils_.sendto(zSd, "]", zBytes(1), 0, NULL);  // 二维json结束符
        } else {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            return -70;
        }
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
    return 0;
}


/*
 * 10：显示差异文件路径列表
 */
static _i
zprint_diff_files(zMeta__ *zpMeta_, _i zSd) {
    zVecWrap__ *zpTopVecWrap_, zSendVecWrap_;
    _i zSplitCnt;

    /* 若上一次布署是部分失败的，返回 -13 错误 */
    if (zRepoDamaged == zpGlobRepo_[zpMeta_->repoId]->repoState) {
        zpMeta_->p_data = "====上一次布署失败，请重试布署====";
        return -13;
    }

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
        zpMeta_->dataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
        zpMeta_->dataType = zIsDpDataType;
    } else {
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
        }

        return -11;
    }

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            zpMeta_->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->p_vec_;
    zSplitCnt = (zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_subVecWrap_->vecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }
    zNetUtils_.sendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
    return 0;
}


/*
 * 11：显示差异文件内容
 */
static _i
zprint_diff_content(zMeta__ *zpMeta_, _i zSd) {
    zVecWrap__ *zpTopVecWrap_, zSendVecWrap_;
    _i zSplitCnt;

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
        zpMeta_->dataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
        zpMeta_->dataType = zIsDpDataType;
    } else {
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)) {
        if ((void *) -1 == zNativeOps_.get_diff_files(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            zpMeta_->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->commitId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zCheck_FileId();  // 宏内部会解锁
    if (NULL == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)) {
        if ((void *) -1 == zNativeOps_.get_diff_contents(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

            if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrap_.vecSiz = 0;
    zSendVecWrap_.p_vec_ = zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->p_vec_;
    zSplitCnt = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrap_.vecSiz = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->commitId, zpMeta_->fileId)->vecSiz - 1) % zSendUnitSiz + 1;
        }
        else {
            zSendVecWrap_.vecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_vec_, zSendVecWrap_.vecSiz, 0, NULL);
        zSendVecWrap_.p_vec_ += zSendVecWrap_.vecSiz;
    }

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
            "echo ${____zSelfIp} >/home/git/.____zself_ip_addr_%d.txt;"\
\
            "exec 777<>/dev/tcp/%s/%s;"\
            "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":%s_SHADOW/tools/post-update}\" >&777;"\
            "rm -f .git/hooks/post-update;"\
            "cat <&777 >.git/hooks/post-update;"\
            "chmod 0755 .git/hooks/post-update;"\
            "exec 777>&-;"\
            "exec 777<&-;",\
\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9, zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,\
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,\
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
    zpRegRes_ = zRegRes_;

    zPosixReg_.compile(zRegInit_ , "([0-9]{1,3}\\.){3}[0-9]{1,3}");
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
        if (0 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr) {
            strcpy(zpMeta_->p_data, zRegRes_->p_rets[zCnter]);
            zpMeta_->p_extraData[0] = '\0';
            return -19;
        }

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
        _ui zFailHostCnt = 0;
        _i zOffSet = sprintf(zpMeta_->p_data, "无法连接的主机:");
        for (_ui zCnter = 0; (zOffSet < zpMeta_->dataLen) && (zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost); zCnter++) {
            if (1 != zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState) {
                zNetUtils_.to_str(zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMeta_->p_data + zOffSet, "([%s]%s)",
                        zIpStrAddrBuf,
                        '\0' == zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg[0] ? "" : zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].errMsg
                        );
                zFailHostCnt++;

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr = 0;
            }
        }

        /* 主机数超过 10 台，且失败率低于 10% 返回成功，否则返回失败 */
        if ((10 < zpGlobRepo_[zpMeta_->repoId]->totalHost) && ( zFailHostCnt < zpGlobRepo_[zpMeta_->repoId]->totalHost / 10)) { return 0; }
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
    zVecWrap__ *zpTopVecWrap_;
    _i zErrNo = 0;
    time_t zRemoteHostInitTimeSpent = 0;

    if (zIsCommitDataType == zpMeta_->dataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->repoId]->commitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->dataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->repoId]->dpVecWrap_);
    } else {
        zpMeta_->p_data = "====[JSON: DataType] 字段指定的数据类型无效====";
        zpMeta_->p_extraData[0] = '\0';
        zErrNo = -10;
        goto zEndMark;
    }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepo_[zpMeta_->repoId]->dpLock) {
        zpMeta_->p_data = "====代码库被锁定，不允许布署====";
        zpMeta_->p_extraData[0] = '\0';
        zErrNo = -6;
        goto zEndMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepo_[zpMeta_->repoId]->cacheId != zpMeta_->cacheId) {
        zpMeta_->p_data = "====已产生新的布署记录，请刷新页面====";
        zpMeta_->p_extraData[0] = '\0';
        zpMeta_->cacheId = zpGlobRepo_[zpMeta_->repoId]->cacheId;
        zErrNo = -8;
        goto zEndMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zpMeta_->commitId)
            || ((zCacheSiz - 1) < zpMeta_->commitId)
            || (NULL == zpTopVecWrap_->p_refData_[zpMeta_->commitId].p_data)) {
        zpMeta_->p_data = "====指定的版本号无效====";
        zpMeta_->p_extraData[0] = '\0';
        zErrNo = -3;
        goto zEndMark;
    }

    /* 预布署动作：须置于 zupdate_ip_db_all(...) 函数之前，因 post-update 会在初始化远程主机时被首先传输 */
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
            "git add --all .;"\
            "git commit --allow-empty -m _;"\
            "git push --force %s/.git master:master_SHADOW",
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,  // 中控机上的代码库路径
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId),  // SHA1 commit sig
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath,
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath + 9,  // 目标机上的代码库路径(即：去掉最前面的 "/home/git" 合计 9 个字符)
            zpGlobRepo_[zpMeta_->repoId]->p_repoPath
            );

    /* 调用 git 命令执行布署前的环境准备；同时用于测算中控机本机所有动作耗时，用作布署超时基数 */
    zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp = time(NULL);
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
        zpMeta_->p_data = "====指定的目标主机 IP 列表无效====";
        zpMeta_->p_extraData[0] = '\0';
        zErrNo = -26;
        goto zEndMark;
    }

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数；另复制一份供失败重试之用 */
    strncpy(zpGlobRepo_[zpMeta_->repoId]->dpingSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId), zBytes(40));
    strncpy(zpMeta_->p_extraData, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->commitId), zBytes(40));

    /* 重置布署相关状态 */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
        zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].dpState = 0;
    }
    zpGlobRepo_[zpMeta_->repoId]->dpTotalTask = zpGlobRepo_[zpMeta_->repoId]->totalHost;
    zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->resType[1] = 0;
    //zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt = 0;
    zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit = 0;

    /* 基于 libgit2 实现 zgit_push(...) 函数，在系统负载上限之下并发布署；参数与之前的SSH动作完全相同，此处无需再次赋值 */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->repoId]->totalHost; zCnter++) {
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
        _i zSendLen = sprintf(zppCommonBuf[0], "[{\"OpsId\":-14,\"data\":\"本次布署时间最长可达 %zd 秒，请稍后查看布署结果\"}]", zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit);
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
        zErrNo = -10000;
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

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].clientAddr = 0;
            }
        }
        zpMeta_->p_extraData = zpGlobRepo_[zpMeta_->repoId]->dpingSig;
        zErrNo = -12;
        goto zEndMark;
    }

    /* 若先前测算的布署耗时 <= 90s ，此处向前端返回布署成功消息 */
    if (90 >= zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) {
        zNetUtils_.sendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
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

        zSubMeta_.dataType = zIsCommitDataType;
        zNativeOps_.get_revs(&zSubMeta_);
        zSubMeta_.dataType = zIsDpDataType;
        zNativeOps_.get_revs(&zSubMeta_);
    }

zEndMark:;
    /* 无论布署成败，都写入 pgSQL 日志表: db_dp_log_<ProjId> */
    zPgResHd__ *zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, "...", false);  // TO DO: SQL cmd
    if (NULL == zpPgResHd_) {
        zPgSQL_.conn_reset(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_);
        zpPgResHd_ = zPgSQL_.exec(zpGlobRepo_[zpMeta_->repoId]->p_pgConnHd_, "...", false);  // TO DO: SQL cmd

        if (NULL == zpPgResHd_) {
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
zself_deploy(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__))) {
    /* 若目标机上已是最新代码，则无需布署 */
    if (0 != strncmp(zpMeta_->p_extraData, zpGlobRepo_[zpMeta_->repoId]->lastDpSig, 40)) {
        zDpCcur__ *zpDpSelf_ = zNativeOps_.alloc(zpMeta_->repoId, sizeof(zDpCcur__));
        zpDpSelf_->repoId = zpMeta_->repoId;
        zpDpSelf_->p_hostIpStrAddr = zpMeta_->p_data;
        zpDpSelf_->p_ccurLock = NULL;  // 标记无需发送通知给调用者的条件变量

        zgit_push_ccur(zpDpSelf_);
    }

    return 0;
}


/*
 * 外壳函数
 * 12：布署／撤销
 */
static _i
zbatch_deploy(zMeta__ *zpMeta_, _i zSd) {
    /* 系统高负载时，不接受布署请求，保留 20% 的性能提供查询等’读‘操作 */
    if (80 < zGlobMemLoad) {
        zpMeta_->p_data = "====当前系统负载超过 80%，请稍后重试====";
        return -16;
    }

    if (0 != pthread_rwlock_trywrlock( &(zpGlobRepo_[zpMeta_->repoId]->rwLock) )) {
        if (0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit / 60.0);
        }
        return -11;
    }

    char *zppCommonBuf[2] = {NULL};
    zRegRes__ *zpHostStrAddrRegRes_ = NULL;
    _i zErrNo = 0, zCommonBufLen = 0;

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个Buf*/
    zCommonBufLen = 2048 + 10 * zpGlobRepo_[zpMeta_->repoId]->repoPathLen + zpMeta_->dataLen;
    zppCommonBuf[0] = zNativeOps_.alloc(zpMeta_->repoId, 2 * zCommonBufLen);
    zppCommonBuf[1] = zppCommonBuf[0] + zCommonBufLen;

    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock = 1;  // 置为 1
    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
    pthread_cond_signal(&zpGlobRepo_[zpMeta_->repoId]->dpSyncCond);  // 通知旧的版本重试动作中止

    pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );

    /* 确认全部成功或确认布署失败这两种情况，直接返回，否则进入不间断重试模式，直到新的布署请求到来 */
    if (-10000 != (zErrNo = zdeploy(zpMeta_, zSd, zppCommonBuf, &zpHostStrAddrRegRes_))) {
        zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepo_[zpMeta_->repoId]->rwLock) );
        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
        return zErrNo;
    } else {
        zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepo_[zpMeta_->repoId]->rwLock) );
        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );

        /* 在没有新的布署动作之前，持续尝试布署失败的目标机 */
        while(1) {
            /* 等待剩余的所有主机状态都得到确认，不必在锁内执行 */
            for (_l zTimeCnter = 0; zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit > zTimeCnter; zTimeCnter++) {
                if ((0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock)  /* 检测是否有新的布署请求 */
                        || ((zpGlobRepo_[zpMeta_->repoId]->totalHost == zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt) && (-1 != zpGlobRepo_[zpMeta_->repoId]->resType[1]))) {

                    //zPg_Alter_Dp_Res("");  /* TO DO: SQL cmd */
                    return 0;
                }

                zNativeUtils_.sleep(0.1);
            }

            pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
            if (0 !=  strncmp(zpGlobRepo_[zpMeta_->repoId]->dpingSig, zpMeta_->p_extraData, 40)) {
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
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_hostIpStrAddr = zpHostStrAddrRegRes_->p_rets[zCnter];
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_cmd = zppCommonBuf[0];
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurLock = &zpGlobRepo_[zpMeta_->repoId]->dpSyncLock;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_ccurCond = &zpGlobRepo_[zpMeta_->repoId]->dpSyncCond;
                    zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter].p_taskCnt = &zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt;

                    zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_[zCnter]));

                    /* 调整目标机初始化状态数据（布署状态数据不调整！）*/
                    zpGlobRepo_[zpMeta_->repoId]->p_dpResList_[zCnter].initState = 0;
                } else {
                    zpGlobRepo_[zpMeta_->repoId]->dpTotalTask -= 1;
                    zpHostStrAddrRegRes_->p_rets[zCnter] = NULL;  // 去掉已成功的 IP 地址，只保留失败的部分
                }
            }

            /* 等待所有 SSH 任务完成，此处不再检查执行结果 */
            pthread_mutex_lock(&zpGlobRepo_[zpMeta_->repoId]->dpSyncLock);
            while ((0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) && (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask)) {
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
                //zPg_Alter_Dp_Res("");  /* TO DO: SQL cmd */
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                return 0;
            } else {
                /* 对失败的目标主机重试布署 */
                for (_ui zCnter = 0; zCnter < zpHostStrAddrRegRes_->cnt; zCnter++) {
                    /* 检测是否有新的布署请求 */
                    if (0 != zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) {
                        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
                        return 0;
                    }

                    /* 结构体各成员参数与目标机初始化时一致，无需修改，直接复用即可 */
                    if (NULL != zpHostStrAddrRegRes_->p_rets[zCnter]) {
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
                while ((0 == zpGlobRepo_[zpMeta_->repoId]->whoGetWrLock) && (zpGlobRepo_[zpMeta_->repoId]->dpTaskFinCnt < zpGlobRepo_[zpMeta_->repoId]->dpTotalTask)) {
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

            /* 超时上限延长为 2 倍 */
            zpGlobRepo_[zpMeta_->repoId]->dpTimeWaitLimit *= 2;

            pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->repoId]->dpRetryLock) );
        }
    }
}


/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
static _i
zstate_confirm(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmp_ = zpGlobRepo_[zpMeta_->repoId]->p_dpResHash_[zpMeta_->hostId % zDpHashSiz];
    time_t zTimeSpent = 0;

    for (; zpTmp_ != NULL; zpTmp_ = zpTmp_->p_next) {  // 遍历
        if (zpTmp_->clientAddr == zpMeta_->hostId) {
            pthread_mutex_lock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));

            char *zpLogStrId;
            /* 'B' 标识布署状态回复，'C' 目标机的 keep alive 消息 */
            if ('B' == zpMeta_->p_extraData[0]) {
                if (0 != zpTmp_->dpState) {
                    // SQL log... ???
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                    return 0;
                }

                if (0 != strncmp(zpGlobRepo_[zpMeta_->repoId]->dpingSig, zpMeta_->p_data, zBytes(40))) {
                    // SQL log...
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                    return -101;  // 返回负数，用于打印日志
                }

                if ('+' == zpMeta_->p_extraData[1]) {  // 负号 '-' 表示是异常返回，正号 '+' 表示是成功返回
                    zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt++;
                    zpTmp_->dpState = 1;

                    zpLogStrId = zpGlobRepo_[zpMeta_->repoId]->dpingSig;

                    /* 调试功能：布署耗时统计，必须在锁内执行 */
                    char zIpStrAddr[INET_ADDRSTRLEN];
                    zNetUtils_.to_str(zpMeta_->hostId, zIpStrAddr);
                    zTimeSpent = time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp;

                    // SQL log...
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                    if (zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt == zpGlobRepo_[zpMeta_->repoId]->dpTotalTask) {
                        pthread_cond_signal(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_->p_ccurCond);
                    }
                    return 0;
                } else if ('-' == zpMeta_->p_extraData[1]) {
                    zpGlobRepo_[zpMeta_->repoId]->dpReplyCnt = zpGlobRepo_[zpMeta_->repoId]->dpTotalTask;  // 发生错误，计数打满，用于通知结束布署等待状态
                    zpTmp_->dpState = -1;
                    zpGlobRepo_[zpMeta_->repoId]->resType[1] = -1;
                    zTimeSpent = time(NULL) - zpGlobRepo_[zpMeta_->repoId]->dpBaseTimeStamp;

                    snprintf(zpTmp_->errMsg, 256, "%s", zpMeta_->p_data + 40);  // 所有的状态回复前40个字节均是 git SHA1sig

                    // SQL log...
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                    pthread_cond_signal(zpGlobRepo_[zpMeta_->repoId]->p_dpCcur_->p_ccurCond);
                    return -102;  // 返回负数，用于打印日志
                } else {
                    // SQL log...
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                    return -103;  // 未知的返回内容
                }
            } else if ('C' == zpMeta_->p_extraData[0]) {
                zpGlobRepo_[zpMeta_->repoId]->dpKeepAliveStamp = time(NULL);
                pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                return 0;
            } else {
                // SQL log...
                pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->repoId]->dpSyncLock));
                return -103;  // 未知的返回内容
            }
        }
    }

    return 0;
}
#undef zPg_Alter_Dp_Res

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
static _i
zlock_repo(zMeta__ *zpMeta_, _i zSd) {
    pthread_rwlock_wrlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

    if (2 == zpMeta_->opsId) {
        zpGlobRepo_[zpMeta_->repoId]->dpLock = zDpLocked;
    } else {
        zpGlobRepo_[zpMeta_->repoId]->dpLock = zDpUnLock;
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->repoId]->rwLock));

    zNetUtils_.sendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);

    return 0;
}


/* 14: 向目标机传输指定的文件 */
static _i
zreq_file(zMeta__ *zpMeta_, _i zSd) {
    char zSendBuf[4096];
    _i zFd, zDataLen;

    zCheck_Negative_Return(zFd = open(zpMeta_->p_data, O_RDONLY), -80);
    while (0 < (zDataLen = read(zFd, zSendBuf, 4096))) {
        zNetUtils_.sendto(zSd, zSendBuf, zDataLen, 0, NULL);
    }

    close(zFd);
    return 0;
}
