#ifndef ZPOSIXREG_H
#define ZPOSIXREG_H

#include <regex.h>
#include "zCommon.h"

typedef struct __zRegRes__ {
    char **pp_rets;  //matched results
    _ui *p_resLen;  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, _ui);
    _i repoId;
} zRegRes__ ;

typedef regex_t zRegInit__;

struct zPosixReg__ {
    void (* init) (zRegInit__ *, const char *);
    void (* match) (zRegRes__ *, regex_t *, const char *);

    void (* free_meta) (zRegInit__ *);
    void (* free_res) (zRegRes__ *);
};

#endif  // #ifndef ZPOSIXREG_H
