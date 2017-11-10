#ifndef ZPOSIXREG_H
#define ZPOSIXREG_H

#include <regex.h>
#include "zCommon.h"

#define zMatchLimit 1024
typedef struct __zRegRes__ {
    char *p_rets[zMatchLimit];  //matched results
    _ui resLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, _ui);
    _i repoId;
} zRegRes__ ;

typedef regex_t zRegInit__;

struct zPosixReg__ {
    void (* compile) (zRegInit__ *, const char *);
    void (* match) (zRegRes__ *, regex_t *, const char *);

    void (* free_meta) (zRegInit__ *);
    void (* free_res) (zRegRes__ *);
};

#endif  // #ifndef ZPOSIXREG_H
