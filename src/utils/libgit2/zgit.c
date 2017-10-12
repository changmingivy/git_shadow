#ifndef _Z
    #include "../../zmain.c"
#endif

//#include "git2.h"

#define zGit_Check_Err_Return(zOps) do {\
    if (0 != zOps) {\
        git_remote_free(zRemote);\
        if (NULL == giterr_last()) { fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n"); }\
        else { fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message); }\
        return -1;\
    }\
} while (0)

/* 代码库新建或载入时调用一次即可；zpLocallRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式 */
git_repository *
zgit_env_init(char *zpLocalRepoAddr) {
    git_repository *zpRepoMetaIf;

    if (0 > git_libgit2_init()) {  // 此处要使用 0 > ... 作为条件
        if (NULL == giterr_last()) { fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n"); }
        else { fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message); }
        zpRepoMetaIf = NULL;
    }

    if (0 != git_repository_open(&zpRepoMetaIf, zpLocalRepoAddr)) {
        if (NULL == giterr_last()) { fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n"); }
        else { fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message); }
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
        if (NULL == giterr_last()) { fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n"); }
        else { fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message); }
        return -1;
    };

    /* connect to remote */
    git_remote_callbacks zConnOpts = GIT_REMOTE_CALLBACKS_INIT;  //git_remote_init_callbacks(&zConnOpts, GIT_REMOTE_CALLBACKS_VERSION);
    zConnOpts.credentials = zgit_cred_acquire_cb;  // 指定身份认证所用的回调函数
    zGit_Check_Err_Return( git_remote_connect(zRemote, GIT_DIRECTION_PUSH, &zConnOpts, NULL, NULL) );

    /* add [a] push refspec[s] */
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = zppRefs;
    zGitRefsArray.count = 1;

    git_push_options zPushOpts = GIT_PUSH_OPTIONS_INIT;  //git_push_init_options(&zPush_Opts, GIT_PUSH_OPTIONS_VERSION);

    /* do the push */
    zGit_Check_Err_Return( git_remote_upload(zRemote, &zGitRefsArray, &zPushOpts) );

    return 0; 
}

void *
zgit_push_ccur(void *zpIf) {
    zGitPushInfo *zpGitPushIf = (zGitPushInfo *) zpIf;

    char zRemoteRepoAddrBuf[64 + zppGlobRepoIf[zpGitPushIf->RepoId]->RepoPathLen];
    char zGitRefsBuf[64 + 2 * sizeof("refs/heads/:")];
    char *zpGitRefs = zGitRefsBuf;

    sprintf(zRemoteRepoAddrBuf, "git@%s:%s/.git", zpGitPushIf->p_HostStrAddr, zppGlobRepoIf[zpGitPushIf->RepoId]->p_RepoPath + 9);
    sprintf(zpGitRefs, "refs/heads/master:refs/heads/server%d", zpGitPushIf->RepoId);

    if (0 != zgit_push(zppGlobRepoIf[zpGitPushIf->RepoId]->p_GitRepoMetaIf, zRemoteRepoAddrBuf, &zpGitRefs)) {
        sprintf(zpGitRefs, "refs/heads/master:refs/heads/server%dN", zpGitPushIf->RepoId);
        zgit_push(zppGlobRepoIf[zpGitPushIf->RepoId]->p_GitRepoMetaIf, zRemoteRepoAddrBuf, &zpGitRefs);
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
