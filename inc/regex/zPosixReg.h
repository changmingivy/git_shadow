#ifndef ZPOSIXREG_H
#define ZPOSIXREG_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include <regex.h>
#include "zCommon.h"

typedef struct __zRegRes__ {
    char **pp_rets;  //matched results
    _i *p_resLen;  // results' strlen
    _i cnt;         //total num of matched substrings

    void * (* alloc_fn) (size_t);
    _i repoId;
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
