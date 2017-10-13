#ifndef _Z
    #include "../../zmain.c"
#endif

//#include "git2.h"

#define zGit_Check_Err_Return(zOps) do {\
    if (0 != zOps) {\
        git_remote_free(zRemote);\
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);\
        return -1;\
    }\
} while (0)

/* 代码库新建或载入时调用一次即可；zpLocallRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式 */
git_repository *
zgit_env_init(char *zpLocalRepoAddr) {
    git_repository *zpRepoMetaIf;

    if (0 > git_libgit2_init()) {  // 此处要使用 0 > ... 作为条件
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoMetaIf = NULL;
    }

    if (0 != git_repository_open(&zpRepoMetaIf, zpLocalRepoAddr)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoMetaIf = NULL;
    }

    return zpRepoMetaIf;
}

/* 通常无须调用，随布署系统运行一直处于使用状态 */
void
zgit_env_clean(git_repository *zpRepoIf) {
    git_repository_free(zpRepoIf);
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

    /* 固定为 git 用户权限 */
#ifdef _Z_BSD
    if (0 != git_cred_ssh_key_new(zppResOut, "git", "/usr/home/git/.ssh/id_rsa.pub", "/usr/home/git/.ssh/id_rsa", NULL)) {
#else
    if (0 != git_cred_ssh_key_new(zppResOut, "git", "/home/git/.ssh/id_rsa.pub", "/home/git/.ssh/id_rsa", NULL)) {
#endif
        if (NULL == giterr_last()) { fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n"); }
        else { fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message); }
        return -1;
    }

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
    git_remote_callbacks zConnOpts;  // = GIT_REMOTE_CALLBACKS_INIT;
    git_remote_init_callbacks(&zConnOpts, GIT_REMOTE_CALLBACKS_VERSION);
    zConnOpts.credentials = zgit_cred_acquire_cb;  // 指定身份认证所用的回调函数
    zGit_Check_Err_Return( git_remote_connect(zRemote, GIT_DIRECTION_PUSH, &zConnOpts, NULL, NULL) );

    /* add [a] push refspec[s] */
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = zppRefs;
    zGitRefsArray.count = 2;

    git_push_options zPushOpts;  // = GIT_PUSH_OPTIONS_INIT;
    git_push_init_options(&zPushOpts, GIT_PUSH_OPTIONS_VERSION);

    /* do the push */
    zGit_Check_Err_Return( git_remote_upload(zRemote, &zGitRefsArray, &zPushOpts) );

    return 0;
}

#define zNative_Fail_Confirm() do {\
    _ui ____zHostId = zconvert_ip_str_to_bin(zpGitPushIf->p_HostStrAddr);\
    zDpResInfo *____zpTmpIf = zppGlobRepoIf[zpGitPushIf->RepoId]->p_DpResHashIf[____zHostId % zDpHashSiz];\
    for (; ____zpTmpIf != NULL; ____zpTmpIf = ____zpTmpIf->p_next) {\
        if (____zHostId == ____zpTmpIf->ClientAddr) {\
            pthread_mutex_lock(&(zppGlobRepoIf[zpGitPushIf->RepoId]->ReplyCntLock));\
            if (0 == ____zpTmpIf->DpState) {\
                ____zpTmpIf->DpState = -1;\
                zppGlobRepoIf[zpGitPushIf->RepoId]->ReplyCnt[1]++;\
                zppGlobRepoIf[zpGitPushIf->RepoId]->ResType[1] = -1;\
            }\
            pthread_mutex_unlock(&(zppGlobRepoIf[zpGitPushIf->RepoId]->ReplyCntLock));\
            break;\
        }\
    }\
} while(0)

void *
zgit_push_ccur(void *zpIf) {
    zGitPushInfo *zpGitPushIf = (zGitPushInfo *) zpIf;

    char zRemoteRepoAddrBuf[64 + zppGlobRepoIf[zpGitPushIf->RepoId]->RepoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* generate remote URL */
    sprintf(zRemoteRepoAddrBuf, "git@%s:%s/.git", zpGitPushIf->p_HostStrAddr, zppGlobRepoIf[zpGitPushIf->RepoId]->p_RepoPath + 9);

    /* push TWO branchs together */
    sprintf(zpGitRefs[0], "refs/heads/master:refs/heads/server%d", zpGitPushIf->RepoId);
    sprintf(zpGitRefs[1], "refs/heads/master_SHADOW:refs/heads/server%d_SHADOW", zpGitPushIf->RepoId);
    if (0 != zgit_push(zppGlobRepoIf[zpGitPushIf->RepoId]->p_GitRepoMetaIf, zRemoteRepoAddrBuf, zpGitRefs)) {

        /* if directly push failed, then try push to a new remote branch: NEWserver... */
        sprintf(zpGitRefs[0], "refs/heads/master:refs/heads/NEWserver%d", zpGitPushIf->RepoId);
        sprintf(zpGitRefs[1], "refs/heads/master_SHADOW:refs/heads/NEWserver%d_SHADOW", zpGitPushIf->RepoId);
        if (0 !=zgit_push(zppGlobRepoIf[zpGitPushIf->RepoId]->p_GitRepoMetaIf, zRemoteRepoAddrBuf, zpGitRefs)) {

            /* if failed again, then try delete the remote branch: NEWserver... */
            char zCmdBuf[128 + zppGlobRepoIf[zpGitPushIf->RepoId]->RepoPathLen];
            sprintf(zCmdBuf, "cd %s; git branch -D NEWserver%d; git branch -D NEWserver%d_SHADOW",
                    zppGlobRepoIf[zpGitPushIf->RepoId]->p_RepoPath + 9,
                    zpGitPushIf->RepoId,
                    zpGitPushIf->RepoId
                    );
            if (0 == zssh_exec_simple(zpGitPushIf->p_HostStrAddr, zCmdBuf, &(zppGlobRepoIf[zpGitPushIf->RepoId]->SshSyncLock))) {

                /* and, try push to NEWserver once more... */
                if (0 !=zgit_push(zppGlobRepoIf[zpGitPushIf->RepoId]->p_GitRepoMetaIf, zRemoteRepoAddrBuf, zpGitRefs)) {
                    zNative_Fail_Confirm();
                    return (void *) -1;
                }
            } else {
                /* if ssh exec failed, just deal with error */
                zNative_Fail_Confirm();
                return (void *) -1;
            }
        }
    }

    return NULL;
}

#undef zGit_Check_Err_Return

// /* Just for test. */
// _i
// main(void) {
//     git_repository *zpRepoMetaIf = zgit_env_init("/tmp/test_repo/.git");
//
//     zgit_push(zpRepoMetaIf, "fh@127.0.0.1:/tmp/test_repo/.git", "master", "server_test");
//
//     zgit_env_clean(zpRepoMetaIf);
//     return 0;
// }
