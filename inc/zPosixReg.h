#define ZPOSIXREG_H

#include <regex.h>

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#define zMatchLimit 1024
typedef struct {
    char *p_rets[zMatchLimit];  //matched results
    _ui ResLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, size_t);
    _i RepoId;
} zRegRes__ ;

typedef regex_t zRegInit__;

struct zPosixReg__ {
    void (* compile) (zRegInit__ *, const char *);
    void (* match) (zRegRes__ *, regex_t *, const char *);

    void (* free_meta) (zRegInit__ *);
    void (* free_res) (zRegRes__ *);
};


// extern struct zPosixReg__ zPosixReg_;
