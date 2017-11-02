#define ZDPOPS_H

#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "zNetUtils.h"
#include "zLibSsh.h"
#include "zLibGit.h"
#include "zLocalOps.h"
#include "zLocalUtils.h"
#include "zPosixReg.h"
#include "zThreadPool.h"
#include "zRun.h"

#include "zDpOps.h"

extern struct zNetUtils__ zNetUtils_;
extern struct zLibSsh__ zLibSsh_;
extern struct zLibGit__ zLibGit_;
extern struct zLocalOps__ zLocalOps_;
extern struct zLocalUtils__ zLocalUtils_;
extern struct zPosixReg__ zPosixReg_;
extern struct zThreadPool__ zThreadPool_;
extern struct zRun__ zRun_;

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

static void *
zops_route(void *zpParam);

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
    .route = zops_route,
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
    zRegRes__ zRegRes_[1] = {{.RepoId = -1}};  // 此时尚没取得 zpMeta_->Repo_ 之值，不可使用项目内存池

    zPosixReg_.compile(zRegInit_, "[^][}{\",:][^][}{\",]*");  // posix 的扩展正则语法中，中括号中匹配'[' 或 ']' 时需要将后一半括号放在第一个位置，而且不能转义
    zPosixReg_.match(zRegRes_, zRegInit_, zpJsonStr);
    zPosixReg_.free_meta(zRegInit_);

    zRegRes_->cnt -= zRegRes_->cnt % 2;  // 若末端有换行、空白之类字符，忽略之

    void *zpBuf[128];
    zpBuf['O'] = &(zpMeta_->OpsId);
    zpBuf['P'] = &(zpMeta_->RepoId);
    zpBuf['R'] = &(zpMeta_->CommitId);
    zpBuf['F'] = &(zpMeta_->FileId);
    zpBuf['H'] = &(zpMeta_->HostId);
    zpBuf['C'] = &(zpMeta_->CacheId);
    zpBuf['D'] = &(zpMeta_->DataType);
    zpBuf['d'] = zpMeta_->p_data;
    zpBuf['E'] = zpMeta_->p_ExtraData;

    for (_ui zCnter = 0; zCnter < zRegRes_->cnt; zCnter += 2) {
        if (NULL == zLocalOps_.json_parser[(_i)(zRegRes_->p_rets[zCnter][0])]) {
            strcpy(zpMeta_->p_data, zpJsonStr);  // 必须复制，不能调整指针，zpJsonStr 缓存区会被上层调用者复用
            zPosixReg_.free_res(zRegRes_);
            return -7;
        }

        zLocalOps_.json_parser[(_i)(zRegRes_->p_rets[zCnter][0])](zRegRes_->p_rets[zCnter + 1], zpBuf[(_i)(zRegRes_->p_rets[zCnter][0])]);
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
            zpMeta_->OpsId,
            zpMeta_->CacheId,
            zpMeta_->RepoId,
            zpMeta_->CommitId,
            zpMeta_->FileId,
            zpMeta_->DataType,
            (NULL == zpMeta_->p_data) ? "_" : zpMeta_->p_data,
            (NULL == zpMeta_->p_ExtraData) ? "_" : zpMeta_->p_ExtraData
            );
}


static void *
zssh_ccur(void  *zpParam) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;

    zLibSsh_.exec(zpDpCcur_->p_HostIpStrAddr, zpDpCcur_->p_HostServPort, zpDpCcur_->p_Cmd,
            zpDpCcur_->p_UserName, zpDpCcur_->p_PubKeyPath, zpDpCcur_->p_PrivateKeyPath, zpDpCcur_->p_PassWd, zpDpCcur_->zAuthType,
            zpDpCcur_->p_RemoteOutPutBuf, zpDpCcur_->RemoteOutPutBufSiz, zpDpCcur_->p_CcurLock);

    pthread_mutex_lock(zpDpCcur_->p_CcurLock);
    (* (zpDpCcur_->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcur_->p_CcurLock);
    pthread_cond_signal(zpDpCcur_->p_CcurCond);

    return NULL;
};


/* 简化参数版函数 */
static _i
zssh_exec_simple(char *zpHostIpAddr, char *zpCmd, pthread_mutex_t *zpCcurLock) {
    return zLibSsh_.exec(zpHostIpAddr, "22", zpCmd, "git", "/home/git/.ssh/id_rsa.pub", "/home/git/.ssh/id_rsa", NULL, 1, NULL, 0, zpCcurLock);
}


/* 简化参数版函数 */
static void *
zssh_ccur_simple(void  *zpParam) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;

    zssh_exec_simple(zpDpCcur_->p_HostIpStrAddr, zpDpCcur_->p_Cmd, zpDpCcur_->p_CcurLock);

    pthread_mutex_lock(zpDpCcur_->p_CcurLock);
    (* (zpDpCcur_->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcur_->p_CcurLock);
    pthread_cond_signal(zpDpCcur_->p_CcurCond);

    return NULL;
};


/* 远程主机初始化专用 */
static void *
zssh_ccur_simple_init_host(void  *zpParam) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zpParam;

    _ui zHostId = zNetUtils_.to_bin(zpDpCcur_->p_HostIpStrAddr);
    zDpRes__ *zpTmp_ = zpGlobRepo_[zpDpCcur_->RepoId]->p_DpResHash_[zHostId % zDpHashSiz];
    for (; NULL != zpTmp_; zpTmp_ = zpTmp_->p_next) {
        if (zHostId == zpTmp_->ClientAddr) {
            if (0 == zssh_exec_simple(zpDpCcur_->p_HostIpStrAddr, zpDpCcur_->p_Cmd, zpDpCcur_->p_CcurLock)) {
                zpTmp_->InitState = 1;
            } else {
                zpTmp_->InitState = -1;
                zpGlobRepo_[zpDpCcur_->RepoId]->ResType[0] = -1;
            }

            pthread_mutex_lock(zpDpCcur_->p_CcurLock);
            (* (zpDpCcur_->p_TaskCnt))++;
            pthread_mutex_unlock(zpDpCcur_->p_CcurLock);
            pthread_cond_signal(zpDpCcur_->p_CcurCond);

            break;
        }
    }

    return NULL;
};


#define zNative_Fail_Confirm() do {\
    _ui ____zHostId = zNetUtils_.to_bin(zpDpCcur_->p_HostIpStrAddr);\
    zDpRes__ *____zpTmp_ = zpGlobRepo_[zpDpCcur_->RepoId]->p_DpResHash_[____zHostId % zDpHashSiz];\
    for (; NULL != ____zpTmp_; ____zpTmp_ = ____zpTmp_->p_next) {\
        if (____zHostId == ____zpTmp_->ClientAddr) {\
            pthread_mutex_lock(&(zpGlobRepo_[zpDpCcur_->RepoId]->DpSyncLock));\
            ____zpTmp_->DpState = -1;\
            zpGlobRepo_[zpDpCcur_->RepoId]->DpReplyCnt = zpGlobRepo_[zpDpCcur_->RepoId]->DpTotalTask;  /* 发生错误，计数打满，用于通知结束布署等待状态 */\
            zpGlobRepo_[zpDpCcur_->RepoId]->ResType[1] = -1;\
            pthread_cond_signal(zpGlobRepo_[zpDpCcur_->RepoId]->p_DpCcur_->p_CcurCond);\
            pthread_mutex_unlock(&(zpGlobRepo_[zpDpCcur_->RepoId]->DpSyncLock));\
            break;\
        }\
    }\
} while(0)


static void *
zgit_push_ccur(void *zp_) {
    zDpCcur__ *zpDpCcur_ = (zDpCcur__ *) zp_;

    char zRemoteRepoAddrBuf[64 + zpGlobRepo_[zpDpCcur_->RepoId]->RepoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_wait(&(zpGlobRepo_[zpDpCcur_->RepoId]->DpTraficControl)) );

    /* when memory load > 80%，waiting ... */
    pthread_mutex_lock(&zGlobCommonLock);
    while (80 < zGlobMemLoad) {
        pthread_cond_wait(&zSysLoadCond, &zGlobCommonLock);
    }
    pthread_mutex_unlock(&zGlobCommonLock);

    /* generate remote URL */
    sprintf(zRemoteRepoAddrBuf, "ssh://git@%s/%s/.git", zpDpCcur_->p_HostIpStrAddr, zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9);

    /* {'+' == git push --force} push TWO branchs together */
    sprintf(zpGitRefs[0], "+refs/heads/master:refs/heads/server%d", zpDpCcur_->RepoId);
    sprintf(zpGitRefs[1], "+refs/heads/master_SHADOW:refs/heads/server%d_SHADOW", zpDpCcur_->RepoId);
    if (0 != zLibGit_.remote_push(zpGlobRepo_[zpDpCcur_->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2)) {
        /* if failed, delete '.git', ReInit the remote host */
        char zCmdBuf[1024 + 7 * zpGlobRepo_[zpDpCcur_->RepoId]->RepoPathLen];
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

                zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9, zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9,
                zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9, zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9,
                zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9,
                zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath + 9,
                zpDpCcur_->p_HostIpStrAddr, zpDpCcur_->RepoId,

                zNetSrv_.p_IpAddr, zNetSrv_.p_port,
                zpDpCcur_->RepoId, zpGlobRepo_[zpDpCcur_->RepoId]->p_RepoPath
                );
        if (0 == zssh_exec_simple(zpDpCcur_->p_HostIpStrAddr, zCmdBuf, &(zpGlobRepo_[zpDpCcur_->RepoId]->DpSyncLock))) {
            /* if init-ops success, then try deploy once more... */
            if (0 != zLibGit_.remote_push(zpGlobRepo_[zpDpCcur_->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2)) { zNative_Fail_Confirm(); }
        } else {
            zNative_Fail_Confirm();
        }
    }

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepo_[zpDpCcur_->RepoId]->DpTraficControl)) );

    return NULL;
}


/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if ((0 > zpMeta_->CommitId)\
            || ((zCacheSiz - 1) < zpMeta_->CommitId)\
            || (NULL == zpTopVecWrap_->p_RefData_[zpMeta_->CommitId].p_data)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_ExtraData[0] = '\0';\
        return -3;\
    }\
} while(0)


/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMeta_->FileId)\
            || (NULL == zpTopVecWrap_->p_RefData_[zpMeta_->CommitId].p_SubVecWrap_)\
            || ((zpTopVecWrap_->p_RefData_[zpMeta_->CommitId].p_SubVecWrap_->VecSiz - 1) < zpMeta_->FileId)) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_ExtraData[0] = '\0';\
        return -4;\
    }\
} while(0)


/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zpGlobRepo_[zpMeta_->RepoId]->CacheId != zpMeta_->CacheId) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_ExtraData[0] = '\0';\
        zpMeta_->CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;\
        return -8;\
    }\
} while(0)


/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDpLocked == zpGlobRepo_[zpMeta_->RepoId]->DpLock) {\
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));\
        zpMeta_->p_data[0] = '\0';\
        zpMeta_->p_ExtraData[0] = '\0';\
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
        if (NULL == zpGlobRepo_[zCnter] || 0 == zpGlobRepo_[zCnter]->zInitRepoFinMark) { continue; }

        if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zCnter]->RwLock))) {
            sprintf(zSendBuf, "{\"OpsId\":-11,\"data\":\"Id %d\"},", zCnter);
            zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);
            continue;
        };

        sprintf(zSendBuf, "{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"},",
                zCnter,
                zpGlobRepo_[zCnter]->p_RepoPath,
                zDpLocked == zpGlobRepo_[zCnter]->DpLock ? "No" : "Yes",
                '\0' == zpGlobRepo_[zCnter]->zLastDpSig[0] ? "_" : zpGlobRepo_[zCnter]->zLastDpSig,
                zRepoDamaged == zpGlobRepo_[zCnter]->RepoState ? "fail" : "success",
                zpGlobRepo_[zCnter]->TotalHost
                );
        zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

        pthread_rwlock_unlock(&(zpGlobRepo_[zCnter]->RwLock));
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

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    sprintf(zSendBuf, "[{\"OpsId\":0,\"data\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"}]",
            zpMeta_->RepoId,
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath,
            zDpLocked == zpGlobRepo_[zpMeta_->RepoId]->DpLock ? "No" : "Yes",
            '\0' == zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig[0] ? "_" : zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig,
            zRepoDamaged == zpGlobRepo_[zpMeta_->RepoId]->RepoState ? "fail" : "success",
            zpGlobRepo_[zpMeta_->RepoId]->TotalHost
            );
    zNetUtils_.sendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
    return 0;
}


/*
 * 1：添加新项目（代码库）
 */
static _i
zadd_repo(zMeta__ *zpMeta_, _i zSd) {
    _i zErrNo;
    if (0 == (zErrNo = zLocalOps_.proj_init(zpMeta_->p_data))) {
        zNetUtils_.sendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    }

    return zErrNo;
}


/*
 * 全量刷新：只刷新版本号列表
 * 需要继承下层已存在的缓存
 */
static _i
zrefresh_cache(zMeta__ *zpMeta_) {
//    _i zCnter[2];
//    struct iovec zOldVec_[zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.VecSiz];
//    zRefData__ zOldRefData_[zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.VecSiz];
//
//    for (zCnter[0] = 0; zCnter[0] < zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.VecSiz; zCnter[0]++) {
//        zOldVec_[zCnter[0]].iov_base = zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_Vec_[zCnter[0]].iov_base;
//        zOldVec_[zCnter[0]].iov_len = zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_Vec_[zCnter[0]].iov_len;
//        zOldRefData_[zCnter[0]].p_data  = zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_RefData_[zCnter[0]].p_data;
//        zOldRefData_[zCnter[0]].p_SubVecWrap_ = zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_RefData_[zCnter[0]].p_SubVecWrap_;
//    }

    zLocalOps_.get_revs(zpMeta_);  // 复用了 zops_route 函数传下来的 Meta__ 结构体(栈内存)

//    zCnter[1] = zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.VecSiz;
//    if (zCnter[1] > zCnter[0]) {
//        for (zCnter[0]--, zCnter[1]--; zCnter[0] >= 0; zCnter[0]--, zCnter[1]--) {
//            if (NULL == zOldRefData_[zCnter[0]].p_SubVecWrap_) { continue; }
//            if (NULL == zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_RefData_[zCnter[1]].p_SubVecWrap_) { break; }  // 若新内容为空，说明已经无法一一对应，后续内容无需再比较
//            if (0 == (strcmp(zOldRefData_[zCnter[0]].p_data, zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_RefData_[zCnter[1]].p_data))) {
//                zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_Vec_[zCnter[1]].iov_base = zOldVec_[zCnter[0]].iov_base;
//                zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_Vec_[zCnter[1]].iov_len = zOldVec_[zCnter[0]].iov_len;
//                zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_.p_RefData_[zCnter[1]].p_SubVecWrap_ = zOldRefData_[zCnter[0]].p_SubVecWrap_;
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

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->SortedCommitVecWrap_);
        /*
         * 如果该项目被标记为被动拉取模式（相对的是主动推送模式），则：
         *     若距离最近一次 “git pull“ 的时间间隔超过 10 秒，尝试拉取远程代码
         *     放在取得读写锁之后执行，防止与布署过程中的同类运作冲突
         *     取到锁，则拉取；否则跳过此步，直接打印列表
         *     打印布署记录时不需要执行
         */
        if (10 < (time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->LastPullTime)) {
            if ((0 == zpGlobRepo_[zpMeta_->RepoId]->SelfPushMark)
                    && (0 == pthread_mutex_trylock( &(zpGlobRepo_[zpMeta_->RepoId]->PullLock) ))) {

                system(zpGlobRepo_[zpMeta_->RepoId]->p_PullCmd);  /* 不能多线程，因为多个 git pull 会产生文件锁竞争 */
                zpGlobRepo_[zpMeta_->RepoId]->LastPullTime = time(NULL); /* 以取完远程代码的时间重新赋值 */

                zGitRevWalk__ *zpRevWalker;
                char zCommonBuf[64] = {'\0'};
                sprintf(zCommonBuf, "refs/heads/server%d", zpMeta_->RepoId);
                if (NULL != (zpRevWalker = zLibGit_.generate_revwalker(zpGlobRepo_[zpMeta_->RepoId]->p_GitRepoHandler, zCommonBuf, 0))) {
                    zLibGit_.get_one_commitsig_and_timestamp(zCommonBuf, zpGlobRepo_[zpMeta_->RepoId]->p_GitRepoHandler, zpRevWalker);
                    zLibGit_.destroy_revwalker(zpRevWalker);
                }
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->PullLock) );

                if ((NULL == zpGlobRepo_[zpMeta_->RepoId]->CommitRefData_[0].p_data)
                        || (0 != strncmp(zCommonBuf, zpGlobRepo_[zpMeta_->RepoId]->CommitRefData_[0].p_data, 40))) {
                    zpMeta_->DataType = zIsCommitDataType;

                    /* 此处进行换锁：读锁与写锁进行两次互换 */
                    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
                    if (0 != pthread_rwlock_trywrlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
                        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };

                    zrefresh_cache(zpMeta_);

                    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
                    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
                        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };
                }
            }
        }
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpSortedTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->SortedDpVecWrap_);
    } else {
        pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
        return -10;
    }

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrap_->VecSiz) {
        if (0 < zNetUtils_.sendmsg(zSd, zpSortedTopVecWrap_->p_Vec_, zpSortedTopVecWrap_->VecSiz, 0, NULL)) {
            zNetUtils_.sendto(zSd, "]", zBytes(1), 0, NULL);  // 二维json结束符
        } else {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
            return -70;
        }
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
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
    if (zRepoDamaged == zpGlobRepo_[zpMeta_->RepoId]->RepoState) {
        zpMeta_->p_data = "====上一次布署失败，请重试布署====";
        return -13;
    }

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
        zpMeta_->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
        zpMeta_->DataType = zIsDpDataType;
    } else {
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    }

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)) {
        if ((void *) -1 == zLocalOps_.get_diff_files(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
            zpMeta_->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));

            if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrap_.VecSiz = 0;
    zSendVecWrap_.p_Vec_ = zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->p_Vec_;
    zSplitCnt = (zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrap_.VecSiz = (zpTopVecWrap_->p_RefData_[zpMeta_->CommitId].p_SubVecWrap_->VecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrap_.VecSiz = zSendUnitSiz;
        }

        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_Vec_, zSendVecWrap_.VecSiz, 0, NULL);
        zSendVecWrap_.p_Vec_ += zSendVecWrap_.VecSiz;
    }
    zNetUtils_.sendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
    return 0;
}


/*
 * 11：显示差异文件内容
 */
static _i
zprint_diff_content(zMeta__ *zpMeta_, _i zSd) {
    zVecWrap__ *zpTopVecWrap_, zSendVecWrap_;
    _i zSplitCnt;

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
        zpMeta_->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
        zpMeta_->DataType = zIsDpDataType;
    } else {
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock))) {
        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)) {
        if ((void *) -1 == zLocalOps_.get_diff_files(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
            zpMeta_->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrap_(zpTopVecWrap_, zpMeta_->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));

            if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zCheck_FileId();  // 宏内部会解锁
    if (NULL == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)) {
        if ((void *) -1 == zLocalOps_.get_diff_contents(zpMeta_)) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));

            if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrap_.VecSiz = 0;
    zSendVecWrap_.p_Vec_ = zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->p_Vec_;
    zSplitCnt = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrap_.VecSiz = (zGet_OneFileVecWrap_(zpTopVecWrap_, zpMeta_->CommitId, zpMeta_->FileId)->VecSiz - 1) % zSendUnitSiz + 1;
        }
        else {
            zSendVecWrap_.VecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zNetUtils_.sendmsg(zSd, zSendVecWrap_.p_Vec_, zSendVecWrap_.VecSiz, 0, NULL);
        zSendVecWrap_.p_Vec_ += zSendVecWrap_.VecSiz;
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));
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
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9, zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,\
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9, zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,\
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9, zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,\
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,\
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,\
            zpMeta_->RepoId,\
\
            zNetSrv_.p_IpAddr, zNetSrv_.p_port,\
            zpMeta_->RepoId, zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath\
            );\
} while(0)


static _i
zupdate_ip_db_all(zMeta__ *zpMeta_, char *zpCommonBuf, zRegRes__ **zppRegRes_Out) {
    zDpRes__ *zpOldDpResList_, *zpTmpDpRes_, *zpOldDpResHash_[zDpHashSiz];

    zRegInit__ zRegInit_[1];
    zRegRes__ *zpRegRes_, zRegRes_[1] = {{.RepoId = zpMeta_->RepoId}};  // 使用项目内存池
    zpRegRes_ = zRegRes_;

    zPosixReg_.compile(zRegInit_ , "([0-9]{1,3}\\.){3}[0-9]{1,3}");
    zPosixReg_.match(zRegRes_, zRegInit_, zpMeta_->p_data);
    zPosixReg_.free_meta(zRegInit_);
    *zppRegRes_Out = zpRegRes_;

    if (strtol(zpMeta_->p_ExtraData, NULL, 10) != zRegRes_->cnt) { return -28; }

    if (zForecastedHostNum < zRegRes_->cnt) {
        /* 若指定的目标主机数量大于预测的主机数量，则另行分配内存 */
        /* 加空格最长16字节，如："123.123.123.123 " */
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_ = zLocalOps_.alloc(zpMeta_->RepoId, zRegRes_->cnt * sizeof(zDpCcur__));
    } else {
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_ = zpGlobRepo_[zpMeta_->RepoId]->DpCcur_;
    }

    /* 暂留旧数据 */
    zpOldDpResList_ = zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_;
    memcpy(zpOldDpResHash_, zpGlobRepo_[zpMeta_->RepoId]->p_DpResHash_, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配
     * 分配清零的空间，用于重置状态及检查重复 IP
     */
    zMem_C_Alloc(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_, zDpRes__, zRegRes_->cnt);

    /* 重置各项状态 */
    zpGlobRepo_[zpMeta_->RepoId]->TotalHost = zRegRes_->cnt;
    zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask = zpGlobRepo_[zpMeta_->RepoId]->TotalHost;
    //zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt = 0;
    zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt = 0;
    zpGlobRepo_[zpMeta_->RepoId]->ResType[0] = 0;
    zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp = time(NULL);
    memset(zpGlobRepo_[zpMeta_->RepoId]->p_DpResHash_, 0, zDpHashSiz * sizeof(zDpRes__ *));  /* Clear hash buf before reuse it!!! */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
        zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].InitState = 0;
    }

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zConfig_Dp_Host_Ssh_Cmd(zpCommonBuf);

    for (_ui zCnter = 0; zCnter < zRegRes_->cnt; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr) {
            strcpy(zpMeta_->p_data, zRegRes_->p_rets[zCnter]);
            zpMeta_->p_ExtraData[0] = '\0';
            return -19;
        }

        /* 注：需要全量赋值，因为后续的布署会直接复用；否则会造成只布署新加入的主机及内存访问错误 */
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_ = NULL;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].RepoId = zpMeta_->RepoId;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_HostIpStrAddr = zRegRes_->p_rets[zCnter];
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_Cmd = zpCommonBuf;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_CcurLock = &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_CcurCond = &zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_TaskCnt = &zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt;

        /* 线性链表斌值；转换字符串点分格式 IPv4 为 _ui 型 */
        zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr = zNetUtils_.to_bin(zRegRes_->p_rets[zCnter]);
        zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].InitState = 0;
        zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].p_next = NULL;

        /* 更新HASH */
        zpTmpDpRes_ = zpGlobRepo_[zpMeta_->RepoId]->p_DpResHash_[(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr) % zDpHashSiz];
        if (NULL == zpTmpDpRes_) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepo_[zpMeta_->RepoId]->p_DpResHash_[(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr) % zDpHashSiz]
                = &(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter]);
        } else {
            while (NULL != zpTmpDpRes_->p_next) { zpTmpDpRes_ = zpTmpDpRes_->p_next; }
            zpTmpDpRes_->p_next = &(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter]);
        }

        zpTmpDpRes_ = zpOldDpResHash_[zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr % zDpHashSiz];
        while (NULL != zpTmpDpRes_) {
            /* 若 IPv4 address 已存在，则跳过初始化远程主机的环节 */
            if (zpTmpDpRes_->ClientAddr == zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr) {
                /* 先前已被初始化过的主机，状态置 1，防止后续收集结果时误报失败 */
                zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].InitState = 1;
                /* 从总任务数中去除已经初始化的主机数 */
                zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask--;
                goto zExistMark;
            }
            zpTmpDpRes_ = zpTmpDpRes_->p_next;
        }

        /* 对新加入的目标机执行初始化动作 */
        zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter]));
zExistMark:;
    }

    /* 释放资源 */
    if (NULL != zpOldDpResList_) { free(zpOldDpResList_); }

    /* 等待所有 SSH 任务完成 */
    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
    while (zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask) {
        pthread_cond_wait(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond, &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
    }
    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);

    /* 检测执行结果，并返回失败列表 */
    if ((-1 == zpGlobRepo_[zpMeta_->RepoId]->ResType[0])
            || (zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask)) {
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _ui zFailHostCnt = 0;
        _i zOffSet = sprintf(zpMeta_->p_data, "无法连接的主机:");
        for (_ui zCnter = 0; (zOffSet < zpMeta_->DataLen) && (zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost); zCnter++) {
            if (1 != zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].InitState) {
                zNetUtils_.to_str(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMeta_->p_data + zOffSet, " %s", zIpStrAddrBuf);
                zFailHostCnt++;

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr = 0;
            }
        }

        /* 主机数超过 10 台，且失败率低于 10% 返回成功，否则返回失败 */
        if ((10 < zpGlobRepo_[zpMeta_->RepoId]->TotalHost) && ( zFailHostCnt < zpGlobRepo_[zpMeta_->RepoId]->TotalHost / 10)) { return 0; }
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
    time_t zRemoteHostInitTimeSpent;

    if (zIsCommitDataType == zpMeta_->DataType) {
        zpTopVecWrap_= &(zpGlobRepo_[zpMeta_->RepoId]->CommitVecWrap_);
    } else if (zIsDpDataType == zpMeta_->DataType) {
        zpTopVecWrap_ = &(zpGlobRepo_[zpMeta_->RepoId]->DpVecWrap_);
    } else {
        zpMeta_->p_data = "====[JSON: DataType] 字段指定的数据类型无效====";
        zpMeta_->p_ExtraData[0] = '\0';
        zErrNo = -10;
        goto zEndMark;
    }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepo_[zpMeta_->RepoId]->DpLock) {
        zpMeta_->p_data = "====代码库被锁定，不允许布署====";
        zpMeta_->p_ExtraData[0] = '\0';
        zErrNo = -6;
        goto zEndMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepo_[zpMeta_->RepoId]->CacheId != zpMeta_->CacheId) {
        zpMeta_->p_data = "====已产生新的布署记录，请刷新页面====";
        zpMeta_->p_ExtraData[0] = '\0';
        zpMeta_->CacheId = zpGlobRepo_[zpMeta_->RepoId]->CacheId;
        zErrNo = -8;
        goto zEndMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zpMeta_->CommitId)
            || ((zCacheSiz - 1) < zpMeta_->CommitId)
            || (NULL == zpTopVecWrap_->p_RefData_[zpMeta_->CommitId].p_data)) {
        zpMeta_->p_data = "====指定的版本号无效====";
        zpMeta_->p_ExtraData[0] = '\0';
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
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath,  // 中控机上的代码库路径
            zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId),  // SHA1 commit sig
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath,
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath + 9,  // 目标机上的代码库路径(即：去掉最前面的 "/home/git" 合计 9 个字符)
            zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath
            );

    /* 调用 git 命令执行布署前的环境准备；同时用于测算中控机本机所有动作耗时，用作布署超时基数 */
    zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp = time(NULL);
    if (0 != WEXITSTATUS( system(zppCommonBuf[1]) )) {
        zErrNo = -15;
        goto zEndMark;
    }

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMeta_->p_data[0]) {
        if (0 > (zErrNo = zupdate_ip_db_all(zpMeta_, zppCommonBuf[0], zppHostStrAddrRegRes_Out))) {
            goto zEndMark;
        }
        zRemoteHostInitTimeSpent = time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp;
    }

    /* 检查部署目标主机集合是否存在 */
    if (0 == zpGlobRepo_[zpMeta_->RepoId]->TotalHost) {
        zpMeta_->p_data = "====指定的目标主机 IP 列表无效====";
        zpMeta_->p_ExtraData[0] = '\0';
        zErrNo = -26;
        goto zEndMark;
    }

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数；另复制一份供失败重试之用 */
    strncpy(zpGlobRepo_[zpMeta_->RepoId]->zDpingSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId), zBytes(40));
    strncpy(zpMeta_->p_ExtraData, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId), zBytes(40));

    /* 重置布署相关状态 */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
        zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].DpState = 0;
    }
    zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask = zpGlobRepo_[zpMeta_->RepoId]->TotalHost;
    zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt = 0;
    zpGlobRepo_[zpMeta_->RepoId]->ResType[1] = 0;
    //zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt = 0;
    zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit = 0;

    /* 基于 libgit2 实现 zgit_push(...) 函数，在系统负载上限之下并发布署；参数与之前的SSH动作完全相同，此处无需再次赋值 */
    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
        zThreadPool_.add(zgit_push_ccur, &(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter]));
    }

    /* 测算超时时间 */
    if (('\0' == zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig[0])
            || (0 == strcmp(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zpGlobRepo_[zpMeta_->RepoId]->zDpingSig))) {
        /* 无法测算时: 默认超时时间 ==  60s + 中控机本地所有动作耗时 */
        zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit = 60
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepo_[zpMeta_->RepoId]->TotalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp);
    } else {
        /*
         * [基数 = 30s + 中控机本地所有动作耗时之和] + [远程主机初始化时间 + 中控机与目标机上计算SHA1 checksum 的时间] + [网络数据总量每增加 ?M，超时上限递增 1 秒]
         * [网络数据总量 == 主机数 X 每台的数据量]
         * [单位：秒]
         */
        zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit = 30
            + zpGlobRepo_[zpMeta_->RepoId]->TotalHost / 10  /* 临时算式：每增加一台目标机，递增 0.1 秒 */
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepo_[zpMeta_->RepoId]->TotalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp);  /* 本地动作耗时 */
    }

    /* 最长 10 分钟 */
    if (600 < zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) { zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit = 600; }

    /* DEBUG */
    fprintf(stderr, "\n\033[31;01m[ DEBUG ] 布署时间测算结果：%zd 秒\033[00m\n\n", zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit);

    /* 耗时预测超过 90 秒的情况，通知前端不必阻塞等待，可异步于布署列表中查询布署结果 */
    if (90 < zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) {
        _i zSendLen = sprintf(zppCommonBuf[0], "[{\"OpsId\":-14,\"data\":\"本次布署时间最长可达 %zd 秒，请稍后查看布署结果\"}]", zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit);
        zNetUtils_.sendto(zSd, zppCommonBuf[0], zSendLen, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }

    /* 等待所有 git push 任务完成或达到超时时间 */
    struct timespec zAbsoluteTimeStamp_;
    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);

    if (zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask) {
        zAbsoluteTimeStamp_.tv_sec = zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit + time(NULL) + 1;
        zAbsoluteTimeStamp_.tv_nsec = 0;
        pthread_cond_timedwait(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond, &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock, &zAbsoluteTimeStamp_);
    }

    /* 若 8 秒内收到过 keepalive 消息，则延长超时时间 15 秒*/
    while (8 > (time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->DpKeepAliveStamp)) {
        if (zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask) {
            zAbsoluteTimeStamp_.tv_sec = 15 + time(NULL) + 1;
            zAbsoluteTimeStamp_.tv_nsec = 0;
            pthread_cond_timedwait(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond, &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock, &zAbsoluteTimeStamp_);
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);

    /* 若收到错误，则可确认此次布署一定会失败，进入错误处理环节 */
    if (-1 == zpGlobRepo_[zpMeta_->RepoId]->ResType[1]) { goto zErrMark; }

    if (zpGlobRepo_[zpMeta_->RepoId]->TotalHost == zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt) {
        zErrNo = 0;
    } else if ( ((10 <= zpGlobRepo_[zpMeta_->RepoId]->TotalHost) && ((zpGlobRepo_[zpMeta_->RepoId]->TotalHost * 9 / 10) <= zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt))) {
        /*
         * 对于10 台及以上的目标机集群，达到 90％ 的主机状态得到确认即返回成功，未成功的部分，在下次新的版本布署之前，持续重试布署
         * 10 台以下，则须全部确认
         */
        zErrNo = -10000;
    } else {
zErrMark:
        /* 若为部分布署失败，代码库状态置为 "损坏" 状态；若为全部布署失败，则无需此步 */
        if (0 < zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt) {
            //zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig[0] = '\0';
            zpGlobRepo_[zpMeta_->RepoId]->RepoState = zRepoDamaged;
        }

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _i zOffSet = 0;
        for (_ui zCnter = 0; (zOffSet < zpMeta_->DataLen) && (zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost); zCnter++) {
            if (1 != zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].DpState) {
                zNetUtils_.to_str(zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMeta_->p_data + zOffSet, "([%s]%s)",
                        zIpStrAddrBuf,
                        '\0' == zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ErrMsg[0] ? "" : zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ErrMsg
                        );

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].ClientAddr = 0;
            }
        }
        zpMeta_->p_ExtraData = zpGlobRepo_[zpMeta_->RepoId]->zDpingSig;
        zErrNo = -12;
        goto zEndMark;
    }

    /* 若先前测算的布署耗时 <= 90s ，此处向前端返回布署成功消息 */
    if (90 >= zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) {
        zNetUtils_.sendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }
    zpGlobRepo_[zpMeta_->RepoId]->RepoState = zRepoGood;

    /* 更新最近一次布署的版本号到项目元信息中，复位代码库状态；若请求布署的版本号与最近一次布署的相同，则不必再重复生成缓存 */
    if (0 != strcmp(zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId), zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig)) {
        /* 更新最新一次布署版本号，并将本次布署信息写入日志 */
        strcpy(zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrap_, zpMeta_->CommitId));

        /* 换行符要写入，但'\0' 不能写入 */
        _i zLogStrLen = sprintf(zppCommonBuf[0], "%s_%zd\n", zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, time(NULL));
        if (zLogStrLen != write(zpGlobRepo_[zpMeta_->RepoId]->DpSigLogFd, zppCommonBuf[0], zLogStrLen)) {
            zPrint_Err(0, NULL, "日志写入失败： <_SHADOW/log/deploy/meta> !");
            //exit(1);
        }

        /* deploy success, create a new "CURRENT" branch */
        sprintf(zppCommonBuf[0], "cd %s; git branch -f `git log CURRENT -1 --format=%%H`; git branch -f CURRENT", zpGlobRepo_[zpMeta_->RepoId]->p_RepoPath);
        if (0 != WEXITSTATUS( system(zppCommonBuf[0])) ) {
            zPrint_Err(0, NULL, "\"CURRENT\" branch refresh failed");
        }

        /* 若已确认全部成功，重置内存池状态 */
        if (0 == zErrNo) { zReset_Mem_Pool_State(zpMeta_->RepoId); }

        /* 如下部分：更新全局缓存 */
        zpGlobRepo_[zpMeta_->RepoId]->CacheId = time(NULL);

        zMeta__ zSubMeta_;
        zSubMeta_.RepoId = zpMeta_->RepoId;

        zSubMeta_.DataType = zIsCommitDataType;
        zLocalOps_.get_revs(&zSubMeta_);
        zSubMeta_.DataType = zIsDpDataType;
        zLocalOps_.get_revs(&zSubMeta_);
    }

zEndMark:
    return zErrNo;
}


/*
 * 外壳函数
 * 13：新加入的主机请求布署自身：不拿锁、不刷系统IP列表、不刷新缓存
 */
static _i
zself_deploy(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__))) {
    /* 若目标机上已是最新代码，则无需布署 */
    if (0 != strncmp(zpMeta_->p_ExtraData, zpGlobRepo_[zpMeta_->RepoId]->zLastDpSig, 40)) {
        zDpCcur__ *zpDpSelf_ = zLocalOps_.alloc(zpMeta_->RepoId, sizeof(zDpCcur__));
        zpDpSelf_->RepoId = zpMeta_->RepoId;
        zpDpSelf_->p_HostIpStrAddr = zpMeta_->p_data;
        zpDpSelf_->p_CcurLock = NULL;  // 标记无需发送通知给调用者的条件变量

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

    if (0 != pthread_rwlock_trywrlock( &(zpGlobRepo_[zpMeta_->RepoId]->RwLock) )) {
        if (0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
            sprintf(zpMeta_->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMeta_->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit / 60.0);
        }
        return -11;
    }

    _i zErrNo, zCommonBufLen;
    char *zppCommonBuf[2];
    zRegRes__ *zpHostStrAddrRegRes_;

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个Buf*/
    zCommonBufLen = 2048 + 10 * zpGlobRepo_[zpMeta_->RepoId]->RepoPathLen + zpMeta_->DataLen;
    zppCommonBuf[0] = zLocalOps_.alloc(zpMeta_->RepoId, 2 * zCommonBufLen);
    zppCommonBuf[1] = zppCommonBuf[0] + zCommonBufLen;

    pthread_mutex_lock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
    zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock = 1;  // 置为 1
    pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
    pthread_cond_signal(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond);  // 通知旧的版本重试动作中止

    pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );

    /* 确认全部成功或确认布署失败这两种情况，直接返回，否则进入不间断重试模式，直到新的布署请求到来 */
    if (-10000 != (zErrNo = zdeploy(zpMeta_, zSd, zppCommonBuf, &zpHostStrAddrRegRes_))) {
        zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->RwLock) );
        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
        return zErrNo;
    } else {
        zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->RwLock) );
        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );

        /* 在没有新的布署动作之前，持续尝试布署失败的目标机 */
        while(1) {
            /* 等待剩余的所有主机状态都得到确认，不必在锁内执行 */
            for (_l zTimeCnter = 0; zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit > zTimeCnter; zTimeCnter++) {
                if ((0 != zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock)  /* 检测是否有新的布署请求 */
                        || ((zpGlobRepo_[zpMeta_->RepoId]->TotalHost == zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt) && (-1 != zpGlobRepo_[zpMeta_->RepoId]->ResType[1]))) {
                    return 0;
                }

                zLocalUtils_.sleep(0.1);
            }

            pthread_mutex_lock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
            if (0 !=  strncmp(zpGlobRepo_[zpMeta_->RepoId]->zDpingSig, zpMeta_->p_ExtraData, 40)) {
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 重置时间戳，并生成 SSH 指令 */
            zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp = time(NULL);
            zConfig_Dp_Host_Ssh_Cmd(zppCommonBuf[0]);

            /* 预置值，对失败的目标机重新初始化 */
            zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask = zpGlobRepo_[zpMeta_->RepoId]->TotalHost;
            zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt = 0;

            for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                    pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                    return 0;
                }

                if (1 != zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].DpState) {
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_ = NULL;
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].RepoId = zpMeta_->RepoId;
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_HostIpStrAddr = zpHostStrAddrRegRes_->p_rets[zCnter];
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_Cmd = zppCommonBuf[0];
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_CcurLock = &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock;
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_CcurCond = &zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond;
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].p_TaskCnt = &zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt;

                    zThreadPool_.add(zssh_ccur_simple_init_host, &(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter]));

                    /* 调整目标机初始化状态数据（布署状态数据不调整！）*/
                    zpGlobRepo_[zpMeta_->RepoId]->p_DpResList_[zCnter].InitState = 0;
                } else {
                    zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask -= 1;
                    zpHostStrAddrRegRes_->p_rets[zCnter] = NULL;  // 去掉已成功的 IP 地址，只保留失败的部分
                }
            }

            /* 等待所有 SSH 任务完成，此处不再检查执行结果 */
            pthread_mutex_lock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
            while ((0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) && (zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask)) {
                pthread_cond_wait(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond, &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
            }
            pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);

            /* 检测是否有新的布署请求 */
            if (0 != zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
                    /* 清理旧的未完工的线程，无需持锁 */
                    if (NULL != zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_) {
                        pthread_cancel(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_->SelfTid);
                    }
                }

                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 预置值，对失败的目标机重新布署，任务总量与初始化目标机一致，此处无须再计算 */
            zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt = 0;
            zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp = time(NULL);

            /* 在执行动作之前再检查一次布署结果，防止重新初始化的时间里已全部返回成功状态，从而造成无用的布署重试 */
            if (zpGlobRepo_[zpMeta_->RepoId]->TotalHost > zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt) {
                /* 对失败的目标主机重试布署 */
                for (_ui zCnter = 0; zCnter < zpHostStrAddrRegRes_->cnt; zCnter++) {
                    /* 检测是否有新的布署请求 */
                    if (0 != zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                        pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                        return 0;
                    }

                    /* 结构体各成员参数与目标机初始化时一致，无需修改，直接复用即可 */
                    if (NULL != zpHostStrAddrRegRes_->p_rets[zCnter]) {
                        /* when memory load >= 80%，waiting ... */
                        pthread_mutex_lock(&zGlobCommonLock);
                        while (80 <= zGlobMemLoad) {
                            pthread_cond_wait(&zSysLoadCond, &zGlobCommonLock);
                        }
                        pthread_mutex_unlock(&zGlobCommonLock);

                        zThreadPool_.add(zgit_push_ccur, &(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter]));
                    }
                }

                /* 等待所有 git push 任务完成；重试时不必设置超时 */
                pthread_mutex_lock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
                while ((0 == zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) && (zpGlobRepo_[zpMeta_->RepoId]->DpTaskFinCnt < zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask)) {
                    pthread_cond_wait(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncCond, &zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);
                }
                pthread_mutex_unlock(&zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock);

                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepo_[zpMeta_->RepoId]->zWhoGetWrLock) {
                    for (_ui zCnter = 0; zCnter < zpGlobRepo_[zpMeta_->RepoId]->TotalHost; zCnter++) {
                        /* 清理旧的未完工的线程，无需持锁 */
                        if (NULL != zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_) {
                            pthread_cancel(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_[zCnter].zpThreadSource_->SelfTid);
                        }
                    }

                    pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                    return 0;
                }
            } else {
                pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 超时上限延长为 2 倍 */
            zpGlobRepo_[zpMeta_->RepoId]->DpTimeWaitLimit *= 2;

            pthread_mutex_unlock( &(zpGlobRepo_[zpMeta_->RepoId]->DpRetryLock) );
        }
    }
}


/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
static _i
zstate_confirm(zMeta__ *zpMeta_, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmp_ = zpGlobRepo_[zpMeta_->RepoId]->p_DpResHash_[zpMeta_->HostId % zDpHashSiz];

    for (; zpTmp_ != NULL; zpTmp_ = zpTmp_->p_next) {  // 遍历
        if (zpTmp_->ClientAddr == zpMeta_->HostId) {
            pthread_mutex_lock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));

            char *zpLogStrId;
            /* 'B' 标识布署状态回复，'C' 目标机的 keep alive 消息 */
            if ('B' == zpMeta_->p_ExtraData[0]) {
                if (0 != zpTmp_->DpState) {
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                    return 0;
                }

                if (0 != strncmp(zpGlobRepo_[zpMeta_->RepoId]->zDpingSig, zpMeta_->p_data, zBytes(40))) {
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                    return -101;  // 返回负数，用于打印日志
                }

                if ('+' == zpMeta_->p_ExtraData[1]) {  // 负号 '-' 表示是异常返回，正号 '+' 表示是成功返回
                    zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt++;
                    zpTmp_->DpState = 1;

                    zpLogStrId = zpGlobRepo_[zpMeta_->RepoId]->zDpingSig;
                } else if ('-' == zpMeta_->p_ExtraData[1]) {
                    zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt = zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask;  // 发生错误，计数打满，用于通知结束布署等待状态
                    zpTmp_->DpState = -1;
                    zpGlobRepo_[zpMeta_->RepoId]->ResType[1] = -1;

                    snprintf(zpTmp_->ErrMsg, 256, "%s", zpMeta_->p_data + 40);  // 所有的状态回复前40个字节均是 git SHA1sig
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                    pthread_cond_signal(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_->p_CcurCond);
                    return -102;  // 返回负数，用于打印日志
                } else {
                    pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                    return -103;  // 未知的返回内容
                }
            } else if ('C' == zpMeta_->p_ExtraData[0]) {
                zpGlobRepo_[zpMeta_->RepoId]->DpKeepAliveStamp = time(NULL);
                pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                return 0;
            } else {
                pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
                return -103;  // 未知的返回内容
            }

            /* 调试功能：布署耗时统计，必须在锁内执行 */
            char zIpStrAddr[INET_ADDRSTRLEN], zTimeCntBuf[128];
            zNetUtils_.to_str(zpMeta_->HostId, zIpStrAddr);
            _i zWrLen = sprintf(zTimeCntBuf, "[%s] [%s]\t\t[TimeSpent(s): %ld]\n",
                    zpLogStrId,
                    zIpStrAddr,
                    time(NULL) - zpGlobRepo_[zpMeta_->RepoId]->DpBaseTimeStamp);
            write(zpGlobRepo_[zpMeta_->RepoId]->DpTimeSpentLogFd, zTimeCntBuf, zWrLen);

            pthread_mutex_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->DpSyncLock));
            if (zpGlobRepo_[zpMeta_->RepoId]->DpReplyCnt == zpGlobRepo_[zpMeta_->RepoId]->DpTotalTask) {
                pthread_cond_signal(zpGlobRepo_[zpMeta_->RepoId]->p_DpCcur_->p_CcurCond);
            }
            return 0;
        }
    }

    return 0;
}


/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
static _i
zlock_repo(zMeta__ *zpMeta_, _i zSd) {
    pthread_rwlock_wrlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));

    if (2 == zpMeta_->OpsId) {
        zpGlobRepo_[zpMeta_->RepoId]->DpLock = zDpLocked;
    } else {
        zpGlobRepo_[zpMeta_->RepoId]->DpLock = zDpUnLock;
    }

    pthread_rwlock_unlock(&(zpGlobRepo_[zpMeta_->RepoId]->RwLock));

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


/*
 * 网络服务路由函数
 */
static void *
zops_route(void *zpParam) {
    _i zSd = ((zSockAcceptParam__ *) zpParam)->ConnSd;
    _i zErrNo;
    char zJsonBuf[zGlobCommonBufSiz] = {'\0'};
    char *zpJsonBuf = zJsonBuf;

    /* 必须清零，以防脏栈数据导致问题 */
    zMeta__ zMeta_;
    memset(&zMeta_, 0, sizeof(zMeta__));

    /* 若收到大体量数据，直接一次性扩展为1024倍的缓冲区，以简化逻辑 */
    if (zGlobCommonBufSiz == (zMeta_.DataLen = recv(zSd, zpJsonBuf, zGlobCommonBufSiz, 0))) {
        zMem_C_Alloc(zpJsonBuf, char, zGlobCommonBufSiz * 1024);  // 用清零的空间，保障正则匹配不出现混乱
        strcpy(zpJsonBuf, zJsonBuf);
        zMeta_.DataLen += recv(zSd, zpJsonBuf + zMeta_.DataLen, zGlobCommonBufSiz * 1024 - zMeta_.DataLen, 0);
    }

    if (zBytes(6) > zMeta_.DataLen) {
        close(zSd);
        zPrint_Err(errno, "zBytes(6) > recv(...)", NULL);
        return NULL;
    }

    /* .p_data 与 .p_ExtraData 成员空间 */
    zMeta_.DataLen += (zMeta_.DataLen > zGlobCommonBufSiz) ? zMeta_.DataLen : zGlobCommonBufSiz;
    zMeta_.ExtraDataLen = zGlobCommonBufSiz;
    char zDataBuf[zMeta_.DataLen], zExtraDataBuf[zMeta_.ExtraDataLen];
    memset(zDataBuf, 0, zMeta_.DataLen);
    memset(zExtraDataBuf, 0, zMeta_.ExtraDataLen);
    zMeta_.p_data = zDataBuf;
    zMeta_.p_ExtraData = zExtraDataBuf;

    if (0 > (zErrNo = zconvert_json_str_to_struct(zpJsonBuf, &zMeta_))) {
        zMeta_.OpsId = zErrNo;
        goto zMarkCommonAction;
    }

    if (0 > zMeta_.OpsId || 16 <= zMeta_.OpsId || NULL == zRun_.ops[zMeta_.OpsId]) {
        zMeta_.OpsId = -1;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if ((1 != zMeta_.OpsId) && (5 != zMeta_.OpsId)
            && ((zGlobMaxRepoId < zMeta_.RepoId) || (0 >= zMeta_.RepoId) || (NULL == zpGlobRepo_[zMeta_.RepoId]))) {
        zMeta_.OpsId = -2;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if (0 > (zErrNo = zRun_.ops[zMeta_.OpsId](&zMeta_, zSd))) {
        zMeta_.OpsId = zErrNo;  // 此时代表错误码
zMarkCommonAction:
        zconvert_struct_to_json_str(zpJsonBuf, &zMeta_);
        zpJsonBuf[0] = '[';
        zNetUtils_.sendto(zSd, zpJsonBuf, strlen(zpJsonBuf), 0, NULL);
        zNetUtils_.sendto(zSd, "]", zBytes(1), 0, NULL);

        fprintf(stderr, "\n\033[31;01m[ DEBUG ] \033[00m%s", zpJsonBuf);  // 错误信息，打印出一份，防止客户端socket已关闭时，信息丢失
    }

    close(zSd);
    if (zpJsonBuf != &(zJsonBuf[0])) { free(zpJsonBuf); }
    return NULL;
}
