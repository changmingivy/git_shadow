#include <regex.h>

#include "zCommon.h"

#define zMatchLimit 1024
struct zRegResInfo {
    char *p_rets[zMatchLimit];  //matched results
    _ui ResLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, size_t);
    _i RepoId;
};
typedef struct zRegResInfo zRegResInfo;

typedef regex_t zRegInitInfo;

struct zPosixReg__ {
    void (* compile) (zRegInitInfo *, const char *);
    void (* match) (zRegResInfo *, regex_t *, const char *);

    void (* free_meta) (zRegInitInfo *);
    void (* free_res) (zRegResInfo *);
};

#ifndef _SELF_
extern struct zPosixReg__ zPosixReg_;
#endif
