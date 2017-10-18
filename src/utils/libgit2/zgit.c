#ifndef _Z
    #include "../../zmain.c"
#endif

//#include "git2.h"

/* 代码库新建或载入时调用一次即可；zpLocallRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式 */
git_repository *
zgit_env_init(char *zpLocalRepoAddr) {
    git_repository *zpRepoHandler;

    if (0 > git_libgit2_init()) {  // 此处要使用 0 > ... 作为条件
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoHandler = NULL;
    }

    if (0 != git_repository_open(&zpRepoHandler, zpLocalRepoAddr)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoHandler = NULL;
    }

    return zpRepoHandler;
}

/* 通常无须调用，随布署系统运行一直处于使用状态 */
void
zgit_env_clean(git_repository *zpRepoCredHandler) {
    git_cred_free(zpGlobCred);  // 释放全局 SSH 证书
    git_repository_free(zpRepoCredHandler);
    git_libgit2_shutdown();
}

/* SSH 身份认证 */
_i
zgit_cred_acquire_cb(git_cred **zppResOut, const char *zpUrl_Unused, const char * zpUsernameFromUrl_Unused, unsigned int zpAllowedTypes_Unused, void * zPayload_Unusued) {
    /* 仅用作消除编译时的警告信息 */
    zpUrl_Unused = NULL;
    zpUsernameFromUrl_Unused = NULL;
    zpAllowedTypes_Unused = 0;
    zPayload_Unusued = NULL;

    *zppResOut = zpGlobCred;

    return 0;
}

/*
 * [ git push ]
 * zpRemoteRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式
 */
_i
zgit_push(git_repository *zRepo, char *zpRemoteRepoAddr, char **zppRefs) {
    /* get the remote */
    git_remote* zRemote = NULL;
    //git_remote_lookup( &zRemote, zRepo, "origin" );  // 使用已命名分支时，调用此函数
    if (0 != git_remote_create_anonymous(&zRemote, zRepo, zpRemoteRepoAddr)) {  // 直接使用 URL 时调用此函数
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    };

    /* connect to remote */
    git_remote_callbacks zConnOpts = GIT_REMOTE_CALLBACKS_INIT;
    zConnOpts.credentials = zgit_cred_acquire_cb;  // 指定身份认证所用的回调函数

    if (0 != git_remote_connect(zRemote, GIT_DIRECTION_PUSH, &zConnOpts, NULL, NULL)) {
        git_remote_free(zRemote);
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    /* add [a] push refspec[s] */
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = zppRefs;
    zGitRefsArray.count = 2;

    git_push_options zPushOpts = GIT_PUSH_OPTIONS_INIT;
    zPushOpts.pb_parallelism = 1;  // 限定单个 push 动作可以使用的线程数，若指定为 0，则将与本地的CPU数量相同

    /* do the push */
    if (0 != git_remote_upload(zRemote, &zGitRefsArray, &zPushOpts)) {
        git_remote_disconnect(zRemote);
        git_remote_free(zRemote);
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    git_remote_disconnect(zRemote);
    git_remote_free(zRemote);
    return 0;
}

#define zNative_Fail_Confirm() do {\
    _ui ____zHostId = zconvert_ip_str_to_bin(zpDpCcurIf->p_HostIpStrAddr);\
    zDpResInfo *____zpTmpIf = zpGlobRepoIf[zpDpCcurIf->RepoId]->p_DpResHashIf[____zHostId % zDpHashSiz];\
    for (; NULL != ____zpTmpIf; ____zpTmpIf = ____zpTmpIf->p_next) {\
        if (____zHostId == ____zpTmpIf->ClientAddr) {\
            pthread_mutex_lock(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock));\
            if (0 == ____zpTmpIf->DpState) {\
                ____zpTmpIf->DpState = -1;\
                zpGlobRepoIf[zpDpCcurIf->RepoId]->DpReplyCnt++;\
                zpGlobRepoIf[zpDpCcurIf->RepoId]->ResType[1] = -1;\
            }\
            pthread_mutex_unlock(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock));\
            break;\
        }\
    }\
} while(0)

void *
zgit_push_ccur(void *zpIf) {
    zDpCcurInfo *zpDpCcurIf = (zDpCcurInfo *) zpIf;

    char zRemoteRepoAddrBuf[64 + zpGlobRepoIf[zpDpCcurIf->RepoId]->RepoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_wait(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpTraficControl)) );

    /* when memory load > 80%，waiting ... */
    pthread_mutex_lock(&zGlobCommonLock);
    while (80 < zGlobMemLoad) {
        pthread_cond_wait(&zSysLoadCond, &zGlobCommonLock);
    }
    pthread_mutex_unlock(&zGlobCommonLock);

    /* generate remote URL */
    sprintf(zRemoteRepoAddrBuf, "git@%s:%s/.git", zpDpCcurIf->p_HostIpStrAddr, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9);

    /* push TWO branchs together */
    sprintf(zpGitRefs[0], "refs/heads/master:refs/heads/server%d", zpDpCcurIf->RepoId);
    sprintf(zpGitRefs[1], "refs/heads/master_SHADOW:refs/heads/server%d_SHADOW", zpDpCcurIf->RepoId);
    if (0 != zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs)) {

        /* if directly push failed, then try push to a new remote branch: NEWserver... */
        sprintf(zpGitRefs[0], "refs/heads/master:refs/heads/NEWserver%d", zpDpCcurIf->RepoId);
        sprintf(zpGitRefs[1], "refs/heads/master_SHADOW:refs/heads/NEWserver%d_SHADOW", zpDpCcurIf->RepoId);
        if (0 !=zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs)) {

            /* if failed again, then try delete the remote branch: NEWserver... */
            char zCmdBuf[1024 + 7 * zpGlobRepoIf[zpDpCcurIf->RepoId]->RepoPathLen];
            sprintf(zCmdBuf, "cd %s; git branch -D NEWserver%d; git branch -D NEWserver%d_SHADOW",
                    zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                    zpDpCcurIf->RepoId,
                    zpDpCcurIf->RepoId
                    );
            if (0 == zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zCmdBuf, &(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock))) {

                /* and, try push to NEWserver once more... */
                if (0 !=zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs)) {

                    /* if failed again, to do last retry, delete '.git', reinit the host */
                    sprintf(zCmdBuf,
                            "rm -f %s %s_SHADOW;"  // 若为软链则清除，是目录则保留
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

                            zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                            zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                            zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                            zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                            zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->RepoId,

                            zNetServIf.p_IpAddr, zNetServIf.p_port,
                            zpDpCcurIf->RepoId, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath
                            );
                    if (0 == zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zCmdBuf, &(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock))) {
                        if (0 !=zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs)) { zNative_Fail_Confirm(); }
                    } else {
                        zNative_Fail_Confirm();
                    }
                }
            } else {
                /* if ssh exec failed, just deal with error */
                zNative_Fail_Confirm();
            }
        }
    }

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpTraficControl)) );

    return NULL;
}
