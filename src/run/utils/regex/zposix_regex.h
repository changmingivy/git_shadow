#ifndef ZPOSIXREG_H
#define ZPOSIXREG_H

#include "zcommon.h"
#include <regex.h>

typedef struct __zRegRes__ {
    char **pp_rets;  //matched results
    _i *p_resLen;  // results' strlen
    _i cnt;         //total num of matched substrings

    void * (* alloc_fn) (size_t);
} zRegRes__ ;

typedef regex_t zRegInit__;

struct zPosixReg__ {
    void (* init) (zRegInit__ *, const char *);
    void (* match) (zRegRes__ *, regex_t *, const char *);

    void (* str_split) (zRegRes__ *, char *, char *);
    void (* str_split_fast) (zRegRes__ *, char *, char *);

    void (* free_meta) (zRegInit__ *);
    void (* free_res) (zRegRes__ *);
};

#endif  // #ifndef ZPOSIXREG_H
