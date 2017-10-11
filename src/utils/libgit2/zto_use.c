#ifndef _Z
    #include "../../zmain.c"
#endif

#include "git2.h"

#define zGit_Check_Err_Return(zOps) do {\
    if (0 != (zErrNo = zOps)) {\
        git_remote_free(zRemote);\
        if (NULL == giterr_last()) { fprintf(stderr, "====Error message====\nError without message.\n"); }\
        else { fprintf(stderr, "====Error message====\n%s\n", giterr_last()->message); }\
        return zErrNo;\
    }\
} while (0)

/* 代码库初始化时调用一次即可；zpRepoPath 参数不包含 .git */
git_repository *
zgit_env_init(char *zpRepoPath) {
    git_libgit2_init();

    char zGitPathBuf[strlen(zpRepoPath) + sizeof("/.git")];
    sprintf(zGitPathBuf, "%s/.git", zpRepoPath);

    git_repository *zpRepoMetaIf;
    if (0 != git_repository_open(&zpRepoMetaIf, zGitPathBuf)) { zpRepoMetaIf = NULL; }

    return zpRepoMetaIf;
}

/* 通常无须调用，随布署系统运行一直处于使用状态 */
void
zgit_env_clean(git_repository *zpRepoIf) {
    git_repository_free(zpRepoIf);
    git_libgit2_shutdown();
}

/*
 * [ git push ]
 * zpRemoteRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 的格式
 */
_i
zgit_push(git_repository *zRepo, char *zpRemoteRepoAddr, char *zpLocalBranchName, char *zpRemoteBranchName) {
    /* get the remote */
    _i zErrNo = 0;
    git_remote* zRemote = NULL;
    //git_remote_lookup( &remote, zRepo, "origin" );  // 使用已命名分支时，调用此函数
    if (0 != (zErrNo = git_remote_create_anonymous(&zRemote, zRepo, zpRemoteRepoAddr))) { return zErrNo; };  // 直接使用 URL 时调用此函数

    /* connect to remote */
	git_remote_callbacks zCallBackOpts;
	git_remote_init_callbacks(&zCallBackOpts, GIT_REMOTE_CALLBACKS_VERSION);
    zGit_Check_Err_Return( git_remote_connect(zRemote, GIT_DIRECTION_PUSH, &zCallBackOpts, NULL, NULL) );

    /* add [a] push refspec[s] */
    char zRefsBuf[2 * sizeof("refs/heads/:") + strlen(zpLocalBranchName) + strlen(zpRemoteBranchName)], *zpRefs;
	zpRefs = zRefsBuf;
    sprintf(zRefsBuf, "refs/heads/%s:refs/heads/%s", zpLocalBranchName, zpRemoteBranchName);
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = &zpRefs;
    zGitRefsArray.count = 1;

    /* configure options */
    git_push_options options;
    zGit_Check_Err_Return(git_push_init_options(&options, GIT_PUSH_OPTIONS_VERSION));

    /* do the push */
    zGit_Check_Err_Return(git_remote_push(zRemote, &zGitRefsArray, &options));

    return 0; 
}

#undef zGit_Check_Err_Return

/*
 * Just for test.
 */
_i
main(void) {
    git_repository *zpRepoMetaIf = zgit_env_init("/tmp/test_repo");

    zgit_push(zpRepoMetaIf, "git@127.0.0.1:/tmp/test_repo/.git", "master", "server");

    zgit_env_clean(zpRepoMetaIf);
    return 0;
}
