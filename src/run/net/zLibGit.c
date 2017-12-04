#include "zLibGit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static git_repository *
zgit_env_init(char *zpNativeRepoAddr);

static void
zgit_env_clean(git_repository *zpRepoCredHandler);

static _i
zgit_remote_push(git_repository *zpRepo, char *zpRemoteRepoAddr, char **zppRefs, _i zRefsCnt, char *zpErrBufOUT);

static _i
zgit_remote_fetch(git_repository *zpRepo, char *zpRemoteRepoAddr, char **zppRefs, _i zRefsCnt, char *zpErrBufOUT);

static zGitRevWalk__ *
zgit_generate_revwalker(git_repository *zpRepo, char *zpRef, _i zSortType);

static void
zgit_destroy_revwalker(git_revwalk *zpRevWalker);

static _i
zgit_get_one_commitsig_and_timestamp(char *zpRevSigOUT, git_repository *zpRepo, git_revwalk *zpRevWalker);

static _i
zgit_branch_add_local(git_repository *zpRepo, char *zpBranchName, char *zpBaseRev, zbool_t zForceMark);

static _i
zgit_branch_del_local(git_repository *zpRepo, char *zpBranchName);

static _i
zgit_branch_rename_local(git_repository *zpRepo, char *zpOldName, char *zpNewName, zbool_t zForceMark);

static _i
zgit_branch_switch_local(git_repository *zpRepo, char *zpBranchName);

static _i
zgit_branch_list_local(git_repository *zpRepo, char *zpResBufOUT, _i zBufLen, _i *zpResItemCnt);


extern struct zRun__ zRun_;


struct zLibGit__ zLibGit_ = {
    .env_init = zgit_env_init,
    .env_clean = zgit_env_clean,

    .remote_push = zgit_remote_push,
    .remote_fetch = zgit_remote_fetch,

    .generate_revwalker = zgit_generate_revwalker,
    .destroy_revwalker = zgit_destroy_revwalker,
    .get_one_commitsig_and_timestamp = zgit_get_one_commitsig_and_timestamp,

    .branch_add = zgit_branch_add_local,
    .branch_del = zgit_branch_del_local,
    .branch_rename = zgit_branch_rename_local,
    .branch_switch = zgit_branch_switch_local,
    .branch_list_all = zgit_branch_list_local
};

/* 代码库新建或载入时调用一次即可；zpNativelRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式 */
static git_repository *
zgit_env_init(char *zpNativeRepoAddr) {
    git_repository *zpRepoHandler;

    if (0 > git_libgit2_init()) {  // 此处要使用 0 > ... 作为条件
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoHandler = NULL;
    }

    if (0 != git_repository_open(&zpRepoHandler, zpNativeRepoAddr)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zpRepoHandler = NULL;
    }

    return zpRepoHandler;
}

/* 通常无须调用，随布署系统运行一直处于使用状态 */
static void
zgit_env_clean(git_repository *zpRepoCredHandler) {
    git_repository_free(zpRepoCredHandler);
    git_libgit2_shutdown();
}

/* SSH 身份认证 */
static _i
zgit_cred_acquire_cb(git_cred **zppResOUT,
        const char *zpUrl __attribute__ ((__unused__)),
        const char * zpUsernameFromUrl,
        unsigned int zpAllowedTypes __attribute__ ((__unused__)),
        void * zPayload __attribute__ ((__unused__))) {

    if (0 != git_cred_ssh_key_new(zppResOUT, zpUsernameFromUrl, zRun_.p_SSHPubKeyPath, zRun_.p_SSHPrvKeyPath, NULL)) {
        if (NULL == giterr_last()) {
            fprintf(stderr, "\033[31;01m====Error message====\033[00m\nError without message.\n");
        } else {
            fprintf(stderr, "\033[31;01m====Error message====\033[00m\n%s\n", giterr_last()->message);
        }
    }

    return 0;
}

/*
 * 用于拉取远程仓库代码，效果相当于 git fetch origin，但不执行 git merge origin/xxxBranch：
 *     仅更新到 refs/remotes/origin/master，不合并到本地分支
 * zpRemoteRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式
 */
_i
zgit_remote_fetch(git_repository *zpRepo, char *zpRemoteRepoAddr, char **zppRefs, _i zRefsCnt, char *zpErrBufOUT/* size: 256 */) {
    /* get the remote */
    git_remote* zRemote = NULL;
    //git_remote_lookup( &zRemote, zpRepo, "origin" );  // 使用已命名分支时，调用此函数
    if (0 != git_remote_create_anonymous(&zRemote, zpRepo, zpRemoteRepoAddr)) {  // 直接使用 URL 时调用此函数
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        return -1;
    };

    /* connect to remote */
    git_remote_callbacks zConnOpts = GIT_REMOTE_CALLBACKS_INIT;
    zConnOpts.credentials = zgit_cred_acquire_cb;  // 指定身份认证所用的回调函数

    if (0 != git_remote_connect(zRemote, GIT_DIRECTION_FETCH, &zConnOpts, NULL, NULL)) {
        git_remote_free(zRemote);
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        return -1;
    }

    /* add [a] fetch refspec[s] */
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = zppRefs;
    zGitRefsArray.count = zRefsCnt;

    git_fetch_options zFetchOpts = GIT_FETCH_OPTIONS_INIT;

    /* do the fetch */
    //if (0 != git_remote_fetch(zRemote, &zGitRefsArray, &zFetchOpts, NULL)) {
    if (0 != git_remote_fetch(zRemote, &zGitRefsArray, &zFetchOpts, "pull")) {
        git_remote_disconnect(zRemote);
        git_remote_free(zRemote);
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        return -1;
    }

    git_remote_disconnect(zRemote);
    git_remote_free(zRemote);
    return 0;
}

/*
 * [ git push ]
 * zpRemoteRepoAddr 参数必须是 路径/.git 或 URL/仓库名.git 或 bare repo 的格式
 */
    /*
     * << 错误类型 >>
     * err1 bit[0]:服务端错误
     * err2 bit[1]:网络不通
     * err3 bit[2]:SSH 连接认证失败
     * err4 bit[3]:目标端磁盘容量不足
     * err5 bit[4]:目标端权限不足
     * err6 bit[5]:目标端文件冲突
     * err7 bit[6]:目标端布署后动作执行失败
     * err8 bit[7]:目标端收到重复布署指令(同一目标机的多个不同IP)
     */
static _i
zgit_remote_push(git_repository *zpRepo, char *zpRemoteRepoAddr, char **zppRefs, _i zRefsCnt, char *zpErrBufOUT/* size: 256 */) {
    _i zErrNo = 0;
    /* get the remote */
    git_remote* zRemote = NULL;
    //git_remote_lookup( &zRemote, zRepo, "origin" );  // 使用已命名分支时，调用此函数
    if (0 != git_remote_create_anonymous(&zRemote, zpRepo, zpRemoteRepoAddr)) {  // 直接使用 URL 时调用此函数
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        zErrNo = -1;
        goto zEndMark;
    };

    /* connect to remote */
    git_remote_callbacks zConnOpts = GIT_REMOTE_CALLBACKS_INIT;
    zConnOpts.credentials = zgit_cred_acquire_cb;  // 指定身份认证所用的回调函数

    if (0 != git_remote_connect(zRemote, GIT_DIRECTION_PUSH, &zConnOpts, NULL, NULL)) {
        git_remote_free(zRemote);
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        zErrNo = -2;  /* 网络不通 */
        goto zEndMark;
    }

    /* add [a] push refspec[s] */
    git_strarray zGitRefsArray;
    zGitRefsArray.strings = zppRefs;
    zGitRefsArray.count = zRefsCnt;

    git_push_options zPushOpts = GIT_PUSH_OPTIONS_INIT;
    zPushOpts.pb_parallelism = 1;  // 限定单个 push 动作可以使用的线程数，若指定为 0，则将与本地的CPU数量相同

    /* do the push */
    if (0 != (zErrNo = git_remote_upload(zRemote, &zGitRefsArray, &zPushOpts))) {
        git_remote_disconnect(zRemote);
        git_remote_free(zRemote);
        if (NULL == zpErrBufOUT) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message);
        } else {
            strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
            zpErrBufOUT[255] = '\0';
        }

        /*
         * 更改了 libgit2 中 git_push_finish() 的源码
         * 远程代码无法展开的情况，通常代表目标机磁盘已满
         * 此时会返回定制的错误码 -40000
         */
        if (-40000 == zErrNo) {
            zErrNo = -4;
        } else {
            zErrNo = -1;
        }

        goto zEndMark;
    }

    /* 同步 TAGS 之类的信息 */
    // if (0 != git_remote_update_tips(zRemote, &zConnOpts, 0, 0, NULL)) {
    //     git_remote_disconnect(zRemote);
    //     git_remote_free(zRemote);
    //    strncpy(zpErrBufOUT, NULL == giterr_last() ? "[zgit_remote_push]:error without message" : giterr_last()->message, 255);
    //    zpErrBufOUT[255] = '\0';
    //     return -1;
    // }

    git_remote_disconnect(zRemote);
    git_remote_free(zRemote);

zEndMark:
    return zErrNo;
}

/*
 * [ git log --format=%H ] and [ git log --format=%ct ]
 * success return zpRevWalker, fail return NULL
 */
static zGitRevWalk__ *
zgit_generate_revwalker(git_repository *zpRepo, char *zpRef, _i zSortType) {
    git_object *zpObj;
    git_revwalk *zpRevWalker = NULL;

    if (0 != git_revwalk_new(&zpRevWalker, zpRepo)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return NULL;
    }

    /* zSortType 显示順序：[0] git 默认排序，新记录在上、[1]逆序，旧记录在上 */
    if (0 == zSortType) {
        zSortType = GIT_SORT_TIME;
    } else {
        zSortType = GIT_SORT_TIME | GIT_SORT_REVERSE;
    }

    git_revwalk_sorting(zpRevWalker, zSortType);

    if ((0 != git_revparse_single(&zpObj, zpRepo, zpRef))
            || (0 != git_revwalk_push(zpRevWalker, git_object_id(zpObj)))) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        zgit_destroy_revwalker(zpRevWalker);
        return NULL;
    }

    return zpRevWalker;
}

static void
zgit_destroy_revwalker(git_revwalk *zpRevWalker) {
    git_revwalk_free(zpRevWalker);
}

/*
 * 结果返回：正常返回时间戳，若返回 0 表示已取完所有记录，返回 -1 表示出错
 * 参数返回：git log --format="%H" 格式的单条数据
 * 用途：每调用一次取出一条数据
 */
static _i
zgit_get_one_commitsig_and_timestamp(char *zpRevSigOUT, git_repository *zpRepo, git_revwalk *zpRevWalker) {
    git_oid zOid;
    git_commit *zpCommit = NULL;
    _i zErrNo = 0;
    time_t zTimeStamp = 0;

    zErrNo = git_revwalk_next(&zOid, zpRevWalker);

    if (0 == zErrNo) {
        if (0 != git_commit_lookup(&zpCommit, zpRepo, &zOid)) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
            return -1;
        }

        /* 取完整的 40 位 SHA1 sig，第二个参数指定调用者传入的缓冲区大小 */
        if ('\0' == git_oid_tostr(zpRevSigOUT, 41, &zOid)[0]) {
            zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
            git_commit_free(zpCommit);
            return -1;
        }

        zTimeStamp = git_commit_time(zpCommit);
        git_commit_free(zpCommit);
        return zTimeStamp;
    } else if (GIT_ITEROVER == zErrNo) {
        return 0;
    } else {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }
}


/*
 * 创建新分支，若 zForceMark 指定为 true，则将覆盖已存在的同名分支
 */
static _i
zgit_branch_add_local(git_repository *zpRepo, char *zpBranchName, char *zpBaseRev, zbool_t zForceMark) {
    git_reference *zpBaseRef = NULL;
    git_reference *zpNewBranch = NULL;

    const git_oid *zpBaseRefOid = NULL;
    git_commit *zpLastCommit = NULL;

    /* 获取基准版本 BaseRev 的 ref 指向 */
    //if (0 != git_repository_head(&zpBaseRef, zpRepo)) {
    if (0 != git_reference_lookup(&zpBaseRef, zpRepo, NULL == zpBaseRev ? "HEAD" : zpBaseRev)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    /* 提取其 oid 对象 */
    if (NULL == (zpBaseRefOid = git_reference_target(zpBaseRef))) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);

        git_reference_free(zpBaseRef);
        return -1;
    }

    /* 获取 HEAD 的最后一次 commit  */
    if (0 != git_commit_lookup(&zpLastCommit, zpRepo, zpBaseRefOid)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);

        git_reference_free(zpBaseRef);
        return -1;
    }

    /* 基于 HEAD 的最后一次 commit 创建新分支 */
    if (0 != git_branch_create(&zpNewBranch, zpRepo, zpBranchName, zpLastCommit, zForceMark)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);

        git_reference_free(zpBaseRef);
        git_commit_free(zpLastCommit);
        return -1;
    }

    git_reference_free(zpBaseRef);
    git_commit_free(zpLastCommit);
    git_reference_free(zpNewBranch);

    return 0;
}


/*
 * 删除分支
 */
static _i
zgit_branch_del_local(git_repository *zpRepo, char *zpBranchName) {
    git_reference *zpBranch = NULL;

    if (0 != git_branch_lookup(&zpBranch, zpRepo, zpBranchName, GIT_BRANCH_LOCAL)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    if (NULL == zpBranch) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpBranch);
        return -1;
    }

    // if (git_branch_is_head(zpBranch)) {
    //     zPrint_Err(0, NULL, "Delete HEAD will failed!");
    //     git_reference_free(zpBranch);
    //     return -1;
    // }

    if (0 != git_branch_delete(zpBranch)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpBranch);
        return -1;
    }

    git_reference_free(zpBranch);

    return 0;
}


/*
 * 分支改名
 */
static _i
zgit_branch_rename_local(git_repository *zpRepo, char *zpOldName, char *zpNewName, zbool_t zForceMark) {
    git_reference *zpOld = NULL;
    git_reference *zpNew = NULL;

    if (0 != git_branch_lookup(&zpOld, zpRepo, zpOldName, GIT_BRANCH_LOCAL)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    if (NULL == zpOld) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpOld);
        return -1;
    }

    if (0 != git_branch_move(&zpNew, zpOld, zpNewName, zForceMark)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpOld);
        return -1;
    }

    git_reference_free(zpOld);
    git_reference_free(zpNew);

    return 0;
}


/*
 * 切换分支
 */
static _i
zgit_branch_switch_local(git_repository *zpRepo, char *zpBranchName) {
    git_reference *zpBranch = NULL;

    if (0 != git_branch_lookup(&zpBranch, zpRepo, zpBranchName, GIT_BRANCH_LOCAL)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    if (NULL == zpBranch) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpBranch);
        return -1;
    }

    if (0 != git_repository_set_head(zpRepo, git_reference_name(zpBranch))) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        git_reference_free(zpBranch);
        return -1;
    }

    git_reference_free(zpBranch);

    return 0;
}


/*
 * 将所有本地分支名称写出到传入的 zpResBufOUT 中
 * zBufLen 指定可写出的缓冲区总大小
 * 分支总数写入到 zpResItemCnt 所指向的整数
 * 若缓冲区大小不足，写出的内容将会不完整，但分支数量将是完整的
 */
static _i
zgit_branch_list_local(git_repository *zpRepo, char *zpResBufOUT, _i zBufLen, _i *zpResItemCnt) {
    git_branch_iterator *zpBranchIter = NULL;
    git_reference *zpTmpBranch = NULL;
    git_branch_t zBranchTypeOUT;
    const char *zpBranchName = NULL;

    if (0 != git_branch_iterator_new(&zpBranchIter, zpRepo, GIT_BRANCH_LOCAL)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    _i zOffSet = 0;
    while (GIT_ITEROVER != git_branch_next(&zpTmpBranch, &zBranchTypeOUT, zpBranchIter)
            && zOffSet < zBufLen) {
        (* zpResItemCnt)++;

        git_branch_name(&zpBranchName, zpTmpBranch);  /* 迭代出的分支信息不会出错，此处不必检查返回值 */
        zOffSet += snprintf(zpResBufOUT + zOffSet + 1, zBufLen - zOffSet - 1, "%s", zpBranchName);
    }

    git_branch_iterator_free(zpBranchIter);

    return 0;
}


/*
 * git init .
 * zIsBare 置为 1/zTrue，则会初始化成 bare 库；否则就是普通库
 */
static git_repository *
zgit_init(char *zpPath, zbool_t zIsBare) {
    git_repository *zpRepo = NULL;

    /* With working directory or bare */
    if (0 != git_repository_init(&zpRepo, zpPath, zIsBare)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return NULL;
    }

    return zpRepo;
}


/*
 * git clone URL/.git
 * zpBranchName: clone 后使用的默认分支，置为 NULL 表示使用远程库的默认分支
 * zIsBare 为 1/zTrue 表示要生成 bare 库，否则为普通带工作区的库
 */
static git_repository *
zgit_clone(char *zpRepoAddr, char *zpPath, char *zpBranchName, zbool_t zIsBare) {
    git_repository *zpRepo = NULL;

    git_clone_options zOpt = GIT_CLONE_OPTIONS_INIT;

    zOpt.fetch_opts.callbacks.credentials = zgit_cred_acquire_cb;
    zOpt.bare = zIsBare;
    zOpt.checkout_branch = zpBranchName;

    if (0 != git_clone(&zpRepo, zpRepoAddr, zpPath, &zOpt)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return NULL;
    }

    return zpRepo;
}


/*
 * git config user.name _
 * && git config user.email _
 * && git add --all PATH
 * && git commit --allow-empty -m "_"
 */
static _i
zgit_add_and_commit(git_repository *zpRepo, char *zpPath) {
    struct stat zS;
    zCheck_Negative_Return(stat(zpPath, &zS), -1);

    /* get index */
    git_index* zpIndex = NULL;
    if (0 != git_repository_index(&zpIndex, zpRepo)) {
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    /* git add file */
    _i zErrNo = 0;
    if (S_ISDIR(zS.st_mode)) {
        git_strarray zPaths;
        zPaths.strings = &zpPath;
        zPaths.count = 1;

        zErrNo = git_index_add_all(zpIndex, &zPaths, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
    } else {
        zErrNo = git_index_add_bypath(zpIndex, zpPath);
    }

    if (0 != zErrNo) {
        git_index_free(zpIndex);
        zPrint_Err(0, NULL, NULL == giterr_last() ? "Error without message" : giterr_last()->message);
        return -1;
    }

    /* Write the in-memory index to disk */
    git_index_write(zpIndex);

    /* clean... */
    git_index_free(zpIndex);

    return 0;
}


/*
 * git config
 */
static _i
zgit_config() {
    git_signature_now(NULL, NULL, NULL);

}


// /*
//  * TO DO...
//  */
// void
// zgit_diff_path_list(git_repository *zpRepo, char *zpRef0, char *zpRef1) {
//     git_object *zpObj;
//     git_tree *zpTree[2];
//     git_diff *zpDiff;
//     git_diff_options zDiffOpts;
//     git_diff_find_options zDiffFindOpts;
//     git_diff_format_t zDiffFormat = GIT_DIFF_FORMAT_NAME_ONLY;
//
//     git_diff_init_options(&zDiffOpts, GIT_DIFF_OPTIONS_VERSION);
//     git_diff_find_init_options(&zDiffFindOpts, GIT_DIFF_FIND_OPTIONS_VERSION);
//     zDiffFindOpts.flags |= GIT_DIFF_FIND_ALL;
//
//     git_revparse_single(&zpObj, zpRepo, zpRef0);
//     git_object_peel((git_object **) zpTree, zpObj, GIT_OBJ_TREE);
//     git_object_free(zpObj);
//
//     git_revparse_single(&zpObj, zpRepo, zpRef1);
//     git_object_peel((git_object **) zpTree, zpObj, GIT_OBJ_TREE);
//     git_object_free(zpObj);
//
//     git_diff_tree_to_tree(&zpDiff, zpRepo, zpTree[0], zpTree[1], &zDiffOpts);
//     git_diff_find_similar(zpDiff, &zDiffFindOpts);
//
//     git_diff_print(zpDiff, zDiffFormat, NULL, NULL);  // the last two NULL param：diff res ops and it's inner param ptr
//
//     git_diff_free(zpDiff);
//     git_tree_free(zpTree[0]);
//     git_tree_free(zpTree[1]);
//
//     // 参见 log.c diff.c 实现 git log --format=%H、git diff --name-only、git diff -- filepath_0 filepath_N
//     // 可以反向显示提交记录
//     // 优化生成缓存的相关的函数实现
// }
//
// /*
//  * TO DO...
//  */
// void
// zgit_diff_onefile(char *zpFilePath) {
//     git_diff_options zDiffOpts;
//     git_diff_init_options(&zDiffOpts, GIT_DIFF_OPTIONS_VERSION);
//     zDiffOpts.pathspec.strings = &zpFilePath;
//     zDiffOpts.pathspec.count = 1;
//
//     //git_pathspec_new(NULL, NULL);
// }


// typedef void * (* zalloc_cb) (void *, void *);
//
// typedef struct {
//     void *p_meta;
//     size_t siz;
// } zAllocParam__;
//
// void *
// zsystem_alloc_wrap(void *zpParam) {
//     void *zpRes = NULL;
//     zMem_Alloc(zpRes, char, ((zAllocParam__ *) zpParam)->siz);
//     return zpRes;
// }
//
// void *
// zrepo_alloc_wrap(void *zpParam) {
//     return zalloc_cache(* ((_i *) ((zAllocParam__ *) zpParam)->p_meta), ((zAllocParam__ *) zpParam)->siz);
// }
