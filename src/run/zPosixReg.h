#include <regex.h>

#include "zCommon.h"

#define zMatchLimit 1024
struct zRegRes__ {
    char *p_rets[zMatchLimit];  //matched results
    _ui ResLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, size_t);
    _i RepoId;
};
typedef struct zRegRes__ zRegRes__;

typedef regex_t zRegInit__;

struct zPosixReg__ {
    void (* compile) (zRegInit__ *, const char *);
    void (* match) (zRegRes__ *, regex_t *, const char *);

    void (* free_meta) (zRegInit__ *);
    void (* free_res) (zRegRes__ *);
};

#ifndef _SELF_
extern struct zPosixReg__ zPosixReg_;
#endif
