#ifndef ZLIBGIT_H
#define ZLIBGIT_H

#include "git2.h"
#include "zCommon.h"

typedef struct git_revwalk zGitRevWalk__;

struct zLibGit__ {
    git_repository * (* env_init) (char *);
    void (* env_clean) (git_repository *);

    _i (* remote_push) (git_repository *, char *, char **, _i, char *);
    _i (* remote_fetch) (git_repository *, char *, char **, _i, char *);

    zGitRevWalk__ * (* generate_revwalker) (git_repository *, char *, _i);
    void (* destroy_revwalker) (git_revwalk *);

    _i (* get_one_commitsig_and_timestamp) (char *, git_repository *, git_revwalk *);

    _i (* branch_add) (git_repository *, char *, zBool__);
    _i (* branch_del) (git_repository *, char *);
    _i (* branch_rename) (git_repository *, char *, char *, zBool__);
    _i (* branch_switch)(git_repository *, char *);
    _i (* branch_list_all) (git_repository *, char *, _i, _i *);
};

#endif  // #ifndef ZLIBGIT_H
